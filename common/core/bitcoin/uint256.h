#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "bitcoin/hash256.h"

namespace tb {

/// UInt256 is an unsigned 256-bit integer for proof of work: block hashes
/// read as numbers and the difficulty targets they must not exceed.
struct UInt256 {
    /// Little-endian 64-bit limbs: l[0] is the least significant.
    uint64_t l[4] = {0, 0, 0, 0};

    UInt256() = default;
    explicit UInt256(uint64_t v) : l{v, 0, 0, 0} {}
    UInt256(uint64_t l0, uint64_t l1, uint64_t l2, uint64_t l3) : l{l0, l1, l2, l3} {}

    /// The hash read as a little-endian number, as proof of work compares it
    /// against the target.
    static UInt256 of(const Hash256& hash) { return {hash.word(0), hash.word(1), hash.word(2), hash.word(3)}; }

    /// Parses big-endian hex of up to 64 digits.
    static std::optional<UInt256> fromHex(std::string_view hex);

    static UInt256 max() { return {~0ull, ~0ull, ~0ull, ~0ull}; }

    bool isZero() const { return (l[0] | l[1] | l[2] | l[3]) == 0; }

    /// The number of significant bits.
    int bitLength() const;

    friend bool operator==(const UInt256& a, const UInt256& b) {
        return a.l[0] == b.l[0] && a.l[1] == b.l[1] && a.l[2] == b.l[2] && a.l[3] == b.l[3];
    }
    friend bool operator<(const UInt256& a, const UInt256& b) {
        for (int i = 3; i >= 0; --i)
            if (a.l[i] != b.l[i]) return a.l[i] < b.l[i];
        return false;
    }
    friend bool operator>(const UInt256& a, const UInt256& b) { return b < a; }
    friend bool operator<=(const UInt256& a, const UInt256& b) { return !(b < a); }

    UInt256 operator>>(int shift) const;

    /// Shifts left, reporting whether any set bit was shifted out.
    std::pair<UInt256, bool> shiftedLeft(int shift) const;

    /// Multiplies by a small factor and divides by a small divisor, with an
    /// intermediate wide enough that the product never overflows, as the
    /// difficulty retarget does: target × actual timespan / expected
    /// timespan. A quotient beyond 256 bits saturates to the maximum.
    UInt256 scaled(uint64_t factor, uint64_t divisor) const;

    uint64_t low64() const { return l[0]; }

    /// Big-endian hex, 64 digits.
    std::string hex() const;
};

/// Compact is the 32-bit floating point encoding of a difficulty target in
/// the bits field of a block header: an exponent byte and a 23-bit mantissa
/// with a sign bit.
namespace Compact {

/// Decodes compact bits into the target. Negative and overflowing encodings
/// give nothing: Bitcoin Core rejects their blocks, and no valid target
/// needs them.
std::optional<UInt256> target(uint32_t compact);

/// Encodes a target in compact bits, rounding the mantissa down to 23 bits,
/// as btcd's BigToCompact does.
uint32_t bits(const UInt256& target);

} // namespace Compact

} // namespace tb
