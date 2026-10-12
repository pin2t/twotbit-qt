#include "p2p/messages.h"

#include <cstdio>

#include "crypto/hashes.h"
#include "p2p/hostport.h"

namespace tb {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

void writeInv(ByteWriter& w, const std::vector<InvVect>& list) {
    w.writeVarInt(list.size());
    for (const auto& iv : list) {
        w.writeUInt32(iv.type);
        w.write(iv.hash);
    }
}

} // namespace

std::string ServiceFlags::description() const {
    if (raw == 0) return "0x0";
    static const std::pair<uint64_t, const char*> names[] = {
        {network, "SFNodeNetwork"},   {getUTXO, "SFNodeGetUTXO"}, {bloom, "SFNodeBloom"},
        {witness, "SFNodeWitness"},   {xthin, "SFNodeXthin"},     {bitcoinCash, "SFNodeBit5"},
        {compactFilters, "SFNodeCF"}, {segwit2x, "SFNode2X"},     {networkLimited, "SFNodeNetworkLimited"},
        {p2pV2, "SFNodeP2PV2"},
    };
    std::string out;
    uint64_t rest = raw;
    for (const auto& [flag, name] : names) {
        if ((raw & flag) == 0) continue;
        if (!out.empty()) out += "|";
        out += name;
        rest &= ~flag;
    }
    if (rest != 0) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)rest);
        if (!out.empty()) out += "|";
        out += buf;
    }
    return out;
}

NetAddress NetAddress::of(const std::string& host, uint16_t port, ServiceFlags services) {
    NetAddress a;
    a.services = services;
    if (auto ip = IPAddress::bytes(host)) a.ip = *ip;
    a.port = port;
    return a;
}

std::string NetAddress::host() const { return IPAddress::string(ip); }

void NetAddress::write(ByteWriter& w, bool withTime) const {
    if (withTime) w.writeUInt32(time);
    w.writeUInt64(services.raw);
    w.write(ip);
    w.writeUInt16BE(port);
}

NetAddress NetAddress::read(ByteReader& r, bool withTime) {
    NetAddress a;
    a.time = withTime ? r.readUInt32() : 0;
    a.services.raw = r.readUInt64();
    Bytes ip = r.readBytes(16);
    std::copy(ip.begin(), ip.end(), a.ip.begin());
    a.port = r.readUInt16BE();
    return a;
}

std::optional<std::string> NetAddressV2::host() const {
    if (networkID == ipv4 && address.size() == 4) return IPAddress::string(address);
    if (networkID == ipv6 && address.size() == 16) return IPAddress::string(address);
    return std::nullopt;
}

