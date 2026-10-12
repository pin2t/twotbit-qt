#pragma once

#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "format/format.h"
#include "options.h"
#include "rates/rates.h"
#include "storage/secrets.h"
#include "sync/engine.h"
#include "util/task.h"

/// OutPointLess orders outpoints for sets of chosen coins.
struct OutPointLess {
    bool operator()(const tb::OutPoint& a, const tb::OutPoint& b) const {
        if (a.hash != b.hash) return a.hash < b.hash;
        return a.index < b.index;
    }
};

using CoinSet = std::set<tb::OutPoint, OutPointLess>;

/// AppModel is the wallet as the window shows it: it opens the wallet, runs
/// the sync and the rates, and keeps what the views show up to date. It
/// lives on the main thread; the sync reports to it from its own threads
/// through queued calls.
class AppModel : public QObject {
    Q_OBJECT

public:
    enum class Phase { loading, ready, failed, missingPhrase };
    enum class Section { home, coins, transactions, settings };

    /// The fee rate the send form starts with, in sat/vB: the wallet has no
    /// fee estimator yet, so it is up to the user.
    static constexpr int64_t defaultFeeRate = 2;
    /// How long the pinned peer field waits after an edit before it connects
    /// to the typed peer, so typing does not dial every partial address.
    static constexpr int peerCheckDelayMs = 700;
    /// The service name the recovery phrases are kept under in the Windows
    /// Credential Manager.
    static constexpr const char* credentialService = "com.pin2t.twotbit";

    explicit AppModel(LaunchOptions options, QObject* parent = nullptr);
    ~AppModel() override;

    const LaunchOptions& options() const { return options_; }
    const tb::Network& network() const { return *options_.network; }
    Phase phase() const { return phase_; }
    const QString& failure() const { return failure_; }
    Section selection() const { return selection_; }
    void select(Section section);

    const tb::SyncStatus& status() const { return status_; }
    const QString& address() const { return address_; }
    const std::vector<tb::Coin>& coins() const { return coins_; }
    const std::vector<tb::WalletTransaction>& history() const { return history_; }
    const tb::WalletStats& stats() const { return stats_; }
    int64_t rate() const { return rate_; }
    int64_t createdAt() const { return createdAt_; }
    int64_t now() const { return now_; }
    bool isShutDown() const { return shutDown_; }

    /// The coins chosen in the Coins section, which Send starts with.
    const CoinSet& selectedCoins() const { return selectedCoins_; }
    void setSelectedCoins(CoinSet coins);

    /// The balance in US dollars at the latest rate, empty while no rate is
    /// known.
    QString usdBalance() const;
    /// The worth of an amount in US dollars, empty while no rate is known.
    QString usd(int64_t sats) const;

    QString accountXPub() const;
    QStringList phraseWords() const;

    /// Opens the wallet, creating it on first run, and starts the sync, the
    /// rates and the minute ticker. The database and the stored header chain,
    /// over 900 000 headers on mainnet, load off the main thread while the
    /// window shows progress.
    void start();

    /// Stops everything as the app quits: the sync and the rates, and closes
    /// the database.
    void shutdown();

    /// Restores the recovery phrase the Credential Manager lost, then opens
    /// the wallet. It throws when the phrase does not fit the wallet.
    void restore(const QString& phrase);

    /// Puts the text on the clipboard and says what was copied.
    void copy(const QString& text, const QString& what);

    /// Shows a short note at the bottom of the window for a while: what was
    /// done, or an error, which stays longer.
    void show(const QString& message, bool error = false);

    void openReceive() { emit receiveRequested(); }
    void openSend(const CoinSet& coins = {}) { emit sendRequested(coins); }

    // Settings

    /// The peer the sync is pinned to: the -peer option when given, the peer
    /// saved in the settings otherwise.
    QString pinnedPeer() const;
    bool peerFromFlag() const { return !options_.peer.isEmpty(); }
    const QString& peerText() const { return peerText_; }
    const QString& peerStatus() const { return peerStatus_; }
    bool peerConnected() const { return peerConnected_; }
    /// The notes under the pinned peer field.
    QStringList peerNote() const;

