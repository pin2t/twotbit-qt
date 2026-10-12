#include "appmodel.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QProcess>
#include <QSettings>

#include <ctime>
#include <filesystem>

#include "bitcoin/script.h"
#include "sync/probe.h"
#include "util/log.h"

using namespace tb;

namespace {

/// SendError is a payment the form cannot plan or send.
class SendError : public Error {
public:
    explicit SendError(const std::string& what) : Error(what) {}
};

QString q(const std::string& s) { return QString::fromStdString(s); }

int64_t unixNow() { return int64_t(std::time(nullptr)); }

} // namespace

AppModel::AppModel(LaunchOptions options, QObject* parent) : QObject(parent), options_(std::move(options)), now_(unixNow()) {
    qRegisterMetaType<CoinSet>();
    ticker_.setInterval(60'000);
    connect(&ticker_, &QTimer::timeout, this, [this] {
        now_ = unixNow();
        emit nowChanged();
    });
}

AppModel::~AppModel() { shutdown(); }

void AppModel::select(Section section) {
    if (section == selection_) return;
    selection_ = section;
    emit selectionChanged();
}

void AppModel::setSelectedCoins(CoinSet coins) {
    if (coins == selectedCoins_) return;
    selectedCoins_ = std::move(coins);
    emit selectedCoinsChanged();
}

QString AppModel::usdBalance() const { return usd(status_.balance + status_.pending); }

QString AppModel::usd(int64_t sats) const { return rate_ > 0 ? q(Format::usd(Format::usdValue(sats, rate_))) : QString(); }

QString AppModel::accountXPub() const { return wallet_ ? q(wallet_->accountXPub()) : QString(); }

QStringList AppModel::phraseWords() const {
    if (!wallet_) return {};
    return q(wallet_->mnemonic()).split(' ', Qt::SkipEmptyParts);
}

void AppModel::start() {
    if (phase_ != Phase::loading || starting_ || stopped_) return;
    starting_ = true;
    QDir().mkpath(options_.dataDirectory);
    // One window per wallet: two would sync the same database.
    if (!lock_) {
        auto lock = std::make_unique<QLockFile>(QDir(options_.dataDirectory).filePath(q(network().databaseFileName()) + ".lock"));
        lock->setStaleLockTime(0);
        if (!lock->tryLock(200)) {
            starting_ = false;
            failure_ = "Twotbit is already running on " + q(network().displayName()) + " with the data folder " +
                       QDir::toNativeSeparators(options_.dataDirectory) + ".";
            phase_ = Phase::failed;
            emit phaseChanged();
            return;
        }
        lock_ = std::move(lock);
    }
    QPointer<AppModel> self(this);
    std::string dir = options_.dataDirectory.toStdString();
    const Network& net = network();
    loader_ = std::make_unique<Task>([self, dir, &net] {
        Log::configure(pathOf(dir) / "twotbit.log");
        Log::info("twotbit started on " + net.displayName() + ", pid " + std::to_string(QCoreApplication::applicationPid()));
        auto opened = std::make_shared<Opened>();
        std::exception_ptr failure;
        try {
            CredentialSecretStore secrets(credentialService);
            opened->wallet = WalletLoader::open(dir, net, secrets, unixNow());
            try {
                Rates::seed(*opened->wallet.store);
            } catch (const std::exception& e) {
                Log::info(std::string("rates: ") + e.what());
            }
            try {
                opened->engine = std::make_unique<SyncEngine>(net, *opened->wallet.store);
            } catch (...) {
                opened->wallet.store->close();
                throw;
            }
        } catch (...) {
            failure = std::current_exception();
        }
        QMetaObject::invokeMethod(
            qApp,
            [self, opened, failure] {
                if (!self) return;
                self->starting_ = false;
                if (!failure) {
                    self->run(opened);
                    return;
                }
                try {
                    std::rethrow_exception(failure);
                } catch (const WalletError& e) {
                    if (e.kind == WalletError::Kind::missingRecoveryPhrase) {
                        self->phase_ = Phase::missingPhrase;
                    } else {
                        Log::info(std::string("startup failed: ") + e.what());
                        self->failure_ = q(e.what());
                        self->phase_ = Phase::failed;
                    }
                } catch (const std::exception& e) {
                    Log::info(std::string("startup failed: ") + e.what());
                    self->failure_ = q(e.what());
                    self->phase_ = Phase::failed;
                }
                emit self->phaseChanged();
            },
            Qt::QueuedConnection);
    });
}

void AppModel::run(std::shared_ptr<Opened> opened) {
    if (stopped_) {
        opened->engine.reset();
        opened->wallet.store->close();
        return;
    }
    store_ = opened->wallet.store;
    wallet_ = opened->wallet.wallet;
    createdAt_ = opened->wallet.meta.createdAt;
    index_ = opened->wallet.meta.nextIndex;
    try {
        if (auto latest = store_->latestRate()) rate_ = latest->cents;
    } catch (const std::exception& e) {
        Log::info(std::string("rates: read latest rate: ") + e.what());
    }
    engine_ = std::move(opened->engine);
    refreshAddress();
    showWallet();
    loadSettings();
    QString peer = pinnedPeer();
    if (!peer.isEmpty() && options_.peer.isEmpty()) Log::info("pinned peer " + peer.toStdString() + " from the settings");
    try {
        QPointer<AppModel> self(this);
        engine_->start(peer.toStdString(), [self](const SyncStatus& s) {
            QMetaObject::invokeMethod(
                qApp,
                [self, s] {
                    if (self) self->apply(s);
                },
                Qt::QueuedConnection);
        });
    } catch (const std::exception& e) {
        Log::info(std::string("sync failed to start: ") + e.what());
        show(QString("Sync failed: ") + e.what(), true);
    }
    rates_ = std::make_unique<RateService>(store_);
    connect(rates_.get(), &RateService::rateStored, this, [this](qint64, qint64 cents) {
        rate_ = cents;
        emit rateChanged();
    });
    rates_->start();
    now_ = unixNow();
    ticker_.start();
    phase_ = Phase::ready;
    emit phaseChanged();
}

void AppModel::shutdown() {
    if (shutDown_) return;
    stopped_ = true;
    ticker_.stop();
    stopPeerCheck();
    if (loader_) loader_->join();
    if (engine_) engine_->stop();
    if (rates_) rates_->stop();
    if (store_) store_->close();
    if (store_) Log::info("twotbit stopped");
    lock_.reset();
    shutDown_ = true;
}

void AppModel::restore(const QString& phrase) {
    CredentialSecretStore secrets(credentialService);
    WalletLoader::restore(phrase.toStdString(), options_.dataDirectory.toStdString(), network(), secrets);
    phase_ = Phase::loading;
    emit phaseChanged();
    start();
}

void AppModel::apply(const SyncStatus& s) {
    if (stopped_) return;
    status_ = s;
    statusKnown_ = true;
    emit statusChanged();
    showWallet();
    rotateIfUsed();
}

void AppModel::showWallet() {
    if (!engine_) return;
    std::vector<Coin> coins = engine_->coins();
    std::vector<WalletTransaction> history = engine_->history();
    bool changed = coins != coins_ || history != history_;
    coins_ = std::move(coins);
    history_ = std::move(history);
    stats_ = WalletStats::of(history_, int(coins_.size()));
    CoinSet kept;
    for (const auto& c : coins_)
        if (selectedCoins_.count(c.outPoint)) kept.insert(c.outPoint);
    setSelectedCoins(kept);
    if (!statusKnown_) {
        status_ = engine_->status();
        statusKnown_ = true;
        emit statusChanged();
    }
    if (changed) emit walletChanged();
}

void AppModel::refreshAddress() {
    if (!wallet_ || !store_) return;
    try {
        DerivedAddress derived = wallet_->receiveAddress(index_);
        store_->addAddress(derived);
        address_ = q(derived.address);
        emit addressChanged();
        if (engine_) engine_->watch(derived);
    } catch (const std::exception& e) {
        show("Address " + QString::number(index_) + ": " + e.what(), true);
    }
}

void AppModel::rotateIfUsed() {
    if (!engine_ || !store_) return;
    while (!address_.isEmpty() && engine_->isUsed(address_.toStdString())) {
        QString used = address_;
        ++index_;
        try {
            store_->updateNextIndex(index_);
        } catch (const std::exception& e) {
            show(QString("Save next address index: ") + e.what(), true);
            return;
        }
        refreshAddress();
        Log::info("address " + used.toStdString() + " received a payment, showing next address " + address_.toStdString() + " (index " +
                  std::to_string(index_) + ")");
    }
}

void AppModel::copy(const QString& text, const QString& what) {
    QGuiApplication::clipboard()->setText(text);
    show(what + " copied");
}

void AppModel::show(const QString& message, bool error) { emit toast(message, error); }

QString AppModel::pinnedPeer() const {
    if (!options_.peer.isEmpty()) return options_.peer;
    if (!store_) return {};
    try {
        return q(store_->setting(Store::settingPinnedPeer));
    } catch (const std::exception& e) {
        Log::info(std::string("settings: read pinned peer: ") + e.what());
        return {};
    }
}

QStringList AppModel::peerNote() const {
    if (peerFromFlag()) return {"Set by the -peer flag for this run.", "A peer confirmed here takes over; the flag wins at the next start."};
    return {"Headers and filters are synced from this peer.", "Leave it empty to choose peers automatically."};
}

void AppModel::loadSettings() {
    peerInUse_ = pinnedPeer();
    peerText_ = peerInUse_;
    emit peerChanged();
}

void AppModel::recheckPeer() {
    if (phase_ != Phase::ready) return;
    peerEdited(peerText_);
}

void AppModel::peerEdited(const QString& text) {
    stopPeerCheck();
    peerText_ = text;
    QString trimmed = text.trimmed();
    peerConnected_ = false;
    if (trimmed.isEmpty()) {
        peerStatus_.clear();
        emit peerChanged();
        usePeer("");
        return;
    }
    auto parsed = PeerProbe::parse(trimmed.toStdString(), network().defaultPort);
    if (!parsed) {
        peerStatus_ = "Not connected: not an IP address or IP:port";
        emit peerChanged();
        return;
    }
    QString addr = q(*parsed);
    int edit = peerEdits_;
    peerStatus_ = "Connecting to " + addr + "…";
    emit peerChanged();
    QTimer::singleShot(peerCheckDelayMs, this, [this, edit, addr] {
        if (edit != peerEdits_ || stopped_) return;
        QPointer<AppModel> self(this);
        Network net = network();
        std::string a = addr.toStdString();
        peerCheck_ = std::make_unique<Task>([self, net, a, edit] {
            std::exception_ptr failure;
            try {
                PeerProbe::probe(net, a);
            } catch (...) {
                failure = std::current_exception();
            }
            if (task::isCancelled()) return;
            QMetaObject::invokeMethod(
                qApp,
                [self, edit, a, failure] {
                    if (self) self->peerProbed(edit, q(a), failure);
                },
                Qt::QueuedConnection);
        });
    });
}

void AppModel::peerProbed(int edit, const QString& addr, std::exception_ptr failure) {
    if (edit != peerEdits_) return;
    if (failure) {
        Log::info("settings: pinned peer " + addr.toStdString() + ": " + describe(failure));
        peerStatus_ = "Not connected: " + q(PeerProbe::reason(failure));
    } else {
        peerStatus_ = "Connected";
        peerConnected_ = true;
        usePeer(addr);
    }
    emit peerChanged();
}

void AppModel::stopPeerCheck() {
    ++peerEdits_;
    if (peerCheck_) {
        // The cancelled probe ends on its own; it is waited for only when the
        // next one starts or the app quits.
        peerCheck_->cancel();
    }
}

void AppModel::usePeer(const QString& addr) {
    if (addr == peerInUse_ || !store_ || !engine_) return;
    try {
        store_->setSetting(Store::settingPinnedPeer, addr.toStdString());
        engine_->setPinned(addr.toStdString());
    } catch (const std::exception& e) {
        show(e.what(), true);
        return;
    }
    peerInUse_ = addr;
    Log::info(addr.isEmpty() ? "settings: pinned peer removed" : "settings: pinned peer " + addr.toStdString() + " saved and in use");
}

void AppModel::switchNetwork(const Network& next) {
    QSettings().setValue("net", q(next.displayName()));
    shutdown();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), options_.relaunchArguments());
    QCoreApplication::quit();
}