namespace Messages {

std::string command(const Message& message) {
    return std::visit(Overloaded{
                          [](const msg::Version&) -> std::string { return "version"; },
                          [](const msg::Verack&) -> std::string { return "verack"; },
                          [](const msg::SendAddrV2&) -> std::string { return "sendaddrv2"; },
                          [](const msg::WtxidRelay&) -> std::string { return "wtxidrelay"; },
                          [](const msg::Ping&) -> std::string { return "ping"; },
                          [](const msg::Pong&) -> std::string { return "pong"; },
                          [](const msg::GetAddr&) -> std::string { return "getaddr"; },
                          [](const msg::Addr&) -> std::string { return "addr"; },
                          [](const msg::AddrV2&) -> std::string { return "addrv2"; },
                          [](const msg::GetHeaders&) -> std::string { return "getheaders"; },
                          [](const msg::Headers&) -> std::string { return "headers"; },
                          [](const msg::GetCFHeaders&) -> std::string { return "getcfheaders"; },
                          [](const msg::CFHeadersMsg&) -> std::string { return "cfheaders"; },
                          [](const msg::GetCFilters&) -> std::string { return "getcfilters"; },
                          [](const msg::CFilterMsg&) -> std::string { return "cfilter"; },
                          [](const msg::GetData&) -> std::string { return "getdata"; },
                          [](const msg::Inv&) -> std::string { return "inv"; },
                          [](const msg::NotFound&) -> std::string { return "notfound"; },
                          [](const msg::BlockMsg&) -> std::string { return "block"; },
                          [](const msg::Tx&) -> std::string { return "tx"; },
                          [](const msg::FilterLoad&) -> std::string { return "filterload"; },
                          [](const msg::MemPool&) -> std::string { return "mempool"; },
                          [](const msg::Unknown& u) -> std::string { return u.command; },
                      },
                      message);
}

Bytes payload(const Message& message) {
    ByteWriter w;
    std::visit(Overloaded{
                   [&](const msg::Version& m) {
                       const VersionMessage& v = m.v;
                       w.writeInt32(v.protocolVersion);
                       w.writeUInt64(v.services.raw);
                       w.writeInt64(v.timestamp);
                       v.addrRecv.write(w, false);
                       v.addrFrom.write(w, false);
                       w.writeUInt64(v.nonce);
                       w.writeVarBytes(bytesOf(v.userAgent));
                       w.writeInt32(v.startHeight);
                       w.writeBool(v.relay);
                   },
                   [&](const msg::Ping& m) { w.writeUInt64(m.nonce); },
                   [&](const msg::Pong& m) { w.writeUInt64(m.nonce); },
                   [&](const msg::Addr& m) {
                       w.writeVarInt(m.list.size());
                       for (const auto& a : m.list) a.write(w, true);
                   },
                   [&](const msg::AddrV2& m) {
                       w.writeVarInt(m.list.size());
                       for (const auto& a : m.list) {
                           w.writeUInt32(a.time);
                           w.writeVarInt(a.services.raw);
                           w.writeUInt8(a.networkID);
                           w.writeVarBytes(a.address);
                           w.writeUInt16BE(a.port);
                       }
                   },
                   [&](const msg::GetHeaders& m) {
                       w.writeUInt32(uint32_t(protocolVersion));
                       w.writeVarInt(m.locator.size());
                       for (const auto& h : m.locator) w.write(h);
                       w.write(m.stop);
                   },
                   [&](const msg::Headers& m) {
                       w.writeVarInt(m.headers.size());
                       for (const auto& h : m.headers) {
                           h.write(w);
                           w.writeVarInt(0);
                       }
                   },
                   [&](const msg::GetCFHeaders& m) {
                       w.writeUInt8(basicFilter);
                       w.writeUInt32(m.startHeight);
                       w.write(m.stop);
                   },
                   [&](const msg::GetCFilters& m) {
                       w.writeUInt8(basicFilter);
                       w.writeUInt32(m.startHeight);
                       w.write(m.stop);
                   },
                   [&](const msg::CFHeadersMsg& c) {
                       w.writeUInt8(c.m.filterType);
                       w.write(c.m.stopHash);
                       w.write(c.m.prevFilterHeader);
                       w.writeVarInt(c.m.filterHashes.size());
                       for (const auto& h : c.m.filterHashes) w.write(h);
                   },
                   [&](const msg::CFilterMsg& c) {
                       w.writeUInt8(c.m.filterType);
                       w.write(c.m.blockHash);
                       w.writeVarBytes(c.m.data);
                   },
                   [&](const msg::GetData& m) { writeInv(w, m.list); },
                   [&](const msg::Inv& m) { writeInv(w, m.list); },
                   [&](const msg::NotFound& m) { writeInv(w, m.list); },
                   [&](const msg::BlockMsg& m) { w.write(m.block.serialized()); },
                   [&](const msg::Tx& m) { m.tx.write(w, true); },
                   [&](const msg::FilterLoad& m) {
                       w.writeVarBytes(m.filter.data());
                       w.writeUInt32(m.filter.hashFuncs());
                       w.writeUInt32(m.filter.tweak());
                       w.writeUInt8(m.filter.flags());
                   },
                   [&](const auto&) {},
               },
               message);
    return w.take();
}

Message decode(const std::string& command, ByteView payload) {
    ByteReader r(payload);
    if (command == "version") {
        VersionMessage v;
        v.protocolVersion = r.readInt32();
        v.services.raw = r.readUInt64();
        v.timestamp = r.readInt64();
        v.addrRecv = NetAddress::read(r, false);
        if (r.remaining() > 0) {
            v.addrFrom = NetAddress::read(r, false);
            v.nonce = r.readUInt64();
        }
        if (r.remaining() > 0) v.userAgent = stringOf(r.readVarBytes(256));
        if (r.remaining() > 0) v.startHeight = r.readInt32();
        if (r.remaining() > 0) v.relay = r.readBool();
        return msg::Version{v};
    }
    if (command == "verack") return msg::Verack{};
    if (command == "sendaddrv2") return msg::SendAddrV2{};
    if (command == "wtxidrelay") return msg::WtxidRelay{};
    if (command == "getaddr") return msg::GetAddr{};
    if (command == "mempool") return msg::MemPool{};
    if (command == "ping") return msg::Ping{r.remaining() >= 8 ? r.readUInt64() : 0};
    if (command == "pong") return msg::Pong{r.readUInt64()};
    if (command == "addr") {
        size_t count = r.readCount(1000);
        msg::Addr m;
        for (size_t i = 0; i < count; ++i) m.list.push_back(NetAddress::read(r, true));
        return m;
    }
    if (command == "addrv2") {
        size_t count = r.readCount(1000);
        msg::AddrV2 m;
        for (size_t i = 0; i < count; ++i) {
            NetAddressV2 a;
            a.time = r.readUInt32();
            a.services.raw = r.readVarInt();
            a.networkID = r.readUInt8();
            a.address = r.readVarBytes(512);
            a.port = r.readUInt16BE();
            m.list.push_back(std::move(a));
        }
        return m;
    }
    if (command == "getheaders") {
        r.readUInt32();
        size_t count = r.readCount(2000);
        msg::GetHeaders m;
        for (size_t i = 0; i < count; ++i) m.locator.push_back(r.readHash());
        m.stop = r.readHash();
        return m;
    }
    if (command == "headers") {
        size_t count = r.readCount(2000);
        msg::Headers m;
        m.headers.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            m.headers.push_back(BlockHeader::read(r));
            if (r.readVarInt() != 0) throw DecodeError("block headers may not contain transactions");
        }
        return m;
    }
    if (command == "getcfheaders" || command == "getcfilters") {
        r.readUInt8();
        uint32_t start = r.readUInt32();
        Hash256 stop = r.readHash();
        if (command == "getcfheaders") return msg::GetCFHeaders{start, stop};
        return msg::GetCFilters{start, stop};
    }
    if (command == "cfheaders") {
        CFHeaders c;
        c.filterType = r.readUInt8();
        c.stopHash = r.readHash();
        c.prevFilterHeader = r.readHash();
        size_t count = r.readCount(2000);
        for (size_t i = 0; i < count; ++i) c.filterHashes.push_back(r.readHash());
        return msg::CFHeadersMsg{std::move(c)};
    }
    if (command == "cfilter") {
        CFilter c;
        c.filterType = r.readUInt8();
        c.blockHash = r.readHash();
        c.data = r.readVarBytes(4'000'000);
        return msg::CFilterMsg{std::move(c)};
    }
    if (command == "getdata" || command == "inv" || command == "notfound") {
        size_t count = r.readCount(maxInv);
        std::vector<InvVect> list;
        list.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            uint32_t type = r.readUInt32();
            list.emplace_back(type, r.readHash());
        }
        if (command == "getdata") return msg::GetData{std::move(list)};
        if (command == "inv") return msg::Inv{std::move(list)};
        return msg::NotFound{std::move(list)};
    }
    if (command == "block") return msg::BlockMsg{Block::read(r)};
    if (command == "tx") return msg::Tx{Transaction::read(r)};
    if (command == "filterload") {
        Bytes data = r.readVarBytes(BloomFilter::maxFilterSize);
        uint32_t funcs = r.readUInt32();
        uint32_t tweak = r.readUInt32();
        uint8_t flags = r.readUInt8();
        return msg::FilterLoad{BloomFilter(std::move(data), funcs, tweak, flags)};
    }
    return msg::Unknown{command};
}

} // namespace Messages

