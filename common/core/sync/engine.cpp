#include "sync/engine.h"

#include <algorithm>

#include "bitcoin/script.h"
#include "p2p/hostport.h"

namespace tb {

int SyncStatus::bars() const { return std::min(peers, SyncEngine::targetPeers); }

std::string SyncStatus::text() const {
    if (peers == 0) return "Not connected";
    if (!synced) return "Syncing (" + std::to_string(height) + ")...";
    return "Connected";
}

SyncEngine::SyncEngine(const Network& network, Store& store)
    : network_(network), store_(store), chain_{HeaderChain(network), 0, 0, Hash256(), false} {
    HeaderChain headers(network);
    size_t count = store.headerCount();
    if (count == 0) {
        headers.add(network.genesis);
        store.saveHeaders({StoredHeader{0, network.genesis}});
    } else {
        headers.reserve(count);
        int32_t next = 0;
        store.forEachHeader([&](const StoredHeader& stored) {
            if (stored.height != next) throw ChainError("stored chain has no header at height " + std::to_string(next));
            headers.appendTrusted(stored.header);
            ++next;
        });
    }
    int32_t tip = headers.height();
    {
        std::lock_guard lock(chainMutex_);
        chain_.headers = std::move(headers);
    }
    std::vector<StoredAddress> addresses = store.addresses();
    for (auto& a : store.changeAddresses()) addresses.push_back(std::move(a));
    {
        std::lock_guard lock(walletMutex_);
        wallet_.initialized = true;
        for (const auto& a : addresses) {
            Bytes script = Script::p2wpkhOfPubkey(a.pubkey);
            wallet_.scriptIndex[script] = wallet_.scripts.size();
            wallet_.scripts.push_back(WatchScript{a.address, a.path, script});
        }
    }
    loadWallet();
    auto meta = store.meta();
    int64_t seed = meta ? meta->createdAt : 0;
    {
        std::lock_guard lock(chainMutex_);
        chain_.seedTime = seed;
        chain_.filterStart = filterStartHeight(chain_.headers, seed);
    }
    size_t scripts, coins;
    {
        std::lock_guard lock(walletMutex_);
        scripts = wallet_.scripts.size();
        coins = wallet_.outpoints.size();
    }
    Log::info("sync: loaded " + std::to_string(tip + 1) + " headers, tip " + std::to_string(tip) + ", watching " + std::to_string(scripts) +
              " addresses and " + std::to_string(coins) + " coins");
}

SyncEngine::~SyncEngine() {
    stop();
    std::vector<std::shared_ptr<Task>> tasks;
    {
        std::lock_guard lock(managerMutex_);
        tasks.swap(manager_.tasks);
    }
    for (auto& t : tasks) t->join();
}

int32_t SyncEngine::chainHeight() const {
    std::lock_guard lock(chainMutex_);
    return chain_.headers.height();
}

std::optional<ChainEntry> SyncEngine::entry(int32_t height) const {
    std::lock_guard lock(chainMutex_);
    const ChainEntry* e = chain_.headers.entry(height);
    if (!e) return std::nullopt;
    return *e;
}

int32_t SyncEngine::heightOf(const Hash256& hash) const {
    std::lock_guard lock(chainMutex_);
    return chain_.headers.heightOf(hash);
}

void SyncEngine::start(const std::string& pinned, StatusCallback onStatus) {
    if (!pinned.empty()) {
        try {
            HostPort::split(pinned);
        } catch (const HostPortError&) {
            throw PeerError("peer address \"" + pinned + "\" must include a port");
        }
    }
    int32_t tip = chainHeight();
    {
        std::lock_guard lock(managerMutex_);
        ManagerState& m = manager_;
        m.pool.clear();
        m.activity = SyncStatus::Activity::idle;
        m.height = tip;
        m.onStatus = std::move(onStatus);
        m.stopped = false;
        m.pinnedAddr = pinned;
        m.pinnedSeen = Clock::now();
        m.refill = true;
        m.roundOK = false;
        m.announced = 0;
        m.syncedThrough = 0;
        m.candidates.clear();
        m.candNext = 0;
        m.anchorTried = false;
    }
    emitStatus();
    spawn([this] { maintain(); });
    spawn([this] { syncLoop(); });
    if (!pinned.empty()) startPinned(pinned);
}

void SyncEngine::spawn(std::function<void()> work) {
    std::lock_guard lock(managerMutex_);
    auto& tasks = manager_.tasks;
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [](const std::shared_ptr<Task>& t) { return t->done(); }), tasks.end());
    // Engine work is not a child of the task that started it: it runs until
    // the engine stops.
    auto task = std::make_shared<Task>(std::move(work), std::stop_token());
    if (manager_.stopped) task->cancel();
    tasks.push_back(task);
}

