#include "bitcoin/script.h"

#include <set>

#include "crypto/hashes.h"
#include "crypto/secp256k1.h"
#include "encoding/encodings.h"
#include "util/strings.h"

namespace tb {

namespace Script {

Bytes p2wpkh(ByteView hash) { return concat({Bytes{0x00, 0x14}, hash}); }

Bytes p2wpkhOfPubkey(ByteView pubkey) { return p2wpkh(Hashes::hash160(pubkey)); }

Bytes p2pkh(ByteView hash) { return concat({Bytes{opDup, opHash160, 0x14}, hash, Bytes{opEqualVerify, opCheckSig}}); }

Bytes p2sh(ByteView hash) { return concat({Bytes{opHash160, 0x14}, hash, Bytes{opEqual}}); }

Bytes witnessProgram(uint8_t version, ByteView program) {
    return concat({Bytes{uint8_t(version == 0 ? 0x00 : 0x50 + version), uint8_t(program.size())}, program});
}

Bytes p2pk(ByteView pubkey) { return concat({Bytes{uint8_t(pubkey.size())}, pubkey, Bytes{opCheckSig}}); }

bool isWitnessProgram(ByteView s) {
    if (s.size() < 4 || s.size() > 42) return false;
    if (!(s[0] == 0x00 || (s[0] >= 0x51 && s[0] <= 0x60))) return false;
    return size_t(s[1]) == s.size() - 2 && s[1] >= 2 && s[1] <= 40;
}

std::vector<Bytes> pushedData(ByteView s) {
    std::vector<Bytes> out;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t op = s[i++];
        size_t length;
        if (op == 0x00) {
            out.emplace_back();
            continue;
        } else if (op <= 0x4b) {
            length = op;
        } else if (op == 0x4c) {
            if (i + 1 > s.size()) throw DecodeError("truncated OP_PUSHDATA1");
            length = s[i];
            i += 1;
        } else if (op == 0x4d) {
            if (i + 2 > s.size()) throw DecodeError("truncated OP_PUSHDATA2");
            length = size_t(s[i]) | size_t(s[i + 1]) << 8;
            i += 2;
        } else if (op == 0x4e) {
            if (i + 4 > s.size()) throw DecodeError("truncated OP_PUSHDATA4");
            length = size_t(s[i]) | size_t(s[i + 1]) << 8 | size_t(s[i + 2]) << 16 | size_t(s[i + 3]) << 24;
            i += 4;
        } else {
            continue;
        }
        if (length > s.size() - i) throw DecodeError("push past the end of the script");
        out.emplace_back(s.begin() + long(i), s.begin() + long(i + length));
        i += length;
    }
    return out;
}

bool isPubKey(ByteView data) {
    switch (data.size()) {
    case 33: return data[0] == 0x02 || data[0] == 0x03;
    case 65: return data[0] == 0x04;
    default: return false;
    }
}

bool isControlBlock(ByteView data) { return data.size() >= 33 && (data.size() - 33) % 32 == 0 && (data[0] & 0xfe) == 0xc0; }

} // namespace Script

namespace Address {

namespace {

/// The segwit address prefixes of every network.
const std::set<std::string>& segwitHRPs() {
    static const std::set<std::string> hrps = [] {
        std::set<std::string> s;
        for (const Network* n : Network::all()) s.insert(n->bech32HRP);
        return s;
    }();
    return hrps;
}

/// The P2PKH address of a pay-to-pubkey output, of the key as it is
/// serialized there, when the key is valid.
std::optional<std::string> pubkeyAddress(ByteView pubkey, const Network& network) {
    if (!Secp256k1::isValidPublicKey(pubkey)) return std::nullopt;
    return p2pkh(Hashes::hash160(pubkey), network);
}

} // namespace

Bytes script(const std::string& text, const Network& network) {
    std::string address = trim(text);
    size_t one = address.rfind('1');
    if (one != std::string::npos && one > 1) {
        std::string hrp = lower(address.substr(0, one));
        if (segwitHRPs().count(hrp)) {
            auto decoded = Bech32::decodeSegwit(hrp, address);
            if (!decoded) throw AddressError("invalid address: not a valid segwit address");
            if (decoded->version > 1) throw AddressError("invalid address: unsupported witness version " + std::to_string(decoded->version));
            if (decoded->version != 0 && decoded->program.size() != 32)
                throw AddressError("invalid address: unsupported witness program of " + std::to_string(decoded->program.size()) + " bytes");
            if (hrp != network.bech32HRP) throw AddressError("address is not for " + network.name);
            return Script::witnessProgram(decoded->version, decoded->program);
        }
    }
    if (address.size() == 66 || address.size() == 130) {
        if (auto pubkey = fromHex(address)) {
            if (!Secp256k1::isValidPublicKey(*pubkey)) throw AddressError("invalid address: not a valid public key");
            return Script::p2pk(*pubkey);
        }
    }
    auto payload = Base58::decodeCheck(address);
    if (!payload) throw AddressError("invalid address: decoded address is of unknown format");
    if (payload->size() != 21) throw AddressError("invalid address: decoded address is of unknown size");
    uint8_t version = (*payload)[0];
    ByteView hash = ByteView(*payload).subspan(1);
    if (version == network.pubKeyHashID) return Script::p2pkh(hash);
    if (version == network.scriptHashID) return Script::p2sh(hash);
    throw AddressError("invalid address: unknown address type");
}

bool isValid(const std::string& text, const Network& network) {
    try {
        script(text, network);
        return true;
    } catch (const Error&) {
        return false;
    }
}

std::string p2pkh(ByteView hash, const Network& network) { return Base58::encodeCheck(concat({Bytes{network.pubKeyHashID}, hash})); }

std::string p2sh(ByteView hash, const Network& network) { return Base58::encodeCheck(concat({Bytes{network.scriptHashID}, hash})); }

std::string segwit(uint8_t version, ByteView program, const Network& network) {
    return Bech32::encodeSegwit(network.bech32HRP, version, program);
}

std::string p2wpkh(ByteView pubkey, const Network& network) { return segwit(0, Hashes::hash160(pubkey), network); }

std::optional<std::string> of(ByteView s, const Network& network) {
    switch (s.size()) {
    case 25:
        if (s[0] == Script::opDup && s[1] == Script::opHash160 && s[2] == 0x14 && s[23] == Script::opEqualVerify && s[24] == Script::opCheckSig)
            return p2pkh(s.subspan(3, 20), network);
        break;
    case 23:
        if (s[0] == Script::opHash160 && s[1] == 0x14 && s[22] == Script::opEqual) return p2sh(s.subspan(2, 20), network);
        break;
    case 22:
        if (s[0] == 0x00 && s[1] == 0x14) return segwit(0, s.subspan(2, 20), network);
        break;
    case 34:
        if (s[0] == 0x00 && s[1] == 0x20) return segwit(0, s.subspan(2, 32), network);
        if (s[0] == Script::op1 && s[1] == 0x20) return segwit(1, s.subspan(2, 32), network);
        break;
    case 35:
        if (s[0] == 0x21 && s[34] == Script::opCheckSig && (s[1] == 0x02 || s[1] == 0x03)) return pubkeyAddress(s.subspan(1, 33), network);
        break;
    case 67:
        if (s[0] == 0x41 && s[66] == Script::opCheckSig && (s[1] == 0x04 || s[1] == 0x06 || s[1] == 0x07))
            return pubkeyAddress(s.subspan(1, 65), network);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace Address

} // namespace tb
