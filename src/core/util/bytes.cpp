#include "util/bytes.h"

namespace tb {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::string toHex(ByteView bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

std::optional<Bytes> fromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    Bytes out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexValue(hex[i]), lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(uint8_t(hi << 4 | lo));
    }
    return out;
}

std::optional<Hash256> Hash256::fromHex(std::string_view hex) {
    if (hex.size() != 64) return std::nullopt;
    auto raw = tb::fromHex(hex);
    if (!raw) return std::nullopt;
    Hash256 h;
    for (size_t i = 0; i < 32; ++i) h.bytes[i] = (*raw)[31 - i];
    return h;
}

std::string Hash256::hex() const {
    Bytes reversed(bytes.rbegin(), bytes.rend());
    return toHex(reversed);
}

void ByteWriter::writeVarInt(uint64_t v) {
    if (v < 0xfd) {
        data_.push_back(uint8_t(v));
    } else if (v <= 0xffff) {
        data_.push_back(0xfd);
        appendLE(v, 2);
    } else if (v <= 0xffff'ffff) {
        data_.push_back(0xfe);
        appendLE(v, 4);
    } else {
        data_.push_back(0xff);
        appendLE(v, 8);
    }
}

size_t varIntSize(uint64_t v) {
    if (v < 0xfd) return 1;
    if (v <= 0xffff) return 3;
    if (v <= 0xffff'ffff) return 5;
    return 9;
}

uint64_t ByteReader::readVarInt() {
    uint8_t first = readUInt8();
    switch (first) {
    case 0xfd: {
        uint64_t v = readUInt16();
        if (v < 0xfd) throw DecodeError("non-canonical compact size");
        return v;
    }
    case 0xfe: {
        uint64_t v = readUInt32();
        if (v <= 0xffff) throw DecodeError("non-canonical compact size");
        return v;
    }
    case 0xff: {
        uint64_t v = readUInt64();
        if (v <= 0xffff'ffff) throw DecodeError("non-canonical compact size");
        return v;
    }
    default:
        return first;
    }
}

size_t ByteReader::readCount(size_t limit) {
    uint64_t n = readVarInt();
    if (n > limit) throw DecodeError("count " + std::to_string(n) + " exceeds " + std::to_string(limit));
    return size_t(n);
}

} // namespace tb
