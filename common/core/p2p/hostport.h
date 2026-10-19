#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "util/bytes.h"

namespace tb {

class HostPortError : public Error {
public:
    explicit HostPortError(const std::string& what) : Error(what) {}
};

/// HostPort splits and joins host:port addresses the way Go's net package
/// does.
namespace HostPort {

std::string join(const std::string& host, uint16_t port);

/// Splits host:port or [host]:port, as net.SplitHostPort does, and parses
/// the port.
std::pair<std::string, uint16_t> split(const std::string& text);

/// Splits without parsing the port.
std::pair<std::string, std::string> splitRaw(const std::string& text);

} // namespace HostPort

/// IPAddress parses and prints IP addresses as Go's net.IP does.
namespace IPAddress {

/// The 16-byte form of an IPv4 or IPv6 address, IPv4 mapped into IPv6, or
/// nothing for text that is not an IP address.
std::optional<std::array<uint8_t, 16>> bytes(const std::string& text);

/// Whether the text is an IPv4 or IPv6 address.
inline bool isIP(const std::string& text) { return bytes(text).has_value(); }

/// The canonical text of an IP address: dotted for IPv4 and IPv4-mapped
/// IPv6, RFC 5952 for IPv6. Text that is not an IP gives nothing.
std::optional<std::string> canonical(const std::string& text);

/// The text of a 4- or 16-byte address.
std::string string(ByteView raw);

} // namespace IPAddress

/// Whether the host is a Tor onion or I2P name. The wallet reaches peers
/// directly only, so it never dials those.
bool isOverlayHost(const std::string& host);

/// Starts Windows Sockets once for the process, before any socket call. The
/// Mac's BSD sockets need no start.
void ensureSockets();

} // namespace tb
