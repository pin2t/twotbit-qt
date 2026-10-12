#include <algorithm>
#include <atomic>
#include <random>
#include <set>

#include "p2p/hostport.h"
#include "sync/engine.h"

namespace tb {

void SyncEngine::maintain() {
    // Fills the pool at the start, then keeps it filled, dialing as many next
    // candidates at once as peers are missing. While a pinned peer is set one
    // slot is kept for it; keepPinned dials it on its own.
    while (!task::isCancelled()) {
        bool again;
        {
            std::lock_guard lock(managerMutex_);
            again = manager_.refill;
            manager_.refill = false;
        }
        if (again) fillPool();
        Duration wait = retryDelay;
        int need = openSlots();
        if (need > 0) {
            std::vector<std::string> addrs = takeCandidates(size_t(need));
            {
                TaskGroup group;
                for (const auto& addr : addrs) group.add([this, addr] { connect(addr, false); });
                group.waitAll();
            }
            if (!addrs.empty()) wait = 1s;
        }
        poolChanged_.wait(wait);
    }
}

void SyncEngine::fillPool() {
    // Connects the first peers quickly: connectWorkers workers each take the
    // next candidate of a freshly shuffled list, dial it unless it is
    // connected already, and repeat until the pool is full or the list runs
    // out. Handshakes still in flight once the pool is full are abandoned.
    // The candidates not dialed are left for maintain to continue with.
    if (openSlots() <= 0) return;
    std::vector<std::string> list;
    try {
        list = peerCandidates(currentPinned());
    } catch (const std::exception& e) {
        Log::info(std::string("peer discovery: ") + e.what());
        return;
    }
    std::atomic<size_t> next{0};
    {
        TaskGroup group;
        for (int w = 0; w < connectWorkers; ++w) {
            group.add([&] {
                while (!task::isCancelled() && openSlots() > 0) {
                    size_t index = next.fetch_add(1);
                    if (index >= list.size()) return;
                    if (connected(list[index])) continue;
                    if (connect(list[index], false) && openSlots() <= 0) return;
                }
            });
        }
        while (group.next()) {
            if (openSlots() <= 0) group.cancelAll();
        }
    }
    size_t fed = std::min(next.load(), list.size());
    size_t count;
    {
        std::lock_guard lock(managerMutex_);
        manager_.candidates = list;
        manager_.candNext = fed;
        count = manager_.pool.size();
    }
    Log::info("peers: initial connect dialed " + std::to_string(fed) + " of " + std::to_string(list.size()) + " candidates, " +
              std::to_string(count) + " connected");
}

int SyncEngine::openSlots() const {
    std::lock_guard lock(managerMutex_);
    return openSlots(manager_);
}

int SyncEngine::openSlots(const ManagerState& m) {
    int n = targetPeers - int(m.pool.size());
    if (!m.pinnedAddr.empty() && std::none_of(m.pool.begin(), m.pool.end(), [](const PeerPtr& c) { return c->pinned; })) n -= 1;
    return n;
}

void SyncEngine::keepPinned(const std::string& addr) {
    // Keeps the pinned peer connected until the task is cancelled, as the
    // sync stops or another peer is pinned: it dials it, waits for the
    // connection to drop and dials again, backing off while the peer is
    // unreachable or keeps dropping the connection soon after the handshake,
    // as a node with full inbound slots evicting its newest peer does.
    Duration backoff = retryDelay;
    while (!task::isCancelled()) {
        if (connect(addr, true)) {
            TimePoint since = Clock::now();
            if (PeerPtr c = pinnedConn()) c->waitForDisconnect();
            if (task::isCancelled()) return;
            Duration lasted = Clock::now() - since;
            if (lasted >= pinnedStableAfter) {
                backoff = retryDelay;
                continue;
            }
            Log::info("pinned peer " + addr + " dropped the connection after " + formatDuration(lasted));
        }
        if (task::isCancelled()) return;
        Log::info("pinned peer " + addr + ": retrying in " + formatDuration(backoff));
        if (!task::sleep(backoff)) return;
        backoff = std::min<Duration>(backoff * 2, pinnedMaxBackoff);
    }
}

bool SyncEngine::connected(const std::string& addr) const {
    std::lock_guard lock(managerMutex_);
    return std::any_of(manager_.pool.begin(), manager_.pool.end(), [&](const PeerPtr& c) { return c->address == addr; });
}

std::vector<PeerPtr> SyncEngine::poolPeers() const {
    std::lock_guard lock(managerMutex_);
    return manager_.pool;
}

std::vector<PeerPtr> SyncEngine::livePeers(const std::vector<PeerPtr>& peers) {
    std::vector<PeerPtr> out;
    for (const auto& c : peers)
        if (c->isLive()) out.push_back(c);
    return out;
}

PeerPtr SyncEngine::pinnedConn() const {
    for (const auto& c : livePeers(poolPeers()))
        if (c->pinned) return c;
    return nullptr;
}

std::vector<PeerPtr> SyncEngine::orderedPeers() const {
    std::vector<PeerPtr> peers = livePeers(poolPeers());
    std::stable_partition(peers.begin(), peers.end(), [](const PeerPtr& c) { return c->pinned; });
    return peers;
}

PeerPtr SyncEngine::primary() const {
    // The connected peer driving the sync. With a pinned peer set only it
    // qualifies, none meaning waiting for it, until it has been away for
    // pinnedFallbackAfter: then the oldest other peer does.
    if (!currentPinned().empty()) {
        if (PeerPtr c = pinnedConn()) return c;
        if (!pinnedAway()) return nullptr;
    }
    std::vector<PeerPtr> peers = orderedPeers();
    return peers.empty() ? nullptr : peers.front();
}

std::vector<PeerPtr> SyncEngine::filterPeers() const {
    // Up to targetPeers connected peers to download and cross-check filters
    // from, the primary first; none while a pinned peer is set but not
    // connected.
    if (!primary()) return {};
    std::vector<PeerPtr> peers = orderedPeers();
    if (peers.size() > size_t(targetPeers)) peers.resize(size_t(targetPeers));
    return peers;
}

bool SyncEngine::pinnedAway() const {
    std::string addr;
    TimePoint seen;
    {
        std::lock_guard lock(managerMutex_);
        addr = manager_.pinnedAddr;
        seen = manager_.pinnedSeen;
    }
    if (addr.empty() || pinnedConn()) return false;
    return Clock::now() - seen >= pinnedFallbackAfter;
}

std::optional<Duration> SyncEngine::fallbackDelay() const {
    std::string addr;
    TimePoint seen;
    {
        std::lock_guard lock(managerMutex_);
        addr = manager_.pinnedAddr;
        seen = manager_.pinnedSeen;
    }
    Duration left = pinnedFallbackAfter - (Clock::now() - seen);
    if (addr.empty() || left <= Duration::zero()) return std::nullopt;
    return left;
}

void SyncEngine::requirePrimary(const std::vector<PeerPtr>& peers) const {
    // Fails when a pinned peer is set but is not among the peers that
    // answered: headers and filters are synced from it unless it is away.
    std::string addr = currentPinned();
    if (addr.empty() || (!peers.empty() && peers.front()->pinned) || pinnedAway()) return;
    throw PeerError("pinned peer " + addr + " did not answer");
}

std::vector<std::string> SyncEngine::takeCandidates(size_t n) {
    // Up to n candidates not connected, rebuilding the list from the stored
    // peers and the seeds when it runs out.
    std::vector<std::string> taken;
    for (int round = 0; round < 2 && taken.size() < n; ++round) {
        bool exhausted;
        {
            std::lock_guard lock(managerMutex_);
            ManagerState& m = manager_;
            std::set<std::string> connected;
            for (const auto& c : m.pool) connected.insert(c->address);
            while (m.candNext < m.candidates.size() && taken.size() < n) {
                const std::string& addr = m.candidates[m.candNext++];
                if (!connected.count(addr) && std::find(taken.begin(), taken.end(), addr) == taken.end()) taken.push_back(addr);
            }
            exhausted = m.candNext >= m.candidates.size();
        }
        if (!exhausted || taken.size() >= n) break;
        try {
            std::vector<std::string> fresh = peerCandidates(currentPinned());
            std::lock_guard lock(managerMutex_);
            manager_.candidates = std::move(fresh);
            manager_.candNext = 0;
        } catch (const std::exception& e) {
            Log::info(std::string("peer discovery: ") + e.what());
            break;
        }
    }
    return taken;
}

std::vector<std::string> SyncEngine::peerCandidates(const std::string& pinned) {
    // The peers to fill the pool with: stored and freshly discovered peers in
    // random order, without the pinned peer. Stored peers known not to serve
    // compact filters are skipped, and so are onion and I2P peers the wallet
    // does not dial. Peers come from the store and the DNS seeds. An empty
    // list is an error only without a pinned peer.
    std::vector<std::string> list;
    std::set<std::string> seen{pinned};
    auto add = [&](const std::string& addr) {
        if (addr.empty() || !seen.insert(addr).second) return;
        list.push_back(addr);
    };
    for (const auto& p : store_.peers()) {
        if (p.services != 0 && !ServiceFlags{p.services}.contains(ServiceFlags::compactFilters)) continue;
        if (isOverlayHost(p.host)) continue;
        add(HostPort::join(p.host, p.port));
    }
    for (const auto& seed : Dns::seeds(network_)) add(seed.description());
    std::random_device device;
    std::mt19937_64 rng(uint64_t(device()) << 32 | device());
    std::shuffle(list.begin(), list.end(), rng);
    if (list.empty() && pinned.empty())
        throw PeerError("no peer addresses for network " + network_.name + " (pass -peer or run a local node)");
    return list;
}

bool SyncEngine::connect(const std::string& addr, bool pinned) {
    std::string host;
    uint16_t port;
    try {
        std::tie(host, port) = HostPort::split(addr);
    } catch (const HostPortError&) {
        return false;
    }
    if (isOverlayHost(host)) return false;
    PeerPtr c;
    Duration handshake;
    try {
        std::tie(c, handshake) =
            PeerConnection::connect(network_, addr, true, pinned, [this](PeerConnection& peer, const Message& message) { handle(peer, message); });
    } catch (const std::exception& e) {
        if (task::isCancelled()) return false;
        Log::info("peer " + addr + ": connect failed: " + e.what());
        try {
            store_.recordPeerResult(host, port, false, 0);
        } catch (const std::exception&) {
        }
        return false;
    }
    try {
        store_.upsertPeer(host, port, c->services().raw, toMilliseconds(handshake));
    } catch (const std::exception& e) {
        Log::info("store peer " + addr + ": " + e.what());
    }
    if (!c->services().contains(ServiceFlags::compactFilters)) {
        if (pinned)
            Log::info("pinned peer " + addr + ": no compact filter service (" + c->services().description() +
                      "), disconnecting; it needs blockfilterindex=1 and peerblockfilters=1");
        else
            Log::info("peer " + addr + ": no compact filter service (" + c->services().description() + "), disconnecting");
        c->disconnect();
        return false;
    }
    c->send(msg::GetAddr{});
    std::optional<std::string> refusal;
    bool refused = false;
    size_t count = 0;
    {
        std::lock_guard lock(managerMutex_);
        ManagerState& m = manager_;
        if (m.stopped) {
            refused = true;
        } else if (pinned && addr != m.pinnedAddr) {
            refused = true;
            refusal = "no longer the pinned peer, disconnecting";
        } else if (std::any_of(m.pool.begin(), m.pool.end(), [&](const PeerPtr& p) { return p->address == addr; })) {
            refused = true;
            refusal = "connected already, disconnecting the second connection";
        } else if (!pinned && openSlots(m) <= 0) {
            refused = true;
            refusal = "pool already full, disconnecting";
        } else {
            m.pool.push_back(c);
            count = m.pool.size();
        }
    }
    if (refused) {
        if (refusal) Log::info("peer " + addr + ": " + *refusal);
        c->disconnect();
        return false;
    }
    std::string kind = pinned ? "pinned peer" : "peer";
    Log::info(kind + " " + addr + " connected: " + c->remote.userAgent + ", height " + std::to_string(c->remote.startHeight) + ", services " +
              c->services().description() + ", handshake " + formatDuration(handshake) + " (" + std::to_string(count) + " connected)");
    loadBloom(*c);
    rebroadcast(*c);
    logRelayMode();
    emitStatus();
    wake_.signal();
    PeerConnection* raw = c.get();
    c->onDisconnect([this, raw, kind, addr] {
        size_t left;
        {
            std::lock_guard lock(managerMutex_);
            auto& pool = manager_.pool;
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const PeerPtr& p) { return p.get() == raw; }), pool.end());
            if (raw->pinned) manager_.pinnedSeen = Clock::now();
            left = pool.size();
        }
        auto reason = raw->disconnectReason();
        Log::info(kind + " " + addr + " disconnected" + (reason ? ": " + *reason : "") + " (" + std::to_string(left) + " connected)");
        logRelayMode();
        emitStatus();
        poolChanged_.signal();
    });
    return true;
}

