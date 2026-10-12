#include "spv/headerchain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tb {

namespace {

std::string hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x", v);
    return buf;
}

std::string at(const Hash256& hash, int32_t height) { return "header " + hash.hex() + " at height " + std::to_string(height) + ": "; }

} // namespace

int32_t HeightIndex::find(uint64_t key, const std::vector<ChainEntry>& entries) const {
    if (slots_.empty()) return -1;
    size_t mask = slots_.size() - 1;
    for (size_t i = home(key);; i = (i + 1) & mask) {
        int32_t slot = slots_[i];
        if (slot == 0) return -1;
        if (entries[size_t(slot - 1)].hash.word(0) == key) return slot - 1;
    }
}

void HeightIndex::insert(uint64_t key, int32_t height, const std::vector<ChainEntry>& entries) {
    if ((count_ + 1) * 2 > slots_.size()) rehash(std::max(bits_ + 1, 4), entries);
    size_t mask = slots_.size() - 1;
    size_t i = home(key);
    for (; slots_[i] != 0; i = (i + 1) & mask) {
        if (entries[size_t(slots_[i] - 1)].hash.word(0) == key) return;
    }
    slots_[i] = height + 1;
    ++count_;
}

void HeightIndex::erase(uint64_t key, const std::vector<ChainEntry>& entries) {
    if (slots_.empty()) return;
    size_t mask = slots_.size() - 1;
    size_t i = home(key);
    for (;; i = (i + 1) & mask) {
        if (slots_[i] == 0) return;
        if (entries[size_t(slots_[i] - 1)].hash.word(0) == key) break;
    }
    // Shifts the entries of the probe run after the hole back into it, so
    // every entry stays reachable from its home slot.
    for (size_t j = i;;) {
        j = (j + 1) & mask;
        if (slots_[j] == 0) break;
        size_t k = home(entries[size_t(slots_[j] - 1)].hash.word(0));
        bool stays = i <= j ? (i < k && k <= j) : (i < k || k <= j);
        if (!stays) {
            slots_[i] = slots_[j];
            i = j;
        }
    }
    slots_[i] = 0;
    --count_;
}

void HeightIndex::reserve(size_t count, const std::vector<ChainEntry>& entries) {
    int bits = std::max(bits_, 4);
    while ((size_t(1) << bits) < count * 2) ++bits;
    if (bits != bits_) rehash(bits, entries);
}

void HeightIndex::rehash(int bits, const std::vector<ChainEntry>& entries) {
    std::vector<int32_t> old;
    old.swap(slots_);
    bits_ = bits;
    slots_.assign(size_t(1) << bits, 0);
    size_t mask = slots_.size() - 1;
    for (int32_t slot : old) {
        if (slot == 0) continue;
        size_t i = home(entries[size_t(slot - 1)].hash.word(0));
        while (slots_[i] != 0) i = (i + 1) & mask;
        slots_[i] = slot;
    }
}

int32_t HeaderChain::heightOf(const Hash256& hash) const {
    int32_t h = index_.find(hash.word(0), entries_);
    if (h < 0) return -1;
    if (entries_[size_t(h)].hash == hash) return h;
    for (size_t i = entries_.size(); i-- > 0;)
        if (entries_[i].hash == hash) return int32_t(i);
    return -1;
}

std::vector<Hash256> HeaderChain::locator() const {
    if (entries_.empty()) return {Hash256()};
    std::vector<Hash256> out;
    int32_t step = 1;
    for (int32_t h = height(); h >= 0; h -= step) {
        out.push_back(entries_[size_t(h)].hash);
        if (out.size() > 10) step *= 2;
    }
    return out;
}

void HeaderChain::add(const BlockHeader& header, int64_t now) {
    int32_t h = int32_t(entries_.size());
    Hash256 hash = header.hash();
    try {
        checkProofOfWork(header, hash);
    } catch (const ChainError& e) {
        throw ChainError(at(hash, h) + e.what());
    }
    if (h == 0) {
        if (hash != network_.genesisHash) throw ChainError("first header " + hash.hex() + ", want genesis " + network_.genesisHash.hex());
    } else {
        const ChainEntry& tip = entries_[size_t(h - 1)];
        if (header.prevBlock != tip.hash) throw ChainError(at(hash, h) + "prev block " + header.prevBlock.hex() + ", want " + tip.hash.hex());
        uint32_t median = medianTime();
        if (header.timestamp <= median)
            throw ChainError(at(hash, h) + "timestamp " + std::to_string(header.timestamp) + " not after median " + std::to_string(median));
        if (int64_t(header.timestamp) > now + maxFutureDrift)
            throw ChainError(at(hash, h) + "timestamp " + std::to_string(header.timestamp) + " too far in the future");
        try {
            checkDifficulty(header, h);
        } catch (const ChainError& e) {
            throw ChainError(at(hash, h) + e.what());
        }
    }
    append(hash, header);
}

