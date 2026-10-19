#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "bitcoin/network.h"
#include "p2p/peer.h"
#include "spv/headerchain.h"
#include "storage/store.h"
#include "util/log.h"
#include "util/task.h"
#include "wallet/hdwallet.h"
#include "wallet/history.h"
#include "wallet/spend.h"

namespace tb {

/// SyncStatus is a snapshot of the sync for display: the connected peers,
/// the current activity and the block height it reached, whether the wallet
/// is synced to the tip, and the balance in satoshis: confirmed, and the
/// change pending unconfirmed transactions make to it.
struct SyncStatus {
    enum class Activity { idle, headers, filters };

    Activity activity = Activity::idle;
    int peers = 0;
    int32_t height = 0;
    bool synced = false;
    int64_t balance = 0;
    int64_t pending = 0;

    /// The connectivity level from 0 to the target peer count: one bar per
    /// connected peer.
    int bars() const;

    /// The status line: not connected, syncing with the block reached so
    /// far, or connected once synced to the tip.
    std::string text() const;

    friend bool operator==(const SyncStatus&, const SyncStatus&) = default;
};

/// WatchScript is one wallet output script kept for matching, with its
/// address and the derivation path of its key.
struct WatchScript {
    std::string address;
    std::string path;
    Bytes script;
};

struct BytesHasher {
    size_t operator()(const Bytes& b) const {
        uint64_t h = 0xcbf29ce484222325ull;
        for (uint8_t c : b) h = (h ^ c) * 0x100000001b3ull;
        return size_t(h);
    }
};

/// WalletState is the wallet rebuilt from its stored confirmed and
/// unconfirmed transactions.
struct WalletState {
    bool initialized = false;
    std::vector<WatchScript> scripts;
    std::unordered_map<Bytes, size_t, BytesHasher> scriptIndex;
    std::unordered_map<OutPoint, std::string, OutPointHasher> outpoints;
    std::unordered_set<Hash256, Hash256Hasher> confirmedIDs;
    std::unordered_set<Hash256, Hash256Hasher> pendingIDs;
    std::unordered_map<OutPoint, Hash256, OutPointHasher> pendingSpends;
    std::unordered_set<Hash256, Hash256Hasher> seenTxs;
    std::unordered_set<std::string> usedAddrs;
    std::unordered_map<std::string, int32_t> firstPaid;
    std::vector<Coin> coins;
    std::vector<Transaction> pendingTxs;
    std::vector<WalletTransaction> history;
    int64_t confirmedBalance = 0;
    int64_t pendingBalance = 0;
};

/// SyncEngine keeps the wallet synchronised with the Bitcoin network: it
/// holds a small pool of P2P peers, downloads and validates block headers
/// and BIP158 filters, matches the wallet scripts, downloads the matching
/// blocks and follows new blocks and relayed transactions while the pool
/// stays connected.
class SyncEngine {
public:
    /// The number of peer connections kept open.
    static constexpr int targetPeers = 3;
    /// How many candidates are dialed at once while the pool is first filled.
    static constexpr int connectWorkers = 5;
    /// How many peers are asked for the first filter header when the anchor
    /// of the filter header chain is proven by majority, and how many of them
    /// are dialed at once.
    static constexpr size_t anchorPeerLimit = 10;
    static constexpr int anchorDialers = 8;
    /// The longest delay between reconnects to the pinned peer; a connection
    /// lasting pinnedStableAfter resets it.
    static constexpr Duration pinnedMaxBackoff = 60s;
    static constexpr Duration pinnedStableAfter = 30s;
    /// How long the pinned peer may be away before the sync falls back to the
    /// other peers until it is back.
    static constexpr Duration pinnedFallbackAfter = 300s;
    /// The pace of reconnects and failed sync rounds.
    static constexpr Duration retryDelay = 5s;
    /// The fallback re-sync period, in case a block announcement is missed.
    static constexpr Duration pollInterval = 600s;
    /// How long a request may wait for its response.
    static constexpr Duration requestTimeout = 90s;
    /// The least time the other peers get to answer a parallel request after
    /// the primary did; beyond it a peer may take at most as long as the
    /// primary took before it is dropped as too slow.
    static constexpr Duration stragglerGrace = 5s;
    /// The size limits of one request: headers and filter headers per batch,
    /// and the smaller limit for full filters.
    static constexpr size_t headerBatch = 2000;
    static constexpr int32_t filterHeaderBatch = 2000;
    static constexpr int32_t filterBatch = 1000;
    /// Filters start this many blocks before the wallet's creation, so no
    /// relevant transaction is missed by timestamp jitter.
    static constexpr int32_t safetyGap = 10;
    /// Bounds one peer's answer while the filter header anchor is proven; ten
    /// peers are asked, so it is shorter than a normal request.
    static constexpr Duration anchorRequestTimeout = 20s;
    /// Drops unconfirmed transactions that never confirmed: Bitcoin Core's
    /// default mempool expiry.
    static constexpr int64_t pendingExpiry = 14 * 24 * 60 * 60;
    /// Bounds the set of relayed transaction ids already requested.
    static constexpr size_t seenLimit = 10'000;
    /// The false positive rate of the wallet's BIP37 filter: lower rates leak
    /// more about which addresses are the wallet's, higher ones make peers
    /// relay more unrelated transactions. The filter is sized for at least
    /// bloomMinElements, as a filter of one or two is far above its nominal
    /// rate.
    static constexpr double bloomFPRate = 0.0001;
    static constexpr uint32_t bloomMinElements = 20;
    /// How many checked relayed transactions are counted between two log
    /// lines.
    static constexpr int64_t relayLogEvery = 1000;
    /// The input address of a coinbase transaction.
    static constexpr const char* coinbaseInput = "coinbase";

