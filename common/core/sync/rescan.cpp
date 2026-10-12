#include <algorithm>
#include <map>

#include "sync/engine.h"

namespace tb {

void SyncEngine::rescan() {
    // Matches the synced blocks again for the queued addresses: those watched
    // after blocks paying them were scanned, such as a change address another
    // wallet on the same seed used first. Their coins were found late, from
    // the stored transactions, so spends of them in the blocks scanned before
    // were missed. Each address is rescanned from the first stored block
    // paying it up to the last synced filter: the pruned filters are
    // downloaded again and checked against the stored filter header chain,
    // and every block matching an address it was not matched for before is
    // processed.
    std::vector<RescanJob> jobs;
    try {
        jobs = store_.rescans();
    } catch (const std::exception& e) {
        throw PeerError(std::string("load rescans: ") + e.what());
    }
    if (jobs.empty()) return;
    int32_t resume;
    try {
        resume = store_.filterResumeHeight(filterStart());
    } catch (const std::exception& e) {
        throw PeerError(std::string("filter resume height: ") + e.what());
    }
    int32_t end = resume - 1;
    auto [targets, idle] = rescanTargets(jobs, end);
    for (const auto& job : idle) {
        try {
            store_.deleteRescan(job);
        } catch (const std::exception& e) {
            throw PeerError("drop rescan of " + job.address + ": " + e.what());
        }
    }
    if (targets.empty()) return;
    int32_t start = targets.front().from;
    std::set<MatchKey> matched;
    try {
        for (const auto& m : store_.matches()) matched.insert(MatchKey{m.height, m.address});
    } catch (const std::exception& e) {
        throw PeerError(std::string("load matches: ") + e.what());
    }
    Log::info("sync: rescanning blocks " + std::to_string(start) + "-" + std::to_string(end) + " for " + std::to_string(targets.size()) +
              " addresses watched after they were paid");
    for (int32_t fStart = start; fStart <= end; fStart += filterBatch) {
        int32_t fEnd = std::min(fStart + filterBatch - 1, end);
        report(SyncStatus::Activity::filters, fStart);
        std::vector<Bytes> filters;
        try {
            filters = refetchFilters(fStart, fEnd);
        } catch (const CancellationError&) {
            throw;
        } catch (const std::exception& e) {
            throw PeerError("filters " + std::to_string(fStart) + "-" + std::to_string(fEnd) + ": " + e.what());
        }
        for (size_t i = 0; i < filters.size(); ++i)
            rescanBlock(fStart + int32_t(i), filters[i], targets, matched, [this](int32_t height) { return fetchBlock(height); });
    }
    for (const auto& t : targets) {
        try {
            store_.deleteRescan(t.job);
        } catch (const std::exception& e) {
            throw PeerError("finish rescan of " + t.job.address + ": " + e.what());
        }
    }
    Log::info("sync: rescan of blocks " + std::to_string(start) + "-" + std::to_string(end) + " done");
}

std::pair<std::vector<SyncEngine::RescanTarget>, std::vector<RescanJob>> SyncEngine::rescanTargets(const std::vector<RescanJob>& jobs,
                                                                                                    int32_t end) {
    // Splits the queued rescans into the targets to scan up to the end
    // height, the lowest start first, and those with nothing to scan. A
    // target starts at the first stored block paying its address, and not
    // below its queued height or the filter start.
    int32_t start = filterStart();
    std::lock_guard lock(walletMutex_);
    std::map<std::string, WatchScript> byAddress;
    for (const auto& s : wallet_.scripts) byAddress[s.address] = s;
    std::vector<RescanTarget> targets;
    std::vector<RescanJob> idle;
    for (const auto& job : jobs) {
        auto script = byAddress.find(job.address);
        auto first = wallet_.firstPaid.find(job.address);
        if (script == byAddress.end() || first == wallet_.firstPaid.end()) {
            idle.push_back(job);
            continue;
        }
        int32_t from = std::max({job.from, first->second, start});
        if (from > end) {
            idle.push_back(job);
            continue;
        }
        targets.push_back(RescanTarget{job, from, script->second});
    }
    std::stable_sort(targets.begin(), targets.end(), [](const RescanTarget& a, const RescanTarget& b) { return a.from < b.from; });
    return {targets, idle};
}

void SyncEngine::rescanBlock(int32_t height, const Bytes& data, const std::vector<RescanTarget>& targets, std::set<MatchKey>& matched,
                             const std::function<Block(int32_t)>& getBlock) {
    // Matches the filter of the block at the height against the targets
    // started at or below it. A block matched for an address before was
    // processed while the address was watched and is skipped for it; when
    // any other target matches, the block is downloaded with getBlock and its
    // wallet transactions are stored.
    auto e = entry(height);
    if (!e) throw PeerError("no header at height " + std::to_string(height));
    std::vector<WatchScript> active;
    for (const auto& t : targets)
        if (t.from <= height) active.push_back(t.script);
    if (active.empty()) return;
    std::vector<Bytes> scripts;
    for (const auto& s : active) scripts.push_back(s.script);
    std::vector<bool> hits;
    try {
        hits = GCSFilter::matchScripts(data, e->hash, scripts);
    } catch (const std::exception& err) {
        throw PeerError("match filter at height " + std::to_string(height) + ": " + err.what());
    }
    std::vector<WatchScript> fresh;
    for (size_t i = 0; i < active.size(); ++i)
        if (hits[i] && !matched.count(MatchKey{height, active[i].address})) fresh.push_back(active[i]);
    if (fresh.empty()) return;
    for (const auto& s : fresh)
        Log::info("match: rescanned filter of block " + std::to_string(height) + " (" + e->hash.hex() + ") matches address " + s.address);
    Block block;
    try {
        block = getBlock(height);
    } catch (const CancellationError&) {
        throw;
    } catch (const std::exception& err) {
        throw PeerError("download matched block " + std::to_string(height) + ": " + err.what());
    }
    if (processBlock(height, block) == 0)
        Log::info("match: block " + std::to_string(height) + " holds no wallet transaction (filter false positive)");
    for (const auto& s : fresh) {
        try {
            store_.saveMatch(FilterMatch{height, e->hash, s.address, s.script});
        } catch (const std::exception& err) {
            throw PeerError("store match at height " + std::to_string(height) + ": " + err.what());
        }
        matched.insert(MatchKey{height, s.address});
    }
}

std::vector<Bytes> SyncEngine::refetchFilters(int32_t start, int32_t end) {
    // Downloads the filters of the synced blocks start...end again from the
    // peers in turn, the primary first, and returns the first set whose
    // chained headers reproduce the stored filter header chain. Below the
    // first stored header the peer's cfheaders supply the previous header; a
    // wrong one cannot link to the stored chain either. A peer serving
    // filters that do not link is disconnected.
    auto stopEntry = entry(end);
    if (!stopEntry) throw PeerError("no header at height " + std::to_string(end));
    Hash256 stop = stopEntry->hash;
    Hash256 prev;
    bool known = start == 0;
    if (!known) {
        if (auto stored = store_.filterHeader(start - 1)) {
            prev = *stored;
            known = true;
        }
    }
    std::vector<PeerPtr> peers = orderedPeers();
    if (peers.empty()) throw PeerError("no connected peers");
    std::string lastError = "no peer answered";
    for (const auto& c : peers) {
        Hash256 from = prev;
        try {
            if (!known) from = getCFHeaders(*c, start, stop, requestTimeout).prevFilterHeader;
            std::vector<Bytes> filters = getCFilters(*c, start, stop);
            try {
                checkStoredChain(start, from, filters);
            } catch (const std::exception& e) {
                drop(*c, "rescan filters", e.what());
                throw;
            }
            return filters;
        } catch (const std::exception& e) {
            lastError = e.what();
            if (task::isCancelled()) throw CancellationError();
        }
    }
    throw PeerError("no peer served filters linking to the stored headers: " + lastError);
}

void SyncEngine::checkStoredChain(int32_t start, const Hash256& from, const std::vector<Bytes>& filters) {
    // Verifies that the filters from the height on, chained from prev,
    // reproduce the stored filter headers.
    Hash256 prev = from;
    for (size_t i = 0; i < filters.size(); ++i) {
        int32_t height = start + int32_t(i);
        auto stored = store_.filterHeader(height);
        if (!stored) throw PeerError("filter header at height " + std::to_string(height) + " not stored");
        prev = GCSFilter::header(GCSFilter::hash(filters[i]), prev);
        if (prev != *stored)
            throw PeerError("filter at height " + std::to_string(height) + " chains to header " + prev.hex() + ", stored " + stored->hex());
    }
}

} // namespace tb
