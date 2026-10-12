#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bitcoin/network.h"
#include "p2p/messages.h"
#include "p2p/tcp.h"
#include "spv/filters.h"
#include "util/task.h"

namespace tb::test {

/// MockNode is a regtest Bitcoin node in the test process: it listens on a
/// local port, completes the handshake and serves a mined chain's headers,
/// BIP158 basic filters, filter headers and blocks, relays its mempool and
/// records what the wallet sends it. Behaviour can be bent to test the
/// wallet against failing and lying peers.
class MockNode {
public:
    struct State {
        std::vector<Block> blocks;
        std::vector<Bytes> filters;
        std::vector<Hash256> filterHeaders;
        std::unordered_map<OutPoint, Bytes, OutPointHasher> utxos;
        std::unordered_map<Hash256, Transaction, Hash256Hasher> mempool;
        ServiceFlags services{ServiceFlags::network | ServiceFlags::witness | ServiceFlags::compactFilters};
        std::vector<Transaction> received;
        int bloomLoads = 0;
        int connections = 0;
        std::vector<std::shared_ptr<TcpStream>> peers;
        /// Filters served in place of the real ones, by height.
        std::map<int32_t, Bytes> wrongFilters;
        /// Whether requests are left unanswered.
        bool silent = false;
    };

    /// Starts a node with the chain of blocks after the regtest genesis.
    static std::unique_ptr<MockNode> start(const std::vector<Block>& blocks = {});
    ~MockNode();

    void stop();

    /// Replaces the chain with genesis and the blocks, rebuilding the filters
    /// and filter headers.
    void reset(const std::vector<Block>& blocks);
    void add(const std::vector<Block>& blocks);

    template <class F>
    auto with(F body) {
        std::lock_guard lock(mutex_);
        return body(state_);
    }

    Block tip() {
        return with([](State& s) { return s.blocks.back(); });
    }
    int32_t height() {
        return with([](State& s) { return int32_t(s.blocks.size()) - 1; });
    }
    std::vector<Transaction> received() {
        return with([](State& s) { return s.received; });
    }

    /// Puts the transaction in the mempool and announces it to every
    /// connected wallet.
    void relay(const Transaction& tx);

    /// Announces the tip to every connected wallet.
    void announceTip();

    /// Disconnects every connected wallet.
    void dropConnections();

    uint16_t port() const { return port_; }
    std::string address() const { return "127.0.0.1:" + std::to_string(port_); }

    const Network& network = Network::regtest();

private:
    MockNode() = default;
    static void append(State& s, const Block& block);
    void acceptLoop();
    void serve(std::shared_ptr<TcpStream> stream);

    std::mutex mutex_;
    State state_;
    uintptr_t listener_ = 0;
    uint16_t port_ = 0;
    std::atomic<bool> stopped_{false};
    std::thread acceptor_;
    std::mutex threadsMutex_;
    std::vector<std::thread> servers_;
};

/// ChainBuilder mines regtest blocks with valid merkle roots and witness
/// commitments, paying and spending as told.
struct ChainBuilder {
    BlockHeader tip;
    uint32_t time;
    int32_t height = 0;

    explicit ChainBuilder(const Network& network = Network::regtest(), std::optional<uint32_t> start = std::nullopt)
        : tip(network.genesis), time(start.value_or(network.genesis.timestamp + 600)) {}

    /// Mines the next block with a coinbase paying the miner script and the
    /// transactions.
    Block mine(const std::vector<Transaction>& txs = {}, const Bytes& coinbaseTo = Bytes{0x51});

    /// Mines n empty blocks.
    std::vector<Block> mine(int n);
};

/// A transaction paying the outputs from an outside coin, as another
/// wallet's payment to this one looks.
Transaction payment(const std::vector<TxOut>& outputs, uint8_t seed = 1);

/// The outside script payments go to and come from.
Bytes outside();

/// Waits until the condition holds, polling, and reports whether it did
/// before the timeout.
bool eventually(Duration timeout, const std::function<bool()>& condition);

/// Mines a header on the parent with the bits and time.
BlockHeader mineHeader(const Hash256& prev, uint32_t bits, uint32_t time);

/// easyBits is a minimal difficulty, so tests can mine headers at once.
constexpr uint32_t easyBits = 0x207fffff;

} // namespace tb::test
