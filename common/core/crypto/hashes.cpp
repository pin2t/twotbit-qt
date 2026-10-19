#include "crypto/hashes.h"

#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <random>
#endif

namespace tb {

namespace {

inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint64_t rotl64(uint64_t x, int n) { return (x << n) | (x >> (64 - n)); }

inline uint32_t loadBE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline uint64_t loadBE64(const uint8_t* p) { return uint64_t(loadBE32(p)) << 32 | loadBE32(p + 4); }
inline uint32_t loadLE32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
inline uint64_t loadLE64(const uint8_t* p) { return uint64_t(loadLE32(p + 4)) << 32 | loadLE32(p); }

inline void storeBE32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}
inline void storeBE64(uint8_t* p, uint64_t v) {
    storeBE32(p, uint32_t(v >> 32));
    storeBE32(p + 4, uint32_t(v));
}

constexpr uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void compress256(std::array<uint32_t, 8>& h, const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = loadBE32(block + 4 * i);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + s1 + ch + k256[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

constexpr uint64_t k512[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
    0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
    0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
    0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
    0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
    0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
    0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
    0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
    0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
    0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

void compress512(std::array<uint64_t, 8>& h, const uint8_t* block) {
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = loadBE64(block + 8 * i);
    for (int i = 16; i < 80; ++i) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; ++i) {
        uint64_t s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + s1 + ch + k512[i] + w[i];
        uint64_t s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

} // namespace

SHA256::SHA256() : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

SHA256& SHA256::write(const uint8_t* data, size_t size) {
    size_t used = size_t(bytes_ % 64);
    bytes_ += size;
    if (used > 0) {
        size_t take = std::min(size, 64 - used);
        std::memcpy(buffer_.data() + used, data, take);
        data += take;
        size -= take;
        if (used + take < 64) return *this;
        compress256(state_, buffer_.data());
    }
    while (size >= 64) {
        compress256(state_, data);
        data += 64;
        size -= 64;
    }
    if (size > 0) std::memcpy(buffer_.data(), data, size);
    return *this;
}

void SHA256::finalize(uint8_t out[32]) {
    uint64_t bits = bytes_ * 8;
    static const uint8_t pad[64] = {0x80};
    write(pad, 1 + ((119 - (bytes_ % 64)) % 64));
    uint8_t length[8];
    storeBE64(length, bits);
    write(length, 8);
    for (int i = 0; i < 8; ++i) storeBE32(out + 4 * i, state_[size_t(i)]);
}

SHA512::SHA512()
    : state_{0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
             0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179} {}

SHA512& SHA512::write(const uint8_t* data, size_t size) {
    size_t used = size_t(bytes_ % 128);
    bytes_ += size;
    if (used > 0) {
        size_t take = std::min(size, 128 - used);
        std::memcpy(buffer_.data() + used, data, take);
        data += take;
        size -= take;
        if (used + take < 128) return *this;
        compress512(state_, buffer_.data());
    }
    while (size >= 128) {
        compress512(state_, data);
        data += 128;
        size -= 128;
    }
    if (size > 0) std::memcpy(buffer_.data(), data, size);
    return *this;
}

void SHA512::finalize(uint8_t out[64]) {
    uint64_t bits = bytes_ * 8;
    static const uint8_t pad[128] = {0x80};
    write(pad, 1 + ((239 - (bytes_ % 128)) % 128));
    uint8_t length[16] = {};
    storeBE64(length + 8, bits);
    write(length, 16);
    for (int i = 0; i < 8; ++i) storeBE64(out + 8 * i, state_[size_t(i)]);
}

namespace Hashes {

Bytes sha256(ByteView data) {
    Bytes out(32);
    SHA256().write(data).finalize(out.data());
    return out;
}

Hash256 sha256d(ByteView data) {
    uint8_t first[32];
    SHA256().write(data).finalize(first);
    Hash256 h;
    SHA256().write(first, 32).finalize(h.bytes.data());
    return h;
}

Bytes hash160(ByteView data) { return RIPEMD160::hash(sha256(data)); }

Bytes sha512(ByteView data) {
    Bytes out(64);
    SHA512().write(data).finalize(out.data());
    return out;
}

namespace {

/// HMAC-SHA512 with the inner and outer pads hashed once, so PBKDF2 hashes
/// only the data of every round.
class HMACSHA512 {
public:
    explicit HMACSHA512(ByteView key) {
        uint8_t block[128] = {};
        if (key.size() > 128) {
            SHA512().write(key).finalize(block);
        } else if (!key.empty()) {
            std::memcpy(block, key.data(), key.size());
        }
        uint8_t pad[128];
        for (int i = 0; i < 128; ++i) pad[i] = block[i] ^ 0x5c;
        outer_.write(pad, 128);
        for (int i = 0; i < 128; ++i) pad[i] = block[i] ^ 0x36;
        inner_.write(pad, 128);
    }

    void mac(ByteView data, uint8_t out[64]) const {
        uint8_t innerHash[64];
        SHA512 inner = inner_;
        inner.write(data).finalize(innerHash);
        SHA512 outer = outer_;
        outer.write(innerHash, 64).finalize(out);
    }

private:
    SHA512 inner_, outer_;
};

} // namespace

Bytes hmacSHA512(ByteView key, ByteView data) {
    Bytes out(64);
    HMACSHA512(key).mac(data, out.data());
    return out;
}

Bytes pbkdf2SHA512(ByteView password, ByteView salt, uint32_t rounds, size_t length) {
    HMACSHA512 prf(password);
    Bytes out;
    out.reserve(length);
    for (uint32_t blockIndex = 1; out.size() < length; ++blockIndex) {
        Bytes first(salt.begin(), salt.end());
        uint8_t counter[4];
        storeBE32(counter, blockIndex);
        first.insert(first.end(), counter, counter + 4);
        uint8_t u[64], t[64];
        prf.mac(first, u);
        std::memcpy(t, u, 64);
        for (uint32_t r = 1; r < rounds; ++r) {
            prf.mac(ByteView(u, 64), u);
            for (int i = 0; i < 64; ++i) t[i] ^= u[i];
        }
        size_t take = std::min<size_t>(64, length - out.size());
        out.insert(out.end(), t, t + take);
    }
    return out;
}

Bytes randomBytes(size_t count) {
    Bytes out(count);
    if (count == 0) return out;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, out.data(), ULONG(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("no secure random bytes");
#elif defined(__APPLE__)
    arc4random_buf(out.data(), count);
#else
    std::random_device device;
    for (auto& b : out) b = uint8_t(device());
#endif
    return out;
}

uint64_t randomUInt64() {
    Bytes b = randomBytes(8);
    return loadLE64(b.data());
}

} // namespace Hashes

namespace RIPEMD160 {

namespace {

constexpr int rl[80] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
    3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12, 1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
    4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13,
};
constexpr int rr[80] = {
    5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12, 6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
    15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13, 8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
    12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11,
};
constexpr int sl[80] = {
    11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8, 7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
    11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5, 11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
    9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6,
};
constexpr int sr[80] = {
    8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6, 9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
    9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5, 15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
    8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11,
};
constexpr uint32_t kl[5] = {0x00000000, 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xA953FD4E};
constexpr uint32_t kr[5] = {0x50A28BE6, 0x5C4DD124, 0x6D703EF3, 0x7A6D76E9, 0x00000000};

uint32_t f(int j, uint32_t x, uint32_t y, uint32_t z) {
    switch (j / 16) {
    case 0: return x ^ y ^ z;
    case 1: return (x & y) | (~x & z);
    case 2: return (x | ~y) ^ z;
    case 3: return (x & z) | (y & ~z);
    default: return x ^ (y | ~z);
    }
}

void compress(uint32_t h[5], const uint32_t x[16]) {
    uint32_t al = h[0], bl = h[1], cl = h[2], dl = h[3], el = h[4];
    uint32_t ar = h[0], br = h[1], cr = h[2], dr = h[3], er = h[4];
    for (int j = 0; j < 80; ++j) {
        uint32_t t = rotl32(al + f(j, bl, cl, dl) + x[rl[j]] + kl[j / 16], sl[j]) + el;
        al = el;
        el = dl;
        dl = rotl32(cl, 10);
        cl = bl;
        bl = t;
        t = rotl32(ar + f(79 - j, br, cr, dr) + x[rr[j]] + kr[j / 16], sr[j]) + er;
        ar = er;
        er = dr;
        dr = rotl32(cr, 10);
        cr = br;
        br = t;
    }
    uint32_t t = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = t;
}

} // namespace

Bytes hash(ByteView message) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    Bytes padded(message.begin(), message.end());
    uint64_t bitLength = uint64_t(message.size()) * 8;
    padded.push_back(0x80);
    while (padded.size() % 64 != 56) padded.push_back(0);
    for (int i = 0; i < 8; ++i) padded.push_back(uint8_t(bitLength >> (8 * i)));
    uint32_t x[16];
    for (size_t block = 0; block < padded.size(); block += 64) {
        for (int i = 0; i < 16; ++i) x[i] = loadLE32(padded.data() + block + size_t(i) * 4);
        compress(h, x);
    }
    Bytes out;
    out.reserve(20);
    for (uint32_t word : h)
        for (int i = 0; i < 4; ++i) out.push_back(uint8_t(word >> (8 * i)));
    return out;
}

} // namespace RIPEMD160

namespace SipHash {

uint64_t hash(uint64_t k0, uint64_t k1, ByteView data) {
    uint64_t v0 = k0 ^ 0x736f6d6570736575ull;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dull;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ull;
    uint64_t v3 = k1 ^ 0x7465646279746573ull;
    auto round = [&] {
        v0 += v1;
        v1 = rotl64(v1, 13);
        v1 ^= v0;
        v0 = rotl64(v0, 32);
        v2 += v3;
        v3 = rotl64(v3, 16);
        v3 ^= v2;
        v0 += v3;
        v3 = rotl64(v3, 21);
        v3 ^= v0;
        v2 += v1;
        v1 = rotl64(v1, 17);
        v1 ^= v2;
        v2 = rotl64(v2, 32);
    };
    size_t blocks = data.size() / 8;
    for (size_t i = 0; i < blocks; ++i) {
        uint64_t m = loadLE64(data.data() + i * 8);
        v3 ^= m;
        round();
        round();
        v0 ^= m;
    }
    uint64_t last = uint64_t(data.size() & 0xff) << 56;
    for (size_t b = 0; b < data.size() % 8; ++b) last |= uint64_t(data[blocks * 8 + b]) << (8 * b);
    v3 ^= last;
    round();
    round();
    v0 ^= last;
    v2 ^= 0xff;
    round();
    round();
    round();
    round();
    return v0 ^ v1 ^ v2 ^ v3;
}

} // namespace SipHash

namespace Murmur3 {

uint32_t hash(uint32_t seed, ByteView data) {
    const uint32_t c1 = 0xcc9e2d51, c2 = 0x1b873593;
    uint32_t h = seed;
    size_t blocks = data.size() / 4;
    for (size_t i = 0; i < blocks; ++i) {
        uint32_t k = loadLE32(data.data() + i * 4);
        k *= c1;
        k = rotl32(k, 15);
        k *= c2;
        h ^= k;
        h = rotl32(h, 13);
        h = h * 5 + 0xe6546b64;
    }
    uint32_t k = 0;
    size_t tail = blocks * 4;
    switch (data.size() & 3) {
    case 3: k ^= uint32_t(data[tail + 2]) << 16; [[fallthrough]];
    case 2: k ^= uint32_t(data[tail + 1]) << 8; [[fallthrough]];
    case 1:
        k ^= uint32_t(data[tail]);
        k *= c1;
        k = rotl32(k, 15);
        k *= c2;
        h ^= k;
        break;
    default: break;
    }
    h ^= uint32_t(data.size());
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

} // namespace Murmur3

} // namespace tb
