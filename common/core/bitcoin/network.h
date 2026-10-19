#pragma once

#include <string>
#include <vector>

#include "bitcoin/primitives.h"
#include "bitcoin/uint256.h"

namespace tb {

/// PeerAddress is one candidate peer: a host, an IP address or a DNS seed
/// name, and its TCP port.
struct PeerAddress {
    std::string host;
    uint16_t port = 0;

    /// The dialable host:port form, the host in brackets when it is an IPv6
    /// address, as Go's net.JoinHostPort writes it.
    std::string description() const {
        return host.find(':') != std::string::npos ? "[" + host + "]:" + std::to_string(port) : host + ":" + std::to_string(port);
    }

    friend bool operator==(const PeerAddress&, const PeerAddress&) = default;
};

/// Network holds the parameters of one Bitcoin network the wallet runs on:
/// its P2P magic and port, genesis block, proof of work rules, address
/// encodings and DNS seeds. The values match btcd's chaincfg.
struct Network {
    enum class Kind { mainnet, testnet3, testnet4, signet, regtest, simnet };

    Kind kind = Kind::mainnet;
    /// The network's name in btcd: mainnet, testnet3, testnet4, signet,
    /// regtest or simnet.
    std::string name;
    /// The magic number that starts every P2P message, little-endian on the
    /// wire.
    uint32_t magic = 0;
    uint16_t defaultPort = 0;
    BlockHeader genesis;
    Hash256 genesisHash;
    UInt256 powLimit;
    uint32_t powLimitBits = 0;
    /// Whether a block more than minDiffReductionTime after its parent may
    /// have the minimum difficulty, as on the test networks.
    bool reduceMinDifficulty = false;
    int64_t minDiffReductionTime = 0;
    /// Whether BIP94 is enforced: the timewarp fix and retargeting from the
    /// first block of the period, as on testnet4.
    bool enforceBIP94 = false;
    std::string bech32HRP;
    uint8_t pubKeyHashID = 0;
    uint8_t scriptHashID = 0;
    Bytes hdPublicKeyID;
    std::vector<PeerAddress> dnsSeeds;

    /// Whether the header chain checks the difficulty of every header. The
    /// local test networks mine at the minimum difficulty and are not
    /// checked, as btcd's checks are not.
    bool checksDifficulty() const { return kind != Kind::regtest && kind != Kind::simnet; }

    /// Whether the wallet's recovery phrase is kept in the Windows
    /// Credential Manager or the Keychain. The local test networks keep it in their
    /// database instead: their coins have no value.
    bool keepsPhraseInCredentials() const { return kind != Kind::regtest && kind != Kind::simnet; }

    /// The BIP44 coin type of the BIP84 derivation: 0 on mainnet, 1 on every
    /// other network.
    uint32_t coinType() const { return kind == Kind::mainnet ? 0 : 1; }

    /// The name the network is selected by and stored under in the wallet:
    /// mainnet, testnet, testnet4, signet, regtest or simnet.
    std::string displayName() const { return kind == Kind::testnet3 ? "testnet" : name; }

    /// The wallet database file of the network: twotbit.db on mainnet and
    /// twotbit-<network>.db otherwise, so the wallets of different networks
    /// never share a file.
    std::string databaseFileName() const { return kind == Kind::mainnet ? "twotbit.db" : "twotbit-" + displayName() + ".db"; }

    friend bool operator==(const Network& a, const Network& b) { return a.kind == b.kind; }

    /// Maps a network name, as given on the command line or stored in the
    /// wallet, to its parameters.
    static const Network* named(const std::string& name);

    static const Network& mainnet();
    static const Network& testnet3();
    static const Network& testnet4();
    static const Network& signet();
    static const Network& regtest();
    static const Network& simnet();
    static const std::vector<const Network*>& all();
};

} // namespace tb
