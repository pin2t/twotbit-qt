#include "p2p/peer.h"

#include <cstdio>
#include <ctime>

#include "crypto/hashes.h"
#include "p2p/hostport.h"

namespace tb {

namespace {

std::string hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x", v);
    return buf;
}

} // namespace

PeerConnection::PeerConnection(std::string address, std::string host, uint16_t port, bool pinned, Network network, RemoteVersion remote,
                               std::unique_ptr<TcpStream> stream, Handler handler)
    : address(std::move(address)), host(std::move(host)), port(port), pinned(pinned), network(std::move(network)),
      remote(std::move(remote)), stream_(std::move(stream)), handler_(std::move(handler)), lastReceived_(Clock::now().time_since_epoch().count()) {}

PeerConnection::~PeerConnection() {
    stream_->close();
    if (!reader_.joinable()) return;
    // The last reference goes away on the peer's own thread as it ends.
    if (reader_.get_id() == std::this_thread::get_id())
        reader_.detach();
    else
        reader_.join();
}

std::pair<PeerPtr, Duration> PeerConnection::connect(const Network& network, const std::string& address, bool relay, bool pinned,
                                                     Handler handler) {
    auto [host, port] = HostPort::split(address);
    TimePoint started = Clock::now();
    std::unique_ptr<TcpStream> stream;
    try {
        stream = TcpStream::connect(host, port, handshakeTimeout);
    } catch (const CancellationError&) {
        throw;
    } catch (const std::exception& e) {
        throw PeerError("dial peer " + address + ": " + e.what(), std::current_exception());
    }
    RemoteVersion remote;
    try {
        remote = handshake(*stream, network, host, port, relay, Clock::now() + handshakeTimeout);
    } catch (const CancellationError&) {
        stream->close();
        throw;
    } catch (const NetError& e) {
        stream->close();
        if (e.kind == NetError::Kind::timeout) throw PeerError("handshake with " + address + " timed out");
        throw PeerError("handshake with " + address + ": " + e.what(), std::current_exception());
    } catch (const std::exception& e) {
        stream->close();
        throw PeerError("handshake with " + address + ": " + e.what(), std::current_exception());
    }
    PeerPtr peer(new PeerConnection(address, host, port, pinned, network, remote, std::move(stream), std::move(handler)));
    peer->start();
    return {peer, Clock::now() - started};
}

RemoteVersion PeerConnection::handshake(TcpStream& stream, const Network& network, const std::string& host, uint16_t port, bool relay,
                                        TimePoint deadline) {
    uint64_t nonce = 0;
    while (nonce == 0) nonce = Hashes::randomUInt64();
    NetAddress recv;
    if (auto ip = IPAddress::bytes(host)) {
        recv.ip = *ip;
    } else {
        recv.ip[10] = 0xff;
        recv.ip[11] = 0xff;
    }
    recv.port = port;
    VersionMessage version;
    version.protocolVersion = Messages::protocolVersion;
    version.timestamp = int64_t(std::time(nullptr));
    version.addrRecv = recv;
    version.nonce = nonce;
    version.userAgent = userAgent;
    version.startHeight = 0;
    version.relay = relay;
    stream.send(Wire::frame(msg::Version{version}, network.magic));
    std::optional<RemoteVersion> remote;
    while (true) {
        auto [command, payload] = readFrame(stream, network, deadline);
        Message message = Messages::decode(command, payload);
        if (auto* v = std::get_if<msg::Version>(&message)) {
            if (remote) throw PeerError("duplicate version message");
            if (v->v.nonce == nonce) throw PeerError("disconnecting peer connected to self");
            if (v->v.protocolVersion < 209) throw PeerError("protocol version " + std::to_string(v->v.protocolVersion) + " is too old");
            remote = RemoteVersion{v->v.protocolVersion, v->v.services, v->v.userAgent, v->v.startHeight};
            if (std::min(v->v.protocolVersion, Messages::protocolVersion) >= 70016) stream.send(Wire::frame(msg::SendAddrV2{}, network.magic));
            stream.send(Wire::frame(msg::Verack{}, network.magic));
        } else if (std::holds_alternative<msg::Verack>(message)) {
            if (!remote) throw PeerError("verack before version");
            return *remote;
        }
    }
}

std::pair<std::string, Bytes> PeerConnection::readFrame(TcpStream& stream, const Network& network, std::optional<TimePoint> deadline) {
    Wire::Header header = Wire::parseHeader(stream.read(Wire::headerSize, deadline));
    if (header.magic != network.magic) throw PeerError("message from network " + hex32(header.magic) + ", want " + network.name);
    Bytes payload = header.length > 0 ? stream.read(header.length, deadline) : Bytes();
    if (!Wire::checksumMatches(header, payload)) throw PeerError(header.command + " message checksum mismatch");
    return {header.command, std::move(payload)};
}

