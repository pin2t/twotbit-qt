#include "encoding/encodings.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "crypto/hashes.h"
#include "util/strings.h"

namespace tb {

namespace Base58 {

namespace {

const char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

const std::array<int8_t, 128>& indexes() {
    static const std::array<int8_t, 128> table = [] {
        std::array<int8_t, 128> t;
        t.fill(-1);
        for (int i = 0; i < 58; ++i) t[size_t(uint8_t(alphabet[i]))] = int8_t(i);
        return t;
    }();
    return table;
}

} // namespace

std::string encode(ByteView data) {
    size_t zeros = 0;
    while (zeros < data.size() && data[zeros] == 0) ++zeros;
    std::vector<uint8_t> digits;
    for (uint8_t byte : data) {
        int carry = byte;
        for (auto& d : digits) {
            carry += int(d) << 8;
            d = uint8_t(carry % 58);
            carry /= 58;
        }
        while (carry > 0) {
            digits.push_back(uint8_t(carry % 58));
            carry /= 58;
        }
    }
    std::string out(zeros, alphabet[0]);
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) out.push_back(alphabet[*it]);
    return out;
}

std::optional<Bytes> decode(std::string_view text) {
    size_t zeros = 0;
    while (zeros < text.size() && text[zeros] == alphabet[0]) ++zeros;
    Bytes bytes;
    for (char ch : text) {
        uint8_t c = uint8_t(ch);
        if (c >= 128 || indexes()[c] < 0) return std::nullopt;
        int carry = indexes()[c];
        for (auto& b : bytes) {
            carry += int(b) * 58;
            b = uint8_t(carry & 0xff);
            carry >>= 8;
        }
        while (carry > 0) {
            bytes.push_back(uint8_t(carry & 0xff));
            carry >>= 8;
        }
    }
    Bytes out(zeros, 0);
    out.insert(out.end(), bytes.rbegin(), bytes.rend());
    return out;
}

std::string encodeCheck(ByteView payload) {
    Hash256 checksum = Hashes::sha256d(payload);
    Bytes data(payload.begin(), payload.end());
    data.insert(data.end(), checksum.bytes.begin(), checksum.bytes.begin() + 4);
    return encode(data);
}

std::optional<Bytes> decodeCheck(std::string_view text) {
    auto raw = decode(text);
    if (!raw || raw->size() < 4) return std::nullopt;
    Bytes payload(raw->begin(), raw->end() - 4);
    Hash256 checksum = Hashes::sha256d(payload);
    if (!std::equal(checksum.bytes.begin(), checksum.bytes.begin() + 4, raw->end() - 4)) return std::nullopt;
    return payload;
}

} // namespace Base58

namespace Bech32 {

namespace {

const char charset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
constexpr uint32_t generator[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};

uint32_t constant(Variant v) { return v == Variant::bech32 ? 1 : 0x2bc830a3; }

uint32_t polymod(const Bytes& values) {
    uint32_t chk = 1;
    for (uint8_t v : values) {
        uint32_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ v;
        for (int i = 0; i < 5; ++i)
            if ((top >> i) & 1) chk ^= generator[i];
    }
    return chk;
}

Bytes expand(const std::string& hrp) {
    Bytes out;
    for (char c : hrp) out.push_back(uint8_t(c) >> 5);
    out.push_back(0);
    for (char c : hrp) out.push_back(uint8_t(c) & 31);
    return out;
}

} // namespace

std::optional<Bytes> convertBits(ByteView data, int from, int to, bool pad) {
    int acc = 0, bits = 0;
    Bytes out;
    int maxv = (1 << to) - 1;
    for (uint8_t value : data) {
        if ((int(value) >> from) != 0) return std::nullopt;
        acc = (acc << from) | value;
        bits += from;
        while (bits >= to) {
            bits -= to;
            out.push_back(uint8_t((acc >> bits) & maxv));
        }
        acc &= (1 << bits) - 1;
    }
    if (pad) {
        if (bits > 0) out.push_back(uint8_t((acc << (to - bits)) & maxv));
    } else if (bits >= from || ((acc << (to - bits)) & maxv) != 0) {
        return std::nullopt;
    }
    return out;
}

std::string encode(const std::string& hrp, ByteView data, Variant variant) {
    std::string h = lower(hrp);
    Bytes values = expand(h);
    values.insert(values.end(), data.begin(), data.end());
    Bytes padded = values;
    padded.insert(padded.end(), 6, 0);
    uint32_t mod = polymod(padded) ^ constant(variant);
    std::string out = h + "1";
    for (uint8_t d : data) out.push_back(charset[d]);
    for (int i = 0; i < 6; ++i) out.push_back(charset[(mod >> (5 * (5 - i))) & 31]);
    return out;
}

std::optional<Decoded> decode(std::string_view text) {
    if (text.size() > 90) return std::nullopt;
    bool hasLower = std::any_of(text.begin(), text.end(), [](char c) { return c >= 'a' && c <= 'z'; });
    bool hasUpper = std::any_of(text.begin(), text.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    if (hasLower && hasUpper) return std::nullopt;
    std::string low = lower(text);
    size_t sep = low.rfind('1');
    if (sep == std::string::npos || sep < 1 || sep + 7 > low.size()) return std::nullopt;
    std::string hrp = low.substr(0, sep);
    for (char c : hrp)
        if (uint8_t(c) < 33 || uint8_t(c) > 126) return std::nullopt;
    Bytes data;
    for (size_t i = sep + 1; i < low.size(); ++i) {
        const char* at = std::strchr(charset, low[i]);
        if (!at || low[i] == '\0') return std::nullopt;
        data.push_back(uint8_t(at - charset));
    }
    Bytes values = expand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    uint32_t mod = polymod(values);
    Variant variant;
    if (mod == constant(Variant::bech32))
        variant = Variant::bech32;
    else if (mod == constant(Variant::bech32m))
        variant = Variant::bech32m;
    else
        return std::nullopt;
    data.resize(data.size() - 6);
    return Decoded{hrp, data, variant};
}

std::string encodeSegwit(const std::string& hrp, uint8_t version, ByteView program) {
    Bytes data{version};
    Bytes five = *convertBits(program, 8, 5, true);
    data.insert(data.end(), five.begin(), five.end());
    return encode(hrp, data, version == 0 ? Variant::bech32 : Variant::bech32m);
}

std::optional<Segwit> decodeSegwit(const std::string& hrp, std::string_view text) {
    auto decoded = decode(text);
    if (!decoded || decoded->hrp != lower(hrp) || decoded->data.empty()) return std::nullopt;
    uint8_t version = decoded->data[0];
    if (version > 16) return std::nullopt;
    auto program = convertBits(ByteView(decoded->data).subspan(1), 5, 8, false);
    if (!program || program->size() < 2 || program->size() > 40) return std::nullopt;
    if (version == 0 && program->size() != 20 && program->size() != 32) return std::nullopt;
    if (decoded->variant != (version == 0 ? Variant::bech32 : Variant::bech32m)) return std::nullopt;
    return Segwit{version, *program};
}

} // namespace Bech32

} // namespace tb
