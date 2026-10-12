#include "p2p/tcp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <algorithm>
#include <climits>
#include <condition_variable>

#include "p2p/hostport.h"

#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT 16
#endif

namespace tb {

namespace {

/// The longest a wait goes without looking at the cancellation of its task
/// and the closing of its stream.
constexpr Duration slice = 200ms;

SOCKET sock(uintptr_t s) { return SOCKET(s); }

timeval timevalOf(Duration d) {
    int64_t us = std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    timeval tv;
    tv.tv_sec = long(us / 1'000'000);
    tv.tv_usec = long(us % 1'000'000);
    return tv;
}

/// One resolved address of a host to connect to.
struct Endpoint {
    sockaddr_storage address;
    int length;
};

std::vector<Endpoint> endpointsOf(addrinfo* list) {
    std::vector<Endpoint> out;
    for (addrinfo* ai = list; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) continue;
        Endpoint e{};
        std::memcpy(&e.address, ai->ai_addr, ai->ai_addrlen);
        e.length = int(ai->ai_addrlen);
        out.push_back(e);
    }
    return out;
}

/// The result of a name lookup run on a thread of its own, as getaddrinfo
/// cannot be cancelled: a cancelled task stops waiting for it.
struct Lookup {
    std::mutex mutex;
    std::condition_variable_any cv;
    bool done = false;
    int status = 0;
    std::vector<Endpoint> endpoints;
};

std::shared_ptr<Lookup> startLookup(const std::string& host, const std::string& service, bool numeric) {
    auto lookup = std::make_shared<Lookup>();
    auto run = [lookup, host, service, numeric] {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (numeric) hints.ai_flags = AI_NUMERICHOST;
        addrinfo* list = nullptr;
        int status = getaddrinfo(host.c_str(), service.empty() ? nullptr : service.c_str(), &hints, &list);
        std::vector<Endpoint> endpoints;
        if (status == 0) {
            endpoints = endpointsOf(list);
            freeaddrinfo(list);
        }
        {
            std::lock_guard lock(lookup->mutex);
            lookup->status = status;
            lookup->endpoints = std::move(endpoints);
            lookup->done = true;
        }
        lookup->cv.notify_all();
    };
    if (numeric)
        run();
    else
        std::thread(run).detach();
    return lookup;
}

/// Waits for the lookup until the deadline or the cancellation of the task;
/// false when it did not finish.
bool await(Lookup& lookup, std::optional<TimePoint> deadline) {
    std::unique_lock lock(lookup.mutex);
    if (deadline) return lookup.cv.wait_until(lock, task::token(), *deadline, [&] { return lookup.done; });
    return lookup.cv.wait(lock, task::token(), [&] { return lookup.done; });
}

void configure(SOCKET s) {
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&on), sizeof on);
    tcp_keepalive keepalive{1, TcpStream::keepAlive / 2 * 1000, TcpStream::keepAlive / 2 / 3 * 1000};
    DWORD returned = 0;
    WSAIoctl(s, SIO_KEEPALIVE_VALS, &keepalive, sizeof keepalive, nullptr, 0, &returned, nullptr, nullptr);
    DWORD probes = 3;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, reinterpret_cast<const char*>(&probes), sizeof probes);
}

} // namespace

NetError NetError::fromCode(int code) {
    switch (code) {
    case WSAECONNREFUSED: return {Kind::refused, "connection refused"};
    case WSAETIMEDOUT: return {Kind::timeout, "i/o timeout"};
    case WSAENETUNREACH: return {Kind::unreachable, "network is unreachable"};
    case WSAEHOSTUNREACH: return {Kind::unreachable, "no route to host"};
    case WSAEHOSTDOWN: return {Kind::unreachable, "host is down"};
    case WSAENETDOWN: return {Kind::unreachable, "network is down"};
    case WSAEADDRNOTAVAIL: return {Kind::unreachable, "can't assign requested address"};
    case WSAECONNRESET: return {Kind::closed, "connection reset by peer"};
    case WSAECONNABORTED: return {Kind::closed, "connection aborted"};
    case WSAENOTCONN: return {Kind::closed, "socket is not connected"};
    case WSAESHUTDOWN: return {Kind::closed, "connection closed"};
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA: return {Kind::unreachable, "no such host"};
    default: return {Kind::other, "socket error " + std::to_string(code)};
    }
}

