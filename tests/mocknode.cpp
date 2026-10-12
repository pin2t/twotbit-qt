#include "mocknode.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <ctime>
#include <stdexcept>

#include "bitcoin/script.h"
#include "bitcoin/uint256.h"
#include "crypto/hashes.h"
#include "p2p/hostport.h"
#include "p2p/peer.h"

namespace tb::test {

std::unique_ptr<MockNode> MockNode::start(const std::vector<Block>& blocks) {
    ensureSockets();
    std::unique_ptr<MockNode> node(new MockNode());
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("mock node: no socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(s, 16) != 0) {
        closesocket(s);
        throw std::runtime_error("mock node: cannot listen");
    }
    int length = sizeof address;
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    node->listener_ = uintptr_t(s);
    node->port_ = ntohs(address.sin_port);
    node->reset(blocks);
    node->acceptor_ = std::thread([n = node.get()] { n->acceptLoop(); });
    return node;
}

MockNode::~MockNode() { stop(); }

void MockNode::stop() {
    if (stopped_.exchange(true)) return;
    closesocket(SOCKET(listener_));
    if (acceptor_.joinable()) acceptor_.join();
    for (auto& p : with([](State& s) { return s.peers; })) p->close();
    std::vector<std::thread> servers;
    {
        std::lock_guard lock(threadsMutex_);
        servers.swap(servers_);
    }
    for (auto& t : servers) t.join();
}

void MockNode::acceptLoop() {
    while (!stopped_) {
        SOCKET client = accept(SOCKET(listener_), nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        auto stream = std::shared_ptr<TcpStream>(TcpStream::adopt(uintptr_t(client), "mock peer"));
        with([&](State& s) {
            s.peers.push_back(stream);
            return 0;
        });
        std::lock_guard lock(threadsMutex_);
        if (stopped_) {
            stream->close();
            return;
        }
        servers_.emplace_back([this, stream] { serve(stream); });
    }
}

void MockNode::reset(const std::vector<Block>& blocks) {
    with([&](State& s) {
        s.blocks.clear();
        s.filters.clear();
        s.filterHeaders.clear();
        s.utxos.clear();
        append(s, Block{network.genesis, {}});
        for (const auto& b : blocks) append(s, b);
        return 0;
    });
}

void MockNode::add(const std::vector<Block>& blocks) {
    with([&](State& s) {
        for (const auto& b : blocks) append(s, b);
        return 0;
    });
}

void MockNode::append(State& s, const Block& block) {
    std::vector<Bytes> scripts;
    for (const auto& tx : block.transactions) {
        for (const auto& out : tx.outputs)
            if (!out.pkScript.empty() && out.pkScript.front() != Script::opReturn) scripts.push_back(out.pkScript);
        if (!tx.isCoinbase()) {
            for (const auto& in : tx.inputs) {
                auto spent = s.utxos.find(in.previousOutPoint);
                if (spent != s.utxos.end() && !spent->second.empty()) scripts.push_back(spent->second);
            }
        }
    }
    std::vector<Bytes> unique;
    for (const auto& script : scripts)
        if (std::find(unique.begin(), unique.end(), script) == unique.end()) unique.push_back(script);
    Hash256 blockHash = block.hash();
    Bytes filter = GCSFilter::build(unique, blockHash);
    Hash256 prev = s.filterHeaders.empty() ? Hash256() : s.filterHeaders.back();
    s.filterHeaders.push_back(GCSFilter::header(GCSFilter::hash(filter), prev));
    s.filters.push_back(filter);
    for (const auto& tx : block.transactions) {
        for (const auto& in : tx.inputs) s.utxos.erase(in.previousOutPoint);
        Hash256 txid = tx.txid();
        for (size_t i = 0; i < tx.outputs.size(); ++i) s.utxos[OutPoint{txid, uint32_t(i)}] = tx.outputs[i].pkScript;
        s.mempool.erase(txid);
    }
    s.blocks.push_back(block);
}

void MockNode::relay(const Transaction& tx) {
    auto peers = with([&](State& s) {
        s.mempool[tx.txid()] = tx;
        return s.peers;
    });
    for (auto& p : peers) p->send(Wire::frame(msg::Inv{{InvVect(InvType::tx, tx.txid())}}, network.magic));
}

void MockNode::announceTip() {
    auto [peers, hash] = with([](State& s) { return std::make_pair(s.peers, s.blocks.back().hash()); });
    for (auto& p : peers) p->send(Wire::frame(msg::Inv{{InvVect(InvType::block, hash)}}, network.magic));
}

void MockNode::dropConnections() {
    auto peers = with([](State& s) {
        auto out = s.peers;
        s.peers.clear();
        return out;
    });
    for (auto& p : peers) p->close();
}

void MockNode::serve(std::shared_ptr<TcpStream> stream) {
    with([&](State& s) { return s.connections += 1; });
    auto send = [&](const Message& m) { stream->send(Wire::frame(m, network.magic)); };
    auto indexOf = [](const State& s, const Hash256& hash) -> int {
        for (size_t i = 0; i < s.blocks.size(); ++i)
            if (s.blocks[i].hash() == hash) return int(i);
        return -1;
    };
    while (!stopped_) {
        Message message;
        try {
            auto [command, payload] = PeerConnection::readFrame(*stream, network);
            message = Messages::decode(command, payload);
        } catch (const std::exception&) {
            break;
        }
        State s = with([](State& st) {
            State copy;
            copy.blocks = st.blocks;
            copy.filters = st.filters;
            copy.filterHeaders = st.filterHeaders;
            copy.mempool = st.mempool;
            copy.services = st.services;
            copy.wrongFilters = st.wrongFilters;
            copy.silent = st.silent;
            return copy;
        });
        if (s.silent && !std::holds_alternative<msg::Version>(message)) continue;
        if (std::holds_alternative<msg::Version>(message)) {
            NetAddress me = NetAddress::of("127.0.0.1", port_);
            VersionMessage v;
            v.protocolVersion = 70016;
            v.services = s.services;
            v.timestamp = int64_t(std::time(nullptr));
            v.addrRecv = me;
            v.addrFrom = me;
            v.nonce = Hashes::randomUInt64() | 1;
            v.userAgent = "/Mock:1.0/";
            v.startHeight = int32_t(s.blocks.size()) - 1;
            v.relay = true;
            send(msg::Version{v});
            send(msg::Verack{});
        } else if (auto* ping = std::get_if<msg::Ping>(&message)) {
            send(msg::Pong{ping->nonce});
        } else if (auto* gh = std::get_if<msg::GetHeaders>(&message)) {
            int start = 0;
            for (const auto& hash : gh->locator) {
                int i = indexOf(s, hash);
                if (i >= 0) {
                    start = i;
                    break;
                }
            }
            msg::Headers headers;
            for (size_t i = size_t(start) + 1; i < s.blocks.size() && headers.headers.size() < 2000; ++i) headers.headers.push_back(s.blocks[i].header);
            send(headers);
        } else if (auto* gch = std::get_if<msg::GetCFHeaders>(&message)) {
            int end = indexOf(s, gch->stop);
            if (end < 0 || int(gch->startHeight) > end) continue;
            CFHeaders c;
            c.filterType = 0;
            c.stopHash = gch->stop;
            c.prevFilterHeader = gch->startHeight == 0 ? Hash256() : s.filterHeaders[gch->startHeight - 1];
            for (int h = int(gch->startHeight); h <= end; ++h) {
                auto wrong = s.wrongFilters.find(h);
                c.filterHashes.push_back(GCSFilter::hash(wrong != s.wrongFilters.end() ? wrong->second : s.filters[size_t(h)]));
            }
            send(msg::CFHeadersMsg{c});
        } else if (auto* gcf = std::get_if<msg::GetCFilters>(&message)) {
            int end = indexOf(s, gcf->stop);
            if (end < 0 || int(gcf->startHeight) > end) continue;
            for (int h = int(gcf->startHeight); h <= end; ++h) {
                auto wrong = s.wrongFilters.find(h);
                send(msg::CFilterMsg{CFilter{0, s.blocks[size_t(h)].hash(), wrong != s.wrongFilters.end() ? wrong->second : s.filters[size_t(h)]}});
            }
        } else if (auto* gd = std::get_if<msg::GetData>(&message)) {
            std::vector<InvVect> missing;
            for (const auto& iv : gd->list) {
                int i = indexOf(s, iv.hash);
                if (i >= 0) {
                    send(msg::BlockMsg{s.blocks[size_t(i)]});
                } else if (auto tx = s.mempool.find(iv.hash); tx != s.mempool.end()) {
                    send(msg::Tx{tx->second});
                } else {
                    missing.push_back(iv);
                }
            }
            if (!missing.empty()) send(msg::NotFound{missing});
        } else if (auto* tx = std::get_if<msg::Tx>(&message)) {
            with([&](State& st) {
                st.received.push_back(tx->tx);
                st.mempool[tx->tx.txid()] = tx->tx;
                return 0;
            });
        } else if (std::holds_alternative<msg::FilterLoad>(message)) {
            with([](State& st) { return st.bloomLoads += 1; });
        } else if (std::holds_alternative<msg::MemPool>(message)) {
            msg::Inv inv;
            for (const auto& [txid, tx] : s.mempool) inv.list.emplace_back(InvType::tx, txid);
            send(inv);
        }
    }
    stream->close();
}

Block ChainBuilder::mine(const std::vector<Transaction>& txs, const Bytes& coinbaseTo) {
    height += 1;
    Transaction coinbase;
    coinbase.version = 1;
    TxIn in;
    in.previousOutPoint = OutPoint::coinbase();
    in.signatureScript = Bytes{0x03, uint8_t(height & 0xff), uint8_t(height >> 8), 0};
    coinbase.inputs.push_back(in);
    coinbase.outputs.push_back(TxOut{int64_t(50) * 100'000'000, coinbaseTo});
    bool witness = std::any_of(txs.begin(), txs.end(), [](const Transaction& tx) { return tx.hasWitness(); });
    if (witness) {
        coinbase.inputs[0].witness = {Bytes(32, 0)};
        std::vector<Hash256> wtxids{Hash256()};
        for (const auto& tx : txs) wtxids.push_back(tx.wtxid());
        Bytes preimage = Block::merkleRoot(wtxids).data();
        append(preimage, Bytes(32, 0));
        Bytes commitment = Block::witnessMagic();
        append(commitment, Hashes::sha256d(preimage).data());
        coinbase.outputs.push_back(TxOut{0, commitment});
    }
    std::vector<Transaction> all{coinbase};
    all.insert(all.end(), txs.begin(), txs.end());
    std::vector<Hash256> txids;
    for (const auto& tx : all) txids.push_back(tx.txid());
    BlockHeader header{0x20000000, tip.hash(), Block::merkleRoot(txids), time, easyBits, 0};
    UInt256 target = *Compact::target(header.bits);
    while (UInt256::of(header.hash()) > target) header.nonce += 1;
    tip = header;
    time += 600;
    return Block{header, all};
}

std::vector<Block> ChainBuilder::mine(int n) {
    std::vector<Block> out;
    for (int i = 0; i < n; ++i) out.push_back(mine());
    return out;
}

Transaction payment(const std::vector<TxOut>& outputs, uint8_t seed) {
    Transaction tx;
    tx.version = 2;
    TxIn in;
    in.previousOutPoint = OutPoint{Hash256::fromWords(seed, 0xfeed, 0, 0), 0};
    in.signatureScript = Bytes{0x51};
    tx.inputs.push_back(in);
    tx.outputs = outputs;
    return tx;
}

Bytes outside() { return Script::p2wpkh(Bytes(20, 0x42)); }

bool eventually(Duration timeout, const std::function<bool()>& condition) {
    TimePoint deadline = Clock::now() + timeout;
    while (!condition()) {
        if (Clock::now() >= deadline) return false;
        std::this_thread::sleep_for(50ms);
    }
    return true;
}

BlockHeader mineHeader(const Hash256& prev, uint32_t bits, uint32_t time) {
    BlockHeader header{1, prev, Hash256(), time, bits, 0};
    UInt256 target = *Compact::target(bits);
    while (UInt256::of(header.hash()) > target) header.nonce += 1;
    return header;
}

} // namespace tb::test