AppModel::ChangeAddress AppModel::nextChange() {
    if (!wallet_ || !store_ || !engine_) throw SendError("wallet not open");
    std::vector<StoredAddress> stored = store_->changeAddresses();
    for (const auto& a : stored)
        if (!engine_->isUsed(a.address)) return {wallet_->changeAddress(a.index), true};
    return {wallet_->changeAddress(uint32_t(stored.size())), false};
}

AppModel::PlannedSend AppModel::planSend(const QString& address, int64_t amount, int64_t feeRate, const std::vector<Coin>& chosen) {
    if (!wallet_ || !engine_) throw SendError("wallet not open");
    QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) throw SendError("enter the destination address");
    Bytes dest = wallet_->addressScript(trimmed.toStdString());
    if (amount <= 0) throw SendError("enter an amount");
    ChangeAddress change = nextChange();
    bool useAll = !chosen.empty();
    try {
        Spend spend = SpendPlanner::plan(useAll ? chosen : engine_->coins(), useAll, dest, amount, feeRate, change.derived.script());
        return PlannedSend{trimmed, dest, spend, feeRate, change};
    } catch (const SpendError& e) {
        if (e.kind == SpendError::Kind::insufficientFunds && useAll) throw SendError("the selected coins do not cover the amount and the fee");
        throw;
    }
}

