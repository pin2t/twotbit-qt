#pragma once

#include <QObject>
#include <QTimer>

#include <memory>
#include <string>
#include <vector>

#include "storage/store.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace tb {

class RateError : public Error {
public:
    explicit RateError(const std::string& what) : Error(what) {}
};

/// Rates reads the BTC/USD rates: the history shipped with the app and the
/// quotes of public exchange APIs.
namespace Rates {

/// The rate history shipped with the app: daily BTC/USD rates since 2010 and
/// finer ones of late.
std::vector<Rate> embeddedHistory();

/// Fills an empty rates table with the shipped rate history.
void seed(Store& store);

/// Reads "ts,cents" rows after their header line.
std::vector<Rate> parseCSV(const std::string& text);

/// The decimal USD price in whole cents, rounded.
int64_t toCents(const std::string& price);

/// The rate to store from the source rates: their average, or the highest
/// when the lowest is more than maxSpread below it.
int64_t combine(const std::vector<int64_t>& cents);

/// {"data":{"amount":"65000.12",...}}
std::string parseCoinbase(const std::string& body);

/// The last trade price of {"error":[],"result":{"XXBTZUSD":{"c":["65000.1","0.01"],...}}}.
std::string parseKraken(const std::string& body);

/// {"last":"65000",...}
std::string parseBitstamp(const std::string& body);

} // namespace Rates

/// RateService keeps the BTC/USD rate table up to date. The table is seeded
/// from the history shipped with the app, and while the app runs it records
/// a fresh rate from public exchange APIs whenever the latest stored one gets
/// old. It lives on the thread of its event loop, the app's main thread.
class RateService : public QObject {
    Q_OBJECT

public:
    /// How often the service looks at the age of the latest rate, and the age
    /// from which it is refreshed.
    static constexpr int checkIntervalMs = 600'000;
    static constexpr int64_t maxAge = 30 * 60;
    /// The relative distance between the lowest and highest source rate
    /// above which they are not averaged: the highest is stored instead.
    static constexpr double maxSpread = 0.10;
    /// Bounds one request to a rate source.
    static constexpr int fetchTimeoutMs = 20'000;

    explicit RateService(std::shared_ptr<Store> store, QObject* parent = nullptr);
    ~RateService() override;

    /// Checks the latest rate at once and then every ten minutes, fetching a
    /// new one when it is thirty minutes old or more.
    void start();

    /// Ends the refreshing, aborting a fetch in flight.
    void stop();

signals:
    /// A new rate was stored.
    void rateStored(qint64 time, qint64 cents);

private:
    struct Fetch;

    void check();
    void finish();

    std::shared_ptr<Store> store_;
    QNetworkAccessManager* network_;
    QTimer timer_;
    std::unique_ptr<Fetch> fetch_;
};

} // namespace tb
