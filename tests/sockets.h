#pragma once

#include <cstdint>
#include <optional>

namespace tb::test {

/// The listening sockets of the tests' local peers, as the platform makes
/// them: windows/tests/sockets.cpp on Windows, macos/tests/sockets.cpp on
/// the Mac.
namespace Sockets {

/// A socket listening on a free port of the loopback address.
struct Listener {
    uintptr_t socket;
    uint16_t port;
};

/// Listens with the backlog, or throws.
Listener listen(int backlog);

/// The next connection to the listener, or nothing once it is closed.
std::optional<uintptr_t> accept(uintptr_t listener);

void close(uintptr_t socket);

} // namespace Sockets

} // namespace tb::test
