#include "tests.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>

#include "bitcoin/script.h"
#include "crypto/hashes.h"
#include "mocknode.h"
#include "rates/rates.h"
#include "sync/engine.h"
#include "wallet/bip39.h"

namespace tb::test {

namespace {

/// A regtest wallet in a temporary directory for the sync tests: the BIP84
/// vector wallet with its first receive and change addresses stored.
struct TestWallet {
    std::filesystem::path dir;
    std::unique_ptr<Store> store;
    HDWallet wallet;

    explicit TestWallet(int64_t createdAt = 0, uint32_t receive = 4)
        : dir(std::filesystem::temp_directory_path() / ("twotbit-sync-" + toHex(Hashes::randomBytes(8)))),
          store(Store::open(utf8Of(dir / "w.db"))), wallet(vectorMnemonic, Network::regtest()) {
        store->saveMeta("", wallet.accountXPub(), "regtest", createdAt);
        for (uint32_t i = 0; i < receive; ++i) store->addAddress(wallet.receiveAddress(i));
        store->addChangeAddress(wallet.changeAddress(0));
    }

    ~TestWallet() {
        store->close();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    DerivedAddress address(uint32_t i) const { return wallet.receiveAddress(i); }

    void addPeer(const MockNode& node) {
        store->upsertPeer("127.0.0.1", node.port(), ServiceFlags::network | ServiceFlags::witness | ServiceFlags::compactFilters, 1);
    }
};

/// Starts the engine of the wallet and runs it until the condition holds.
std::unique_ptr<SyncEngine> sync(TestWallet& w, const std::string& pinned, Duration timeout, const std::function<bool(SyncEngine&)>& condition) {
    auto engine = std::make_unique<SyncEngine>(Network::regtest(), *w.store);
    engine->start(pinned, [](const SyncStatus&) {});
    if (!eventually(timeout, [&] { return condition(*engine); })) {
        engine->stop();
        return nullptr;
    }
    return engine;
}

std::function<bool(SyncEngine&)> synced(int32_t height) {
    return [height](SyncEngine& e) { return e.status().synced && e.chainHeight() == height; };
}

TxOut pay(int64_t value, const Bytes& script) { return TxOut{value, script}; }

} // namespace

void SyncTests::spentAddresses() {
    const Json& f = fixture("sync");
    for (const auto& v : f["spent"].items()) {
        std::vector<Bytes> witness = v.has("witness") ? v["witness"].hexes() : std::vector<Bytes>();
        EXPECT_EQ(SyncEngine::spentAddress(v["sigScript"].hex(), witness, Network::mainnet()), v["address"].str(), v["sigScript"].str());
    }
    for (const auto& v : f["scriptAddresses"].items())
        EXPECT_EQ(Address::of(v["script"].hex(), Network::mainnet()).value_or(""), v["address"].str(), v["script"].str());
    for (const auto& v : f["testnetScriptAddresses"].items())
        EXPECT_EQ(Address::of(v["script"].hex(), Network::testnet3()).value_or(""), v["address"].str(), v["script"].str());
}

void SyncTests::historiesAndBalances() {
    // The history and balances of wallets of four scripts match bitfyn's for
    // payments, spends, self transfers, coinbases and pending ones.
    for (const auto& v : fixture("sync")["histories"].items()) {
        WalletState w;
        w.initialized = true;
        std::vector<Bytes> scripts = v["walletScripts"].hexes();
        std::vector<std::string> addresses = v["walletAddresses"].strings();
        for (size_t i = 0; i < scripts.size(); ++i) {
            w.scriptIndex[scripts[i]] = w.scripts.size();
            w.scripts.push_back(WatchScript{addresses[i], "m/84'/0'/0'/0/0", scripts[i]});
        }
        std::vector<Transaction> txs;
        for (const auto& hex : v["txs"].hexes()) txs.push_back(Transaction::parse(hex));
        std::vector<int64_t> times;
        for (const auto& t : v["times"].items()) times.push_back(t.i64());
        size_t confirmed = size_t(v["confirmed"].u64());
        auto history = SyncEngine::buildHistory(txs, times, confirmed, w, Network::mainnet());
        QVERIFY(history.size() == v["history"].size());
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& got = history[i];
            const Json& want = v["history"][i];
            std::string id = want["txid"].str();
            EXPECT_EQ(got.txid.hex(), id, id);
            EXPECT_EQ(got.time, want["time"].i64(), id);
            EXPECT_EQ(got.confirmed, want["confirmed"].boolean(), id);
            EXPECT_EQ(got.net, want["net"].i64(), id);
            EXPECT_EQ(got.fee, want["fee"].i64(), id);
            std::vector<TxAddress> inputs, outputs;
            for (const auto& a : want["inputs"].items()) inputs.push_back(TxAddress{a["address"].str(), a["mine"].boolean()});
            for (const auto& a : want["outputs"].items()) outputs.push_back(TxAddress{a["address"].str(), a["mine"].boolean()});
            EXPECT(got.inputs == inputs, id + " inputs");
            EXPECT(got.outputs == outputs, id + " outputs");
        }
        std::vector<Transaction> c(txs.begin(), txs.begin() + long(confirmed)), u(txs.begin() + long(confirmed), txs.end());
        auto [balance, pending] = SyncEngine::computeBalance(c, u, [&](const Bytes& s) { return w.scriptIndex.count(s) > 0; });
        EXPECT_EQ(balance, v["balance"].i64(), "balance");
        EXPECT_EQ(pending, v["pending"].i64(), "pending");
    }
}

void SyncTests::majorityAnchors() {
    for (const auto& v : fixture("sync")["majority"].items()) {
        std::vector<Hash256> votes;
        if (v.has("votes"))
            for (const auto& s : v["votes"].strings()) votes.push_back(*Hash256::fromHex(s));
        if (v["error"].boolean()) {
            bool threw = false;
            try {
                SyncEngine::majorityAnchor(votes);
            } catch (const PeerError&) {
                threw = true;
            }
            EXPECT(threw, "no majority");
        } else {
            EXPECT_EQ(SyncEngine::majorityAnchor(votes).hex(), v["anchor"].str(), "majority");
        }
    }
}

void SyncTests::statusText() {
    SyncStatus s;
    EXPECT_EQ(s.text(), std::string("Not connected"), "no peers");
    s.peers = 2;
    s.height = 1234;
    EXPECT_EQ(s.text(), std::string("Syncing (1234)..."), "syncing");
    s.synced = true;
    EXPECT_EQ(s.text(), std::string("Connected"), "synced");
    s.peers = 9;
    EXPECT_EQ(s.bars(), 3, "bars");
}

void SyncTests::fullSync() {
    // The engine syncs headers and filters from a node, downloads the blocks
    // matching the wallet and finds its payments and spends.
    TestWallet w;
    ChainBuilder chain;
    std::vector<Block> blocks = chain.mine(5);
    Transaction pay0 = payment({pay(100'000, w.address(0).script()), pay(7, outside())}, 1);
    blocks.push_back(chain.mine({pay0}));
    Transaction pay1 = payment({pay(50'000, w.address(1).script())}, 2);
    blocks.push_back(chain.mine({pay1}));
    for (auto& b : chain.mine(3)) blocks.push_back(b);
    Transaction spend;
    spend.inputs.push_back(TxIn{OutPoint{pay0.txid(), 0}, {}, {Bytes{1}, Bytes{2}}, 0xffffffff});
    spend.outputs = {pay(60'000, outside()), pay(39'000, w.address(2).script())};
    blocks.push_back(chain.mine({spend}));
    auto node = MockNode::start(blocks);
    w.addPeer(*node);
    auto engine = sync(w, "", 30s, synced(11));
    QVERIFY2(engine, "sync to 11");
    EXPECT_EQ(engine->status().balance, int64_t(89'000), "balance");
    EXPECT_EQ(engine->status().pending, int64_t(0), "pending");
    std::vector<int64_t> values;
    for (const auto& c : engine->coins()) values.push_back(c.value);
    EXPECT((values == std::vector<int64_t>{50'000, 39'000}), "coins");
    for (const auto& c : engine->coins()) EXPECT(c.confirmed, "confirmed coin");
    auto history = engine->history();
    std::vector<int64_t> nets;
    for (const auto& t : history) nets.push_back(t.net);
    EXPECT((nets == std::vector<int64_t>{-61'000, 50'000, 100'000}), "history");
    EXPECT_EQ(history[0].fee, int64_t(1'000), "fee");
    EXPECT(engine->isUsed(w.address(0).address) && engine->isUsed(w.address(2).address) && !engine->isUsed(w.address(3).address), "used");
    EXPECT_EQ(w.store->filterCount(), size_t(12), "filters");
    EXPECT_EQ(w.store->matches().size(), size_t(4), "matches");
    EXPECT_EQ(w.store->headerCount(), size_t(12), "headers");
    engine->stop();
    engine.reset();

    SyncEngine reopened(Network::regtest(), *w.store);
    EXPECT_EQ(reopened.chainHeight(), 11, "reopened height");
    EXPECT_EQ(reopened.status().balance, int64_t(89'000), "reopened balance");
}

void SyncTests::crossCheckAndMismatch() {
    // A peer serving a filter that lacks an output script of its block is
    // outvoted by the full block and dropped; the wallet's payment in that
    // block is still found.
    TestWallet w;
    ChainBuilder chain;
    std::vector<Block> blocks = chain.mine(3);
    Transaction payTx = payment({pay(21'000, w.address(0).script())}, 3);
    Block paid = chain.mine({payTx});
    blocks.push_back(paid);
    for (auto& b : chain.mine(2)) blocks.push_back(b);
    std::vector<std::unique_ptr<MockNode>> nodes;
    for (int i = 0; i < 3; ++i) nodes.push_back(MockNode::start(blocks));
    nodes[2]->with([&](MockNode::State& s) { return s.wrongFilters[4] = GCSFilter::build({Bytes{0x51}}, paid.hash()); });
    for (auto& n : nodes) w.addPeer(*n);
    auto engine = sync(w, "", 30s, [](SyncEngine& e) { return e.status().peers == 3; });
    QVERIFY2(engine, "three peers");
    EXPECT(eventually(30s, [&] { return engine->status().synced && engine->chainHeight() == 6; }), "sync to 6");
    EXPECT_EQ(engine->status().balance, int64_t(21'000), "balance");
    engine->stop();
}

void SyncTests::relayBroadcastConfirm() {
    // Relayed payments count as pending; a broadcast payment reaches the
    // node, and both confirm once mined.
    TestWallet w;
    ChainBuilder chain;
    std::vector<Block> blocks = chain.mine(2);
    blocks.push_back(chain.mine({payment({pay(80'000, w.address(0).script())}, 4)}));
    auto node = MockNode::start(blocks);
    w.addPeer(*node);
    auto engine = sync(w, "", 30s, synced(3));
    QVERIFY2(engine, "sync to 3");

    Transaction incoming = payment({pay(12'345, w.address(1).script())}, 5);
    node->relay(incoming);
    EXPECT(eventually(10s, [&] { return engine->status().pending == 12'345; }), "pending payment");
    auto coins = engine->coins();
    EXPECT(std::any_of(coins.begin(), coins.end(), [](const Coin& c) { return !c.confirmed && c.value == 12'345; }), "pending coin");

    Coin coin = *std::find_if(coins.begin(), coins.end(), [](const Coin& c) { return c.confirmed; });
    DerivedAddress change = w.wallet.changeAddress(0);
    Spend spend = SpendPlanner::plan({coin}, true, outside(), 30'000, 2, change.script());
    Transaction tx = w.wallet.sign(spend, outside(), change.script());
    EXPECT_EQ(engine->broadcast(tx), size_t(1), "sent to one peer");
    EXPECT(eventually(10s,
                      [&] {
                          auto received = node->received();
                          return std::any_of(received.begin(), received.end(), [&](const Transaction& t) { return t.txid() == tx.txid(); });
                      }),
           "node got the payment");
    int64_t want = 80'000 + 12'345 - 30'000 - spend.fee;
    EXPECT_EQ(engine->status().balance + engine->status().pending, want, "spendable");

    node->add({chain.mine({incoming, tx})});
    node->announceTip();
    EXPECT(eventually(20s, [&] { return engine->status().synced && engine->chainHeight() == 4 && engine->status().pending == 0; }), "confirmation");
    EXPECT_EQ(engine->status().balance, want, "confirmed balance");
    for (const auto& c : engine->coins()) EXPECT(c.confirmed, "all confirmed");
    engine->stop();
}

void SyncTests::reorgs() {
    // A heavier branch from a fork replaces the blocks after it, with the
    // wallet payments in them; a lighter one is refused.
    TestWallet w;
    ChainBuilder chain;
    std::vector<Block> common = chain.mine(6);
    ChainBuilder x = chain;
    std::vector<Block> branchX{x.mine({payment({pay(11'000, w.address(0).script())}, 6)})};
    for (auto& b : x.mine(3)) branchX.push_back(b);
    std::vector<Block> all = common;
    all.insert(all.end(), branchX.begin(), branchX.end());
    auto node = MockNode::start(all);
    w.addPeer(*node);
    auto engine = sync(w, "", 30s, synced(10));
    QVERIFY2(engine, "sync to 10");
    EXPECT_EQ(engine->status().balance, int64_t(11'000), "branch x");

    ChainBuilder y = chain;
    std::vector<Block> branchY = y.mine(2);
    branchY.push_back(y.mine({payment({pay(22'000, w.address(1).script())}, 7)}));
    for (auto& b : y.mine(2)) branchY.push_back(b);
    all = common;
    all.insert(all.end(), branchY.begin(), branchY.end());
    node->reset(all);
    node->announceTip();
    EXPECT(eventually(20s, [&] { return engine->status().synced && engine->chainHeight() == 11; }), "reorg to 11");
    EXPECT_EQ(engine->status().balance, int64_t(22'000), "branch y");
    EXPECT(engine->entry(11)->hash == branchY.back().hash(), "tip of y");

    ChainBuilder z = chain;
    std::vector<Block> branchZ = z.mine(3);
    all = common;
    all.insert(all.end(), branchZ.begin(), branchZ.end());
    node->reset(all);
    node->announceTip();
    EXPECT(eventually(20s, [&] { return node->with([](MockNode::State& s) { return s.connections; }) >= 2; }), "lighter fork refused");
    std::this_thread::sleep_for(500ms);
    EXPECT_EQ(engine->chainHeight(), 11, "height kept");
    EXPECT(engine->entry(11)->hash == branchY.back().hash(), "tip kept");
    engine->stop();
}

void SyncTests::filterAnchor() {
    // A wallet created after the start of the chain downloads filters from
    // its creation on, the first filter header proven by the peers'
    // majority.
    ChainBuilder chain(Network::regtest(), uint32_t(std::time(nullptr)) - 40 * 600);
    std::vector<Block> blocks = chain.mine(20);
    int64_t created = blocks[15].header.timestamp;
    TestWallet w(created);
    blocks.push_back(chain.mine({payment({pay(5'000, w.address(0).script())}, 8)}));
    std::vector<std::unique_ptr<MockNode>> nodes;
    for (int i = 0; i < 3; ++i) nodes.push_back(MockNode::start(blocks));
    for (auto& n : nodes) w.addPeer(*n);
    auto engine = sync(w, "", 30s, synced(21));
    QVERIFY2(engine, "sync to 21");
    EXPECT_EQ(engine->filterStart(), 16 - SyncEngine::safetyGap, "filter start");
    EXPECT_EQ(w.store->filterCount(), size_t(21 - engine->filterStart() + 1), "filters");
    EXPECT_EQ(engine->status().balance, int64_t(5'000), "balance");
    engine->stop();
}

void SyncTests::watchAndRescan() {
    // Watching an address the wallet's stored transactions already paid, as
    // when another wallet on the seed used it, queues a rescan that finds its
    // spend in a block scanned before.
    TestWallet w(0, 2);
    DerivedAddress late = w.address(3);
    ChainBuilder chain;
    std::vector<Block> blocks = chain.mine(2);
    Transaction payTx = payment({pay(30'000, w.address(0).script()), pay(40'000, late.script())}, 9);
    blocks.push_back(chain.mine({payTx}));
    for (auto& b : chain.mine(2)) blocks.push_back(b);
    Transaction spendLate;
    spendLate.inputs.push_back(TxIn{OutPoint{payTx.txid(), 1}, {}, {}, 0xffffffff});
    spendLate.outputs = {pay(39'000, outside())};
    blocks.push_back(chain.mine({spendLate}));
    auto node = MockNode::start(blocks);
    w.addPeer(*node);
    auto engine = sync(w, "", 30s, synced(6));
    QVERIFY2(engine, "sync to 6");
    EXPECT_EQ(engine->status().balance, int64_t(30'000), "before watching");
    w.store->addAddress(late);
    engine->watch(late);
    EXPECT(engine->isUsed(late.address), "late address used");
    EXPECT(eventually(20s, [&] { return w.store->rescans().empty() && engine->status().synced; }), "rescan");
    EXPECT_EQ(engine->status().balance, int64_t(30'000), "spend found");
    EXPECT_EQ(engine->history().size(), size_t(2), "history");
    engine->stop();
}

void SyncTests::pinnedPeerAndServices() {
    // A pinned peer is synced from; a peer without compact filters is
    // refused.
    {
        TestWallet w;
        ChainBuilder chain;
        auto node = MockNode::start(chain.mine(4));
        auto engine = sync(w, node->address(), 30s, synced(4));
        QVERIFY2(engine, "pinned sync");
        auto peers = engine->orderedPeers();
        EXPECT(!peers.empty() && peers.front()->pinned, "pinned first");
        engine->stop();
    }
    auto plain = MockNode::start();
    plain->with([](MockNode::State& s) { return s.services = ServiceFlags{ServiceFlags::network | ServiceFlags::witness}; });
    TestWallet w2;
    SyncEngine probe(Network::regtest(), *w2.store);
    EXPECT(!probe.connect(plain->address(), false), "refused");
    EXPECT_EQ(probe.status().peers, 0, "no peers");
    auto stored = w2.store->peers();
    auto it = std::find_if(stored.begin(), stored.end(), [&](const PeerRecord& p) { return p.port == plain->port(); });
    EXPECT(it != stored.end() && it->services == (ServiceFlags::network | ServiceFlags::witness), "services stored");
}

void SyncTests::stopIsPrompt() {
    // Stopping an engine waiting on a silent peer and dialing returns in
    // well under a second's slices, every thread joined.
    TestWallet w;
    auto node = MockNode::start(ChainBuilder().mine(3));
    node->with([](MockNode::State& s) { return s.silent = true; });
    w.addPeer(*node);
    w.store->upsertPeer("192.0.2.1", 8333, ServiceFlags::network | ServiceFlags::witness | ServiceFlags::compactFilters, 1);
    SyncEngine engine(Network::regtest(), *w.store);
    engine.start("", [](const SyncStatus&) {});
    EXPECT(eventually(10s, [&] { return engine.status().peers == 1; }), "connected");
    std::this_thread::sleep_for(300ms);
    auto started = Clock::now();
    engine.stop();
    Duration took = Clock::now() - started;
    EXPECT(took < 3s, "stop took " + formatDuration(took));
}

void SyncTests::demoWallet() {
    // Builds a demo regtest wallet for screenshots in the directory named by
    // TWOTBIT_DEMO_DIR: synced from a mock node, with payments received over
    // the past weeks, a payment sent with change, and one unconfirmed payment
    // on the way. Its recovery phrase is the BIP84 test vector's, left in the
    // database, as regtest wallets keep it.
    const char* env = std::getenv("TWOTBIT_DEMO_DIR");
    if (!env) QSKIP("set TWOTBIT_DEMO_DIR to build the demo wallet");
    std::filesystem::path dir = pathOf(env);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    auto store = Store::open(utf8Of(dir / Network::regtest().databaseFileName()));
    HDWallet wallet(vectorMnemonic, Network::regtest());
    int64_t now = int64_t(std::time(nullptr));
    store->saveMeta(vectorMnemonic, wallet.accountXPub(), "regtest", now - 47 * 24 * 3600);
    for (uint32_t i = 0; i < 6; ++i) store->addAddress(wallet.receiveAddress(i));
    store->addChangeAddress(wallet.changeAddress(0));
    Rates::seed(*store);
    store->addRate(Rate{now, 11'234'567});

    ChainBuilder chain(Network::regtest(), uint32_t(now - 50 * 24 * 3600));
    std::vector<Block> blocks;
    auto days = [&](int d) { chain.time = uint32_t(now - int64_t(d) * 24 * 3600); };
    days(50);
    for (auto& b : chain.mine(3)) blocks.push_back(b);
    days(44);
    Transaction first = payment({pay(25'000'000, wallet.receiveAddress(0).script()), pay(1'000'000, outside())}, 11);
    blocks.push_back(chain.mine({first}));
    days(30);
    blocks.push_back(chain.mine({payment({pay(1'850'000, wallet.receiveAddress(1).script())}, 12)}));
    days(9);
    DerivedAddress change = wallet.changeAddress(0);
    DerivedAddress r0 = wallet.receiveAddress(0);
    Coin coin{OutPoint{first.txid(), 0}, 25'000'000, r0.script(), r0.address, r0.path, true, 0};
    Spend spend = SpendPlanner::plan({coin}, true, outside(), 7'500'000, 4, change.script());
    blocks.push_back(chain.mine({wallet.sign(spend, outside(), change.script())}));
    days(2);
    blocks.push_back(chain.mine({payment({pay(640'000, wallet.receiveAddress(2).script())}, 13)}));
    chain.time = uint32_t(now - 3 * 3600);
    for (auto& b : chain.mine(2)) blocks.push_back(b);

    auto node = MockNode::start(blocks);
    store->upsertPeer("127.0.0.1", node->port(), ServiceFlags::network | ServiceFlags::witness | ServiceFlags::compactFilters, 1);
    SyncStatus last;
    {
        SyncEngine engine(Network::regtest(), *store);
        engine.start("", [](const SyncStatus&) {});
        QVERIFY2(eventually(30s, [&] { return engine.status().synced && engine.chainHeight() == int32_t(blocks.size()); }), "demo sync");
        node->relay(payment({pay(120'000, wallet.receiveAddress(3).script())}, 14));
        QVERIFY2(eventually(10s, [&] { return engine.status().pending == 120'000; }), "pending payment");
        engine.stop();
        last = engine.status();
    }
    store->updateNextIndex(4);
    store->close();
    std::cout << "demo wallet in " << utf8Of(dir) << ": balance " << last.balance << " sat, pending " << last.pending << " sat\n";
}

void SyncTests::liveSync() {
    // Syncs a fresh wallet from the real peers of the network named by
    // TWOTBIT_LIVE, such as signet or testnet4: DNS seeds, the handshake with
    // Bitcoin Core nodes, the whole header chain and the filters since the
    // wallet's creation, its anchor proven by the peers' majority. It needs
    // the internet and takes minutes; set TWOTBIT_LOG_STDERR to watch it.
    const char* env = std::getenv("TWOTBIT_LIVE");
    if (!env) QSKIP("set TWOTBIT_LIVE to a network to sync from the real network");
    const Network& network = networkNamed(env);
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("twotbit-live-" + toHex(Hashes::randomBytes(8)));
    auto store = Store::open(utf8Of(dir / network.databaseFileName()));
    HDWallet wallet(BIP39::generateMnemonic(), network);
    store->saveMeta("", wallet.accountXPub(), network.displayName(), int64_t(std::time(nullptr)));
    store->addAddress(wallet.receiveAddress(0));
    {
        SyncEngine engine(network, *store);
        auto started = Clock::now();
        engine.start("", [](const SyncStatus&) {});
        std::string last;
        bool done = eventually(900s, [&] {
            SyncStatus s = engine.status();
            if (s.text() != last) {
                last = s.text();
                std::cout << toMilliseconds(Clock::now() - started) / 1000 << "s: " << s.text() << ", " << s.peers << " peers" << std::endl;
            }
            return s.synced && s.height > 1000;
        });
        QVERIFY2(done, ("live sync of " + network.name).c_str());
        SyncStatus s = engine.status();
        std::cout << "synced " << network.name << " to " << s.height << " in " << formatDuration(Clock::now() - started) << ": " << store->headerCount()
                  << " headers, " << store->filterCount() << " filters from " << engine.filterStart() << ", " << s.peers << " peers" << std::endl;
        EXPECT_EQ(store->filterCount(), size_t(s.height - engine.filterStart() + 1), "filters");
        engine.stop();
    }
    store->close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace tb::test
