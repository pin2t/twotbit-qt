#include "sync/probe.h"

#include "p2p/hostport.h"
#include "p2p/peer.h"
#include "util/strings.h"

namespace tb::PeerProbe {

std::optional<std::string> parse(const std::string& text, uint16_t defaultPort) {
    std::string bare = text;
    if (startsWith(bare, "[") && endsWith(bare, "]")) bare = bare.substr(1, bare.size() - 2);
    if (auto ip = IPAddress::canonical(bare)) return HostPort::join(*ip, defaultPort);
    std::string host, port;
    try {
        std::tie(host, port) = HostPort::splitRaw(text);
    } catch (const HostPortError&) {
        return std::nullopt;
    }
    auto ip = IPAddress::canonical(host);
    uint16_t num;
    if (!ip || port.empty() || !parseUnsigned(port, num) || num == 0) return std::nullopt;
    return HostPort::join(*ip, num);
}

void probe(const Network& network, const std::string& address) {
    auto [peer, took] = PeerConnection::connect(network, address, false, false, nullptr);
    struct Disconnect {
        PeerPtr& peer;
        ~Disconnect() { peer->disconnect(); }
    } disconnect{peer};
    if (!peer->services().contains(ServiceFlags::compactFilters)) throw NoCompactFilters(address);
}

std::string reason(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const NoCompactFilters&) {
        return "no compact filters (needs blockfilterindex=1 and peerblockfilters=1)";
    } catch (const PeerError& e) {
        return e.underlying ? reason(e.underlying) : "no handshake";
    } catch (const TimeoutError&) {
        return "no answer";
    } catch (const NetError& e) {
        switch (e.kind) {
        case NetError::Kind::timeout: return "no answer";
        case NetError::Kind::closed: return "no handshake";
        default: return e.what();
        }
    } catch (...) {
        return "no handshake";
    }
}

} // namespace tb::PeerProbe