void HeaderChain::appendTrusted(const BlockHeader& header) {
    int32_t h = int32_t(entries_.size());
    Hash256 hash = header.hash();
    if (h == 0) {
        if (hash != network_.genesisHash) throw ChainError("first header " + hash.hex() + ", want genesis " + network_.genesisHash.hex());
    } else if (header.prevBlock != entries_[size_t(h - 1)].hash) {
        throw ChainError("stored header " + hash.hex() + " at height " + std::to_string(h) + ": prev block " + header.prevBlock.hex() +
                         ", want " + entries_[size_t(h - 1)].hash.hex());
    }
    append(hash, header);
}

void HeaderChain::append(const Hash256& hash, const BlockHeader& header) {
    entries_.push_back(ChainEntry{hash, header.timestamp, header.bits});
    index_.insert(hash.word(0), int32_t(entries_.size() - 1), entries_);
}

void HeaderChain::truncate(int32_t height) {
    size_t keep = size_t(std::max(height + 1, 0));
    if (keep >= entries_.size()) return;
    for (size_t h = entries_.size(); h-- > keep;) {
        uint64_t key = entries_[h].hash.word(0);
        if (index_.find(key, entries_) >= int32_t(keep)) index_.erase(key, entries_);
    }
    entries_.resize(keep);
}

void HeaderChain::reserve(size_t count) {
    entries_.reserve(count);
    index_.reserve(count, entries_);
}

int32_t HeaderChain::firstHeightAtOrAfter(int64_t unix) const {
    if (unix <= 0) return 0;
    for (size_t h = 0; h < entries_.size(); ++h)
        if (int64_t(entries_[h].time) >= unix) return int32_t(h);
    return height() + 1;
}

double HeaderChain::workAbove(int32_t height) const {
    size_t start = size_t(std::max(height + 1, 0));
    double sum = 0;
    for (size_t h = start; h < entries_.size(); ++h) sum += work(entries_[h].bits);
    return sum;
}

double HeaderChain::work(uint32_t bits) {
    auto target = Compact::target(bits);
    if (!target) return 0;
    double t = std::ldexp(double(target->l[3]), 192) + std::ldexp(double(target->l[2]), 128) + std::ldexp(double(target->l[1]), 64) +
               double(target->l[0]);
    return std::ldexp(1.0, 256) / (t + 1);
}

uint32_t HeaderChain::medianTime() const {
    size_t n = std::min(medianWindow, entries_.size());
    std::vector<uint32_t> times;
    times.reserve(n);
    for (size_t i = entries_.size() - n; i < entries_.size(); ++i) times.push_back(entries_[i].time);
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

void HeaderChain::checkProofOfWork(const BlockHeader& header, const Hash256& hash) const {
    if (header.bits == 0) throw ChainError("zero difficulty bits");
    auto target = Compact::target(header.bits);
    if (!target || target->isZero()) throw ChainError("invalid difficulty bits " + hex32(header.bits));
    if (!(*target <= network_.powLimit)) throw ChainError("difficulty bits " + hex32(header.bits) + " easier than the network's limit");
    if (!(UInt256::of(hash) <= *target)) throw ChainError("hash " + hash.hex() + " exceeds target " + target->hex());
}

void HeaderChain::checkDifficulty(const BlockHeader& header, int32_t height) const {
    if (!network_.checksDifficulty()) return;
    const ChainEntry& parent = entries_[size_t(height - 1)];
    if (height % retargetInterval != 0) {
        uint32_t want = parent.bits;
        if (network_.reduceMinDifficulty) {
            if (int64_t(header.timestamp) - int64_t(parent.time) > network_.minDiffReductionTime)
                want = network_.powLimitBits;
            else
                want = lastRealBits(height - 1);
        }
        if (header.bits != want) throw ChainError("bits " + hex32(header.bits) + ", want " + hex32(want));
        return;
    }
    const ChainEntry& first = entries_[size_t(height - retargetInterval)];
    uint32_t oldBits = parent.bits;
    if (network_.enforceBIP94) {
        if (int64_t(header.timestamp) < int64_t(parent.time) - maxTimeWarp)
            throw ChainError("timestamp " + std::to_string(header.timestamp) + " at difficulty retarget is more than 10 minutes before parent " +
                             std::to_string(parent.time));
        oldBits = first.bits;
    }
    uint32_t want = nextRetargetBits(int64_t(first.time), int64_t(parent.time), oldBits, network_);
    if (header.bits != want) throw ChainError("bits " + hex32(header.bits) + " at difficulty retarget, want " + hex32(want));
}

uint32_t HeaderChain::lastRealBits(int32_t height) const {
    int32_t h = height;
    while (h > 0 && h % retargetInterval != 0 && entries_[size_t(h)].bits == network_.powLimitBits) --h;
    return entries_[size_t(h)].bits;
}

uint32_t HeaderChain::nextRetargetBits(int64_t first, int64_t last, uint32_t oldBits, const Network& network) {
    int64_t actual = std::min(std::max(last - first, retargetTimespan / 4), retargetTimespan * 4);
    auto old = Compact::target(oldBits);
    if (!old) return network.powLimitBits;
    UInt256 target = old->scaled(uint64_t(actual), uint64_t(retargetTimespan));
    if (target > network.powLimit) target = network.powLimit;
    return Compact::bits(target);
}

} // namespace tb