    using StatusCallback = std::function<void(const SyncStatus&)>;

    /// Opens the sync state of the wallet database: the stored header chain,
    /// which was validated before it was stored, and the wallet scripts and
    /// transactions. The genesis header is stored on first use.
    SyncEngine(const Network& network, Store& store);
    ~SyncEngine();
    SyncEngine(const SyncEngine&) = delete;
    SyncEngine& operator=(const SyncEngine&) = delete;

    const Network& network() const { return network_; }

    /// The tip height of the validated header chain.
    int32_t chainHeight() const;
    std::optional<ChainEntry> entry(int32_t height) const;
    int32_t heightOf(const Hash256& hash) const;

    /// The height filter download begins at.
    int32_t filterStart() const;

    /// Starts syncing in the background: it keeps targetPeers peers
    /// connected, syncs headers and filters and stays connected to follow new
    /// blocks. Every change of the status is reported through onStatus, from
    /// any thread. pinned is an optional host:port peer assumed to be always
    /// available: it is never dropped or replaced, it is reconnected whenever
    /// the connection is lost, and headers and filters are synced from it
    /// while it is connected. The other peers cross-check its filters and
    /// take over once it has been away for pinnedFallbackAfter.
    void start(const std::string& pinned, StatusCallback onStatus);

    /// Ends the sync: it closes every peer connection and waits for the
    /// background work to finish. It is safe to call more than once.
    void stop();

    /// Pins the running sync to another peer, as start's pinned does, or
    /// unpins it with "". The pinned peer before is disconnected, and so is a
    /// pool connection to the new one, which is dialed again as pinned.
    void setPinned(const std::string& addr);

    /// The current status.
    SyncStatus status() const;

    /// Whether the address received a payment, confirmed or pending.
    bool isUsed(const std::string& address) const;

    /// The spendable wallet coins, confirmed and unconfirmed, the largest
    /// first.
    std::vector<Coin> coins() const;

    /// The wallet transactions, confirmed and unconfirmed, the newest first.
    std::vector<WalletTransaction> history() const;

    /// Watches a newly derived P2WPKH wallet address, receive or change, so
    /// filters, blocks and relayed transactions are matched against it too,
    /// and refreshes the peers' bloom filters. Stored transactions may pay
    /// the address already, when another wallet on the same seed used it
    /// first: their coins appear at once, and a rescan from the first block
    /// paying it is queued, since spends of them were not looked for in the
    /// blocks scanned before.
    void watch(const DerivedAddress& address);

    /// Sends a signed wallet transaction to every connected peer and records
    /// it as pending, so the balance reflects it at once. It is sent again to
    /// every peer that connects until it confirms. It returns the number of
    /// peers it was sent to.
    size_t broadcast(const Transaction& tx);

    /// Dials one candidate and adds it to the pool, reporting whether it
    /// joined. A peer without the compact filters service is dropped after
    /// its services are stored, and so is one completing its handshake after
    /// others filled the pool. A dial abandoned by cancellation is not held
    /// against the peer.
    bool connect(const std::string& addr, bool pinned);

