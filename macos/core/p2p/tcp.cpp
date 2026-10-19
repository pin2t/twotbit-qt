#include "p2p/tcp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <thread>

#include "p2p/hostport.h"

namespace tb {

namespace {

/// The longest a wait goes without looking at the cancellation of its task
/// and the closing of its stream.
constexpr Duration slice = 200ms;

/// The send buffer of a socket: room for a large block or a batch of
/// headers without waiting for the reading thread to flush the queue.
constexpr int sendBuffer = 4 << 20;

int sock(uintptr_t s) { return int(s); }

int millisOf(Duration d) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    return int(std::clamp<int64_t>(ms, 0, INT_MAX));
}

/// One resolved address of a host to connect to.
struct Endpoint {
    sockaddr_storage address;
    socklen_t length;
};

std::vector<Endpoint> endpointsOf(addrinfo* list) {
    std::vector<Endpoint> out;
    for (addrinfo* ai = list; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) continue;
        Endpoint e{};
        std::memcpy(&e.address, ai->ai_addr, ai->ai_addrlen);
        e.length = ai->ai_addrlen;
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

/// Makes the socket non-blocking, without Nagle's delay, with keepalives and
/// without the SIGPIPE a send to a closed peer would otherwise raise.
void configure(int s) {
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    fcntl(s, F_SETFD, FD_CLOEXEC);
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    int idle = TcpStream::keepAlive / 2;
    int interval = TcpStream::keepAlive / 2 / 3;
    int probes = 3;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof idle);
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof probes);
    // Windows grows a socket's send buffer as the connection needs; macOS
    // keeps it at its default unless asked, so a large message sent while
    // nothing reads this stream would wait in the queue.
    int buffer = sendBuffer;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof buffer);
}

} // namespace

NetError NetError::fromCode(int code) {
    switch (code) {
    case ECONNREFUSED: return {Kind::refused, "connection refused"};
    case ETIMEDOUT: return {Kind::timeout, "i/o timeout"};
    case ENETUNREACH: return {Kind::unreachable, "network is unreachable"};
    case EHOSTUNREACH: return {Kind::unreachable, "no route to host"};
    case EHOSTDOWN: return {Kind::unreachable, "host is down"};
    case ENETDOWN: return {Kind::unreachable, "network is down"};
    case EADDRNOTAVAIL: return {Kind::unreachable, "can't assign requested address"};
    case ECONNRESET: return {Kind::closed, "connection reset by peer"};
    case ECONNABORTED: return {Kind::closed, "connection aborted"};
    case ENOTCONN: return {Kind::closed, "socket is not connected"};
    case EPIPE:
    case ESHUTDOWN: return {Kind::closed, "connection closed"};
    default: return {Kind::other, "socket error " + std::to_string(code) + ": " + std::strerror(code)};
    }
}

TcpStream::TcpStream(uintptr_t socket, std::string remote) : socket_(socket), remote_(std::move(remote)) {}

TcpStream::~TcpStream() { ::close(sock(socket_)); }

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
        int s = socket(e.address.ss_family, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0) {
            lastError = NetError::fromCode(errno);
            continue;
        }
        configure(s);
        bool connected = ::connect(s, reinterpret_cast<const sockaddr*>(&e.address), e.length) == 0;
        if (!connected) {
            int err = errno;
            if (err != EINPROGRESS) {
                ::close(s);
                lastError = NetError::fromCode(err);
                continue;
            }
        }
        while (!connected) {
            if (task::isCancelled()) {
                ::close(s);
                throw CancellationError();
            }
            TimePoint now = Clock::now();
            if (now >= deadline) {
                ::close(s);
                throw timedOut();
            }
            pollfd p{s, POLLOUT, 0};
            int n = poll(&p, 1, millisOf(std::min<Duration>(slice, deadline - now)));
            if (n < 0) {
                if (errno == EINTR) continue;
                lastError = NetError::fromCode(errno);
                break;
            }
            if (n == 0) continue;
            int soError = 0;
            socklen_t length = sizeof soError;
            getsockopt(s, SOL_SOCKET, SO_ERROR, &soError, &length);
            if (soError != 0 || (p.revents & (POLLERR | POLLHUP))) {
                lastError = NetError::fromCode(soError != 0 ? soError : ECONNREFUSED);
                break;
            }
            connected = true;
        }
        if (connected) return std::unique_ptr<TcpStream>(new TcpStream(uintptr_t(s), remote));
        ::close(s);
    }
    throw lastError.value_or(NetError(NetError::Kind::unreachable, "no address of " + host));
}

void TcpStream::fail(const NetError& error) {
    {
        std::lock_guard lock(errorMutex_);
        if (!error_) error_ = error;
    }
    if (!closed_.exchange(true)) shutdown(sock(socket_), SHUT_RDWR);
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
        ssize_t n = ::send(sock(socket_), queue_.data() + queued_, queue_.size() - queued_, 0);
        if (n > 0) {
            queued_ += size_t(n);
            continue;
        }
        int err = errno;
        if (n < 0 && err == EINTR) continue;
        if (n < 0 && (err == EAGAIN || err == EWOULDBLOCK)) break;
        fail(NetError::fromCode(n < 0 ? err : EPIPE));
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
        pollfd p{sock(socket_), short(POLLIN | (flush ? POLLOUT : 0)), 0};
        int n = poll(&p, 1, millisOf(wait));
        if (n < 0) {
            if (errno == EINTR) continue;
            fail(NetError::fromCode(errno));
            continue;
        }
        if (n == 0) continue;
        if (flush && (p.revents & POLLOUT)) {
            std::lock_guard lock(sendMutex_);
            flushLocked();
        }
        if (!(p.revents & (POLLIN | POLLHUP | POLLERR))) continue;
        uint8_t chunk[1 << 16];
        ssize_t got = recv(sock(socket_), chunk, sizeof chunk, 0);
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
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) continue;
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
