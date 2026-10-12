#pragma once

#include <array>
#include <cstdint>

#include "util/bytes.h"

namespace tb {

/// SHA256 hashes data written in parts.
class SHA256 {
public:
    SHA256();
    SHA256& write(const uint8_t* data, size_t size);
    SHA256& write(ByteView data) { return write(data.data(), data.size()); }
    void finalize(uint8_t out[32]);

private:
    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> buffer_{};
    uint64_t bytes_ = 0;
};

/// SHA512 hashes data written in parts.
class SHA512 {
public:
    SHA512();
    SHA512& write(const uint8_t* data, size_t size);
    SHA512& write(ByteView data) { return write(data.data(), data.size()); }
    void finalize(uint8_t out[64]);

private:
    std::array<uint64_t, 8> state_;
    std::array<uint8_t, 128> buffer_{};
    uint64_t bytes_ = 0;
};

/// The hash functions Bitcoin uses.
namespace Hashes {

Bytes sha256(ByteView data);

/// SHA256 applied twice, as block hashes, transaction ids and checksums
/// are.
Hash256 sha256d(ByteView data);

/// RIPEMD160 of SHA256: the 20-byte hash of public keys and scripts in
/// addresses.
Bytes hash160(ByteView data);

Bytes sha512(ByteView data);

Bytes hmacSHA512(ByteView key, ByteView data);

/// PBKDF2 with HMAC-SHA512, as BIP39 stretches a mnemonic into a seed.
Bytes pbkdf2SHA512(ByteView password, ByteView salt, uint32_t rounds, size_t length);

/// Cryptographically secure random bytes from the system.
Bytes randomBytes(size_t count);

uint64_t randomUInt64();

} // namespace Hashes

/// RIPEMD-160.
namespace RIPEMD160 {
Bytes hash(ByteView message);
}

/// SipHash-2-4, the keyed hash BIP158 filters map their items with.
namespace SipHash {
uint64_t hash(uint64_t k0, uint64_t k1, ByteView data);
}

/// MurmurHash3 x86 32-bit, the hash BIP37 bloom filters set their bits with.
namespace Murmur3 {
uint32_t hash(uint32_t seed, ByteView data);
}

} // namespace tb