    /// The connected peers with the primary first: the pinned peer when one
    /// is set, the oldest connection otherwise.
    std::vector<PeerPtr> orderedPeers() const;

    /// The anchor a strict majority of the votes report. No votes, and ties
    /// without a majority, are errors.
    static Hash256 majorityAnchor(const std::vector<Hash256>& votes);

    /// The height filters start at: safetyGap blocks before the first block
    /// at or after the wallet's creation time, clamped to genesis.
    static int32_t filterStartHeight(const HeaderChain& chain, int64_t seedTime);

    using IsMine = std::function<bool(const Bytes&)>;

    /// The confirmed balance, the value of the wallet coins the confirmed
    /// transactions leave unspent, and the change the unconfirmed ones make
    /// to it, in satoshis.
    static std::pair<int64_t, int64_t> computeBalance(const std::vector<Transaction>& confirmed, const std::vector<Transaction>& unconfirmed,
                                                      const IsMine& isMine);

    /// The value of the wallet outputs of the transactions that none of them
    /// spends.
    static int64_t unspentValue(const std::vector<Transaction>& txs, const IsMine& isMine);

    /// Describes the wallet transactions, the confirmed ones in block order
    /// followed by the unconfirmed ones, each with its time, and returns them
    /// newest first, the unconfirmed before the confirmed.
    static std::vector<WalletTransaction> buildHistory(const std::vector<Transaction>& txs, const std::vector<int64_t>& times, size_t confirmed,
                                                       const WalletState& w, const Network& network);

    /// The address of the output an input spends, read from its signature
    /// script and witness: P2WPKH and P2WSH, bare or nested in P2SH, P2PKH
    /// and P2SH. Taproot spends do not reveal the output key and give an
    /// empty address, as does anything not recognised.
    static std::string spentAddress(const Bytes& sigScript, const std::vector<Bytes>& witness, const Network& network);

private:
    struct ChainState {
        HeaderChain headers;
        int64_t seedTime = 0;
        int32_t filterStart = 0;
        Hash256 anchorPrev;
        bool anchorSet = false;
    };

    struct ManagerState {
        std::vector<PeerPtr> pool;
        SyncStatus::Activity activity = SyncStatus::Activity::idle;
        int32_t height = 0;
        bool stopped = true;
        StatusCallback onStatus;
        std::string pinnedAddr;
        TimePoint pinnedSeen = Clock::now();
        std::shared_ptr<Task> pinnedTask;
        bool refill = false;
        bool roundOK = false;
        int announced = 0;
        int syncedThrough = 0;
        std::vector<std::string> candidates;
        size_t candNext = 0;
        bool anchorTried = false;
        /// The background work the engine waits for when it stops.
        std::vector<std::shared_ptr<Task>> tasks;
    };

    struct RelayState {
        std::string mode;
        int64_t txs = 0;
        int64_t bytes = 0;
    };

    /// FilterSource is a peer serving filters with the filter hashes it
    /// committed to in its cfheaders, indexed by height less the batch base.
    struct FilterSource {
        PeerPtr peer;
        std::vector<Hash256> hashes;
    };

    /// Variant is one version of a block's filter with the peers serving it.
    struct Variant {
        Bytes data;
        Hash256 hash;
        std::vector<PeerPtr> peers;
    };

    /// RescanTarget is one queued address with the height its rescan starts
    /// at.
    struct RescanTarget {
        RescanJob job;
        int32_t from = 0;
        WatchScript script;
    };

    /// MatchKey identifies a recorded filter match of an address in a block.
    using MatchKey = std::pair<int32_t, std::string>;

    template <class T>
    using Outcome = std::variant<T, std::string>;

    // The engine and its status (engine.cpp)
    void spawn(std::function<void()> work);
    void startPinned(const std::string& addr);
    std::string currentPinned() const;
    void emitStatus();
    static bool synced(const ManagerState& m);
    void blockAnnounced();
    void report(SyncStatus::Activity activity, int32_t height);

