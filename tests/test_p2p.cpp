#include "tests.h"

#include "p2p/hostport.h"
#include "p2p/messages.h"
#include "p2p/tcp.h"
#include "sockets.h"
#include "util/strings.h"
#include "wallet/spend.h"

namespace tb::test {

namespace {

Bytes vector(const std::string& name) {
    for (const auto& v : fixture("p2p")["messages"].items())
        if (v["name"].str() == name) return v["hex"].hex();
    throw std::runtime_error("no message vector " + name);
}

Bytes payloadOf(const std::string& name) {
    Bytes frame = vector(name);
    return Bytes(frame.begin() + long(Wire::headerSize), frame.end());
}

} // namespace

void WireTests::roundTrips() {
    // Every message btcd wrote decodes and encodes back to the same bytes.
    for (const auto& v : fixture("p2p")["messages"].items()) {
        std::string name = v["name"].str();
        if (name == "tx base") continue;
        Bytes frame = v["hex"].hex();
        Wire::Header header = Wire::parseHeader(ByteView(frame).first(Wire::headerSize));
        EXPECT_EQ(header.command, v["command"].str(), name);
        Bytes payload(frame.begin() + long(Wire::headerSize), frame.end());
        EXPECT_EQ(header.length, payload.size(), name);
        EXPECT(Wire::checksumMatches(header, payload), name);
        Message message = Messages::decode(header.command, payload);
        EXPECT(Wire::frame(message, header.magic) == frame, name);
    }
}

void WireTests::versionMessages() {
    auto version = [](const std::string& host, uint16_t port, uint64_t nonce, int64_t time, bool relay) {
        VersionMessage v;
        v.protocolVersion = 70016;
        v.timestamp = time;
        v.addrRecv = NetAddress::of(host, port);
        v.addrFrom.ip[10] = 0xff;
        v.addrFrom.ip[11] = 0xff;
        v.nonce = nonce;
        v.userAgent = "/twotbit:0.1.0/";
        v.startHeight = 0;
        v.relay = relay;
        return Message(msg::Version{v});
    };
    EXPECT(Wire::frame(version("203.0.113.7", 8333, 0x0123456789abcdefull, 1'760'000'000, true), Network::mainnet().magic) == vector("version relay"),
           "relay");
    EXPECT(Wire::frame(version("2001:db8::1", 18333, 42, 1'760'000'123, false), Network::testnet3().magic) == vector("version norelay testnet ipv6"),
           "ipv6");
    EXPECT(Wire::frame(version("::", 8333, 7, 1'760'000'456, true), Network::mainnet().magic) == vector("version onion"), "unspecified");
}

void WireTests::remoteVersion() {
    Message m = Messages::decode("version", payloadOf("version remote"));
    auto* v = std::get_if<msg::Version>(&m);
    QVERIFY(v);
    EXPECT_EQ(v->v.userAgent, std::string("/Satoshi:28.1.0/"), "agent");
    EXPECT_EQ(v->v.startHeight, 912'345, "height");
    EXPECT(v->v.services.contains(ServiceFlags::compactFilters), "filters");
    EXPECT(v->v.services.contains(ServiceFlags::bloom), "bloom");
    EXPECT_EQ(v->v.services.description(), std::string("SFNodeNetwork|SFNodeBloom|SFNodeWitness|SFNodeCF|SFNodeNetworkLimited"), "services");
    EXPECT(v->v.addrRecv.host() == "0.0.0.0" || v->v.addrRecv.host() == "::", v->v.addrRecv.host());
}

void WireTests::requests() {
    uint32_t magic = Network::mainnet().magic;
    EXPECT(Wire::frame(msg::Verack{}, magic) == vector("verack"), "verack");
    EXPECT(Wire::frame(msg::SendAddrV2{}, magic) == vector("sendaddrv2"), "sendaddrv2");
    EXPECT(Wire::frame(msg::GetAddr{}, magic) == vector("getaddr"), "getaddr");
    EXPECT(Wire::frame(msg::MemPool{}, magic) == vector("mempool"), "mempool");
    EXPECT(Wire::frame(msg::Ping{0xdeadbeefcafebabeull}, magic) == vector("ping"), "ping");
    EXPECT(Wire::frame(msg::Pong{0x1122334455667788ull}, magic) == vector("pong"), "pong");
    Message decoded = Messages::decode("getcfheaders", payloadOf("getcfheaders"));
    auto* get = std::get_if<msg::GetCFHeaders>(&decoded);
    QVERIFY(get);
    EXPECT_EQ(get->startHeight, uint32_t(840'000), "start");
    EXPECT(Wire::frame(msg::GetCFHeaders{840'000, get->stop}, magic) == vector("getcfheaders"), "getcfheaders");
    EXPECT(Wire::frame(msg::GetCFilters{840'100, get->stop}, magic) == vector("getcfilters"), "getcfilters");
    Message txMessage = Messages::decode("tx", payloadOf("tx witness"));
    auto* tx = std::get_if<msg::Tx>(&txMessage);
    QVERIFY(tx);
    EXPECT(Wire::frame("tx", tx->tx.serialized(false), magic) == vector("tx base"), "tx base");
    EXPECT_EQ(tx->tx.inputs[0].sequence, SpendPlanner::sequenceRBF, "sequence");
}

void WireTests::addresses() {
    Message v2 = Messages::decode("addrv2", payloadOf("addrv2"));
    auto* list = std::get_if<msg::AddrV2>(&v2);
    QVERIFY(list && list->list.size() == 3);
    EXPECT(list->list[0].host() == std::optional<std::string>("192.0.2.1"), "ipv4");
    EXPECT(list->list[1].host() == std::optional<std::string>("2001:db8::7"), "ipv6");
    EXPECT(!list->list[2].host() && list->list[2].networkID == NetAddressV2::torV3, "onion peers are not dialed");
    Message legacy = Messages::decode("addr", payloadOf("addr"));
    auto* addr = std::get_if<msg::Addr>(&legacy);
    QVERIFY(addr && addr->list.size() == 2);
    EXPECT_EQ(addr->list[0].host(), std::string("198.51.100.23"), "host 0");
    EXPECT_EQ(addr->list[1].host(), std::string("2001:db8::abcd"), "host 1");
    EXPECT_EQ(addr->list[0].port, uint16_t(8333), "port 0");
    EXPECT_EQ(addr->list[1].port, uint16_t(18333), "port 1");
}

void WireTests::hostPorts() {
    auto [host, port] = HostPort::split("1.2.3.4:8333");
    EXPECT(host == "1.2.3.4" && port == 8333, "ipv4");
    std::tie(host, port) = HostPort::split("[2001:db8::1]:18333");
    EXPECT(host == "2001:db8::1" && port == 18333, "ipv6");
    std::tie(host, port) = HostPort::split("seed.example:1");
    EXPECT(host == "seed.example" && port == 1, "name");
    for (const char* bad : {"1.2.3.4", "2001:db8::1:8333", "[::1]", "[::1]x:1", "host:", "host:99999", "host:-1", "host:+1"}) {
        bool threw = false;
        try {
            HostPort::split(bad);
        } catch (const HostPortError&) {
            threw = true;
        }
        EXPECT(threw, bad);
    }
    EXPECT_EQ(HostPort::join("2001:db8::1", 8333), std::string("[2001:db8::1]:8333"), "join ipv6");
    EXPECT_EQ(HostPort::join("1.2.3.4", 8333), std::string("1.2.3.4:8333"), "join ipv4");
    EXPECT_EQ((PeerAddress{"x.onion", 8333}.description()), std::string("x.onion:8333"), "description");
}

void WireTests::ipAddresses() {
    EXPECT(IPAddress::canonical("1.2.3.4") == std::optional<std::string>("1.2.3.4"), "ipv4");
    EXPECT(IPAddress::canonical("::ffff:1.2.3.4") == std::optional<std::string>("1.2.3.4"), "mapped");
    EXPECT(IPAddress::canonical("2001:0db8:0000:0000:0000:0000:0000:0001") == std::optional<std::string>("2001:db8::1"), "long");
    EXPECT(IPAddress::canonical("2001:db8:0:1:0:0:0:1") == std::optional<std::string>("2001:db8:0:1::1"), "longest zero run");
    EXPECT(IPAddress::canonical("2001:db8:0:1:1:1:1:1") == std::optional<std::string>("2001:db8:0:1:1:1:1:1"), "single zero");
    EXPECT(IPAddress::canonical("::") == std::optional<std::string>("::"), "unspecified");
    EXPECT(IPAddress::canonical("::1") == std::optional<std::string>("::1"), "loopback");
    EXPECT(!IPAddress::canonical("1.2.3"), "short");
    EXPECT(!IPAddress::canonical("example.com"), "name");
    EXPECT(!IPAddress::canonical("1.2.3.256"), "large octet");
}

void WireTests::overlayHosts() {
    // Every onion and I2P host is recognised and never dialed; IP addresses
    // and DNS names are.
    for (const auto& v : fixture("p2p")["hosts"].items()) {
        std::string host = v["host"].str();
        if (v["onion"].boolean() || v["i2p"].boolean()) EXPECT(isOverlayHost(host), host);
        std::string low = lower(host);
        if (!endsWith(low, ".onion") && !endsWith(low, ".i2p")) EXPECT(!isOverlayHost(host), host);
    }
}

void WireTests::tcpStream() {
    // A stream reads exact lengths across chunks, sends what the other side
    // reads, fails its reads once closed and gives up at a deadline.
    Sockets::Listener listener = Sockets::listen(1);
    std::unique_ptr<TcpStream> client = TcpStream::connect("127.0.0.1", listener.port, 5s);
    std::optional<uintptr_t> accepted = Sockets::accept(listener.socket);
    Sockets::close(listener.socket);
    QVERIFY(accepted);
    std::unique_ptr<TcpStream> server = TcpStream::adopt(*accepted, "client");
    Bytes big(300'000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 31);
    client->send(big);
    EXPECT(server->read(5) == Bytes(big.begin(), big.begin() + 5), "head");
    EXPECT(server->read(big.size() - 5) == Bytes(big.begin() + 5, big.end()), "rest");
    server->send(bytesOf("pong"));
    EXPECT(client->read(4) == bytesOf("pong"), "reply");
    bool timedOut = false;
    try {
        client->read(1, Clock::now() + 200ms);
    } catch (const NetError& e) {
        timedOut = e.kind == NetError::Kind::timeout;
    }
    EXPECT(timedOut, "deadline");
    server->close();
    bool closed = false;
    try {
        client->read(1, Clock::now() + 5s);
    } catch (const NetError& e) {
        closed = e.kind == NetError::Kind::closed;
    }
    EXPECT(closed, "peer closed");
    bool refused = false;
    try {
        TcpStream::connect("127.0.0.1", 1, 5s);
    } catch (const NetError& e) {
        refused = e.kind == NetError::Kind::refused;
    }
    EXPECT(refused, "closed port refuses");
}

} // namespace tb::test
