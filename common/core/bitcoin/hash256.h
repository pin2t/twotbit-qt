#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tb {

/// Hash256 is a 32-byte double SHA256 hash, such as a block hash or a
/// transaction id, kept in its internal byte order. Its text form is the
/// reverse byte order the Bitcoin tools show.
struct Hash256 {
    std::array<uint8_t, 32> bytes{};

    static Hash256 zero() { return {}; }

    /// Reads 32 bytes in internal order; other lengths give nothing.
    static std::optional<Hash256> fromBytes(std::span<const uint8_t> data) {
        if (data.size() != 32) return std::nullopt;
        Hash256 h;
        std::memcpy(h.bytes.data(), data.data(), 32);
        return h;
    }

    /// Reads 32 bytes in internal order from a buffer known to hold them.
    static Hash256 fromRaw(const uint8_t* data) {
        Hash256 h;
        std::memcpy(h.bytes.data(), data, 32);
        return h;
    }

    /// The hash of four little-endian words: w0 holds bytes 0-7.
    static Hash256 fromWords(uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3) {
        Hash256 h;
        uint64_t words[4] = {w0, w1, w2, w3};
        for (int i = 0; i < 4; ++i)
            for (int b = 0; b < 8; ++b) h.bytes[size_t(i * 8 + b)] = uint8_t(words[i] >> (8 * b));
        return h;
    }

    /// Parses the displayed, byte-reversed hex form.
    static std::optional<Hash256> fromHex(std::string_view hex);

    /// The little-endian word i of the bytes, 0 to 3.
    uint64_t word(int i) const {
        uint64_t v = 0;
        for (int b = 7; b >= 0; --b) v = v << 8 | bytes[size_t(i * 8 + b)];
        return v;
    }

    std::vector<uint8_t> data() const { return {bytes.begin(), bytes.end()}; }

    /// The displayed hex form: the bytes reversed, as block explorers show
    /// block hashes and transaction ids.
    std::string hex() const;

    bool isZero() const {
        for (uint8_t b : bytes)
            if (b) return false;
        return true;
    }

    friend bool operator==(const Hash256& a, const Hash256& b) { return a.bytes == b.bytes; }

    /// Hashes compare as the numbers their displayed form spells, which is
    /// also the order BIP69 sorts transaction inputs in.
    friend bool operator<(const Hash256& a, const Hash256& b) {
        for (int i = 31; i >= 0; --i)
            if (a.bytes[size_t(i)] != b.bytes[size_t(i)]) return a.bytes[size_t(i)] < b.bytes[size_t(i)];
        return false;
    }
    friend bool operator>(const Hash256& a, const Hash256& b) { return b < a; }
};

/// Hash256Hasher keys hash tables by the first eight bytes, the random end
/// of block hashes and transaction ids.
struct Hash256Hasher {
    size_t operator()(const Hash256& h) const { return size_t(h.word(0)); }
};

} // namespace tb
