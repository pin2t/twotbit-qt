#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "bitcoin/hash256.h"

namespace tb {

using Bytes = std::vector<uint8_t>;
using ByteView = std::span<const uint8_t>;

/// Error is the base of the wallet's errors; what() is the description the
/// log and the window show.
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

/// DecodeError is returned when serialized data cannot be read: it ends
/// early, or holds a value the format does not allow.
class DecodeError : public Error {
public:
    explicit DecodeError(const std::string& what) : Error(what) {}
    static DecodeError truncated() { return DecodeError("data ends early"); }
};

/// The bytes in lower case hex.
std::string toHex(ByteView bytes);

/// Parses hex, upper or lower case, without separators. It fails on an odd
/// length or a character that is not a hex digit.
std::optional<Bytes> fromHex(std::string_view hex);

inline Bytes bytesOf(std::string_view s) { return Bytes(s.begin(), s.end()); }
inline std::string stringOf(ByteView b) { return std::string(b.begin(), b.end()); }

inline void append(Bytes& out, ByteView b) { out.insert(out.end(), b.begin(), b.end()); }

inline Bytes concat(std::initializer_list<ByteView> parts) {
    Bytes out;
    for (ByteView p : parts) append(out, p);
    return out;
}

/// ByteWriter builds serialized data: little-endian integers, Bitcoin
/// compact sizes and length-prefixed byte strings.
class ByteWriter {
public:
    explicit ByteWriter(size_t capacity = 0) { data_.reserve(capacity); }

    const Bytes& data() const { return data_; }
    Bytes take() { return std::move(data_); }

    void write(ByteView bytes) { append(data_, bytes); }
    void write(const Hash256& hash) { data_.insert(data_.end(), hash.bytes.begin(), hash.bytes.end()); }
    void writeUInt8(uint8_t v) { data_.push_back(v); }
    void writeBool(bool v) { data_.push_back(v ? 1 : 0); }
    void writeUInt16(uint16_t v) { appendLE(v, 2); }
    void writeUInt32(uint32_t v) { appendLE(v, 4); }
    void writeInt32(int32_t v) { appendLE(uint32_t(v), 4); }
    void writeUInt64(uint64_t v) { appendLE(v, 8); }
    void writeInt64(int64_t v) { appendLE(uint64_t(v), 8); }

    /// Writes a port, which the protocol sends big-endian.
    void writeUInt16BE(uint16_t v) {
        data_.push_back(uint8_t(v >> 8));
        data_.push_back(uint8_t(v & 0xff));
    }

    /// Writes a Bitcoin compact size.
    void writeVarInt(uint64_t v);

    /// Writes the bytes preceded by their length as a compact size.
    void writeVarBytes(ByteView bytes) {
        writeVarInt(bytes.size());
        write(bytes);
    }

private:
    void appendLE(uint64_t v, int size) {
        for (int i = 0; i < size; ++i) data_.push_back(uint8_t(v >> (8 * i)));
    }

    Bytes data_;
};

/// The size in bytes of a compact size holding the value.
size_t varIntSize(uint64_t v);

/// ByteReader reads serialized data front to back: little-endian integers,
/// canonical Bitcoin compact sizes and length-prefixed byte strings. Every
/// read past the end throws DecodeError::truncated.
class ByteReader {
public:
    explicit ByteReader(ByteView data) : data_(data) {}

    size_t remaining() const { return data_.size() - offset_; }
    bool isAtEnd() const { return offset_ >= data_.size(); }
    size_t offset() const { return offset_; }

    Bytes readBytes(size_t n) {
        need(n);
        Bytes out(data_.begin() + long(offset_), data_.begin() + long(offset_ + n));
        offset_ += n;
        return out;
    }

    void skip(size_t n) {
        need(n);
        offset_ += n;
    }

    uint8_t readUInt8() {
        need(1);
        return data_[offset_++];
    }

    uint8_t peekUInt8(size_t ahead = 0) const {
        if (remaining() <= ahead) throw DecodeError::truncated();
        return data_[offset_ + ahead];
    }

    bool readBool() { return readUInt8() != 0; }
    uint16_t readUInt16() { return uint16_t(readLE(2)); }
    uint32_t readUInt32() { return uint32_t(readLE(4)); }
    int32_t readInt32() { return int32_t(uint32_t(readLE(4))); }
    uint64_t readUInt64() { return readLE(8); }
    int64_t readInt64() { return int64_t(readLE(8)); }

    uint16_t readUInt16BE() {
        uint16_t hi = readUInt8();
        uint16_t lo = readUInt8();
        return uint16_t(hi << 8 | lo);
    }

    Hash256 readHash() {
        need(32);
        Hash256 h = Hash256::fromRaw(data_.data() + offset_);
        offset_ += 32;
        return h;
    }

    /// Reads a compact size, refusing a non-canonical encoding as Bitcoin
    /// Core and btcd do.
    uint64_t readVarInt();

    /// Reads a count that sizes what follows: a compact size no larger than
    /// limit, so a hostile length cannot allocate without bound.
    size_t readCount(size_t limit);

    /// Reads bytes preceded by their length as a compact size, no longer
    /// than limit.
    Bytes readVarBytes(size_t limit = 4'000'000) { return readBytes(readCount(limit)); }

private:
    void need(size_t n) const {
        if (remaining() < n) throw DecodeError::truncated();
    }

    uint64_t readLE(int size) {
        need(size_t(size));
        uint64_t v = 0;
        for (int i = 0; i < size; ++i) v |= uint64_t(data_[offset_ + size_t(i)]) << (8 * i);
        offset_ += size_t(size);
        return v;
    }

    ByteView data_;
    size_t offset_ = 0;
};

} // namespace tb
