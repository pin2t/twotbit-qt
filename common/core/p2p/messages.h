#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "bitcoin/primitives.h"
#include "spv/filters.h"

namespace tb {

/// Service flags a node advertises in its version message.
struct ServiceFlags {
    uint64_t raw = 0;

    static constexpr uint64_t network = 1 << 0;
    static constexpr uint64_t getUTXO = 1 << 1;
    static constexpr uint64_t bloom = 1 << 2;
    static constexpr uint64_t witness = 1 << 3;
    static constexpr uint64_t xthin = 1 << 4;
    static constexpr uint64_t bitcoinCash = 1 << 5;
    static constexpr uint64_t compactFilters = 1 << 6;
    static constexpr uint64_t segwit2x = 1 << 7;
    static constexpr uint64_t networkLimited = 1 << 10;
    static constexpr uint64_t p2pV2 = 1 << 11;

    bool contains(uint64_t flags) const { return (raw & flags) == flags; }

    /// The flag names joined by "|", as btcd writes them.
    std::string description() const;

    friend bool operator==(const ServiceFlags&, const ServiceFlags&) = default;
};

/// InvType is the kind of object an inventory vector names.
enum class InvType : uint32_t {
    error = 0,
    tx = 1,
    block = 2,
    filteredBlock = 3,
    compactBlock = 4,
    wtx = 5,
    witnessTx = 0x40000001,
    witnessBlock = 0x40000002,
    filteredWitnessBlock = 0x40000003,
};

struct InvVect {
    uint32_t type = 0;
    Hash256 hash;

    InvVect() = default;
    InvVect(InvType t, const Hash256& h) : type(uint32_t(t)), hash(h) {}
    InvVect(uint32_t rawType, const Hash256& h) : type(rawType), hash(h) {}

    bool is(InvType t) const { return type == uint32_t(t); }

    friend bool operator==(const InvVect&, const InvVect&) = default;
};

/// NetAddress is a peer address as version and addr messages carry it: an
/// IPv6 or IPv4-mapped address and a port, with the advertised services.
struct NetAddress {
    uint32_t time = 0;
    ServiceFlags services;
    std::array<uint8_t, 16> ip{};
    uint16_t port = 0;

    /// The address of a host: its IP, or the unspecified address for a name
    /// the legacy encoding cannot carry.
    static NetAddress of(const std::string& host, uint16_t port, ServiceFlags services = {});

    std::string host() const;

    void write(ByteWriter& w, bool withTime) const;
    static NetAddress read(ByteReader& r, bool withTime);

    friend bool operator==(const NetAddress&, const NetAddress&) = default;
};

/// NetAddressV2 is a BIP155 addrv2 peer address, which can also carry onion
/// and I2P addresses. Only IP addresses have a host here: the wallet dials
/// no others.
struct NetAddressV2 {
    uint32_t time = 0;
    ServiceFlags services;
    uint8_t networkID = 0;
    Bytes address;
    uint16_t port = 0;

    static constexpr uint8_t ipv4 = 1;
    static constexpr uint8_t ipv6 = 2;
    static constexpr uint8_t torV3 = 4;
    static constexpr uint8_t i2p = 5;

    std::optional<std::string> host() const;

    friend bool operator==(const NetAddressV2&, const NetAddressV2&) = default;
};

struct VersionMessage {
    int32_t protocolVersion = 0;
    ServiceFlags services;
    int64_t timestamp = 0;
    NetAddress addrRecv;
    NetAddress addrFrom;
    uint64_t nonce = 0;
    std::string userAgent;
    int32_t startHeight = 0;
    bool relay = true;
};

struct CFHeaders {
    uint8_t filterType = 0;
    Hash256 stopHash;
    Hash256 prevFilterHeader;
    std::vector<Hash256> filterHashes;
};

struct CFilter {
    uint8_t filterType = 0;
    Hash256 blockHash;
    Bytes data;
};

/// The messages the wallet sends or handles, one type each; others are read
/// as unknown and ignored.
namespace msg {
struct Version { VersionMessage v; };
struct Verack {};
struct SendAddrV2 {};
struct WtxidRelay {};
struct Ping { uint64_t nonce = 0; };
struct Pong { uint64_t nonce = 0; };
struct GetAddr {};
struct Addr { std::vector<NetAddress> list; };
struct AddrV2 { std::vector<NetAddressV2> list; };
struct GetHeaders { std::vector<Hash256> locator; Hash256 stop; };
struct Headers { std::vector<BlockHeader> headers; };
struct GetCFHeaders { uint32_t startHeight = 0; Hash256 stop; };
struct CFHeadersMsg { CFHeaders m; };
struct GetCFilters { uint32_t startHeight = 0; Hash256 stop; };
struct CFilterMsg { CFilter m; };
struct GetData { std::vector<InvVect> list; };
struct Inv { std::vector<InvVect> list; };
struct NotFound { std::vector<InvVect> list; };
struct BlockMsg { Block block; };
struct Tx { Transaction tx; };
struct FilterLoad { BloomFilter filter; };
struct MemPool {};
struct Unknown { std::string command; };
} // namespace msg

using Message = std::variant<msg::Version, msg::Verack, msg::SendAddrV2, msg::WtxidRelay, msg::Ping, msg::Pong, msg::GetAddr, msg::Addr,
                             msg::AddrV2, msg::GetHeaders, msg::Headers, msg::GetCFHeaders, msg::CFHeadersMsg, msg::GetCFilters,
                             msg::CFilterMsg, msg::GetData, msg::Inv, msg::NotFound, msg::BlockMsg, msg::Tx, msg::FilterLoad, msg::MemPool,
                             msg::Unknown>;

namespace Messages {

/// The protocol version the wallet speaks: BIP155 addrv2.
constexpr int32_t protocolVersion = 70016;
/// The BIP158 basic filter type.
constexpr uint8_t basicFilter = 0;
/// The most entries a getdata, inv or notfound may list.
constexpr size_t maxInv = 50'000;

std::string command(const Message& message);

/// The serialized payload. Transactions and blocks are written with their
/// witness data.
Bytes payload(const Message& message);

/// Decodes the payload of a command. Commands the wallet does not handle
/// give Unknown.
Message decode(const std::string& command, ByteView payload);

} // namespace Messages

/// Wire frames messages: the network magic, the command, the payload length
/// and its checksum, then the payload.
namespace Wire {

constexpr size_t headerSize = 24;
/// The largest payload accepted, as btcd's MaxMessagePayload.
constexpr size_t maxPayload = 32 * 1024 * 1024;

struct Header {
    uint32_t magic = 0;
    std::string command;
    size_t length = 0;
    std::array<uint8_t, 4> checksum{};
};

Bytes frame(const Message& message, uint32_t magic);
Bytes frame(const std::string& command, ByteView payload, uint32_t magic);
Header parseHeader(ByteView data);
bool checksumMatches(const Header& header, ByteView payload);

} // namespace Wire

} // namespace tb