    // The peer pool (pool.cpp)
    void maintain();
    void fillPool();
    int openSlots() const;
    static int openSlots(const ManagerState& m);
    void keepPinned(const std::string& addr);
    bool connected(const std::string& addr) const;
    std::vector<PeerPtr> poolPeers() const;
    static std::vector<PeerPtr> livePeers(const std::vector<PeerPtr>& peers);
    PeerPtr pinnedConn() const;
    PeerPtr primary() const;
    std::vector<PeerPtr> filterPeers() const;
    bool pinnedAway() const;
    std::optional<Duration> fallbackDelay() const;
    void requirePrimary(const std::vector<PeerPtr>& peers) const;
    std::vector<std::string> takeCandidates(size_t n);
    std::vector<std::string> peerCandidates(const std::string& pinned);
    void drop(PeerConnection& c, const std::string& reason, const std::string& error);
    void record(const PeerConnection& c, bool ok, TimePoint started);
    void handle(PeerConnection& c, const Message& message);
    void savePeerAddr(const std::string& host, uint16_t port, ServiceFlags services);
    void prunePeers();

    template <class T>
    std::pair<std::vector<PeerPtr>, std::vector<T>> fanOut(const std::vector<PeerPtr>& peers, const std::string& what,
                                                           const std::function<T(const PeerPtr&)>& request);

    // Requests (requests.cpp)
    std::vector<BlockHeader> getHeaders(PeerConnection& c, const std::vector<Hash256>& locator);
    CFHeaders getCFHeaders(PeerConnection& c, int32_t start, const Hash256& stop, Duration timeout);
    std::vector<Bytes> getCFilters(PeerConnection& c, int32_t start, const Hash256& stop);
    Block getBlock(PeerConnection& c, const Hash256& hash);

    // Headers and the filter anchor (headers.cpp)
    void refreshFilterStart();
    bool needsAnchor() const;
    bool anchorPending();
    void setFilterAnchor(const Hash256& anchor);
    Hash256 requestFilterAnchor(PeerConnection& c);
    int32_t syncHeaders(PeerConnection& c);
    void extendHeaders(const std::vector<BlockHeader>& batch);
    void rewind(int32_t fork);
    void syncLoop();
    void syncOnce(const PeerPtr& c);
    std::optional<Hash256> proveFilterAnchor();
    Hash256 dialAnchor(const std::string& addr);

    // Filters (filtersync.cpp)
    int32_t syncFilters();
    std::vector<FilterSource> filterHashes(int32_t start, int32_t end);
    int32_t syncFilterBatch(const std::vector<FilterSource>& sources, int32_t base, int32_t fStart, int32_t fEnd);
    static std::vector<Variant> groupVariants(const std::vector<PeerPtr>& peers, const std::vector<std::vector<Bytes>>& got, size_t index);
    std::pair<Bytes, std::optional<Block>> chooseFilter(int32_t height, const std::vector<Variant>& variants);
    static std::pair<size_t, std::vector<bool>> resolveVariants(const std::vector<Variant>& variants, const Block& block);
    static bool filterHasOutputs(const Bytes& data, const Hash256& blockHash, const Block& block);
    int32_t storeFilters(int32_t fStart, int32_t count, const std::function<std::pair<Bytes, std::optional<Block>>(int32_t)>& pick);
    void checkFilterPrev(int32_t start, const Hash256& got);
    Hash256 prevFilterHeader(int32_t height);
    StoredFilter applyFilter(int32_t height, const Bytes& data, std::optional<Block> block, const Hash256& prev);
    Block fetchBlock(int32_t height);
    static std::string describe(const std::vector<Variant>& variants);

    // The wallet (walletsync.cpp)
    void loadWallet();
    void loadWalletLocked(WalletState& w);
    int64_t blockTime(int32_t height) const;
    std::pair<int64_t, int64_t> walletBalance() const;
    std::vector<WatchScript> watchedScripts() const;
    bool describeTx(const Transaction& tx, const WalletState& w, const std::string& place) const;
    int processBlock(int32_t height, const Block& block);
    void requestTxs(PeerConnection& c, const std::vector<InvVect>& list);
    void acceptPendingTx(PeerConnection& c, const Transaction& tx);
    void expirePending();
    void walletChanged();
    void rebroadcast(PeerConnection& c);
    BloomFilter walletBloom() const;
    bool loadBloom(PeerConnection& c);
    void reloadBlooms();
    void countRelayed(PeerConnection& c, const Transaction& tx);
    static bool relaySource(const PeerConnection& c, const std::vector<PeerPtr>& peers);
    static std::string describeRelay(const std::vector<PeerPtr>& peers);
    void logRelayMode();
    static TxAddress outputAddress(const Bytes& script, const WalletState& w, const Network& network);
    static std::pair<TxAddress, int64_t> inputAddress(const TxIn& input,
                                                      const std::unordered_map<Hash256, const Transaction*, Hash256Hasher>& byID,
                                                      const WalletState& w, const Network& network);

