#include "wallet/hdwallet.h"

#include <unordered_map>

#include "bitcoin/script.h"
#include "crypto/hashes.h"
#include "crypto/secp256k1.h"
#include "encoding/encodings.h"
#include "util/strings.h"
#include "wallet/bip39.h"

namespace tb {

namespace {

void appendBE32(Bytes& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

} // namespace

ExtendedKey ExtendedKey::master(ByteView seed) {
    if (seed.size() < 16 || seed.size() > 64) throw KeyError::invalidSeed();
    Bytes i = Hashes::hmacSHA512(bytesOf("Bitcoin seed"), seed);
    Bytes key(i.begin(), i.begin() + 32);
    if (!Secp256k1::isValidPrivateKey(key)) throw KeyError::invalidSeed();
    return ExtendedKey{key, Bytes(i.begin() + 32, i.end()), 0, 0, 0};
}

// A valid extended key always holds a valid private key.
Bytes ExtendedKey::publicKey() const { return Secp256k1::publicKey(privateKey); }

uint32_t ExtendedKey::fingerprint() const {
    Bytes h = Hashes::hash160(publicKey());
    return uint32_t(h[0]) << 24 | uint32_t(h[1]) << 16 | uint32_t(h[2]) << 8 | h[3];
}

ExtendedKey ExtendedKey::derive(uint32_t index) const {
    Bytes data;
    data.reserve(37);
    if (index >= hardened) {
        data.push_back(0);
        append(data, privateKey);
    } else {
        append(data, publicKey());
    }
    appendBE32(data, index);
    Bytes i = Hashes::hmacSHA512(chainCode, data);
    Bytes child = Secp256k1::tweakAdd(privateKey, ByteView(i).first(32));
    return ExtendedKey{child, Bytes(i.begin() + 32, i.end()), uint8_t(depth + 1), fingerprint(), index};
}

ExtendedKey ExtendedKey::derive(const std::vector<uint32_t>& path) const {
    ExtendedKey key = *this;
    for (uint32_t index : path) key = key.derive(index);
    return key;
}

std::string ExtendedKey::publicString(ByteView version) const {
    Bytes data(version.begin(), version.end());
    data.push_back(depth);
    appendBE32(data, parentFingerprint);
    appendBE32(data, childNumber);
    append(data, chainCode);
    append(data, publicKey());
    return Base58::encodeCheck(data);
}

std::vector<uint32_t> ExtendedKey::parsePath(const std::string& path) {
    std::vector<std::string> parts = split(path, '/');
    if (parts.size() < 2 || parts[0] != "m") throw KeyError::invalidPath(path);
    std::vector<uint32_t> out;
    for (size_t i = 1; i < parts.size(); ++i) {
        std::string digits = parts[i];
        bool isHardened = endsWith(digits, "'");
        if (isHardened) digits.pop_back();
        uint32_t n;
        if (digits.empty() || !parseUnsigned(digits, n) || n >= hardened) throw KeyError::invalidPath(path);
        out.push_back(isHardened ? n + hardened : n);
    }
    return out;
}

Bytes DerivedAddress::script() const { return Script::p2wpkhOfPubkey(pubkey); }

HDWallet::HDWallet(const std::string& mnemonic, const Network& network, const std::string& passphrase)
    : network_(network), mnemonic_(mnemonic) {
    if (!BIP39::isValid(mnemonic)) throw MnemonicError::invalidMnemonic();
    master_ = ExtendedKey::master(BIP39::seed(mnemonic, passphrase));
    const uint32_t h = ExtendedKey::hardened;
    account_ = master_.derive({purpose + h, network.coinType() + h, h});
    chains_[0] = account_.derive(receiveChain);
    chains_[1] = account_.derive(changeChain);
}

std::string HDWallet::accountXPub() const { return account_.publicString(slip132(network_.hdPublicKeyID)); }

Bytes HDWallet::slip132(ByteView version) {
    Bytes v(version.begin(), version.end());
    if (v == Bytes{0x04, 0x88, 0xb2, 0x1e}) return {0x04, 0xb2, 0x47, 0x46};
    if (v == Bytes{0x04, 0x35, 0x87, 0xcf}) return {0x04, 0x5f, 0x1c, 0xf6};
    return v;
}

DerivedAddress HDWallet::derive(uint32_t chain, uint32_t index) const {
    ExtendedKey key = chains_[chain].derive(index);
    Bytes pubkey = key.publicKey();
    std::string path = "m/" + std::to_string(purpose) + "'/" + std::to_string(network_.coinType()) + "'/0'/" + std::to_string(chain) + "/" +
                       std::to_string(index);
    return DerivedAddress{index, Address::p2wpkh(pubkey, network_), path, pubkey};
}

Bytes HDWallet::addressScript(const std::string& address) const { return Address::script(address, network_); }

Bytes HDWallet::privateKey(const std::string& path) const { return master_.derive(ExtendedKey::parsePath(path)).privateKey; }

Transaction HDWallet::sign(const Spend& spend, ByteView dest, ByteView change) const {
    Transaction tx;
    tx.version = 2;
    std::unordered_map<OutPoint, const Coin*, OutPointHasher> coins;
    for (const auto& coin : spend.inputs) {
        TxIn in;
        in.previousOutPoint = coin.outPoint;
        in.sequence = SpendPlanner::sequenceRBF;
        tx.inputs.push_back(in);
        coins[coin.outPoint] = &coin;
    }
    tx.outputs.push_back(TxOut{spend.amount, Bytes(dest.begin(), dest.end())});
    if (spend.change > 0) tx.outputs.push_back(TxOut{spend.change, Bytes(change.begin(), change.end())});
    tx.sortBIP69();
    SigHashes hashes(tx);
    for (size_t i = 0; i < tx.inputs.size(); ++i) {
        auto it = coins.find(tx.inputs[i].previousOutPoint);
        if (it == coins.end()) continue;
        const Coin& coin = *it->second;
        Bytes key;
        try {
            key = privateKey(coin.path);
        } catch (const Error& e) {
            throw SpendError::signing("key for " + coin.address + ": " + e.what());
        }
        Bytes pubkey = Secp256k1::publicKey(key);
        Bytes sighash = hashes.p2wpkh(tx, i, coin.pkScript, coin.value);
        Bytes signature = Secp256k1::sign(sighash, key);
        signature.push_back(SigHashes::all);
        tx.inputs[i].witness = {signature, pubkey};
    }
    return tx;
}

SigHashes::SigHashes(const Transaction& tx) {
    ByteWriter p, s, o;
    for (const auto& in : tx.inputs) {
        in.previousOutPoint.write(p);
        s.writeUInt32(in.sequence);
    }
    for (const auto& out : tx.outputs) out.write(o);
    prevouts = Hashes::sha256d(p.data());
    sequences = Hashes::sha256d(s.data());
    outputs = Hashes::sha256d(o.data());
}

Bytes SigHashes::p2wpkh(const Transaction& tx, size_t input, ByteView pkScript, int64_t value) const {
    ByteWriter w(160);
    w.writeInt32(tx.version);
    w.write(prevouts);
    w.write(sequences);
    tx.inputs[input].previousOutPoint.write(w);
    w.writeVarBytes(Script::p2pkh(pkScript.subspan(2)));
    w.writeInt64(value);
    w.writeUInt32(tx.inputs[input].sequence);
    w.write(outputs);
    w.writeUInt32(tx.lockTime);
    w.writeUInt32(all);
    return Hashes::sha256d(w.data()).data();
}

} // namespace tb