void SyncEngine::startPinned(const std::string& addr) {
    auto task = std::make_shared<Task>([this, addr] { keepPinned(addr); }, std::stop_token());
    std::lock_guard lock(managerMutex_);
    if (manager_.pinnedTask) manager_.pinnedTask->cancel();
    manager_.pinnedTask = task;
    if (manager_.stopped) task->cancel();
    manager_.tasks.push_back(task);
}

void SyncEngine::stop() {
    std::vector<std::shared_ptr<Task>> tasks;
    std::vector<PeerPtr> open;
    {
        std::lock_guard lock(managerMutex_);
        if (manager_.stopped) return;
        manager_.stopped = true;
        if (manager_.pinnedTask) manager_.pinnedTask->cancel();
        tasks = manager_.tasks;
        open = manager_.pool;
    }
    for (auto& t : tasks) t->cancel();
    for (auto& c : open) c->disconnect();
    for (auto& t : tasks) t->join();
}

void SyncEngine::setPinned(const std::string& addr) {
    if (!addr.empty()) {
        try {
            HostPort::split(addr);
        } catch (const HostPortError&) {
            throw PeerError("peer address \"" + addr + "\" must include a port");
        }
    }
    std::string old;
    std::vector<PeerPtr> closing;
    {
        std::lock_guard lock(managerMutex_);
        ManagerState& m = manager_;
        if (m.stopped || addr == m.pinnedAddr) return;
        old = m.pinnedAddr;
        if (m.pinnedTask) m.pinnedTask->cancel();
        m.pinnedTask = nullptr;
        m.pinnedAddr = addr;
        m.pinnedSeen = Clock::now();
        for (const auto& c : m.pool)
            if (c->pinned || c->address == addr) closing.push_back(c);
    }
    if (addr.empty())
        Log::info("sync: pinned peer " + old + " removed");
    else if (old.empty())
        Log::info("sync: pinned peer set to " + addr);
    else
        Log::info("sync: pinned peer changed from " + old + " to " + addr);
    for (auto& c : closing) c->disconnect();
    if (!addr.empty()) startPinned(addr);
    poolChanged_.signal();
    wake_.signal();
    emitStatus();
}

std::string SyncEngine::currentPinned() const {
    std::lock_guard lock(managerMutex_);
    return manager_.pinnedAddr;
}

SyncStatus SyncEngine::status() const {
    auto [confirmed, pending] = walletBalance();
    std::lock_guard lock(managerMutex_);
    SyncStatus s;
    s.activity = manager_.activity;
    s.peers = int(manager_.pool.size());
    s.height = manager_.height;
    s.synced = synced(manager_);
    s.balance = confirmed;
    s.pending = pending;
    return s;
}

void SyncEngine::emitStatus() {
    std::lock_guard lock(emitMutex_);
    SyncStatus s = status();
    StatusCallback callback;
    {
        std::lock_guard m(managerMutex_);
        if (!manager_.stopped) callback = manager_.onStatus;
    }
    if (callback) callback(s);
}

bool SyncEngine::synced(const ManagerState& m) {
    if (m.activity != SyncStatus::Activity::idle || !m.roundOK || m.announced != m.syncedThrough) return false;
    return std::none_of(m.pool.begin(), m.pool.end(), [&](const PeerPtr& c) { return c->remote.startHeight > m.height; });
}

void SyncEngine::blockAnnounced() {
    {
        std::lock_guard lock(managerMutex_);
        manager_.announced += 1;
    }
    emitStatus();
}

void SyncEngine::report(SyncStatus::Activity activity, int32_t height) {
    {
        std::lock_guard lock(managerMutex_);
        manager_.activity = activity;
        manager_.height = height;
    }
    emitStatus();
}

} // namespace tb