std::pair<Hash256, size_t> AppModel::sendPlanned(const PlannedSend& p) {
    if (!wallet_ || !store_ || !engine_) throw SendError("wallet not open");
    if (p.spend.change > 0) {
        if (!p.change.stored) store_->addChangeAddress(p.change.derived);
        engine_->watch(p.change.derived);
    }
    Transaction tx = wallet_->sign(p.spend, p.dest, p.change.derived.script());
    size_t peers = engine_->broadcast(tx);
    return {tx.txid(), peers};
}

QString AppModel::feeText(const PlannedSend& p) {
    std::vector<Bytes> outputs{p.dest};
    if (p.spend.change > 0) outputs.push_back(p.change.derived.script());
    int64_t vsize = SpendPlanner::estimateVSize(p.spend.inputs.size(), outputs);
    return q(Format::amount(p.spend.fee)) + " (" + QString::number(vsize) + " vB at " + QString::number(p.feeRate) + " sat/vB)";
}

QString AppModel::confirmText(const PlannedSend& p) {
    size_t n = p.spend.inputs.size();
    QString coins = n == 1 ? "1 coin" : QString::number(n) + " coins";
    QString text = "Pay " + q(Format::amount(p.spend.amount)) + " to\n" + p.address + "\n\nFee: " + feeText(p) + "\nTotal: " +
                   q(Format::amount(p.spend.amount + p.spend.fee)) + "\nSpending " + coins;
    if (p.spend.change > 0) text += ", " + q(Format::amount(p.spend.change)) + " back as change";
    return text;
}

int64_t AppModel::maxAmount(const QString& address, int64_t feeRate, const std::vector<Coin>& chosen) {
    std::vector<Coin> coins = chosen.empty() ? (engine_ ? engine_->coins() : std::vector<Coin>()) : chosen;
    Bytes dest(22, 0);
    if (wallet_) {
        try {
            dest = wallet_->addressScript(address.toStdString());
        } catch (const std::exception&) {
        }
    }
    return SpendPlanner::maxAmount(coins, dest, std::max<int64_t>(feeRate, 1));
}