void SyncEngine::drop(PeerConnection& c, const std::string& reason, const std::string& error) {
    if (c.pinned) {
        Log::info("pinned peer " + c.address + ": " + reason + ": " + error + "; keeping it");
        return;
    }
    Log::info("peer " + c.address + ": " + reason + ": " + error + "; disconnecting");
    c.disconnect();
}

void SyncEngine::record(const PeerConnection& c, bool ok, TimePoint started) {
    try {
        store_.recordPeerResult(c.host, c.port, ok, toMilliseconds(Clock::now() - started));
    } catch (const std::exception& e) {
        Log::info("record peer " + c.address + ": " + e.what());
    }
}

void SyncEngine::handle(PeerConnection& c, const Message& message) {
    // Handles what a peer sends unasked: relayed transactions are checked
    // against the wallet, transactions announced by the relay source are
    // fetched, a block announcement wakes the sync, and advertised peer
    // addresses are stored.
    if (auto* m = std::get_if<msg::Tx>(&message)) {
        countRelayed(c, m->tx);
        acceptPendingTx(c, m->tx);
    } else if (auto* m = std::get_if<msg::Inv>(&message)) {
        if (relaySource(c, orderedPeers())) requestTxs(c, m->list);
        auto block = std::find_if(m->list.begin(), m->list.end(), [](const InvVect& iv) { return iv.is(InvType::block); });
        if (block != m->list.end()) {
            if (heightOf(block->hash) < 0) {
                Log::info("peer " + c.address + " announced block " + block->hash.hex());
                blockAnnounced();
            }
            wake_.signal();
        }
    } else if (auto* m = std::get_if<msg::Addr>(&message)) {
        for (const auto& a : m->list) savePeerAddr(a.host(), a.port, a.services);
    } else if (auto* m = std::get_if<msg::AddrV2>(&message)) {
        for (const auto& a : m->list)
            if (auto host = a.host()) savePeerAddr(*host, a.port, a.services);
    }
}

void SyncEngine::savePeerAddr(const std::string& host, uint16_t port, ServiceFlags services) {
    if (port == 0) return;
    try {
        if (services.raw != 0)
            store_.updatePeerServices(host, port, services.raw);
        else
            store_.savePeer(host, port);
    } catch (const std::exception&) {
    }
}

} // namespace tb
