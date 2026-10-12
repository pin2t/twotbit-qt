#include "spv/filters.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "crypto/hashes.h"

namespace tb {

GCSFilter::GCSFilter(ByteView serialized) {
    ByteReader r(serialized);
    n_ = r.readVarInt();
    if (n_ >= (uint64_t(1) << 32)) throw DecodeError("filter of " + std::to_string(n_) + " items");
    bits_ = r.readBytes(r.remaining());
}

uint64_t GCSFilter::map(ByteView item, std::pair<uint64_t, uint64_t> key, uint64_t modulus) {
    unsigned __int128 product = (unsigned __int128)SipHash::hash(key.first, key.second, item) * modulus;
    return uint64_t(product >> 64);
}

std::vector<bool> GCSFilter::match(const std::vector<Bytes>& items, const Hash256& blockHash) const {
    std::vector<bool> hits(items.size(), false);
    if (n_ == 0 || items.empty()) return hits;
    uint64_t modulus = n_ * m;
    auto k = key(blockHash);
    std::vector<std::pair<uint64_t, size_t>> targets;
    targets.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) targets.emplace_back(map(items[i], k, modulus), i);
    std::stable_sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    BitReader reader(bits_);
    uint64_t value = 0;
    size_t t = 0;
    for (uint64_t i = 0; i < n_; ++i) {
        auto delta = reader.readGolombRice(p);
        if (!delta) break;
        value += *delta;
        while (t < targets.size() && targets[t].first < value) ++t;
        if (t == targets.size()) break;
        while (t < targets.size() && targets[t].first == value) {
            hits[targets[t].second] = true;
            ++t;
        }
        if (t == targets.size()) break;
    }
    return hits;
}

Bytes GCSFilter::build(const std::vector<Bytes>& items, const Hash256& blockHash) {
    ByteWriter writer;
    writer.writeVarInt(items.size());
    uint64_t modulus = uint64_t(items.size()) * m;
    auto k = key(blockHash);
    std::vector<uint64_t> values;
    values.reserve(items.size());
    for (const auto& item : items) values.push_back(map(item, k, modulus));
    std::sort(values.begin(), values.end());
    BitWriter bits;
    uint64_t last = 0;
    for (uint64_t v : values) {
        uint64_t delta = v - last;
        last = v;
        for (uint64_t q = 0; q < (delta >> p); ++q) bits.write(true);
        bits.write(false);
        bits.write(delta & ((uint64_t(1) << p) - 1), int(p));
    }
    writer.write(bits.bytes());
    return writer.take();
}

Hash256 GCSFilter::hash(ByteView serialized) { return Hashes::sha256d(serialized); }

Hash256 GCSFilter::header(const Hash256& filterHash, const Hash256& previous) {
    uint8_t data[64];
    std::copy(filterHash.bytes.begin(), filterHash.bytes.end(), data);
    std::copy(previous.bytes.begin(), previous.bytes.end(), data + 32);
    return Hashes::sha256d(ByteView(data, 64));
}

std::vector<bool> GCSFilter::matchScripts(ByteView serialized, const Hash256& blockHash, const std::vector<Bytes>& scripts) {
    return GCSFilter(serialized).match(scripts, blockHash);
}

std::optional<uint64_t> BitReader::read(int count) {
    uint64_t v = 0;
    for (int i = 0; i < count; ++i) {
        auto bit = readBit();
        if (!bit) return std::nullopt;
        v = v << 1 | (*bit ? 1 : 0);
    }
    return v;
}

std::optional<uint64_t> BitReader::readGolombRice(uint64_t p) {
    uint64_t quotient = 0;
    while (true) {
        auto bit = readBit();
        if (!bit) return std::nullopt;
        if (!*bit) break;
        ++quotient;
    }
    auto remainder = read(int(p));
    if (!remainder) return std::nullopt;
    return quotient << p | *remainder;
}

BloomFilter::BloomFilter(uint32_t elements, uint32_t tweak, double fpRate, uint8_t flags) : tweak_(tweak), flags_(flags) {
    double rate = std::min(std::max(fpRate, 1e-9), 1.0);
    double n = double(std::max<uint32_t>(elements, 1));
    double ln2 = std::numbers::ln2;
    double bits = std::min(-1 * n * std::log(rate) / (ln2 * ln2), double(maxFilterSize * 8));
    uint32_t dataLen = uint32_t(bits) / 8;
    uint32_t funcs = uint32_t(double(dataLen * 8) / n * ln2);
    data_.assign(dataLen, 0);
    hashFuncs_ = std::min(funcs, maxHashFuncs);
}

size_t BloomFilter::hash(uint32_t number, ByteView item) const {
    return size_t(Murmur3::hash(number * 0xfba4c795u + tweak_, item) % (uint32_t(data_.size()) << 3));
}

void BloomFilter::add(ByteView item) {
    if (data_.empty()) return;
    for (uint32_t i = 0; i < hashFuncs_; ++i) {
        size_t index = hash(i, item);
        data_[index >> 3] |= uint8_t(1 << (index & 7));
    }
}

bool BloomFilter::contains(ByteView item) const {
    if (data_.empty()) return true;
    for (uint32_t i = 0; i < hashFuncs_; ++i) {
        size_t index = hash(i, item);
        if ((data_[index >> 3] & (1 << (index & 7))) == 0) return false;
    }
    return true;
}

} // namespace tb
