#include <atomic>
#include <unordered_map>

#include "p2p/hostport.h"
#include "sync/engine.h"

namespace tb {

int32_t SyncEngine::filterStartHeight(const HeaderChain& chain, int64_t seedTime) {
    int32_t first = chain.firstHeightAtOrAfter(seedTime);
    return first > safetyGap ? first - safetyGap : 0;
}

int32_t SyncEngine::filterStart() const {
    std::lock_guard lock(chainMutex_);
    return chain_.filterStart;
}

void SyncEngine::refreshFilterStart() {
    // Recomputes the filter start height after the header sync: the chain may
    // have grown past the wallet's creation time, which moves it.
    std::lock_guard lock(chainMutex_);
    chain_.filterStart = filterStartHeight(chain_.headers, chain_.seedTime);
}

bool SyncEngine::needsAnchor() const {
    // Whether the first filter header must be majority proven before the
    // filter download starts.
    std::lock_guard lock(chainMutex_);
    return chain_.filterStart > 0 && chain_.filterStart <= chain_.headers.height();
}

bool SyncEngine::anchorPending() {
    // Whether the filter download starts at the filter start height and so
    // needs the anchor proven first. Once filters are stored from there on,
    // the stored filter headers link the chain.
    if (!needsAnchor()) return false;
    int32_t start = filterStart();
    return store_.filterResumeHeight(start) == start;
}

void SyncEngine::setFilterAnchor(const Hash256& anchor) {
    // Stores the majority proven anchor of the filter header chain; the first
    // cfheaders batch must then match it.
    std::lock_guard lock(chainMutex_);
    chain_.anchorPrev = anchor;
    chain_.anchorSet = true;
}

Hash256 SyncEngine::requestFilterAnchor(PeerConnection& c) {
    // Asks one peer for the cfheaders batch starting at the filter start
    // height and returns the previous filter header it reports: its
    // candidate anchor.
    int32_t start = filterStart();
    auto e = entry(start);
    if (!e) throw PeerError("filter start header " + std::to_string(start) + " not in chain");
    return getCFHeaders(c, start, e->hash, anchorRequestTimeout).prevFilterHeader;
}

Hash256 SyncEngine::majorityAnchor(const std::vector<Hash256>& votes) {
    if (votes.empty()) throw PeerError("no anchor votes");
    std::unordered_map<Hash256, size_t, Hash256Hasher> counts;
    for (const auto& vote : votes) counts[vote] += 1;
    Hash256 best = votes.front();
    for (const auto& [vote, n] : counts)
        if (n > counts[best]) best = vote;
    if (counts[best] * 2 <= votes.size())
        throw PeerError("no majority anchor: " + std::to_string(counts[best]) + " of " + std::to_string(votes.size()) + " peers agree");
    return best;
}

int32_t SyncEngine::syncHeaders(PeerConnection& c) {
    // Requests headers from the peer until its tip is reached, validating and
    // storing every batch, and returns the new tip height.
    while (true) {
        std::vector<Hash256> locator;
        {
            std::lock_guard lock(chainMutex_);
            locator = chain_.headers.locator();
        }
        std::vector<BlockHeader> batch = getHeaders(c, locator);
        if (batch.empty()) break;
        int32_t from = chainHeight() + 1;
        try {
            extendHeaders(batch);
        } catch (const CancellationError&) {
            throw;
        } catch (const std::exception& e) {
            throw PeerError(std::string("extend headers: ") + e.what());
        }
        int32_t tip = chainHeight();
        Log::info("sync: headers " + std::to_string(std::min(from, tip)) + "-" + std::to_string(tip) + " from " + c.address);
        report(SyncStatus::Activity::headers, tip);
        if (batch.size() < headerBatch) break;
    }
    return chainHeight();
}

void SyncEngine::extendHeaders(const std::vector<BlockHeader>& batch) {
    // Appends a peer's batch to the chain, storing the validated headers in
    // one transaction, those before an invalid one included. A batch forking
    // below the tip is a reorg: it is taken only when its branch has more
    // proof of work than the blocks it replaces, and then the headers,
    // filters, matches and wallet transactions above the fork are dropped.
    Hash256 first = batch.front().prevBlock;
    int32_t fork, tip;
    {
        std::lock_guard lock(chainMutex_);
        fork = chain_.headers.heightOf(first);
        tip = chain_.headers.height();
    }
    if (fork < 0) throw ChainError("peer chain continues from unknown block " + first.hex());
    if (fork < tip) {
        std::optional<HeaderChain> candidate;
        double current;
        {
            std::lock_guard lock(chainMutex_);
            candidate = chain_.headers;
            current = chain_.headers.workAbove(fork);
        }
        candidate->truncate(fork);
        for (const auto& header : batch) {
            try {
                candidate->add(header);
            } catch (const ChainError&) {
                break;
            }
        }
        double branch = candidate->workAbove(fork);
        candidate.reset();
        if (!(branch > current))
            throw ChainError("peer chain forks at height " + std::to_string(fork) + " with less proof of work than the " +
                             std::to_string(tip - fork) + " blocks after it");
        Log::info("sync: reorg, rewinding from height " + std::to_string(tip) + " to " + std::to_string(fork));
        rewind(fork);
    }
    std::vector<StoredHeader> rows;
    std::exception_ptr failure;
    {
        std::lock_guard lock(chainMutex_);
        for (const auto& header : batch) {
            try {
                chain_.headers.add(header);
            } catch (const ChainError&) {
                failure = std::current_exception();
                break;
            }
            rows.push_back(StoredHeader{chain_.headers.height(), header});
        }
    }
    try {
        store_.saveHeaders(rows);
    } catch (const std::exception& e) {
        std::lock_guard lock(chainMutex_);
        chain_.headers.truncate(chain_.headers.height() - int32_t(rows.size()));
        throw ChainError(std::string("store headers: ") + e.what());
    }
    if (failure) std::rethrow_exception(failure);
}

void SyncEngine::rewind(int32_t fork) {
    // Drops every header and every row derived from blocks above the height.
    {
        std::lock_guard lock(chainMutex_);
        chain_.headers.truncate(fork);
    }
    store_.deleteHeaders(fork);
    store_.deleteFilters(fork);
    store_.deleteMatches(fork);
    store_.deleteTransactions(fork);
    loadWallet();
}

void SyncEngine::syncLoop() {
    // Syncs from the primary peer whenever a new block is announced, a peer
    // connects, the pinned peer has been away long enough to fall back to the
    // others, or the poll interval elapses. A failing peer is disconnected so
    // the next round uses another one.
    bool fellBack = false;
    while (!task::isCancelled()) {
        PeerPtr c = primary();
        if (!c) {
            wake_.wait(fallbackDelay());
            continue;
        }
        std::string pinned = currentPinned();
        if (!pinned.empty() && c->pinned == fellBack) {
            fellBack = !c->pinned;
            Log::info(fellBack ? "sync: pinned peer " + pinned + " away for " + formatDuration(pinnedFallbackAfter) + ", syncing from " + c->address
                               : "sync: syncing from pinned peer " + pinned);
        }
        int seen;
        {
            std::lock_guard lock(managerMutex_);
            seen = manager_.announced;
        }
        std::optional<std::string> failure;
        try {
            syncOnce(c);
        } catch (const std::exception& e) {
            failure = e.what();
        }
        if (task::isCancelled()) return;
        {
            std::lock_guard lock(managerMutex_);
            manager_.roundOK = !failure;
            if (!failure) manager_.syncedThrough = seen;
        }
        report(SyncStatus::Activity::idle, chainHeight());
        if (failure) {
            Log::info("sync: round failed: " + *failure);
            if (!task::sleep(retryDelay)) return;
            continue;
        }
        wake_.wait(pollInterval);
    }
}

void SyncEngine::syncOnce(const PeerPtr& c) {
    // Brings the headers up to the primary peer's tip, then the filters and
    // wallet transactions up to the header tip, and runs the queued rescans.
    // The first filter header is majority proven once per run before the
    // filter download starts. A primary failing the header sync is
    // disconnected so the next round uses another.
    expirePending();
    int32_t before = chainHeight();
    report(SyncStatus::Activity::headers, before);
    try {
        syncHeaders(*c);
    } catch (const CancellationError&) {
        throw;
    } catch (const std::exception& e) {
        drop(*c, "headers", e.what());
        throw PeerError(std::string("headers: ") + e.what());
    }
    if (chainHeight() != before) Log::info("sync: headers synced to " + std::to_string(chainHeight()) + " from " + c->address);
    refreshFilterStart();
    bool tried;
    {
        std::lock_guard lock(managerMutex_);
        tried = manager_.anchorTried;
    }
    if (anchorPending() && !tried) {
        report(SyncStatus::Activity::filters, filterStart());
        std::optional<Hash256> anchor;
        try {
            anchor = proveFilterAnchor();
        } catch (const CancellationError&) {
            throw;
        } catch (const std::exception& e) {
            throw PeerError(std::string("prove filter header anchor: ") + e.what());
        }
        {
            std::lock_guard lock(managerMutex_);
            manager_.anchorTried = true;
        }
        if (anchor) {
            setFilterAnchor(*anchor);
            Log::info("sync: filter header anchor at height " + std::to_string(filterStart()) + " proven by peer majority: " + anchor->hex());
        } else {
            Log::info("sync: no peer answered the filter header anchor request; the filter peers will be trusted");
        }
    }
    int32_t stored;
    try {
        stored = syncFilters();
    } catch (const CancellationError&) {
        throw;
    } catch (const std::exception& e) {
        throw PeerError(std::string("filters: ") + e.what());
    }
    try {
        rescan();
    } catch (const CancellationError&) {
        throw;
    } catch (const std::exception& e) {
        throw PeerError(std::string("rescan: ") + e.what());
    }
    if (stored > 0 || chainHeight() != before)
        Log::info("sync: wallet synced to tip " + std::to_string(chainHeight()) + " (" + std::to_string(stored) + " new filters)");
}

std::optional<Hash256> SyncEngine::proveFilterAnchor() {
    // Asks up to anchorPeerLimit peers for the first filter header and
    // returns the value a strict majority reports: the connected peers all at
    // once, then further candidates dialed anchorDialers at a time until
    // enough votes are in. Nothing when no peer answered; peers disagreeing
    // without a majority are an error.
    auto [answered, initial] =
        fanOut<Hash256>(filterPeers(), "filter header anchor", [this](const PeerPtr& c) { return requestFilterAnchor(*c); });
    std::set<std::string> inPool;
    for (const auto& c : poolPeers()) inPool.insert(c->address);
    std::vector<std::string> list;
    {
        std::lock_guard lock(managerMutex_);
        for (const auto& addr : manager_.candidates)
            if (!inPool.count(addr)) list.push_back(addr);
    }
    std::mutex mutex;
    std::vector<Hash256> votes = initial;
    size_t dialed = 0;
    std::atomic<size_t> next{0};
    {
        TaskGroup group;
        for (int w = 0; w < anchorDialers; ++w) {
            group.add([&] {
                while (!task::isCancelled()) {
                    {
                        std::lock_guard lock(mutex);
                        if (votes.size() >= anchorPeerLimit) return;
                    }
                    size_t index = next.fetch_add(1);
                    if (index >= list.size()) return;
                    Hash256 anchor;
                    try {
                        anchor = dialAnchor(list[index]);
                    } catch (const std::exception&) {
                        continue;
                    }
                    std::lock_guard lock(mutex);
                    votes.push_back(anchor);
                    ++dialed;
                }
            });
        }
        group.waitAll();
    }
    task::checkCancelled();
    Log::info("sync: filter header anchor votes: " + std::to_string(answered.size()) + " from connected peers, " + std::to_string(dialed) +
              " from dialed peers");
    if (votes.empty()) return std::nullopt;
    return majorityAnchor(votes);
}

Hash256 SyncEngine::dialAnchor(const std::string& addr) {
    // Connects to one candidate just long enough to ask it for the filter
    // header anchor.
    auto [host, port] = HostPort::split(addr);
    if (isOverlayHost(host)) throw PeerError("peer " + addr + " not dialed directly");
    PeerPtr c;
    try {
        c = PeerConnection::connect(network_, addr, false, false,
                                    [this](PeerConnection& peer, const Message& message) {
                                        if (std::holds_alternative<msg::Addr>(message) || std::holds_alternative<msg::AddrV2>(message))
                                            handle(peer, message);
                                    })
                .first;
    } catch (...) {
        try {
            store_.recordPeerResult(host, port, false, 0);
        } catch (const std::exception&) {
        }
        throw;
    }
    struct Disconnect {
        PeerPtr& c;
        ~Disconnect() { c->disconnect(); }
    } disconnect{c};
    try {
        store_.updatePeerServices(host, port, c->services().raw);
    } catch (const std::exception& e) {
        Log::info("store peer " + addr + ": " + e.what());
    }
    if (!c->services().contains(ServiceFlags::compactFilters)) throw PeerError("no compact filter service");
    try {
        return requestFilterAnchor(*c);
    } catch (const std::exception& e) {
        Log::info("peer " + addr + ": filter header anchor: " + e.what());
        throw;
    }
}

} // namespace tb