    /// Checks the peer in the field again, as the settings open.
    void recheckPeer();

    /// Checks the typed peer: an empty field unpins the sync, an address that
    /// is not an IP, with or without a port, is only reported, and an IP is
    /// connected to after peerCheckDelay. A newer edit abandons the check.
    void peerEdited(const QString& text);

    /// Switches the wallet to another network: the choice is saved and the
    /// app relaunches on it, each network keeping its own wallet.
    void switchNetwork(const tb::Network& next);

    // Send

    /// ChangeAddress is the change address a payment would use; stored is
    /// whether it is in the database already.
    struct ChangeAddress {
        tb::DerivedAddress derived;
        bool stored = false;
    };

    /// PlannedSend is a checked payment ready to sign.
    struct PlannedSend {
        QString address;
        tb::Bytes dest;
        tb::Spend spend;
        int64_t feeRate = 0;
        ChangeAddress change;
    };

    /// The first stored change address that has not received anything, or
    /// else the next one, derived without storing it: planning a payment must
    /// not leave addresses behind.
    ChangeAddress nextChange();

    /// Checks a payment and plans its transaction: exactly the given coins,
    /// or coins picked from the wallet's when none are given.
    PlannedSend planSend(const QString& address, int64_t amount, int64_t feeRate, const std::vector<tb::Coin>& chosen);

    /// Stores and watches the change address, signs the transaction and
    /// broadcasts it, returning its id and how many peers it went to.
    std::pair<tb::Hash256, size_t> sendPlanned(const PlannedSend& p);

    /// The fee of a planned payment with its size and rate.
    static QString feeText(const PlannedSend& p);

    /// The summary shown before a payment is signed.
    static QString confirmText(const PlannedSend& p);

    /// The most the coins, or all coins, can pay to the address at the fee
    /// rate.
    int64_t maxAmount(const QString& address, int64_t feeRate, const std::vector<tb::Coin>& chosen);

signals:
    void phaseChanged();
    void selectionChanged();
    void statusChanged();
    void walletChanged();
    void addressChanged();
    void rateChanged();
    void nowChanged();
    void selectedCoinsChanged();
    void peerChanged();
    void toast(const QString& text, bool error);
    void receiveRequested();
    void sendRequested(const CoinSet& coins);

private:
    struct Opened {
        tb::OpenedWallet wallet;
        std::unique_ptr<tb::SyncEngine> engine;
    };

    void run(std::shared_ptr<Opened> opened);
    void apply(const tb::SyncStatus& s);
    void showWallet();
    void refreshAddress();
    void rotateIfUsed();
    void loadSettings();
    void peerProbed(int edit, const QString& addr, std::exception_ptr failure);
    void stopPeerCheck();
    void usePeer(const QString& addr);

    LaunchOptions options_;
    Phase phase_ = Phase::loading;
    QString failure_;
    Section selection_ = Section::home;
    tb::SyncStatus status_;
    bool statusKnown_ = false;
    QString address_;
    std::vector<tb::Coin> coins_;
    std::vector<tb::WalletTransaction> history_;
    tb::WalletStats stats_;
    int64_t rate_ = 0;
    int64_t createdAt_ = 0;
    int64_t now_ = 0;
    CoinSet selectedCoins_;

    QString peerText_;
    QString peerStatus_;
    bool peerConnected_ = false;
    QString peerInUse_;
    int peerEdits_ = 0;
    QTimer peerTimer_;
    std::unique_ptr<tb::Task> peerCheck_;

    std::shared_ptr<tb::Store> store_;
    std::shared_ptr<tb::HDWallet> wallet_;
    std::unique_ptr<tb::SyncEngine> engine_;
    std::unique_ptr<tb::RateService> rates_;
    std::unique_ptr<tb::Task> loader_;
    std::unique_ptr<QLockFile> lock_;
    uint32_t index_ = 0;
    QTimer ticker_;
    bool starting_ = false;
    bool stopped_ = false;
    bool shutDown_ = false;
};

Q_DECLARE_METATYPE(CoinSet)
