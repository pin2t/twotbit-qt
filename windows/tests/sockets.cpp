#include "sockets.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdexcept>

#include "p2p/hostport.h"

namespace tb::test::Sockets {

Listener listen(int backlog) {
    ensureSockets();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("no socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(s, backlog) != 0) {
        closesocket(s);
        throw std::runtime_error("cannot listen");
    }
    int length = sizeof address;
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    return {uintptr_t(s), ntohs(address.sin_port)};
}

std::optional<uintptr_t> accept(uintptr_t listener) {
    SOCKET client = ::accept(SOCKET(listener), nullptr, nullptr);
    if (client == INVALID_SOCKET) return std::nullopt;
    return uintptr_t(client);
}

void close(uintptr_t socket) { closesocket(SOCKET(socket)); }

} // namespace tb::test::Sockets
