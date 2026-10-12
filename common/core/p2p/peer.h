#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "p2p/messages.h"
#include "p2p/tcp.h"
#include "util/task.h"

namespace tb {

/// RemoteVersion is what a peer told about itself in its version message.
struct RemoteVersion {
    int32_t protocolVersion = 0;
    ServiceFlags services;
    std::string userAgent;
    /// The height of the peer's best block when it connected.
    int32_t startHeight = 0;
};

/// PeerError is a peer that failed or broke the protocol, with the error
/// that caused it when there is one.
class PeerError : public Error {
public:
    explicit PeerError(const std::string& what, std::exception_ptr underlying = nullptr) : Error(what), underlying(std::move(underlying)) {}

    std::exception_ptr underlying;
};

/// PeerConnection is one connected Bitcoin peer after the version/verack
/// handshake: a thread of its own reads the peer's messages, answers and
/// sends pings, hands responses to the mailbox of their kind for the request
/// waiting on them, and passes announcements, relayed transactions and
/// addresses to the handler. The local node advertises no services: it is a
/// light client.
class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
public:
    using Handler = std::function<void(PeerConnection&, const Message&)>;

    /// The user agent the wallet introduces itself with.
    static constexpr const char* userAgent = "/twotbit:0.1.0/";
    /// Bounds on the TCP dial and the handshake.
    static constexpr Duration handshakeTimeout = 15s;
    /// How often a ping goes out, and how long a peer may send nothing
    /// before it is dropped, as btcd's peer does.
    static constexpr Duration pingInterval = 120s;
    static constexpr Duration idleTimeout = 300s;

    /// BlockReply is the answer to a block request: the block, or the peer's
    /// notfound.
    struct BlockReply {
        std::optional<Block> block;
        std::vector<InvVect> notFound;
    };

    /// Dials the peer at the host:port address, completes the handshake and
    /// starts reading its messages. It returns the connected peer and how
    /// long dial and handshake took. relay sets the version message's relay
    /// flag: whether the peer should announce the transactions it relays
    /// without a bloom filter.
    static std::pair<std::shared_ptr<PeerConnection>, Duration> connect(const Network& network, const std::string& address, bool relay,
                                                                        bool pinned, Handler handler);

    /// Reads one framed message: the header, then the payload, checking the
    /// network magic and the checksum.
    static std::pair<std::string, Bytes> readFrame(TcpStream& stream, const Network& network,
                                                   std::optional<TimePoint> deadline = std::nullopt);

    ~PeerConnection();

    const std::string address;
    const std::string host;
    const uint16_t port;
    /// A pinned peer was asked for explicitly and is never dropped.
    const bool pinned;
    const Network network;
    const RemoteVersion remote;

    ServiceFlags services() const { return remote.services; }
    bool isLive() const { return !quit.isFired(); }

    /// Queues a message for the peer. Transactions go out with their witness
    /// data.
    void send(const Message& message);

    /// Disconnects the peer: pending and later requests fail, quit fires and
    /// the disconnect callback runs. Called from any thread but the peer's
    /// own, it returns once the peer's thread is done with its messages. The
    /// reason, when given, is kept for the log.
    void disconnect(std::optional<std::string> why = std::nullopt);

    /// Why the peer disconnected, when it was not asked to.
    std::optional<std::string> disconnectReason() const;

    /// Runs the callback once the peer is disconnected, at once when it is
    /// already.
    void onDisconnect(std::function<void()> callback);

    /// Waits until the peer is disconnected or the task is cancelled.
    void waitForDisconnect() { quit.wait(); }

    /// Fires once the peer is disconnected, for whatever reason.
    Signal quit;
    Mailbox<std::vector<BlockHeader>> headers{4};
    Mailbox<CFHeaders> cfHeaders{4};
    Mailbox<CFilter> cfilters{2000};
    Mailbox<BlockReply> blockReplies{8};

private:
    PeerConnection(std::string address, std::string host, uint16_t port, bool pinned, Network network, RemoteVersion remote,
                   std::unique_ptr<TcpStream> stream, Handler handler);

    static RemoteVersion handshake(TcpStream& stream, const Network& network, const std::string& host, uint16_t port, bool relay,
                                   TimePoint deadline);

    void start();
    void readLoop();
    void dispatch(const Message& message);

    std::unique_ptr<TcpStream> stream_;
    Handler handler_;
    std::thread reader_;
    std::atomic<std::thread::id> readerID_;
    std::once_flag joined_;
    mutable std::mutex mutex_;
    bool quitting_ = false;
    std::optional<std::string> reason_;
    std::function<void()> onQuit_;
    std::atomic<TimePoint::rep> lastReceived_;
};

using PeerPtr = std::shared_ptr<PeerConnection>;

} // namespace tb