namespace Wire {

Bytes frame(const Message& message, uint32_t magic) { return frame(Messages::command(message), Messages::payload(message), magic); }

Bytes frame(const std::string& command, ByteView payload, uint32_t magic) {
    ByteWriter w(headerSize + payload.size());
    w.writeUInt32(magic);
    uint8_t name[12] = {};
    std::memcpy(name, command.data(), std::min<size_t>(command.size(), 12));
    w.write(ByteView(name, 12));
    w.writeUInt32(uint32_t(payload.size()));
    Hash256 checksum = Hashes::sha256d(payload);
    w.write(ByteView(checksum.bytes.data(), 4));
    w.write(payload);
    return w.take();
}

Header parseHeader(ByteView data) {
    ByteReader r(data);
    Header h;
    h.magic = r.readUInt32();
    Bytes raw = r.readBytes(12);
    size_t end = 0;
    while (end < 12 && raw[end] != 0) ++end;
    for (size_t i = end; i < 12; ++i)
        if (raw[i] != 0) throw DecodeError("malformed command");
    h.command = std::string(raw.begin(), raw.begin() + long(end));
    h.length = r.readUInt32();
    if (h.length > maxPayload) throw DecodeError("payload of " + std::to_string(h.length) + " bytes exceeds the limit");
    Bytes checksum = r.readBytes(4);
    std::copy(checksum.begin(), checksum.end(), h.checksum.begin());
    return h;
}

bool checksumMatches(const Header& header, ByteView payload) {
    Hash256 checksum = Hashes::sha256d(payload);
    return std::equal(header.checksum.begin(), header.checksum.end(), checksum.bytes.begin());
}

} // namespace Wire

} // namespace tb