TcpStream::TcpStream(uintptr_t socket, std::string remote) : socket_(socket), remote_(std::move(remote)) {}

TcpStream::~TcpStream() { closesocket(sock(socket_)); }

std::unique_ptr<TcpStream> TcpStream::adopt(uintptr_t socket, std::string remote) {
    ensureSockets();
    configure(sock(socket));
    return std::unique_ptr<TcpStream>(new TcpStream(socket, std::move(remote)));
}

std::unique_ptr<TcpStream> TcpStream::connect(const std::string& host, uint16_t port, Duration timeout) {
    ensureSockets();
    std::string remote = HostPort::join(host, port);
    TimePoint deadline = Clock::now() + timeout;
    auto timedOut = [&] { return NetError(NetError::Kind::timeout, "dial " + remote + " timed out after " + formatDuration(timeout)); };
    auto lookup = startLookup(host, std::to_string(port), IPAddress::isIP(host));
    if (!await(*lookup, deadline)) {
        task::checkCancelled();
        throw timedOut();
    }
    if (lookup->status != 0 || lookup->endpoints.empty()) throw NetError(NetError::Kind::unreachable, "no such host " + host);
    std::optional<NetError> lastError;
    for (const Endpoint& e : lookup->endpoints) {
        SOCKET s = socket(e.address.ss_family, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            lastError = NetError::fromCode(WSAGetLastError());
            continue;
        }
        configure(s);
        bool connected = ::connect(s, reinterpret_cast<const sockaddr*>(&e.address), e.length) == 0;
        if (!connected) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                closesocket(s);
                lastError = NetError::fromCode(err);
                continue;
            }
        }
        while (!connected) {
            if (task::isCancelled()) {
                closesocket(s);
                throw CancellationError();
            }
            TimePoint now = Clock::now();
            if (now >= deadline) {
                closesocket(s);
                throw timedOut();
            }
            fd_set writable, failed;
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            FD_SET(s, &writable);
            FD_SET(s, &failed);
            timeval tv = timevalOf(std::min<Duration>(slice, deadline - now));
            int n = select(0, nullptr, &writable, &failed, &tv);
            if (n == SOCKET_ERROR) {
                lastError = NetError::fromCode(WSAGetLastError());
                break;
            }
            if (n == 0) continue;
            int soError = 0;
            int length = sizeof soError;
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &length);
            if (soError != 0 || FD_ISSET(s, &failed)) {
                lastError = NetError::fromCode(soError != 0 ? soError : WSAECONNREFUSED);
                break;
            }
            connected = true;
        }
        if (connected) return std::unique_ptr<TcpStream>(new TcpStream(uintptr_t(s), remote));
        closesocket(s);
    }
    throw lastError.value_or(NetError(NetError::Kind::unreachable, "no address of " + host));
}

void TcpStream::fail(const NetError& error) {
    {
        std::lock_guard lock(errorMutex_);
        if (!error_) error_ = error;
    }
    if (!closed_.exchange(true)) shutdown(sock(socket_), SD_BOTH);
}

void TcpStream::throwClosed() {
    std::lock_guard lock(errorMutex_);
    throw error_.value_or(NetError(NetError::Kind::closed, "connection closed"));
}

void TcpStream::close() { fail(NetError(NetError::Kind::closed, "connection closed")); }

void TcpStream::send(ByteView data) {
    std::lock_guard lock(sendMutex_);
    if (closed_) return;
    queue_.insert(queue_.end(), data.begin(), data.end());
    flushLocked();
}

