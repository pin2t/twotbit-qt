#include "bitcoin/network.h"

#include "util/strings.h"

namespace tb {

namespace {

UInt256 mainPowLimit() { return *UInt256::fromHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); }
UInt256 regtestPowLimit() { return *UInt256::fromHex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); }

BlockHeader parseGenesis(const char* hex) { return BlockHeader::parse(*fromHex(hex)); }

Network make(Network n) {
    n.genesisHash = n.genesis.hash();
    return n;
}

Bytes xpubVersion() { return {0x04, 0x88, 0xb2, 0x1e}; }
Bytes tpubVersion() { return {0x04, 0x35, 0x87, 0xcf}; }

} // namespace

const Network& Network::mainnet() {
    static const Network n = make({
        .kind = Kind::mainnet, .name = "mainnet", .magic = 0xd9b4bef9, .defaultPort = 8333,
        .genesis = parseGenesis("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c"),
        .powLimit = mainPowLimit(), .powLimitBits = 0x1d00ffff, .reduceMinDifficulty = false, .minDiffReductionTime = 0, .enforceBIP94 = false,
        .bech32HRP = "bc", .pubKeyHashID = 0x00, .scriptHashID = 0x05, .hdPublicKeyID = xpubVersion(),
        .dnsSeeds = {
            {"seed.bitcoin.sipa.be", 8333},
            {"dnsseed.bluematt.me", 8333},
            {"dnsseed.bitcoin.dashjr.org", 8333},
            {"seed.bitcoin.jonasschnelli.ch", 8333},
            {"bitcoin.sprovoost.nl", 8333},
            {"dnsseed.emzy.de", 8333},
            {"seed.bitcoin.wiz.biz", 8333},
            {"seed.bitcoinstats.com", 8333},
        },
    });
    return n;
}

const Network& Network::testnet3() {
    static const Network n = make({
        .kind = Kind::testnet3, .name = "testnet3", .magic = 0x0709110b, .defaultPort = 18333,
        .genesis = parseGenesis("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4adae5494dffff001d1aa4ae18"),
        .powLimit = mainPowLimit(), .powLimitBits = 0x1d00ffff, .reduceMinDifficulty = true, .minDiffReductionTime = 1200, .enforceBIP94 = false,
        .bech32HRP = "tb", .pubKeyHashID = 0x6f, .scriptHashID = 0xc4, .hdPublicKeyID = tpubVersion(),
        .dnsSeeds = {
            {"testnet-seed.bitcoin.jonasschnelli.ch", 18333},
            {"seed.tbtc.petertodd.org", 18333},
            {"testnet-seed.bluematt.me", 18333},
            {"testnet-seed.bitcoin.sprovoost.nl", 18333},
            {"testnet-seed.bitcoin.wiz.biz", 18333},
        },
    });
    return n;
}

const Network& Network::testnet4() {
    static const Network n = make({
        .kind = Kind::testnet4, .name = "testnet4", .magic = 0x283f161c, .defaultPort = 48333,
        .genesis = parseGenesis("0100000000000000000000000000000000000000000000000000000000000000000000004e7b2b9128fe0291db0693af2ae418b767e657cd407e80cb1434221eaea7a07a046f3566ffff001dbb0c7817"),
        .powLimit = mainPowLimit(), .powLimitBits = 0x1d00ffff, .reduceMinDifficulty = true, .minDiffReductionTime = 1200, .enforceBIP94 = true,
        .bech32HRP = "tb", .pubKeyHashID = 0x6f, .scriptHashID = 0xc4, .hdPublicKeyID = tpubVersion(),
        .dnsSeeds = {
            {"seed.testnet4.bitcoin.sprovoost.nl", 48333},
            {"seed.testnet4.wiz.biz", 48333},
        },
    });
    return n;
}

const Network& Network::signet() {
    static const Network n = make({
        .kind = Kind::signet, .name = "signet", .magic = 0x40cf030a, .defaultPort = 38333,
        .genesis = parseGenesis("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a008f4d5fae77031e8ad22203"),
        .powLimit = *UInt256::fromHex("00000377ae000000000000000000000000000000000000000000000000000000"), .powLimitBits = 0x1e0377ae,
        .reduceMinDifficulty = false, .minDiffReductionTime = 1200, .enforceBIP94 = false,
        .bech32HRP = "tb", .pubKeyHashID = 0x6f, .scriptHashID = 0xc4, .hdPublicKeyID = tpubVersion(),
        .dnsSeeds = {
            {"seed.signet.bitcoin.sprovoost.nl", 38333},
            {"seed.signet.achownodes.xyz", 38333},
        },
    });
    return n;
}

const Network& Network::regtest() {
    static const Network n = make({
        .kind = Kind::regtest, .name = "regtest", .magic = 0xdab5bffa, .defaultPort = 18444,
        .genesis = parseGenesis("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4adae5494dffff7f2002000000"),
        .powLimit = regtestPowLimit(), .powLimitBits = 0x207fffff, .reduceMinDifficulty = true, .minDiffReductionTime = 1200, .enforceBIP94 = false,
        .bech32HRP = "bcrt", .pubKeyHashID = 0x6f, .scriptHashID = 0xc4, .hdPublicKeyID = tpubVersion(),
        .dnsSeeds = {},
    });
    return n;
}

const Network& Network::simnet() {
    static const Network n = make({
        .kind = Kind::simnet, .name = "simnet", .magic = 0x12141c16, .defaultPort = 18555,
        .genesis = parseGenesis("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a45068653ffff7f2002000000"),
        .powLimit = regtestPowLimit(), .powLimitBits = 0x207fffff, .reduceMinDifficulty = true, .minDiffReductionTime = 1200, .enforceBIP94 = false,
        .bech32HRP = "sb", .pubKeyHashID = 0x3f, .scriptHashID = 0x7b, .hdPublicKeyID = Bytes{0x04, 0x20, 0xbd, 0x3a},
        .dnsSeeds = {},
    });
    return n;
}

const std::vector<const Network*>& Network::all() {
    static const std::vector<const Network*> networks = {&mainnet(), &testnet3(), &testnet4(), &signet(), &regtest(), &simnet()};
    return networks;
}

const Network* Network::named(const std::string& name) {
    std::string n = lower(trimSpaces(name));
    if (n == "mainnet" || n == "bitcoin" || n == "main") return &mainnet();
    if (n == "testnet" || n == "testnet3" || n == "test") return &testnet3();
    if (n == "testnet4") return &testnet4();
    if (n == "signet") return &signet();
    if (n == "regtest") return &regtest();
    if (n == "simnet") return &simnet();
    return nullptr;
}

} // namespace tb
