#include "tests.h"

#include <ctime>
#include <map>

#include "bitcoin/uint256.h"
#include "mocknode.h"
#include "p2p/messages.h"
#include "spv/filters.h"
#include "spv/headerchain.h"

namespace tb::test {

namespace {

/// The network with its minimum difficulty raised to easyBits, as the Go
/// chain tests do to mine test headers on it.
Network easy(const Network& base) {
    Network n = base;
    n.powLimit = *Compact::target(easyBits);
    n.powLimitBits = easyBits;
    n.dnsSeeds.clear();
    return n;
}

BlockHeader headerAt(const Bytes& data, size_t i) { return BlockHeader::parse(ByteView(data).subspan(i * 80, 80)); }

/// trustedChain builds a chain of n headers after genesis, ten minutes
/// apart, without mining.
HeaderChain trustedChain(const Network& network, int n, const std::function<uint32_t(int)>& bits) {
    HeaderChain chain(network);
    chain.appendTrusted(network.genesis);
    Hash256 prev = network.genesisHash;
    for (int height = 1; height <= n; ++height) {
        BlockHeader header{4, prev, Hash256(), network.genesis.timestamp + uint32_t(height * 600), bits(height), 0};
        chain.appendTrusted(header);
        prev = header.hash();
    }
    return chain;
}

template <class F>
bool throwsChainError(F f) {
    try {
        f();
    } catch (const ChainError&) {
        return true;
    }
    return false;
}

} // namespace

void ChainTests::networkParameters() {
    for (const auto& v : fixture("spv")["networks"].items()) {
        const Network& n = networkNamed(v["name"].str());
        std::string name = v["name"].str();
        EXPECT_EQ(n.name, name, name);
        EXPECT_EQ(n.magic, uint32_t(v["net"].u64()), name);
        EXPECT_EQ(std::to_string(n.defaultPort), v["defaultPort"].str(), name);
        EXPECT_EQ(toHex(n.genesis.serialized()), v["genesis"].str(), name);
        EXPECT_EQ(n.genesisHash.hex(), v["genesisHash"].str(), name);
        EXPECT(n.powLimit == *UInt256::fromHex(v["powLimit"].str()), name);
        EXPECT_EQ(n.powLimitBits, uint32_t(v["powLimitBits"].u64()), name);
        EXPECT_EQ(n.reduceMinDifficulty, v["reduceMinDifficulty"].boolean(), name);
        EXPECT(!n.reduceMinDifficulty || n.minDiffReductionTime == v["minDiffReductionTime"].i64(), name);
        EXPECT_EQ(n.enforceBIP94, v["enforceBIP94"].boolean(), name);
        EXPECT_EQ(n.bech32HRP, v["bech32HRP"].str(), name);
        EXPECT_EQ(n.pubKeyHashID, uint8_t(v["pubKeyHashAddrID"].u64()), name);
        EXPECT_EQ(n.scriptHashID, uint8_t(v["scriptHashAddrID"].u64()), name);
        EXPECT_EQ(toHex(n.hdPublicKeyID), v["hdPublicKeyID"].str(), name);
    }
}

void ChainTests::compactBits() {
    for (const auto& v : fixture("spv")["compact"].items()) {
        uint32_t bits = uint32_t(v["bits"].u64());
        auto target = Compact::target(bits);
        std::string what = std::to_string(bits);
        if (v["error"].boolean()) {
            EXPECT(!target, what);
            continue;
        }
        auto want = v.has("target") ? UInt256::fromHex(v["target"].str()) : std::nullopt;
        if (!want) {
            // btcd decodes targets past 256 bits; Bitcoin Core rejects them.
            EXPECT(!target, what);
            continue;
        }
        EXPECT(target && *target == *want, what);
        EXPECT_EQ(Compact::bits(*want), uint32_t(v["back"].u64()), what);
    }
}

void ChainTests::retargets() {
    for (const auto& v : fixture("spv")["retarget"].items())
        EXPECT_EQ(HeaderChain::nextRetargetBits(v["first"].i64(), v["last"].i64(), uint32_t(v["oldBits"].u64()), networkNamed(v["network"].str())),
                  uint32_t(v["bits"].u64()), v["network"].str());
}

void ChainTests::realMainnetHeaders() {
    // The first 4100 mainnet headers, from btcd's test blocks, validate,
    // across the retargets at 2016 and 4032.
    Bytes data = fixtureData("mainnet-headers-0-4099.bin");
    HeaderChain chain(Network::mainnet());
    for (size_t i = 0; i < data.size() / 80; ++i) chain.add(headerAt(data, i));
    EXPECT_EQ(chain.height(), 4099, "height");
    EXPECT(chain.entry(0)->hash == Network::mainnet().genesisHash, "genesis");
    EXPECT_EQ(chain.heightOf(chain.entry(2016)->hash), 2016, "height of 2016");
    BlockHeader tampered = headerAt(data, 4099);
    tampered.nonce ^= 1;
    HeaderChain shorter(Network::mainnet());
    for (size_t i = 0; i < 4099; ++i) shorter.appendTrusted(headerAt(data, i));
    EXPECT(throwsChainError([&] { shorter.add(tampered); }), "tampered");
}

void ChainTests::chainAdd() {
    const Network& network = Network::regtest();
    HeaderChain chain(network);
    chain.add(network.genesis);
    uint32_t time = network.genesis.timestamp + 60;
    Hash256 prev = network.genesisHash;
    for (int i = 1; i <= 3; ++i) {
        BlockHeader header = mineHeader(prev, easyBits, time);
        chain.add(header);
        prev = header.hash();
        time += 60;
    }
    EXPECT_EQ(chain.height(), 3, "height");
    EXPECT(chain.tip()->hash == prev, "tip");
    EXPECT_EQ(chain.heightOf(prev), 3, "height of tip");
    EXPECT_EQ(chain.locator().size(), size_t(4), "locator");
    BlockHeader bad = mineHeader(prev, easyBits, time + 60);
    bad.bits = 0x1d00ffff;
    EXPECT(throwsChainError([&] { chain.add(bad); }), "invalid proof of work");
    bad = mineHeader(Hash256(), easyBits, time + 60);
    EXPECT(throwsChainError([&] { chain.add(bad); }), "broken linkage");
    bad = mineHeader(prev, easyBits, time - 3600);
    EXPECT(throwsChainError([&] { chain.add(bad); }), "stale timestamp");
    bad = mineHeader(prev, easyBits, uint32_t(std::time(nullptr)) + 3 * 3600);
    EXPECT(throwsChainError([&] { chain.add(bad); }), "future timestamp");
    chain.truncate(1);
    EXPECT_EQ(chain.height(), 1, "truncated");
    EXPECT_EQ(chain.heightOf(prev), -1, "truncated tip");
}

void ChainTests::powLimit() {
    // A header easier than the network's limit is refused even with its own
    // proof of work met.
    HeaderChain chain(Network::mainnet());
    chain.add(Network::mainnet().genesis);
    BlockHeader header = mineHeader(Network::mainnet().genesisHash, easyBits, Network::mainnet().genesis.timestamp + 600);
    EXPECT(throwsChainError([&] { chain.add(header); }), "easy header");
}

void ChainTests::bitsWithinPeriod() {
    // Within a difficulty period a header must keep its parent's bits.
    Network network = easy(Network::signet());
    HeaderChain chain(network);
    chain.add(network.genesis);
    uint32_t harder = Compact::bits(*Compact::target(easyBits) >> 1);
    BlockHeader header = mineHeader(network.genesisHash, harder, network.genesis.timestamp + 600);
    EXPECT(throwsChainError([&] { chain.add(header); }), "changed bits");
}

void ChainTests::testnetMinDifficulty() {
    for (const Network* base : {&Network::testnet3(), &Network::testnet4()}) {
        Network network = easy(*base);
        HeaderChain chain(network);
        chain.add(network.genesis);
        uint32_t late = network.genesis.timestamp + 21 * 60;
        BlockHeader header = mineHeader(network.genesisHash, easyBits, late);
        chain.add(header);
        header = mineHeader(header.hash(), easyBits, late + 1);
        EXPECT(throwsChainError([&] { chain.add(header); }), network.name + ": min difficulty without the gap");
    }
}

void ChainTests::testnetDifficultyAtLimit() {
    for (const Network* base : {&Network::testnet3(), &Network::testnet4()}) {
        Network network = easy(*base);
        HeaderChain chain = trustedChain(network, HeaderChain::retargetInterval + 1, [](int) { return easyBits; });
        ChainEntry parent = *chain.tip();
        chain.add(mineHeader(parent.hash, easyBits, parent.time + 60));
        EXPECT_EQ(chain.height(), HeaderChain::retargetInterval + 2, network.name);
    }
}

void ChainTests::retargetBIP94() {
    const uint32_t periodBits = 0x2000ffff;
    Network network = easy(Network::testnet4());
    int last = 2 * HeaderChain::retargetInterval - 1;
    HeaderChain chain = trustedChain(network, last, [&](int h) { return h == last ? easyBits : periodBits; });
    ChainEntry first = *chain.entry(HeaderChain::retargetInterval);
    ChainEntry parent = *chain.tip();
    uint32_t want = HeaderChain::nextRetargetBits(first.time, parent.time, periodBits, network);
    uint32_t testnet3 = HeaderChain::nextRetargetBits(first.time, parent.time, easyBits, network);
    EXPECT(want != testnet3, "BIP94 differs");
    uint32_t late = parent.time + 21 * 60;
    EXPECT(throwsChainError([&] { chain.add(mineHeader(parent.hash, easyBits, late)); }), "min difficulty at the retarget");
    EXPECT(throwsChainError([&] { chain.add(mineHeader(parent.hash, testnet3, late)); }), "retarget from the last block");
    EXPECT(throwsChainError([&] { chain.add(mineHeader(parent.hash, want, parent.time - 11 * 60)); }), "time warp");
    chain.add(mineHeader(parent.hash, want, parent.time - 9 * 60));
}

void ChainTests::firstHeightAtOrAfter() {
    const Network& network = Network::regtest();
    HeaderChain chain = trustedChain(network, 30, [](int) { return easyBits; });
    int64_t base = network.genesis.timestamp;
    EXPECT_EQ(chain.firstHeightAtOrAfter(0), 0, "zero");
    EXPECT_EQ(chain.firstHeightAtOrAfter(base + 600 * 7), 7, "exact");
    EXPECT_EQ(chain.firstHeightAtOrAfter(base + 600 * 7 - 1), 7, "before");
    EXPECT_EQ(chain.firstHeightAtOrAfter(base + 600 * 31), 31, "after the tip");
}

void ChainTests::heightIndex() {
    // The compact index finds every height through growth and truncation,
    // a first eight bytes shared by two hashes included.
    const Network& network = Network::regtest();
    HeaderChain chain = trustedChain(network, 5000, [](int) { return easyBits; });
    std::vector<Hash256> hashes;
    for (int h = 0; h <= 5000; ++h) hashes.push_back(chain.entry(h)->hash);
    for (int h = 0; h <= 5000; h += 7) EXPECT_EQ(chain.heightOf(hashes[size_t(h)]), h, "height " + std::to_string(h));
    chain.truncate(2500);
    for (int h = 2490; h <= 2510; ++h) EXPECT_EQ(chain.heightOf(hashes[size_t(h)]), h <= 2500 ? h : -1, "after truncate " + std::to_string(h));
    for (int h = 0; h <= 2500; h += 11) EXPECT_EQ(chain.heightOf(hashes[size_t(h)]), h, "kept " + std::to_string(h));
    Hash256 twin = hashes[100];
    twin.bytes[31] ^= 1;
    EXPECT_EQ(chain.heightOf(twin), -1, "same first word, other hash");
    HeaderChain copy = chain;
    copy.truncate(10);
    EXPECT_EQ(chain.heightOf(hashes[2000]), 2000, "copy truncated alone");
    EXPECT_EQ(copy.heightOf(hashes[2000]), -1, "copy truncated");
}

void ChainTests::gcsVectors() {
    for (const auto& v : fixture("spv")["gcs"].items()) {
        Hash256 blockHash = *Hash256::fromBytes(v["key"].hex());
        Bytes filter = v["filter"].hex();
        EXPECT_EQ(GCSFilter::hash(filter).hex(), v["hash"].str(), v["key"].str());
        std::vector<bool> hits = GCSFilter::matchScripts(filter, blockHash, v["queries"].hexes());
        std::vector<bool> want;
        for (const auto& m : v["matches"].items()) want.push_back(m.boolean());
        EXPECT(hits == want, v["key"].str());
        EXPECT(GCSFilter::build(v["items"].hexes(), blockHash) == filter, v["key"].str());
    }
}

void ChainTests::realBlockFilters() {
    for (const auto& v : fixture("spv")["blockFilters"].items()) {
        std::string what = "block " + std::to_string(v["height"].i64());
        Hash256 blockHash = *Hash256::fromHex(v["blockHash"].str());
        Bytes filter = v["filter"].hex();
        EXPECT_EQ(GCSFilter::hash(filter).hex(), v["filterHash"].str(), what);
        std::vector<Bytes> scripts = v.has("scripts") ? v["scripts"].hexes() : std::vector<Bytes>();
        std::vector<bool> hits = GCSFilter::matchScripts(filter, blockHash, scripts);
        EXPECT(std::all_of(hits.begin(), hits.end(), [](bool b) { return b; }), what);
        if (v.has("absent") && v.has("absentMatches")) {
            std::vector<bool> want;
            for (const auto& m : v["absentMatches"].items()) want.push_back(m.boolean());
            EXPECT(GCSFilter::matchScripts(filter, blockHash, v["absent"].hexes()) == want, what);
        }
        if (v.has("prevHeader") && v.has("header") && !v["prevHeader"].str().empty())
            EXPECT_EQ(GCSFilter::header(*Hash256::fromHex(v["filterHash"].str()), *Hash256::fromHex(v["prevHeader"].str())).hex(), v["header"].str(),
                      what);
        std::vector<Bytes> unique;
        for (const auto& s : scripts)
            if (std::find(unique.begin(), unique.end(), s) == unique.end()) unique.push_back(s);
        EXPECT(GCSFilter::build(unique, blockHash) == filter, what);
    }
}

void ChainTests::emptyFilter() {
    Bytes filter = GCSFilter::build({}, Hash256());
    EXPECT(filter == Bytes{0}, "empty");
    EXPECT(GCSFilter::matchScripts(filter, Hash256(), {Bytes{1}}) == std::vector<bool>{false}, "no match");
}

void ChainTests::bloomVectors() {
    for (const auto& v : fixture("sync")["blooms"].items()) {
        std::string what = std::to_string(v["elements"].u64()) + " elements, tweak " + std::to_string(v["tweak"].u64());
        BloomFilter filter(uint32_t(v["elements"].u64()), uint32_t(v["tweak"].u64()), v["fpRate"].dbl());
        for (const auto& item : v["items"].hexes()) filter.add(item);
        for (const auto& op : v["outPoints"].strings()) filter.add(*OutPoint::parse(op));
        EXPECT_EQ(toHex(filter.data()), v["filter"].str(), what);
        EXPECT_EQ(filter.hashFuncs(), uint32_t(v["hashFuncs"].u64()), what);
        EXPECT_EQ(toHex(Messages::payload(msg::FilterLoad{filter})), v["message"].str(), what);
        for (const auto& item : v["items"].hexes()) EXPECT(filter.contains(item), what);
    }
}

void ChainTests::blocks() {
    for (const auto& v : fixture("spv")["blocks"].items()) {
        std::string name = v["name"].str();
        Block block = Block::parse(v["hex"].hex());
        EXPECT_EQ(toHex(block.serialized()), v["hex"].str(), name);
        EXPECT_EQ(block.hash().hex(), v["hash"].str(), name);
        std::vector<std::string> txids, wtxids;
        for (const auto& tx : block.transactions) {
            txids.push_back(tx.txid().hex());
            wtxids.push_back(tx.wtxid().hex());
        }
        EXPECT(txids == v["txids"].strings(), name);
        EXPECT(wtxids == v["wtxids"].strings(), name);
        EXPECT_EQ(block.header.merkleRoot.hex(), v["merkleRoot"].str(), name);
        bool verified = true;
        try {
            block.verify(*Hash256::fromHex(v["hash"].str()));
        } catch (const BlockError&) {
            verified = false;
        }
        EXPECT_EQ(verified, v["valid"].boolean(), name);
    }
    Block first = Block::parse(fixture("spv")["blocks"][0]["hex"].hex());
    bool threw = false;
    try {
        first.verify(Hash256());
    } catch (const BlockError&) {
        threw = true;
    }
    EXPECT(threw, "wrong hash");
}

} // namespace tb::test
