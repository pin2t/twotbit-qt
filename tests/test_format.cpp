#include "tests.h"

#include <QImage>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "crypto/hashes.h"
#include "format/format.h"
#include "format/qrcode.h"
#include "mocknode.h"
#include "p2p/peer.h"
#include "rates/rates.h"
#include "storage/store.h"
#include "sync/probe.h"
#include "util/log.h"

namespace tb::test {

void FormatTests::amounts() {
    const std::pair<int64_t, const char*> cases[] = {
        {0, "0 sats"},           {999, "999 sats"},           {1000, "1 000 sats"},           {1'234'567, "1 234 567 sats"},
        {4'999'999, "4 999 999 sats"}, {5'000'000, "0.05 BTC"}, {100'000'000, "1 BTC"},          {123'445'678'900, "1 234.456789 BTC"},
        {210'000'000'000'000, "2 100 000 BTC"}, {12'345'678, "0.12345678 BTC"}, {-1'500, "-1 500 sats"}, {-250'000'000, "-2.5 BTC"},
    };
    for (const auto& [sats, want] : cases) EXPECT_EQ(Format::amount(sats), std::string(want), std::to_string(sats));
}

void FormatTests::balanceAndPending() {
    EXPECT_EQ(Format::balance(1'234'567, 0), std::string("1 234 567 sats"), "confirmed");
    EXPECT_EQ(Format::pendingNote(0), std::string(""), "no note");
    EXPECT_EQ(Format::balance(0, 25'000'000), std::string("0.25 BTC"), "pending");
    EXPECT_EQ(Format::pendingNote(25'000'000), std::string("(0.25 BTC pending)"), "note");
    EXPECT_EQ(Format::balance(10'000'000, -2'000), std::string("0.09998 BTC"), "spend pending");
    EXPECT_EQ(Format::pendingNote(-2'000), std::string(""), "no note for spends");
}

void FormatTests::parseAmounts() {
    struct Case {
        const char* text;
        AmountUnit unit;
        int64_t want;
        bool ok;
    };
    const Case cases[] = {
        {"", AmountUnit::sats, 0, true},
        {"1 234 567", AmountUnit::sats, 1'234'567, true},
        {"12.5", AmountUnit::sats, 0, false},
        {"-5", AmountUnit::sats, 0, false},
        {"0.0015", AmountUnit::btc, 150'000, true},
        {".5", AmountUnit::btc, 50'000'000, true},
        {"1 234.456789", AmountUnit::btc, 123'445'678'900, true},
        {"2", AmountUnit::btc, 200'000'000, true},
        {"0.000000001", AmountUnit::btc, 0, false},
        {"-0.1", AmountUnit::btc, 0, false},
        {"abc", AmountUnit::btc, 0, false},
        {"21000001", AmountUnit::btc, 0, false},
        {"+42", AmountUnit::sats, 42, true},
        {"1.2.3", AmountUnit::btc, 0, false},
        {"99999999999999999999", AmountUnit::btc, 0, false},
    };
    for (const auto& c : cases) {
        std::optional<int64_t> got;
        try {
            got = Format::parseAmount(c.text, c.unit);
        } catch (const AmountError&) {
        }
        EXPECT_EQ(got.has_value(), c.ok, std::string(c.text) + " " + unitName(c.unit));
        if (c.ok) EXPECT_EQ(*got, c.want, c.text);
    }
}

void FormatTests::invoices() {
    EXPECT_EQ(Format::invoiceURI("bc1qexample", 0), std::string("bitcoin:bc1qexample"), "no amount");
    EXPECT_EQ(Format::invoiceURI("bc1qexample", 150'000), std::string("bitcoin:bc1qexample?amount=0.0015"), "amount");
    EXPECT_EQ(Format::unitText(150'000, AmountUnit::sats) + " " + Format::unitText(150'000, AmountUnit::btc), std::string("150000 0.0015"), "units");
    EXPECT_EQ(Format::btcDecimal(210'000'000'000'000), std::string("2100000"), "whole");
}

void FormatTests::usd() {
    const std::tuple<int64_t, int64_t, int64_t> values[] = {
        {0, 8'376'587, 0},
        {100'000'000, 8'376'587, 8'376'587},
        {1'250'000, 8'376'587, 104'707},
        {1, 8'376'587, 0},
        {6, 8'376'587, 1},
        {150'000'000, 8'376'587, 12'564'881},
        {Format::maxSats, 100'000'000'000, 2'100'000'000'000'000'000},
        {-1'250'000, 8'376'587, -104'707},
    };
    for (const auto& [sats, rate, want] : values) EXPECT_EQ(Format::usdValue(sats, rate), want, std::to_string(sats));
    const std::pair<int64_t, const char*> texts[] = {
        {0, "0.00 USD"}, {5, "0.05 USD"}, {1'234'567, "12 345.67 USD"}, {100'000'000, "1 000 000.00 USD"}, {-1'234'567, "-12 345.67 USD"},
    };
    for (const auto& [cents, want] : texts) EXPECT_EQ(Format::usd(cents), std::string(want), std::to_string(cents));
}

void FormatTests::netAmounts() {
    EXPECT_EQ(Format::netAmount(1000), std::string("+ 1 000 sats"), "in");
    EXPECT_EQ(Format::netAmount(-50'000'000), std::string("- 0.5 BTC"), "out");
    EXPECT_EQ(Format::netAmount(0), std::string("0 sats"), "zero");
}

void FormatTests::relativeTimes() {
    const int64_t now = 1'800'000'000, hour = 3600, day = 24 * 3600;
    const std::pair<int64_t, const char*> cases[] = {
        {0, "just now"},          {59, "just now"},        {-hour, "just now"},          {60, "1 minute ago"},  {59 * 60, "59 minutes ago"},
        {hour, "1 hour ago"},     {23 * hour, "23 hours ago"}, {day, "1 day ago"},       {29 * day, "29 days ago"}, {30 * day, "1 month ago"},
        {364 * day, "12 months ago"}, {365 * day, "1 year ago"}, {3 * 365 * day, "3 years ago"},
    };
    for (const auto& [age, want] : cases) EXPECT_EQ(Format::relativeTime(now - age, now), std::string(want), std::to_string(age));
    EXPECT_EQ(Format::relativeTime(0, now), std::string("—"), "unknown");
}

void FormatTests::lifetimes() {
    const int64_t now = 1'800'000'000, day = 24 * 3600;
    const std::tuple<int64_t, const char*, const char*> cases[] = {
        {0, "less than a minute", "less than a minute"}, {300, "5 minutes", "5 minutes"},       {3660, "1 hour 1 minute", "1 hour"},
        {3 * day, "3 days", "3 days"},                   {3 * day + 7200, "3 days 2 hours", "3 days"}, {45 * day, "1 month 15 days", "1 month"},
        {435 * day, "1 year 2 months", "1 year"},        {730 * day, "2 years", "2 years"},
    };
    for (const auto& [age, two, one] : cases) {
        EXPECT_EQ(Format::lifetime(now - age, now, 2), std::string(two), std::to_string(age));
        EXPECT_EQ(Format::lifetime(now - age, now, 1), std::string(one), std::to_string(age));
    }
    EXPECT_EQ(Format::lifetime(0, now, 2), std::string("—"), "unknown");
}

void FormatTests::fitAddresses() {
    const std::string address = "bc1q0r80j388qfu32q67u6zjx3lhm9wsf07gryt4xl";
    auto characters = [](const std::string& s) { return QString::fromStdString(s).size(); };
    const std::pair<int, const char*> cases[] = {
        {100, nullptr}, {int(address.size()), nullptr}, {20, "bc1q0r80j388q…ryt4xl"}, {12, "bc1q0…ryt4xl"}, {11, "…ryt4xl"}, {3, "…ryt4xl"},
    };
    for (const auto& [room, want] : cases) {
        std::string got = Format::fitAddress(address, [&](const std::string& s) { return characters(s) <= room; });
        EXPECT_EQ(got, want ? std::string(want) : address, "room " + std::to_string(room));
    }
}

void FormatTests::stats() {
    // A receive, a payment with change, a payment between the wallet's own
    // addresses, and a spend of wallet coins the wallet did not pay alone.
    TxAddress mine{"m", true}, other{"o", false};
    auto tx = [](int64_t net, int64_t fee, std::vector<TxAddress> inputs, std::vector<TxAddress> outputs) {
        return WalletTransaction{Hash256(), 0, true, net, fee, inputs, outputs};
    };
    std::vector<WalletTransaction> txs = {tx(100'000, 0, {other}, {mine, other}), tx(-30'500, 500, {mine}, {other, mine}),
                                          tx(-300, 300, {mine, mine}, {mine}), tx(-2'000, 0, {mine, other}, {other})};
    EXPECT((WalletStats::of(txs, 2) == WalletStats{100'000, 32'000, 800, 4, 3, 2}), "stats");
    EXPECT((WalletStats::of({}, 0) == WalletStats{}), "empty");
}

void FormatTests::peers() {
    const std::pair<const char*, const char*> cases[] = {
        {"192.0.2.1", "192.0.2.1:8333"},
        {"192.0.2.1:18444", "192.0.2.1:18444"},
        {"192.0.2.1:08333", "192.0.2.1:8333"},
        {"2001:db8::1", "[2001:db8::1]:8333"},
        {"[2001:db8::1]", "[2001:db8::1]:8333"},
        {"[2001:db8::1]:8334", "[2001:db8::1]:8334"},
        {"192.0.2", nullptr},
        {"192.0.2.1:", nullptr},
        {"192.0.2.1:0", nullptr},
        {"192.0.2.1:65536", nullptr},
        {"example.com:8333", nullptr},
        {"[2001:db8::1", nullptr},
    };
    for (const auto& [text, want] : cases) {
        auto got = PeerProbe::parse(text, 8333);
        EXPECT(want ? got == std::optional<std::string>(want) : !got, text);
    }
    EXPECT_EQ(PeerProbe::reason(std::make_exception_ptr(PeerProbe::NoCompactFilters("x"))),
              std::string("no compact filters (needs blockfilterindex=1 and peerblockfilters=1)"), "no filters");
    EXPECT_EQ(PeerProbe::reason(std::make_exception_ptr(
                  PeerError("dial", std::make_exception_ptr(NetError(NetError::Kind::refused, "connection refused"))))),
              std::string("connection refused"), "refused");
    EXPECT_EQ(PeerProbe::reason(std::make_exception_ptr(PeerError("dial", std::make_exception_ptr(NetError(NetError::Kind::timeout, "i/o timeout"))))),
              std::string("no answer"), "timeout");
    EXPECT_EQ(PeerProbe::reason(std::make_exception_ptr(PeerError("handshake with 192.0.2.1:8333 timed out"))), std::string("no handshake"),
              "no handshake");
}

void FormatTests::probes() {
    // A closed port refuses, a node without filters is named, and a serving
    // node passes the probe.
    auto node = MockNode::start();
    PeerProbe::probe(Network::regtest(), node->address());
    node->with([](MockNode::State& s) { return s.services = ServiceFlags{ServiceFlags::network}; });
    bool noFilters = false;
    try {
        PeerProbe::probe(Network::regtest(), node->address());
    } catch (const PeerProbe::NoCompactFilters&) {
        noFilters = true;
    }
    EXPECT(noFilters, "no compact filters");
    try {
        PeerProbe::probe(Network::regtest(), "127.0.0.1:1");
        QFAIL("closed port accepted");
    } catch (const std::exception&) {
        EXPECT_EQ(PeerProbe::reason(std::current_exception()), std::string("connection refused"), "closed port");
    }
}

void FormatTests::rates() {
    std::vector<Rate> embedded = Rates::embeddedHistory();
    EXPECT(embedded.size() > 20'000, "history");
    EXPECT((embedded.front() == Rate{1'282'089'600, 7}), "first rate");
    for (const char* bad : {"", "time,usd\n1,2\n", "ts,cents\n1,x\n", "ts,cents\n1,0\n", "ts,cents\n1\n"}) {
        bool threw = false;
        try {
            Rates::parseCSV(bad);
        } catch (const RateError&) {
            threw = true;
        }
        EXPECT(threw, bad);
    }
    const std::pair<std::vector<int64_t>, int64_t> combine[] = {
        {{100, 101, 103}, 101}, {{100, 101, 102}, 101}, {{90, 95, 100}, 95}, {{89, 95, 100}, 100}, {{100, 100, 50}, 100}, {{6'500'012}, 6'500'012}, {{3, 4}, 4},
    };
    for (const auto& [input, want] : combine) EXPECT_EQ(Rates::combine(input), want, std::to_string(input.size()) + " sources");
    EXPECT_EQ(Rates::toCents(Rates::parseCoinbase(R"({"data":{"amount":"65000.126","base":"BTC","currency":"USD"}})")), int64_t(6'500'013),
              "coinbase");
    EXPECT_EQ(Rates::toCents(Rates::parseKraken(R"({"error":[],"result":{"XXBTZUSD":{"a":["1"],"c":["64999.9","0.1"]}}})")), int64_t(6'499'990),
              "kraken");
    EXPECT_EQ(Rates::toCents(Rates::parseBitstamp(R"({"last":"65001","bid":"65000"})")), int64_t(6'500'100), "bitstamp");
    bool threw = false;
    try {
        Rates::parseKraken(R"({"error":["EQuery:Unknown asset pair"]})");
    } catch (const RateError&) {
        threw = true;
    }
    EXPECT(threw, "kraken error");
    threw = false;
    try {
        Rates::toCents("");
    } catch (const RateError&) {
        threw = true;
    }
    EXPECT(threw, "empty price");
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("twotbit-test-" + toHex(Hashes::randomBytes(8)));
    {
        auto store = Store::open(utf8Of(dir / "wallet.db"));
        Rates::seed(*store);
        EXPECT(store->latestRate() == embedded.back(), "seeded");
        store->close();
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void FormatTests::qrCodes() {
    // The codes of receive invoices stand upright and carry their format
    // information, level M and the mask chosen, in both copies.
    const char* texts[] = {
        "bitcoin:bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",
        "bitcoin:bcrt1q6rz28mcfaxtmd6v789l9rrlrusdprr9pz3cppk?amount=0.0015",
        "bitcoin:tb1q6rz28mcfaxtmd6v789l9rrlrusdprr9pqcpvkl?amount=21000000",
        "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",
    };
    for (const char* text : texts) {
        auto code = QRCode::encode(text);
        QVERIFY(code);
        EXPECT(code->isUpright(), text);
        EXPECT_EQ(code->size(), code->version() * 4 + 17, text);
        int data = 0 << 3 | code->mask();
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int want = (data << 10 | rem) ^ 0x5412;
        int first = 0, second = 0;
        for (int i = 0; i <= 5; ++i) first |= code->isDark(8, i) << i;
        first |= code->isDark(8, 7) << 6 | code->isDark(8, 8) << 7 | code->isDark(7, 8) << 8;
        for (int i = 9; i < 15; ++i) first |= code->isDark(14 - i, 8) << i;
        for (int i = 0; i < 8; ++i) second |= code->isDark(code->size() - 1 - i, 8) << i;
        for (int i = 8; i < 15; ++i) second |= code->isDark(8, code->size() - 15 + i) << i;
        EXPECT_EQ(first, want, text);
        EXPECT_EQ(second, want, text);
        EXPECT(code->isDark(8, code->size() - 8), text);
    }
    // 50 bytes take version 4: version 3 at level M holds 42.
    EXPECT(QRCode::encode("bitcoin:bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu")->version() == 4, "smallest version");
    EXPECT(QRCode::encode(std::string(2331, 'x'))->version() == 40, "largest version");
    EXPECT(!QRCode::encode(std::string(2332, 'x')), "too long");

    // With TWOTBIT_QR_DIR set, the codes of the invoices and of random texts
    // of every length up to version 40's are written there as PNG images,
    // each with its text, for independent decoders to read back.
    const char* dir = std::getenv("TWOTBIT_QR_DIR");
    if (!dir) return;
    std::vector<std::string> all(std::begin(texts), std::end(texts));
    all.push_back("bitcoin:bc1qé€\U0001F600 utf-8");
    Bytes random = Hashes::randomBytes(4096);
    for (size_t length = 1, i = 0; length <= 2331; length += 1 + length / 9, ++i) {
        std::string text;
        for (size_t k = 0; k < length; ++k) text.push_back(char(32 + random[(i * 131 + k) % random.size()] % 95));
        all.push_back(text);
    }
    std::filesystem::create_directories(pathOf(dir));
    for (size_t i = 0; i < all.size(); ++i) {
        auto code = QRCode::encode(all[i]);
        QVERIFY(code);
        const int scale = 4, quiet = 4, side = (code->size() + 2 * quiet) * scale;
        QImage image(side, side, QImage::Format_Grayscale8);
        image.fill(255);
        for (int y = 0; y < code->size(); ++y)
            for (int x = 0; x < code->size(); ++x)
                if (code->isDark(x, y))
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx) image.setPixel((quiet + x) * scale + dx, (quiet + y) * scale + dy, qRgb(0, 0, 0));
        char name[32];
        std::snprintf(name, sizeof name, "%03zu-v%02d-m%d", i, code->version(), code->mask());
        image.save(QString::fromStdString(utf8Of(pathOf(dir) / (std::string(name) + ".png"))));
        std::ofstream(pathOf(dir) / (std::string(name) + ".txt"), std::ios::binary) << all[i];
    }
}

} // namespace tb::test