    // Rescans (rescan.cpp)
    void rescan();
    std::pair<std::vector<RescanTarget>, std::vector<RescanJob>> rescanTargets(const std::vector<RescanJob>& jobs, int32_t end);
    void rescanBlock(int32_t height, const Bytes& data, const std::vector<RescanTarget>& targets, std::set<MatchKey>& matched,
                     const std::function<Block(int32_t)>& getBlock);
    std::vector<Bytes> refetchFilters(int32_t start, int32_t end);
    void checkStoredChain(int32_t start, const Hash256& prev, const std::vector<Bytes>& filters);

    const Network network_;
    Store& store_;

    mutable std::mutex chainMutex_;
    ChainState chain_;
    mutable std::mutex walletMutex_;
    WalletState wallet_;
    mutable std::mutex managerMutex_;
    ManagerState manager_;
    /// Serializes status callbacks so they arrive in the order the status
    /// changed.
    std::mutex emitMutex_;
    Pulse wake_;
    Pulse poolChanged_;
    std::mutex relayMutex_;
    RelayState relay_;
};

template <class T>
std::pair<std::vector<PeerPtr>, std::vector<T>> SyncEngine::fanOut(const std::vector<PeerPtr>& peers, const std::string& what,
                                                                   const std::function<T(const PeerPtr&)>& request) {
    // Runs the request against every peer at once. peers[0] is the primary:
    // once it answered, the others get as long as it took, and at least
    // stragglerGrace, to answer too; when the primary fails, the first
    // successful answer sets that pace. It returns the peers that answered
    // in time, in their order, with their results; a peer whose request
    // failed or came too late is dropped.
    if (peers.empty()) return {};
    TimePoint started = Clock::now();
    std::mutex mutex;
    std::condition_variable_any cv;
    std::vector<std::optional<Outcome<T>>> results(peers.size());
    size_t pending = peers.size();
    size_t completed = 0;
    Duration grace{0};
    std::vector<std::optional<Outcome<T>>> answered;
    {
        TaskGroup group;
        for (size_t i = 0; i < peers.size(); ++i) {
            group.add([&, i] {
                Outcome<T> outcome;
                try {
                    outcome = request(peers[i]);
                } catch (const std::exception& e) {
                    outcome = std::string(e.what());
                }
                {
                    std::lock_guard lock(mutex);
                    results[i] = std::move(outcome);
                    --pending;
                    ++completed;
                }
                cv.notify_all();
            });
        }
        std::unique_lock lock(mutex);
        std::stop_token token = task::token();
        bool paced = false;
        TimePoint deadline{};
        while (pending > 0 && !token.stop_requested()) {
            bool primaryDone = results[0].has_value();
            bool primaryOK = primaryDone && results[0]->index() == 0;
            bool anyOK = false;
            for (const auto& r : results) anyOK = anyOK || (r && r->index() == 0);
            if (!paced && primaryDone && (primaryOK || anyOK)) {
                paced = true;
                grace = std::max<Duration>(stragglerGrace, Clock::now() - started);
                deadline = Clock::now() + grace;
            }
            if (paced) {
                if (!cv.wait_until(lock, token, deadline, [&] { return pending == 0; })) break;
            } else {
                size_t seen = completed;
                cv.wait(lock, token, [&] { return completed != seen; });
            }
        }
        answered = results;
        lock.unlock();
        group.cancelAll();
    }
    if (task::isCancelled()) return {};
    std::vector<PeerPtr> okPeers;
    std::vector<T> okResults;
    for (size_t i = 0; i < peers.size(); ++i) {
        if (!answered[i]) {
            drop(*peers[i], what, "too slow, no answer within " + formatDuration(grace) + " after the primary");
        } else if (answered[i]->index() == 1) {
            drop(*peers[i], what, std::get<1>(*answered[i]));
        } else {
            okPeers.push_back(peers[i]);
            okResults.push_back(std::move(std::get<0>(*answered[i])));
        }
    }
    return {okPeers, okResults};
}

} // namespace tb