void TcpStream::flushLocked() {
    while (queued_ < queue_.size()) {
        int chunk = int(std::min<size_t>(queue_.size() - queued_, INT_MAX));
        int n = ::send(sock(socket_), reinterpret_cast<const char*>(queue_.data() + queued_), chunk, 0);
        if (n > 0) {
            queued_ += size_t(n);
            continue;
        }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) break;
        fail(NetError::fromCode(err));
        queue_.clear();
        queued_ = 0;
        pending_ = false;
        return;
    }
    if (queued_ == queue_.size()) {
        queue_.clear();
        queued_ = 0;
        pending_ = false;
    } else {
        if (queued_ > (1 << 20)) {
            queue_.erase(queue_.begin(), queue_.begin() + long(queued_));
            queued_ = 0;
        }
        pending_ = true;
    }
}

void TcpStream::fill(std::optional<TimePoint> deadline) {
    while (true) {
        if (closed_) throwClosed();
        if (task::isCancelled()) {
            close();
            throw CancellationError();
        }
        TimePoint now = Clock::now();
        if (deadline && now >= *deadline) throw NetError(NetError::Kind::timeout, "i/o timeout");
        Duration wait = deadline ? std::min<Duration>(slice, *deadline - now) : slice;
        bool flush = pending_;
        fd_set readable, writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(sock(socket_), &readable);
        if (flush) FD_SET(sock(socket_), &writable);
        timeval tv = timevalOf(wait);
        int n = select(0, &readable, flush ? &writable : nullptr, nullptr, &tv);
        if (n == SOCKET_ERROR) {
            fail(NetError::fromCode(WSAGetLastError()));
            continue;
        }
        if (n == 0) continue;
        if (flush && FD_ISSET(sock(socket_), &writable)) {
            std::lock_guard lock(sendMutex_);
            flushLocked();
        }
        if (!FD_ISSET(sock(socket_), &readable)) continue;
        uint8_t chunk[1 << 16];
        int got = recv(sock(socket_), reinterpret_cast<char*>(chunk), sizeof chunk, 0);
        if (got > 0) {
            if (start_ > 0 && start_ == buffer_.size()) {
                buffer_.clear();
                start_ = 0;
            } else if (start_ > (1 << 20)) {
                buffer_.erase(buffer_.begin(), buffer_.begin() + long(start_));
                start_ = 0;
            }
            buffer_.insert(buffer_.end(), chunk, chunk + got);
            return;
        }
        if (got == 0) {
            fail(NetError(NetError::Kind::closed, "EOF"));
            continue;
        }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) continue;
        fail(NetError::fromCode(err));
    }
}

Bytes TcpStream::read(size_t count, std::optional<TimePoint> deadline) {
    while (buffer_.size() - start_ < count) fill(deadline);
    Bytes out(buffer_.begin() + long(start_), buffer_.begin() + long(start_ + count));
    start_ += count;
    return out;
}

Bytes TcpStream::readAvailable(std::optional<TimePoint> deadline) {
    if (buffer_.size() == start_) fill(deadline);
    Bytes out(buffer_.begin() + long(start_), buffer_.end());
    start_ = buffer_.size();
    return out;
}

namespace Dns {

std::vector<std::string> lookup(const std::string& host) {
    ensureSockets();
    auto pending = startLookup(host, "", false);
    if (!await(*pending, std::nullopt) || pending->status != 0) return {};
    std::vector<std::string> out;
    for (const Endpoint& e : pending->endpoints) {
        char text[NI_MAXHOST];
        if (getnameinfo(reinterpret_cast<const sockaddr*>(&e.address), e.length, text, sizeof text, nullptr, 0, NI_NUMERICHOST) != 0) continue;
        std::string ip = text;
        if (std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
    return out;
}

std::vector<PeerAddress> seeds(const Network& network) {
    std::mutex mutex;
    std::vector<PeerAddress> out;
    {
        TaskGroup group;
        for (const PeerAddress& seed : network.dnsSeeds) {
            group.add([&, seed] {
                std::vector<std::string> ips = lookup(seed.host);
                std::lock_guard lock(mutex);
                for (const auto& ip : ips) out.push_back(PeerAddress{ip, seed.port});
            });
        }
        group.waitAll();
    }
    return out;
}

} // namespace Dns

} // namespace tb
