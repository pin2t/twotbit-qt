#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "bitcoin/primitives.h"

namespace tb {

/// GCSFilter is a BIP158 basic block filter: a Golomb-Rice coded set of the
/// scripts a block creates and spends, mapped into a range with SipHash
/// keyed by the block hash.
class GCSFilter {
public:
    /// The Golomb-Rice parameter P and the false positive rate M of the
    /// basic filter type.
    static constexpr uint64_t p = 19;
    static constexpr uint64_t m = 784'931;

    /// Reads a serialized filter: the item count as a compact size, then the
    /// coded set.
    explicit GCSFilter(ByteView serialized);

    /// The number of items in the set.
    uint64_t n() const { return n_; }

    /// The SipHash key of a block's filter: the first half of the block hash
    /// in internal byte order.
    static std::pair<uint64_t, uint64_t> key(const Hash256& blockHash) { return {blockHash.word(0), blockHash.word(1)}; }

    /// The item mapped into [0, n·M) by the fast range reduction of its
    /// SipHash.
    static uint64_t map(ByteView item, std::pair<uint64_t, uint64_t> key, uint64_t modulus);

    /// Reports which of the items are in the filter of the block, in one pass
    /// over the coded set.
    std::vector<bool> match(const std::vector<Bytes>& items, const Hash256& blockHash) const;

    /// Builds the serialized filter of the items for the block, as btcd's
    /// BuildGCSFilter does: every item mapped, sorted and coded as deltas.
    static Bytes build(const std::vector<Bytes>& items, const Hash256& blockHash);

    /// The double SHA256 of a serialized filter, which cfheaders messages
    /// list and the filter header chain commits to.
    static Hash256 hash(ByteView serialized);

    /// The filter header chaining a filter hash to the previous header:
    /// SHA256d(filter hash || previous header), per BIP157. The genesis
    /// filter chains to the null hash.
    static Hash256 header(const Hash256& filterHash, const Hash256& previous);

    /// Decodes a serialized filter and reports which scripts are in it.
    static std::vector<bool> matchScripts(ByteView serialized, const Hash256& blockHash, const std::vector<Bytes>& scripts);

private:
    uint64_t n_ = 0;
    Bytes bits_;
};

/// BitReader reads a byte string most significant bit first.
class BitReader {
public:
    explicit BitReader(ByteView bytes) : bytes_(bytes) {}

    std::optional<bool> readBit() {
        if (position_ >= bytes_.size() * 8) return std::nullopt;
        bool bit = (bytes_[position_ >> 3] >> (7 - (position_ & 7))) & 1;
        ++position_;
        return bit;
    }

    std::optional<uint64_t> read(int count);

    /// A Golomb-Rice coded value: the quotient in unary, then P bits of
    /// remainder. Nothing at the end of the data.
    std::optional<uint64_t> readGolombRice(uint64_t p);

private:
    ByteView bytes_;
    size_t position_ = 0;
};

/// BitWriter writes bits most significant first, padding the last byte with
/// zeros.
class BitWriter {
public:
    void write(bool bit) {
        if (used_ == 8) {
            bytes_.push_back(0);
            used_ = 0;
        }
        if (bit) bytes_.back() |= uint8_t(1 << (7 - used_));
        ++used_;
    }

    void write(uint64_t value, int count) {
        for (int i = count - 1; i >= 0; --i) write(((value >> i) & 1) == 1);
    }

    const Bytes& bytes() const { return bytes_; }

private:
    Bytes bytes_;
    int used_ = 8;
};

/// BloomFilter is a BIP37 bloom filter as btcutil's bloom package builds
/// it, for peers to relay only the transactions matching the wallet.
class BloomFilter {
public:
    /// The protocol limits of filterload: filter bytes and hash functions.
    static constexpr size_t maxFilterSize = 36'000;
    static constexpr uint32_t maxHashFuncs = 50;
    /// BloomUpdateNone: the peer must not add matched outputs itself.
    static constexpr uint8_t updateNone = 0;

    /// A filter sized for the number of elements at the false positive rate:
    /// m = -n·ln(p)/ln(2)² bits and k = m/n·ln(2) hash functions, clamped to
    /// the protocol limits.
    BloomFilter(uint32_t elements, uint32_t tweak, double fpRate, uint8_t flags = updateNone);

    /// A filter of the given bits, as a filterload message carries it.
    BloomFilter(Bytes raw, uint32_t hashFuncs, uint32_t tweak, uint8_t flags)
        : data_(std::move(raw)), hashFuncs_(hashFuncs), tweak_(tweak), flags_(flags) {}

    const Bytes& data() const { return data_; }
    uint32_t hashFuncs() const { return hashFuncs_; }
    uint32_t tweak() const { return tweak_; }
    uint8_t flags() const { return flags_; }

    void add(ByteView item);

    /// Adds an outpoint as BIP37 matches spends of it: its 36-byte
    /// serialization.
    void add(const OutPoint& outPoint) { add(outPoint.serialized()); }

    bool contains(ByteView item) const;

private:
    size_t hash(uint32_t number, ByteView item) const;

    Bytes data_;
    uint32_t hashFuncs_ = 0;
    uint32_t tweak_ = 0;
    uint8_t flags_ = 0;
};

} // namespace tb
