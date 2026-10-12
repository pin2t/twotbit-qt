#pragma once

#include <optional>
#include <string>

#include "util/bytes.h"

namespace tb {

/// Base58 and Base58Check, the encoding of legacy addresses and extended
/// keys.
namespace Base58 {
std::string encode(ByteView data);
std::optional<Bytes> decode(std::string_view text);
/// Encodes the payload with its 4-byte double SHA256 checksum.
std::string encodeCheck(ByteView payload);
/// Decodes Base58Check text and returns the payload when the checksum
/// matches.
std::optional<Bytes> decodeCheck(std::string_view text);
} // namespace Base58

/// Bech32 (BIP173) and Bech32m (BIP350), the encodings of segwit addresses:
/// version 0 programs in Bech32 and later versions, taproot first, in
/// Bech32m.
namespace Bech32 {

enum class Variant { bech32, bech32m };

struct Decoded {
    std::string hrp;
    Bytes data;
    Variant variant;
};

struct Segwit {
    uint8_t version;
    Bytes program;
};

/// Regroups bits, as 8-bit bytes into 5-bit groups and back. Without
/// padding, leftover bits must be fewer than from and all zero.
std::optional<Bytes> convertBits(ByteView data, int from, int to, bool pad);

std::string encode(const std::string& hrp, ByteView data, Variant variant);

/// Decodes Bech32 or Bech32m text into its lower case human readable part,
/// its 5-bit data and the variant its checksum matches.
std::optional<Decoded> decode(std::string_view text);

/// Encodes a segwit address: version 0 in Bech32, later versions in
/// Bech32m.
std::string encodeSegwit(const std::string& hrp, uint8_t version, ByteView program);

/// Decodes a segwit address of the human readable part into its witness
/// version and program, enforcing the BIP173 and BIP350 rules.
std::optional<Segwit> decodeSegwit(const std::string& hrp, std::string_view text);

} // namespace Bech32

} // namespace tb
