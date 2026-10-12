#include "p2p/hostport.h"

#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "util/strings.h"

namespace tb {

namespace HostPort {

std::string join(const std::string& host, uint16_t port) {
    return host.find(':') != std::string::npos ? "[" + host + "]:" + std::to_string(port) : host + ":" + std::to_string(port);
}

std::pair<std::string, uint16_t> split(const std::string& text) {
    auto [host, portText] = splitRaw(text);
    uint16_t port;
    if (portText.empty() || !parseUnsigned(portText, port)) throw HostPortError("address " + text + ": invalid port");
    return {host, port};
}

std::pair<std::string, std::string> splitRaw(const std::string& text) {
    size_t colon = text.rfind(':');
    if (colon == std::string::npos) throw HostPortError("address " + text + ": missing port in address");
    std::string port = text.substr(colon + 1);
    std::string host;
    if (startsWith(text, "[")) {
        size_t close = text.find(']');
        if (close == std::string::npos) throw HostPortError("address " + text + ": missing ']' in address");
        if (close + 1 != colon) throw HostPortError("address " + text + ": missing port in address");
        host = text.substr(1, close - 1);
        if (host.find('[') != std::string::npos || host.find(']') != std::string::npos)
            throw HostPortError("address " + text + ": unexpected bracket");
    } else {
        host = text.substr(0, colon);
        if (host.find(':') != std::string::npos) throw HostPortError("address " + text + ": too many colons in address");
        if (host.find('[') != std::string::npos || host.find(']') != std::string::npos)
            throw HostPortError("address " + text + ": unexpected bracket");
    }
    return {host, port};
}

} // namespace HostPort

namespace IPAddress {

std::optional<std::array<uint8_t, 16>> bytes(const std::string& text) {
    if (text.empty() || text.find('%') != std::string::npos) return std::nullopt;
    ensureSockets();
    std::array<uint8_t, 16> out{};
    in_addr v4;
    if (inet_pton(AF_INET, text.c_str(), &v4) == 1) {
        out[10] = 0xff;
        out[11] = 0xff;
        std::memcpy(out.data() + 12, &v4, 4);
        return out;
    }
    in6_addr v6;
    if (inet_pton(AF_INET6, text.c_str(), &v6) == 1) {
        std::memcpy(out.data(), &v6, 16);
        return out;
    }
    return std::nullopt;
}

std::optional<std::string> canonical(const std::string& text) {
    auto raw = bytes(text);
    if (!raw) return std::nullopt;
    return string(*raw);
}

std::string string(ByteView raw) {
    auto dotted = [](ByteView four) {
        return std::to_string(four[0]) + "." + std::to_string(four[1]) + "." + std::to_string(four[2]) + "." + std::to_string(four[3]);
    };
    if (raw.size() == 4) return dotted(raw);
    if (raw.size() != 16) return "?";
    bool mapped = raw[10] == 0xff && raw[11] == 0xff;
    for (size_t i = 0; i < 10 && mapped; ++i) mapped = raw[i] == 0;
    if (mapped) return dotted(raw.subspan(12));
    uint16_t words[8];
    for (int i = 0; i < 8; ++i) words[i] = uint16_t(raw[size_t(2 * i)] << 8 | raw[size_t(2 * i + 1)]);
    int bestStart = -1, bestLength = 0, start = -1;
    for (int i = 0; i <= 8; ++i) {
        if (i < 8 && words[i] == 0) {
            if (start < 0) start = i;
        } else if (start >= 0) {
            if (i - start > bestLength) {
                bestStart = start;
                bestLength = i - start;
            }
            start = -1;
        }
    }
    if (bestLength < 2) bestStart = -1;
    std::string out;
    for (int i = 0; i < 8;) {
        if (i == bestStart) {
            out += "::";
            i += bestLength;
            continue;
        }
        if (!out.empty() && out.back() != ':') out += ":";
        char buf[8];
        std::snprintf(buf, sizeof buf, "%x", words[i]);
        out += buf;
        ++i;
    }
    return out;
}

} // namespace IPAddress

bool isOverlayHost(const std::string& host) {
    std::string h = lower(host);
    return endsWith(h, ".onion") || endsWith(h, ".i2p");
}

void ensureSockets() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
#endif
}

} // namespace tb
