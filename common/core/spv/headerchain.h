#pragma once

#include <cstdint>
#include <ctime>
#include <vector>

#include "bitcoin/network.h"
#include "bitcoin/primitives.h"

namespace tb {

/// ChainEntry is what the header chain keeps of each validated header: its
/// hash, time and difficulty bits, all the validation of later headers and
/// the sync need.
struct ChainEntry {
    Hash256 hash;
    uint32_t time = 0;
    uint32_t bits = 0;

    friend bool operator==(const ChainEntry&, const ChainEntry&) = default;
};

class ChainError : public Error {
public:
    explicit ChainError(const std::string& what) : Error(what) {}
};

/// HeightIndex finds the height of a block hash in the chain's entries: an
/// open addressing table of heights keyed by the first eight bytes of the
/// hash, the random end of it, so mainnet's chain of over 900 000 headers
/// is indexed in a few bytes a header. A hash shared by two entries, which
/// takes about 2^32 blocks to happen once, keeps the lower height.
class HeightIndex {
public:
    /// The indexed height of the key, -1 when it is not indexed.
    int32_t find(uint64_t key, const std::vector<ChainEntry>& entries) const;

    /// Indexes the height under the key, unless a lower height is indexed
    /// under it already.
    void insert(uint64_t key, int32_t height, const std::vector<ChainEntry>& entries);

    /// Removes the key, whose entry must still be in the entries.
    void erase(uint64_t key, const std::vector<ChainEntry>& entries);

    void reserve(size_t count, const std::vector<ChainEntry>& entries);

private:
    size_t home(uint64_t key) const { return size_t((key * 0x9e3779b97f4a7c15ull) >> (64 - bits_)); }
    void rehash(int bits, const std::vector<ChainEntry>& entries);

    /// Height plus one per slot, 0 for an empty slot.
    std::vector<int32_t> slots_;
    size_t count_ = 0;
    int bits_ = 0;
};

/// HeaderChain is an ordered, validated chain of block headers from the
/// genesis block. Every header added is checked as Bitcoin Core checks
/// headers: proof of work against its target and the network's limit,
/// linkage to the tip, a time after the median of the last eleven and
/// within two hours of now, and the difficulty bits the network's rules
/// require, testnet's minimum difficulty exception and testnet4's BIP94
/// included.
class HeaderChain {
public:
    /// The most a block time may be ahead of the local clock.
    static constexpr int64_t maxFutureDrift = 2 * 60 * 60;
    /// The number of past blocks whose median time a new block must pass.
    static constexpr size_t medianWindow = 11;
    /// The blocks between difficulty adjustments and the time they should
    /// take.
    static constexpr int32_t retargetInterval = 2016;
    static constexpr int64_t retargetTimespan = 2016 * 10 * 60;
    /// How far before its parent the first block of a difficulty period may
    /// be dated under BIP94.
    static constexpr int64_t maxTimeWarp = 10 * 60;

    explicit HeaderChain(Network network) : network_(std::move(network)) {}

    const Network& network() const { return network_; }

    /// The tip height, or -1 for an empty chain.
    int32_t height() const { return int32_t(entries_.size()) - 1; }

    const ChainEntry* tip() const { return entries_.empty() ? nullptr : &entries_.back(); }

    const ChainEntry* entry(int32_t height) const {
        if (height < 0 || size_t(height) >= entries_.size()) return nullptr;
        return &entries_[size_t(height)];
    }

    /// The height of the block hash, or -1 when it is not in the chain.
    int32_t heightOf(const Hash256& hash) const;

    bool contains(const Hash256& hash) const { return heightOf(hash) >= 0; }

    /// The block locator of a getheaders request: hashes from the tip back
    /// toward genesis, every one for the first eleven, then with
    /// exponentially growing steps.
    std::vector<Hash256> locator() const;

    /// Validates the header as the direct successor of the tip and appends
    /// it.
    void add(const BlockHeader& header, int64_t now = int64_t(std::time(nullptr)));

    /// Appends a header that was validated before it was stored: the
    /// wallet's own database is trusted, so only the linkage is checked when
    /// reloading.
    void appendTrusted(const BlockHeader& header);

    /// Removes every header above the height.
    void truncate(int32_t height);

    /// Reserves room for the number of headers, to load a stored chain
    /// without regrowing.
    void reserve(size_t count);

    /// The first height whose block time is at or after the unix time, or
    /// the tip height plus one when every block is older.
    int32_t firstHeightAtOrAfter(int64_t unix) const;

    /// The proof of work of the blocks above the height: the expected number
    /// of hashes to find them, 2^256 / (target + 1) each, as Bitcoin Core
    /// sums it, in floating point, which is close enough to tell branches
    /// apart.
    double workAbove(int32_t height) const;

    /// The proof of work of one block of the bits.
    static double work(uint32_t bits);

    /// The median time of the last eleven blocks.
    uint32_t medianTime() const;

    /// Checks the hash against the target of the header's bits, refusing a
    /// negative, zero or overflowing target and one easier than the
    /// network's limit, as Bitcoin Core's CheckProofOfWork does.
    void checkProofOfWork(const BlockHeader& header, const Hash256& hash) const;

    /// Checks the bits against the difficulty the rules require at the
    /// height, as Bitcoin Core's GetNextWorkRequired computes it.
    void checkDifficulty(const BlockHeader& header, int32_t height) const;

    /// The bits of the latest block at or below the height that is not a
    /// minimum difficulty exception: the difficulty of its period.
    uint32_t lastRealBits(int32_t height) const;

    /// The bits of the first block after a difficulty period: the old
    /// target scaled by the time the period took, clamped to a factor of
    /// four either way and to the network's limit.
    static uint32_t nextRetargetBits(int64_t first, int64_t last, uint32_t oldBits, const Network& network);

private:
    void append(const Hash256& hash, const BlockHeader& header);

    Network network_;
    std::vector<ChainEntry> entries_;
    HeightIndex index_;
};

} // namespace tb
