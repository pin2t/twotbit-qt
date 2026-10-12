#pragma once

#include "util/bytes.h"

namespace tb {

/// KeyError is a failure of key derivation or signing.
class KeyError : public Error {
public:
    enum class Kind { invalidPrivateKey, invalidChild, invalidPath, invalidSeed };

    KeyError(Kind kind, const std::string& what) : Error(what), kind(kind) {}
    static KeyError invalidPrivateKey() { return {Kind::invalidPrivateKey, "invalid private key"}; }
    static KeyError invalidChild() { return {Kind::invalidChild, "invalid child key, use the next index"}; }
    static KeyError invalidPath(const std::string& path) { return {Kind::invalidPath, "invalid derivation path \"" + path + "\""}; }
    static KeyError invalidSeed() { return {Kind::invalidSeed, "invalid seed"}; }

    Kind kind;
};

/// Secp256k1 wraps the vendored libsecp256k1 for the operations the wallet
/// needs: public keys of private keys, the BIP32 private key tweak, and
/// ECDSA signatures, deterministic per RFC6979 and with low S.
namespace Secp256k1 {

/// Whether the 32 bytes are a valid private key: not zero and below the
/// curve order.
bool isValidPrivateKey(ByteView key);

/// The 33-byte compressed public key of the private key.
Bytes publicKey(ByteView privateKey);

/// Whether the bytes parse as a compressed or uncompressed public key on
/// the curve.
bool isValidPublicKey(ByteView key);

/// The private key plus the tweak modulo the curve order, as BIP32 derives
/// child keys. It fails when the tweak is not below the order or the sum is
/// zero: BIP32 then skips to the next index.
Bytes tweakAdd(ByteView privateKey, ByteView tweak);

/// Signs the 32-byte hash with the private key and returns the DER encoded
/// signature. The nonce is derived per RFC6979, so the same key and hash
/// always give the same signature, and S is always the low one.
Bytes sign(ByteView hash, ByteView privateKey);

/// Verifies a DER signature with a low S over the hash.
bool verify(ByteView signature, ByteView hash, ByteView publicKey);

} // namespace Secp256k1

} // namespace tb
