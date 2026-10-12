#pragma once

#include <exception>
#include <optional>
#include <string>

#include "bitcoin/network.h"
#include "util/bytes.h"

namespace tb {

/// PeerProbe checks a peer typed in the settings before the sync is pinned
/// to it.
namespace PeerProbe {

/// NoCompactFilters is a peer that does not serve the BIP157 filters the
/// wallet syncs from.
class NoCompactFilters : public Error {
public:
    explicit NoCompactFilters(const std::string& address) : Error(address + ": no compact filter service"), address(address) {}
    std::string address;
};

/// Reads a peer typed as an IP address with a port, or without one, which
/// takes the network's default port, and returns it in host:port form;
/// nothing for anything else.
std::optional<std::string> parse(const std::string& text, uint16_t defaultPort);

/// Connects directly to the peer, completes the handshake and checks it
/// serves compact filters, then disconnects.
void probe(const Network& network, const std::string& address);

/// Why a peer could not be connected to, briefly.
std::string reason(const std::exception_ptr& error);

} // namespace PeerProbe

} // namespace tb
