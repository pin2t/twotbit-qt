#include "tests.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>

#include <filesystem>

#include "appmodel.h"
#include "bitcoin/script.h"
#include "crypto/hashes.h"
#include "mocknode.h"
#include "options.h"
#include "util/log.h"
#include "wallet/bip39.h"
#include "winauth.h"

namespace tb::test {

namespace {

/// Runs the event loop until the condition holds or the timeout passes.
bool waitFor(int ms, const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

} // namespace

void AppTests::pinterfaceIID() {
    // IAsyncOperation<UserConsentVerificationResult>, whose IID the Windows
    // SDK headers list.
    QString iid = WinAuth::pinterfaceIID(
        "pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};enum(Windows.Security.Credentials.UI.UserConsentVerificationResult;i4))");
    EXPECT_EQ(iid.toStdString(), std::string("{fd596ffd-2318-558f-9dbe-d21df43764a5}"), "IID");
}

void AppTests::launchOptions() {
    LaunchOptions o = LaunchOptions::load({"twotbit", "-net", "signet", "-datadir", "C:\\wallets\\x", "-peer", " 1.2.3.4:38333 "});
    EXPECT(*o.network == Network::signet(), "network");
    EXPECT_EQ(QDir::toNativeSeparators(o.dataDirectory).toStdString(), std::string("C:\\wallets\\x"), "data directory");
    EXPECT_EQ(o.peer.toStdString(), std::string("1.2.3.4:38333"), "peer");
    EXPECT((o.relaunchArguments() == QStringList{"-datadir", "C:\\wallets\\x"}), "relaunch keeps the data directory");
    o = LaunchOptions::load({"twotbit", "--net=testnet4", "-snapshot", "shots"});
    EXPECT(*o.network == Network::testnet4(), "--net=");
    EXPECT_EQ(o.snapshot.toStdString(), std::string("shots"), "snapshot");
    EXPECT(o.relaunchArguments().isEmpty(), "nothing to keep");
}

void AppTests::sendFlow() {
    // The window's model opens a regtest wallet, syncs it from a node, moves
    // on to a fresh address once the shown one is paid, and plans, signs and
    // broadcasts a payment with change.
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("twotbit-app-" + toHex(Hashes::randomBytes(8)));
    std::string mnemonic = BIP39::generateMnemonic();
    HDWallet wallet(mnemonic, Network::regtest());
    ChainBuilder chain;
    std::vector<Block> blocks = chain.mine(3);
    blocks.push_back(chain.mine({payment({TxOut{250'000, wallet.receiveAddress(0).script()}}, 21)}));
    blocks.push_back(chain.mine({payment({TxOut{40'000, wallet.receiveAddress(0).script()}}, 22)}));
    auto node = MockNode::start(blocks);
    {
        auto store = Store::open(utf8Of(dir / Network::regtest().databaseFileName()));
        store->saveMeta(mnemonic, wallet.accountXPub(), "regtest", 0);
        store->upsertPeer("127.0.0.1", node->port(), ServiceFlags::network | ServiceFlags::witness | ServiceFlags::compactFilters, 1);
        store->addRate(Rate{int64_t(std::time(nullptr)), 6'500'000});
        store->close();
    }
    LaunchOptions options = LaunchOptions::load({"twotbit", "-net", "regtest", "-datadir", QString::fromStdString(utf8Of(dir))});
    {
        AppModel model(options);
        model.start();
        QVERIFY2(waitFor(20'000, [&] { return model.phase() == AppModel::Phase::ready; }), "wallet open");
        QVERIFY2(waitFor(30'000, [&] { return model.status().synced && model.status().height == 5; }), "synced");
        EXPECT_EQ(model.status().balance, int64_t(290'000), "balance");
        EXPECT_EQ(model.address().toStdString(), wallet.receiveAddress(1).address, "fresh address after the payment");
        EXPECT_EQ(model.usdBalance().toStdString(), std::string("188.50 USD"), "worth");

        std::string dest = *Address::of(outside(), Network::regtest());
        AppModel::PlannedSend plan = model.planSend(QString::fromStdString(dest), 100'000, 3, {});
        EXPECT_EQ(plan.spend.inputs.size(), size_t(1), "one coin is enough");
        EXPECT(plan.spend.change > 0 && !plan.change.stored, "change to a new address");
        EXPECT(AppModel::confirmText(plan).startsWith("Pay 100 000 sats to\n" + QString::fromStdString(dest)), "confirmation");
        EXPECT(AppModel::feeText(plan).endsWith("at 3 sat/vB)"), "fee");
        auto [txid, peers] = model.sendPlanned(plan);
        EXPECT_EQ(peers, size_t(1), "broadcast");
        QVERIFY2(waitFor(10'000,
                         [&] {
                             auto received = node->received();
                             return std::any_of(received.begin(), received.end(), [&](const Transaction& t) { return t.txid() == txid; });
                         }),
                 "node got it");
        int64_t spendable = 290'000 - 100'000 - plan.spend.fee;
        EXPECT(waitFor(5'000, [&] { return model.status().balance + model.status().pending == spendable; }), "spendable after");
        // The pending change paid the change address, so the next payment
        // takes a fresh one, not stored until it is used.
        AppModel::ChangeAddress next = model.nextChange();
        EXPECT(!next.stored && next.derived.index == plan.change.derived.index + 1, "a fresh change address after the change was paid");
        bool threw = false;
        try {
            model.planSend("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", 1000, 1, {});
        } catch (const Error& e) {
            threw = std::string(e.what()) == "address is not for regtest";
        }
        EXPECT(threw, "mainnet address refused");
        model.shutdown();
        EXPECT(model.isShutDown(), "shut down");
    }
    Log::close();
    node->stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace tb::test
