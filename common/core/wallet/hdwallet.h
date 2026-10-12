#pragma once

#include <string>
#include <vector>

#include "bitcoin/network.h"
#include "bitcoin/primitives.h"
#include "wallet/spend.h"

namespace tb {

/// ExtendedKey is a BIP32 extended private key: a private key, its chain
/// code and its place in the tree.
struct ExtendedKey {
    static constexpr uint32_t hardened = 0x80000000;

    Bytes privateKey;
    Bytes chainCode;
    uint8_t depth = 0;
    uint32_t parentFingerprint = 0;
    uint32_t childNumber = 0;

    /// The master key of a seed of 16 to 64 bytes: HMAC-SHA512 keyed with
    /// "Bitcoin seed".
    static ExtendedKey master(ByteView seed);

    /// The compressed public key.
    Bytes publicKey() const;

    /// The first four bytes of the public key's HASH160, which children name
    /// their parent by.
    uint32_t fingerprint() const;

    /// Derives the child at the index, hardened from 2^31 on, per BIP32
    /// CKDpriv.
    ExtendedKey derive(uint32_t index) const;

    /// Derives along the path, one index per step.
    ExtendedKey derive(const std::vector<uint32_t>& path) const;

    /// The neutered key serialized with the version bytes in Base58Check:
    /// the extended public key.
    std::string publicString(ByteView version) const;

    /// Parses a derivation path such as m/84'/0'/0'/1/3 into its indexes,
    /// each below 2^31 before hardening.
    static std::vector<uint32_t> parsePath(const std::string& path);
};

/// DerivedAddress is one derived wallet address with its derivation path
/// and compressed public key.
struct DerivedAddress {
    uint32_t index = 0;
    std::string address;
    std::string path;
    Bytes pubkey;

    /// The P2WPKH output script paying the address.
    Bytes script() const;

    friend bool operator==(const DerivedAddress&, const DerivedAddress&) = default;
};

/// HDWallet is a BIP84 hierarchical deterministic wallet for account 0,
/// with its external (receive) and internal (change) chains of native
/// segwit P2WPKH addresses, derived from a BIP39 mnemonic.
class HDWallet {
public:
    /// BIP84: native segwit, derivation m/84'/coin'/account'/chain/index.
    static constexpr uint32_t purpose = 84;
    static constexpr uint32_t receiveChain = 0;
    static constexpr uint32_t changeChain = 1;

    /// Restores the wallet of the mnemonic, with the optional BIP39
    /// passphrase, for the network.
    HDWallet(const std::string& mnemonic, const Network& network, const std::string& passphrase = "");

    const Network& network() const { return network_; }
    const std::string& mnemonic() const { return mnemonic_; }

    /// The account's extended public key (m/84'/coin'/0'), safe to share for
    /// watching the wallet: a zpub on mainnet and a vpub on the test
    /// networks, the SLIP-132 versions for BIP84.
    std::string accountXPub() const;

    /// The SLIP-132 BIP84 version of BIP32 public key version bytes: xpub
    /// becomes zpub and tpub vpub; others stay.
    static Bytes slip132(ByteView version);

    /// The receive address at the index of the external chain.
    DerivedAddress receiveAddress(uint32_t index) const { return derive(receiveChain, index); }

    /// The change address at the index of the internal chain.
    DerivedAddress changeAddress(uint32_t index) const { return derive(changeChain, index); }

    DerivedAddress derive(uint32_t chain, uint32_t index) const;

    /// The output script paying a destination address of the wallet's
    /// network.
    Bytes addressScript(const std::string& address) const;

    /// The private key at a derivation path such as m/84'/0'/0'/1/3.
    Bytes privateKey(const std::string& path) const;

    /// Builds the planned transaction paying the destination script, with
    /// change to the change script, and signs every input with the key at
    /// its coin's derivation path. Inputs and outputs are sorted per BIP69,
    /// so the change position reveals nothing, and every input signals RBF.
    Transaction sign(const Spend& spend, ByteView dest, ByteView change) const;

private:
    Network network_;
    std::string mnemonic_;
    ExtendedKey master_;
    ExtendedKey account_;
    ExtendedKey chains_[2];
};

/// SigHashes computes BIP143 signature hashes of segwit version 0 inputs,
/// sharing the hashes of all prevouts, sequences and outputs between them.
struct SigHashes {
    static constexpr uint8_t all = 0x01;

    Hash256 prevouts;
    Hash256 sequences;
    Hash256 outputs;

    explicit SigHashes(const Transaction& tx);

    /// The SIGHASH_ALL hash of the input spending a P2WPKH output: its
    /// script code is the P2PKH script of the key hash.
    Bytes p2wpkh(const Transaction& tx, size_t input, ByteView pkScript, int64_t value) const;
};

} // namespace tb
