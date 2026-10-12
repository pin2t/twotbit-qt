#include "bitcoin/uint256.h"

#include <bit>
#include <cstdio>

#include "util/bytes.h"

namespace tb {

std::optional<UInt256> UInt256::fromHex(std::string_view hex) {
    if (hex.size() > 64) return std::nullopt;
    std::string padded(64 - hex.size(), '0');
    padded += hex;
    auto bytes = tb::fromHex(padded);
    if (!bytes) return std::nullopt;
    UInt256 v;
    for (size_t i = 0; i < 32; ++i) {
        size_t limb = 3 - i / 8;
        v.l[limb] = v.l[limb] << 8 | (*bytes)[i];
    }
    return v;
}

int UInt256::bitLength() const {
    for (int i = 3; i >= 0; --i)
        if (l[i] != 0) return 64 * i + 64 - std::countl_zero(l[i]);
    return 0;
}

UInt256 UInt256::operator>>(int shift) const {
    if (shift >= 256) return {};
    if (shift <= 0) return *this;
    int words = shift / 64, bits = shift % 64;
    UInt256 out;
    for (int i = 0; i + words < 4; ++i) {
        out.l[i] = l[i + words] >> bits;
        if (bits > 0 && i + words + 1 < 4) out.l[i] |= l[i + words + 1] << (64 - bits);
    }
    return out;
}

std::pair<UInt256, bool> UInt256::shiftedLeft(int shift) const {
    if (shift <= 0) return {*this, false};
    if (shift >= 256) return {UInt256(), !isZero()};
    bool overflow = bitLength() + shift > 256;
    int words = shift / 64, bits = shift % 64;
    UInt256 out;
    for (int i = 3; i >= 0; --i) {
        if (i - words < 0) continue;
        out.l[i] = l[i - words] << bits;
        if (bits > 0 && i - words - 1 >= 0) out.l[i] |= l[i - words - 1] >> (64 - bits);
    }
    return {out, overflow};
}

UInt256 UInt256::scaled(uint64_t factor, uint64_t divisor) const {
    using u128 = unsigned __int128;
    uint64_t wide[5];
    uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        u128 product = u128(l[i]) * factor + carry;
        wide[i] = uint64_t(product);
        carry = uint64_t(product >> 64);
    }
    wide[4] = carry;
    u128 remainder = 0;
    for (int i = 4; i >= 0; --i) {
        u128 current = remainder << 64 | wide[i];
        wide[i] = uint64_t(current / divisor);
        remainder = current % divisor;
    }
    if (wide[4] != 0) return max();
    return {wide[0], wide[1], wide[2], wide[3]};
}

std::string UInt256::hex() const {
    char buf[65];
    std::snprintf(buf, sizeof buf, "%016llx%016llx%016llx%016llx", (unsigned long long)l[3], (unsigned long long)l[2],
                  (unsigned long long)l[1], (unsigned long long)l[0]);
    return buf;
}

namespace Compact {

std::optional<UInt256> target(uint32_t compact) {
    int exponent = int(compact >> 24);
    uint64_t mantissa = compact & 0x007fffff;
    bool negative = (compact & 0x00800000) != 0;
    if (mantissa != 0 && exponent == 0) return std::nullopt;
    UInt256 t;
    if (exponent <= 3) {
        t = UInt256(mantissa >> (8 * (3 - exponent)));
    } else {
        auto [value, overflow] = UInt256(mantissa).shiftedLeft(8 * (exponent - 3));
        if (overflow) return std::nullopt;
        t = value;
    }
    if (negative && !t.isZero()) return std::nullopt;
    return t;
}

uint32_t bits(const UInt256& target) {
    if (target.isZero()) return 0;
    uint32_t exponent = uint32_t((target.bitLength() + 7) / 8);
    uint64_t mantissa;
    if (exponent > 3) {
        mantissa = (target >> int(8 * (exponent - 3))).low64();
    } else {
        mantissa = target.low64();
        exponent = 3;
    }
    if ((mantissa & 0x00800000) != 0 || (mantissa >> 24) != 0) {
        mantissa >>= 8;
        exponent += 1;
    }
    return exponent << 24 | uint32_t(mantissa & 0x007fffff);
}

} // namespace Compact

} // namespace tb
