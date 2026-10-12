#include "tests.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "crypto/hashes.h"
#include "spv/headerchain.h"
#include "storage/secrets.h"
#include "storage/store.h"
#include "util/log.h"
#include "util/strings.h"

namespace tb::test {

namespace {

/// A fresh directory in the temporary directory, removed with it.
struct TempDir {
    std::filesystem::path path;

    TempDir() : path(std::filesystem::temp_directory_path() / ("twotbit-test-" + toHex(Hashes::randomBytes(8)))) {
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::string str() const { return utf8Of(path); }
    std::string file(const std::string& name) const { return utf8Of(path / name); }
};

template <class E, class F>
bool throws(F f) {
    try {
        f();
    } catch (const E&) {
        return true;
    }
    return false;
}

} // namespace

void StoreTests::metaAndAddresses() {
    TempDir dir;
    auto store = Store::open(dir.file("wallet.db"));
    EXPECT(!store->meta(), "no wallet yet");
    store->saveMeta("", "vpub-test", "testnet", 1'700'000'000);
    store->saveMeta("ignored", "other", "mainnet", 1);
    auto meta = store->meta();
    QVERIFY(meta);
    EXPECT((*meta == WalletMeta{"", "vpub-test", "testnet", 1'700'000'000, 0}), "first meta kept");
    store->updateNextIndex(7);
    EXPECT_EQ(store->meta()->nextIndex, uint32_t(7), "next index");
    HDWallet wallet(vectorMnemonic, Network::testnet3());
    for (uint32_t i = 0; i < 3; ++i) {
        store->addAddress(wallet.receiveAddress(i));
        store->addAddress(wallet.receiveAddress(i));
    }
    store->addChangeAddress(wallet.changeAddress(0));
    std::vector<std::string> got, want;
    for (const auto& a : store->addresses()) got.push_back(a.address);
    for (uint32_t i = 0; i < 3; ++i) want.push_back(wallet.receiveAddress(i).address);
    EXPECT(got == want, "receive addresses");
    auto change = store->changeAddresses();
    EXPECT(change.size() == 1 && change[0].path == "m/84'/1'/0'/1/0", "change address");
    store->close();
}

void StoreTests::headersFiltersAndRewind() {
    TempDir dir;
    auto store = Store::open(dir.file("wallet.db"));
    Bytes data = fixtureData("mainnet-headers-0-4099.bin");
    std::vector<StoredHeader> headers;
    for (int i = 0; i < 50; ++i) headers.push_back(StoredHeader{i, BlockHeader::parse(ByteView(data).subspan(size_t(i) * 80, 80))});
    store->saveHeaders(headers);
    std::vector<StoredHeader> loaded;
    store->forEachHeader([&](const StoredHeader& h) { loaded.push_back(h); });
    EXPECT(loaded == headers, "headers round trip");
    EXPECT_EQ(store->headerCount(), size_t(50), "count");
    store->deleteHeaders(39);
    EXPECT_EQ(store->headerCount(), size_t(40), "after delete");

    EXPECT_EQ(store->filterResumeHeight(10), 10, "no filters");
    std::vector<StoredFilter> filters;
    for (int i = 10; i < 20; ++i)
        filters.push_back(StoredFilter{i, headers[size_t(i)].header.hash(), Hash256::fromWords(uint64_t(i), 0, 0, 0),
                                       i % 2 == 0 ? std::optional<Bytes>(Bytes{1, 2}) : std::nullopt});
    store->saveFilters(filters);
    EXPECT_EQ(store->filterResumeHeight(10), 20, "resume");
    EXPECT_EQ(store->filterResumeHeight(25), 25, "resume past the rows");
    EXPECT(store->filterHeader(15) == Hash256::fromWords(15, 0, 0, 0), "filter header");
    EXPECT(!store->filterHeader(5), "no filter header");
    store->pruneFilterData(0);
    store->deleteFilters(14);
    EXPECT_EQ(store->filterCount(), size_t(5), "filters left");

    store->saveMatch(FilterMatch{12, Hash256(), "a", Bytes{0}});
    store->saveMatch(FilterMatch{12, Hash256(), "a", Bytes{0}});
    store->saveMatch(FilterMatch{30, Hash256(), "b", Bytes{1}});
    store->deleteMatches(20);
    auto matches = store->matches();
    EXPECT(matches.size() == 1 && matches[0].address == "a", "matches");
    store->close();
}

void StoreTests::transactionsAndPending() {
    TempDir dir;
    auto store = Store::open(dir.file("wallet.db"));
    StoredTransaction a{Hash256::fromWords(1, 0, 0, 0), 5, Hash256(), Bytes{1}};
    StoredTransaction b{Hash256::fromWords(2, 0, 0, 0), 3, Hash256(), Bytes{2}};
    store->saveTransaction(a);
    store->saveTransaction(b);
    store->saveTransaction(a);
    auto txs = store->transactions();
    EXPECT(txs.size() == 2 && txs[0].height == 3 && txs[1].height == 5, "block order");
    store->deleteTransactions(4);
    EXPECT(store->transactions() == std::vector<StoredTransaction>{b}, "deleted above");

    store->savePending(PendingTransaction{a.txid, Bytes{9}, 100});
    store->savePending(PendingTransaction{a.txid, Bytes{9}, 200});
    store->savePending(PendingTransaction{b.txid, Bytes{8}, 50});
    auto pending = store->pendingTransactions();
    EXPECT(pending.size() == 2 && pending[0].seenAt == 50 && pending[1].seenAt == 100, "first seen kept");
    EXPECT_EQ(store->deletePendingBefore(60), 1, "expired");
    store->deletePending(a.txid);
    EXPECT(store->pendingTransactions().empty(), "deleted");
    store->close();
}

void StoreTests::rescansPeersSettingsRates() {
    TempDir dir;
    auto store = Store::open(dir.file("wallet.db"));
    store->addRescan(RescanJob{"x", 50});
    store->addRescan(RescanJob{"x", 70});
    store->addRescan(RescanJob{"y", 10});
    EXPECT((store->rescans() == std::vector<RescanJob>{{"y", 10}, {"x", 50}}), "lower height kept");
    store->deleteRescan(RescanJob{"x", 60});
    EXPECT_EQ(store->rescans().size(), size_t(2), "queued again from lower");
    store->deleteRescan(RescanJob{"x", 50});
    auto rescans = store->rescans();
    EXPECT(rescans.size() == 1 && rescans[0].address == "y", "finished");

    store->savePeer("1.2.3.4", 8333);
    store->recordPeerResult("1.2.3.4", 8333, true, 40);
    store->recordPeerResult("1.2.3.4", 8333, false, 90);
    store->upsertPeer("1.2.3.4", 8333, 1097, 30);
    store->updatePeerServices("abc.onion", 8333, 64);
    auto peers = store->peers();
    std::sort(peers.begin(), peers.end(), [](const PeerRecord& x, const PeerRecord& y) { return x.host < y.host; });
    EXPECT((peers == std::vector<PeerRecord>{{"1.2.3.4", 8333, 1097, 30, 1, 1}, {"abc.onion", 8333, 64, 0, 0, 0}}), "peers");

    EXPECT_EQ(store->setting(Store::settingPinnedPeer), std::string(""), "unset");
    store->setSetting(Store::settingPinnedPeer, "1.2.3.4:8333");
    EXPECT_EQ(store->setting(Store::settingPinnedPeer), std::string("1.2.3.4:8333"), "set");
    store->setSetting(Store::settingPinnedPeer, "");
    EXPECT_EQ(store->setting(Store::settingPinnedPeer), std::string(""), "removed");

    EXPECT(!store->latestRate(), "no rate");
    EXPECT(store->seedRates({{1, 10}, {2, 20}}), "seeded");
    EXPECT(!store->seedRates({{3, 30}}), "seeded once");
    store->addRate(Rate{5, 50});
    EXPECT((store->latestRate() == Rate{5, 50}), "latest");
    store->close();
}

void StoreTests::walletLoader() {
    TempDir dir;
    MemorySecretStore secrets;
    OpenedWallet created = WalletLoader::open(dir.str(), Network::signet(), secrets, 1'700'000'000);
    std::string xpub = created.wallet->accountXPub();
    EXPECT_EQ(created.meta.mnemonic, std::string(""), "phrase not in the database");
    EXPECT_EQ(created.meta.network, std::string("signet"), "network");
    EXPECT(secrets.secret(xpub) == created.wallet->mnemonic(), "phrase in the secret store");
    created.store->close();
    EXPECT(std::filesystem::exists(dir.path / "twotbit-signet.db"), "database file");

    OpenedWallet reopened = WalletLoader::open(dir.str(), Network::signet(), secrets, 0);
    EXPECT_EQ(reopened.wallet->accountXPub(), xpub, "reopened");
    reopened.store->close();

    MemorySecretStore empty;
    EXPECT(throws<WalletError>([&] { WalletLoader::open(dir.str(), Network::signet(), empty, 0); }), "missing phrase");
    MemorySecretStore restored;
    EXPECT(throws<WalletError>([&] { WalletLoader::restore(vectorMnemonic, dir.str(), Network::signet(), restored); }), "other wallet's phrase");
    WalletLoader::restore("  " + upper(created.wallet->mnemonic()) + " ", dir.str(), Network::signet(), restored);
    OpenedWallet again = WalletLoader::open(dir.str(), Network::signet(), restored, 0);
    EXPECT_EQ(again.wallet->accountXPub(), xpub, "restored");
    again.store->close();
}

void StoreTests::localNetworkPhrase() {
    // A regtest wallet keeps its phrase in the database and never touches
    // the secret store.
    TempDir dir;
    MemorySecretStore secrets;
    OpenedWallet created = WalletLoader::open(dir.str(), Network::regtest(), secrets, 0);
    EXPECT_EQ(created.meta.mnemonic, created.wallet->mnemonic(), "phrase in the database");
    EXPECT(!secrets.secret(created.wallet->accountXPub()), "not in the secret store");
    created.store->close();
    OpenedWallet reopened = WalletLoader::open(dir.str(), Network::regtest(), secrets, 0);
    EXPECT_EQ(reopened.wallet->accountXPub(), created.wallet->accountXPub(), "reopened");
    EXPECT(!secrets.secret(created.wallet->accountXPub()), "still not in the secret store");
    reopened.store->close();
}

void StoreTests::bitfynDatabase() {
    // A database brought over from bitfyn, its mnemonic in the meta row,
    // opens and has the mnemonic moved into the secret store.
    TempDir dir;
    {
        auto store = Store::open(dir.file("twotbit-testnet.db"));
        HDWallet wallet(vectorMnemonic, Network::testnet3());
        store->saveMeta(vectorMnemonic, wallet.accountXPub(), "testnet3", 1);
        store->close();
    }
    std::filesystem::copy_file(dir.path / "twotbit-testnet.db", dir.path / "twotbit-signet.db");
    MemorySecretStore secrets;
    bool wrongNetwork = false;
    try {
        WalletLoader::open(dir.str(), Network::signet(), secrets, 0);
    } catch (const WalletError& e) {
        wrongNetwork = e.kind == WalletError::Kind::wrongNetwork && std::string(e.what()) == "wallet database is for network \"testnet3\", not \"signet\"";
    }
    EXPECT(wrongNetwork, "wrong network");
    OpenedWallet opened = WalletLoader::open(dir.str(), Network::testnet3(), secrets, 0);
    EXPECT_EQ(opened.meta.mnemonic, std::string(""), "moved out of the database");
    EXPECT(secrets.secret(HDWallet(vectorMnemonic, Network::testnet3()).accountXPub()) == std::optional<std::string>(vectorMnemonic), "moved in");
    opened.store->close();
}

void StoreTests::realChains() {
    // Validates every header of real chains stored by bitfyn or the wallet,
    // with the full rules: TWOTBIT_CHAIN_DB lists network=path pairs, such
    // as mainnet=C:\wallets\twotbit.db, separated by commas. The databases
    // are opened immutable and only their headers are read.
    const char* env = std::getenv("TWOTBIT_CHAIN_DB");
    if (!env) QSKIP("set TWOTBIT_CHAIN_DB to validate stored chains");
    for (const auto& pair : split(env, ',')) {
        size_t eq = pair.find('=');
        QVERIFY(eq != std::string::npos);
        const Network& network = networkNamed(pair.substr(0, eq));
        auto store = Store::openImmutable(pair.substr(eq + 1));
        HeaderChain chain(network);
        chain.reserve(store->headerCount());
        auto started = std::chrono::steady_clock::now();
        store->forEachHeader([&](const StoredHeader& stored) {
            if (stored.height != chain.height() + 1) throw std::runtime_error("gap at " + std::to_string(stored.height));
            chain.add(stored.header);
        });
        EXPECT_EQ(size_t(chain.height() + 1), store->headerCount(), network.name);
        auto took = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << network.name << ": " << chain.height() + 1 << " headers validated in " << took << " s, tip " << chain.tip()->hash.hex() << "\n";
        store->close();
    }
}

} // namespace tb::test
