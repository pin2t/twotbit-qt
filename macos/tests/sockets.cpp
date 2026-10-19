#include "sockets.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>

namespace tb::test::Sockets {

Listener listen(int backlog) {
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) throw std::runtime_error("no socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(s, backlog) != 0) {
        ::close(s);
        throw std::runtime_error("cannot listen");
    }
    socklen_t length = sizeof address;
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    return {uintptr_t(s), ntohs(address.sin_port)};
}

std::optional<uintptr_t> accept(uintptr_t listener) {
    while (true) {
        int client = ::accept(int(listener), nullptr, nullptr);
        if (client >= 0) return uintptr_t(client);
        if (errno != EINTR) return std::nullopt;
    }
}

// Closing a socket does not wake a thread blocked in accept on it on
// macOS; shutting it down first does.
void close(uintptr_t socket) {
    shutdown(int(socket), SHUT_RDWR);
    ::close(int(socket));
}

} // namespace tb::test::Sockets
