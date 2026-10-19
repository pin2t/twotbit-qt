#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bitcoin/network.h"
#include "util/task.h"

namespace tb {

/// NetError is a failed connection, with the short reason the settings
/// show for a peer that could not be reached.
class NetError : public Error {
public:
    enum class Kind { timeout, refused, unreachable, closed, other };

    NetError(Kind kind, const std::string& what) : Error(what), kind(kind) {}

    /// The error of the platform's socket error code: a Windows Sockets
    /// one, or an errno on the Mac.
    static NetError fromCode(int code);

    Kind kind;
};

/// TcpStream is a TCP connection read as a byte stream of exact lengths. Its
/// socket never blocks: reads wait for data, a deadline or the cancellation
/// of their task, which closes the connection, and sends queue what the
/// network does not take at once for the reading thread to flush. One
/// thread reads at a time; any thread may send. TCP keepalives drop a peer
/// that went silent within keepAlive seconds.
class TcpStream {
public:
    /// How long a connection may stay silent, its keepalive probes
    /// unanswered, before it is dropped: the first probe goes out after half
    /// of it, then three probes, the last timing out at the end.
    static constexpr int keepAlive = 30;

    /// Connects to the host and port, an IP address or a host name, giving
    /// up after the timeout or when the task is cancelled. System proxies are
    /// not used: peers are reached directly.
    static std::unique_ptr<TcpStream> connect(const std::string& host, uint16_t port, Duration timeout);

    /// Wraps a connected socket, as a test node's listener accepts one.
    static std::unique_ptr<TcpStream> adopt(uintptr_t socket, std::string remote);

    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    const std::string& remote() const { return remote_; }

    /// Queues the bytes for sending without waiting. A failed send closes the
    /// connection.
    void send(ByteView data);

    /// Reads exactly count bytes, by the deadline when one is given.
    Bytes read(size_t count, std::optional<TimePoint> deadline = std::nullopt);

    /// Reads whatever bytes are buffered or arrive next.
    Bytes readAvailable(std::optional<TimePoint> deadline = std::nullopt);

    /// Closes the connection; pending and later reads and sends fail.
    void close();

    /// Whether the connection was closed or failed.
    bool isClosed() const { return closed_; }

private:
    TcpStream(uintptr_t socket, std::string remote);

    /// Waits until the socket has data, the deadline passes, the stream is
    /// closed or the task is cancelled, flushing queued sends meanwhile, and
    /// reads what arrived into the buffer.
    void fill(std::optional<TimePoint> deadline);
    void flushLocked();
    void fail(const NetError& error);
    [[noreturn]] void throwClosed();

    uintptr_t socket_;
    const std::string remote_;
    std::atomic<bool> closed_{false};
    std::mutex errorMutex_;
    std::optional<NetError> error_;
    std::mutex sendMutex_;
    Bytes queue_;
    size_t queued_ = 0;
    std::atomic<bool> pending_{false};
    Bytes buffer_;
    size_t start_ = 0;
};

/// Dns resolves the network's seed names to peer addresses.
namespace Dns {

/// The IPv4 and IPv6 addresses of the host name, empty when it does not
/// resolve. A lookup the task gives up on is left to finish alone.
std::vector<std::string> lookup(const std::string& host);

/// Every peer address the network's DNS seeds advertise, looked up all at
/// once.
std::vector<PeerAddress> seeds(const Network& network);

} // namespace Dns

} // namespace tb