void PeerConnection::start() {
    // The thread holds the peer until it is done with it, so a peer lives as
    // long as it is connected.
    reader_ = std::thread([self = shared_from_this()] {
        self->readerID_ = std::this_thread::get_id();
        self->readLoop();
    });
}

void PeerConnection::readLoop() {
    TimePoint nextPing = Clock::now() + pingInterval;
    while (isLive()) {
        Bytes head;
        try {
            head = stream_->read(Wire::headerSize, nextPing);
        } catch (const NetError& e) {
            if (e.kind != NetError::Kind::timeout) {
                disconnect(isLive() ? std::optional<std::string>(e.what()) : std::nullopt);
                return;
            }
            Duration silent = Clock::now() - TimePoint(Duration(lastReceived_.load()));
            if (silent > idleTimeout) {
                disconnect("no activity for " + formatDuration(silent));
                return;
            }
            send(msg::Ping{Hashes::randomUInt64()});
            nextPing = Clock::now() + pingInterval;
            continue;
        } catch (const std::exception& e) {
            disconnect(isLive() ? std::optional<std::string>(e.what()) : std::nullopt);
            return;
        }
        try {
            Wire::Header header = Wire::parseHeader(head);
            if (header.magic != network.magic) throw PeerError("message from network " + hex32(header.magic) + ", want " + network.name);
            Bytes payload = header.length > 0 ? stream_->read(header.length, Clock::now() + idleTimeout) : Bytes();
            if (!Wire::checksumMatches(header, payload)) throw PeerError(header.command + " message checksum mismatch");
            lastReceived_ = Clock::now().time_since_epoch().count();
            Message message = Messages::decode(header.command, payload);
            if (!isLive()) return;
            dispatch(message);
        } catch (const std::exception& e) {
            disconnect(isLive() ? std::optional<std::string>(e.what()) : std::nullopt);
            return;
        }
    }
}

void PeerConnection::dispatch(const Message& message) {
    if (auto* m = std::get_if<msg::Ping>(&message)) {
        send(msg::Pong{m->nonce});
    } else if (auto* m = std::get_if<msg::Headers>(&message)) {
        headers.deliver(m->headers);
    } else if (auto* m = std::get_if<msg::CFHeadersMsg>(&message)) {
        cfHeaders.deliver(m->m);
    } else if (auto* m = std::get_if<msg::CFilterMsg>(&message)) {
        cfilters.deliver(m->m);
    } else if (auto* m = std::get_if<msg::BlockMsg>(&message)) {
        blockReplies.deliver(BlockReply{m->block, {}});
    } else if (auto* m = std::get_if<msg::NotFound>(&message)) {
        blockReplies.deliver(BlockReply{std::nullopt, m->list});
    } else if (std::holds_alternative<msg::Tx>(message) || std::holds_alternative<msg::Inv>(message) ||
               std::holds_alternative<msg::Addr>(message) || std::holds_alternative<msg::AddrV2>(message)) {
        if (handler_) handler_(*this, message);
    }
}

void PeerConnection::send(const Message& message) {
    if (!isLive()) return;
    stream_->send(Wire::frame(message, network.magic));
}

void PeerConnection::disconnect(std::optional<std::string> why) {
    PeerPtr self = shared_from_this();
    std::function<void()> callback;
    bool first = false;
    {
        std::lock_guard lock(mutex_);
        if (why && !reason_) reason_ = why;
        if (!quitting_) {
            quitting_ = true;
            first = true;
            callback = std::move(onQuit_);
            onQuit_ = nullptr;
        }
    }
    if (first) {
        auto error = std::make_exception_ptr(PeerError("peer disconnected"));
        headers.close(error);
        cfHeaders.close(error);
        cfilters.close(error);
        blockReplies.close(error);
        stream_->close();
        quit.fire();
    }
    if (readerID_.load() != std::this_thread::get_id()) {
        std::call_once(joined_, [this] {
            if (reader_.joinable()) reader_.join();
        });
    }
    if (callback) callback();
}

std::optional<std::string> PeerConnection::disconnectReason() const {
    std::lock_guard lock(mutex_);
    return reason_;
}

void PeerConnection::onDisconnect(std::function<void()> callback) {
    {
        std::lock_guard lock(mutex_);
        if (!quitting_) {
            onQuit_ = std::move(callback);
            return;
        }
    }
    callback();
}

} // namespace tb
