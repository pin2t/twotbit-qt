#include "rates/rates.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <charconv>
#include <cmath>
#include <ctime>

#include "util/log.h"
#include "util/strings.h"

namespace tb {

namespace Rates {

namespace {

bool parseInt(std::string_view s, int64_t& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size() && !s.empty();
}

QJsonObject object(const std::string& body) {
    QJsonParseError error{};
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(body), &error);
    if (error.error != QJsonParseError::NoError) throw RateError("bad JSON: " + error.errorString().toStdString());
    if (!doc.isObject()) throw RateError("bad JSON: not an object");
    return doc.object();
}

std::string stringAt(const QJsonValue& value, const char* what) {
    if (!value.isString()) throw RateError(std::string("no ") + what + " in response");
    return value.toString().toStdString();
}

} // namespace

std::vector<Rate> embeddedHistory() {
    QFile file(":/rates.csv");
    if (!file.open(QIODevice::ReadOnly)) throw RateError("rates.csv is missing");
    return parseCSV(file.readAll().toStdString());
}

void seed(Store& store) {
    std::vector<Rate> history = embeddedHistory();
    if (store.seedRates(history)) Log::info("rates: seeded " + std::to_string(history.size()) + " historical rates");
}

std::vector<Rate> parseCSV(const std::string& text) {
    std::vector<std::string> lines;
    for (std::string line : split(text, '\n')) {
        std::erase(line, '\r');
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty() || lines[0] != "ts,cents") throw RateError("missing \"ts,cents\" header");
    std::vector<Rate> rates;
    rates.reserve(lines.size());
    for (size_t n = 1; n < lines.size(); ++n) {
        std::vector<std::string> parts;
        for (auto& p : split(lines[n], ','))
            if (!p.empty()) parts.push_back(p);
        int64_t ts, cents;
        if (parts.size() != 2 || !parseInt(parts[0], ts) || !parseInt(parts[1], cents) || cents <= 0)
            throw RateError("line " + std::to_string(n + 1) + ": bad rate \"" + lines[n] + "\"");
        rates.push_back(Rate{ts, cents});
    }
    return rates;
}

int64_t toCents(const std::string& price) {
    std::string t = trimSpaces(price);
    double usd = 0;
    auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), usd);
    if (t.empty() || ec != std::errc() || ptr != t.data() + t.size() || !(usd > 0) || !std::isfinite(usd))
        throw RateError("bad price \"" + price + "\"");
    return int64_t(std::llround(usd * 100));
}

int64_t combine(const std::vector<int64_t>& cents) {
    int64_t lo = *std::min_element(cents.begin(), cents.end());
    int64_t hi = *std::max_element(cents.begin(), cents.end());
    if (double(hi - lo) > RateService::maxSpread * double(hi)) return hi;
    int64_t n = int64_t(cents.size());
    int64_t sum = 0;
    for (int64_t c : cents) sum += c;
    return (sum + n / 2) / n;
}

std::string parseCoinbase(const std::string& body) {
    QJsonValue data = object(body).value("data");
    if (!data.isObject()) throw RateError("no data in response");
    return stringAt(data.toObject().value("amount"), "amount");
}

std::string parseKraken(const std::string& body) {
    QJsonObject root = object(body);
    QJsonValue errors = root.value("error");
    if (!errors.isArray()) throw RateError("no error list in response");
    std::vector<std::string> messages;
    for (const auto& e : errors.toArray()) messages.push_back(e.toString().toStdString());
    if (!messages.empty()) throw RateError(join(messages, "; "));
    QJsonObject result = root.value("result").toObject();
    if (result.isEmpty()) throw RateError("no ticker in response");
    QJsonArray last = result.begin()->toObject().value("c").toArray();
    if (last.isEmpty()) throw RateError("no ticker in response");
    return stringAt(last.first(), "last trade price");
}

std::string parseBitstamp(const std::string& body) { return stringAt(object(body).value("last"), "last price"); }

} // namespace Rates

/// Fetch is one refresh in flight: a request to every source at once.
struct RateService::Fetch {
    struct Source {
        const char* name;
        const char* url;
        std::string (*parse)(const std::string&);
    };

    static constexpr Source sources[] = {
        {"coinbase", "https://api.coinbase.com/v2/prices/BTC-USD/spot", Rates::parseCoinbase},
        {"kraken", "https://api.kraken.com/0/public/Ticker?pair=XBTUSD", Rates::parseKraken},
        {"bitstamp", "https://www.bitstamp.net/api/v2/ticker/btcusd/", Rates::parseBitstamp},
    };

    int64_t now = 0;
    std::vector<QNetworkReply*> replies;
    std::vector<int64_t> rates;
    std::vector<std::string> errors;
    int pending = 0;
};

RateService::RateService(std::shared_ptr<Store> store, QObject* parent)
    : QObject(parent), store_(std::move(store)), network_(new QNetworkAccessManager(this)) {
    timer_.setInterval(checkIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &RateService::check);
}

RateService::~RateService() { stop(); }

void RateService::start() {
    timer_.start();
    check();
}

void RateService::stop() {
    timer_.stop();
    if (!fetch_) return;
    std::unique_ptr<Fetch> fetch = std::move(fetch_);
    for (QNetworkReply* reply : fetch->replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void RateService::check() {
    if (fetch_) return;
    int64_t now = int64_t(std::time(nullptr));
    try {
        if (auto latest = store_->latestRate(); latest && now - latest->time < maxAge) return;
    } catch (const std::exception& e) {
        Log::info(std::string("rates: read latest rate: ") + e.what());
        return;
    }
    fetch_ = std::make_unique<Fetch>();
    fetch_->now = now;
    for (const auto& source : Fetch::sources) {
        QNetworkRequest request(QUrl(QString::fromLatin1(source.url)));
        request.setRawHeader("Accept", "application/json");
        request.setTransferTimeout(fetchTimeoutMs);
        QNetworkReply* reply = network_->get(request);
        fetch_->replies.push_back(reply);
        ++fetch_->pending;
        connect(reply, &QNetworkReply::finished, this, [this, reply, source] {
            reply->deleteLater();
            if (!fetch_) return;
            try {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError && status == 0) throw RateError(reply->errorString().toStdString());
                if (status != 200) throw RateError("HTTP " + std::to_string(status));
                QByteArray body = reply->read(1 << 20);
                fetch_->rates.push_back(Rates::toCents(source.parse(body.toStdString())));
            } catch (const std::exception& e) {
                fetch_->errors.push_back(std::string(source.name) + ": " + e.what());
            }
            if (--fetch_->pending == 0) finish();
        });
    }
}

void RateService::finish() {
    std::unique_ptr<Fetch> fetch = std::move(fetch_);
    size_t total = std::size(Fetch::sources);
    if (fetch->rates.empty()) {
        Log::info("rates: no rate source answered: " + join(fetch->errors, "; "));
        return;
    }
    if (!fetch->errors.empty())
        Log::info("rates: using " + std::to_string(fetch->rates.size()) + " of " + std::to_string(total) + " sources: " + join(fetch->errors, "; "));
    int64_t cents = Rates::combine(fetch->rates);
    Rate rate{fetch->now, cents};
    try {
        store_->addRate(rate);
    } catch (const std::exception& e) {
        Log::info(std::string("rates: store rate: ") + e.what());
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld.%02lld", (long long)(cents / 100), (long long)(cents % 100));
    Log::info(std::string("rates: stored ") + buf + " USD");
    emit rateStored(rate.time, rate.cents);
}

} // namespace tb
