#include <set>

#include "bitcoin/script.h"
#include "sync/engine.h"
#include "util/strings.h"

namespace tb {

int32_t SyncEngine::syncFilters() {
    // Downloads and verifies the BIP158 filters from the wallet's start
    // height, matching each against the wallet scripts. Filter hashes and
    // filters come from up to targetPeers peers at once and are compared
    // block by block, following the BIP157 client guidance: a filter every
    // peer agrees on is accepted, a disagreement is settled against the full
    // block. Every stored filter is pruned right after matching; only its
    // chained header is kept. It returns the number of new filters.
    refreshFilterStart();
    int32_t first = filterStart();
    store_.pruneFilterData(first);
    int32_t start = store_.filterResumeHeight(first);
    int32_t tip = chainHeight();
    int32_t stored = 0;
    if (start <= tip) report(SyncStatus::Activity::filters, start);
    while (start <= tip) {
        int32_t end = std::min(start + filterHeaderBatch - 1, tip);
        std::vector<FilterSource> sources;
        try {
            sources = filterHashes(start, end);
        } catch (const CancellationError&) {
            throw;
        } catch (const std::exception& e) {
            throw PeerError("filter headers " + std::to_string(start) + "-" + std::to_string(end) + ": " + e.what());
        }
        for (int32_t fStart = start; fStart <= end; fStart += filterBatch) {
            int32_t fEnd = std::min(fStart + filterBatch - 1, end);
            try {
                stored += syncFilterBatch(sources, start, fStart, fEnd);
            } catch (const CancellationError&) {
                throw;
            } catch (const std::exception& e) {
                throw PeerError("filters " + std::to_string(fStart) + "-" + std::to_string(fEnd) + ": " + e.what());
            }
            std::erase_if(sources, [](const FilterSource& s) { return !s.peer->isLive(); });
            report(SyncStatus::Activity::filters, fEnd);
        }
        start = end + 1;
    }
    return stored;
}

std::vector<SyncEngine::FilterSource> SyncEngine::filterHashes(int32_t start, int32_t end) {
    // Fetches the cfheaders batch start...end from the filter peers and keeps
    // those whose batch links to the stored filter header chain.
    std::vector<PeerPtr> peers = filterPeers();
    if (peers.empty()) throw PeerError("no connected peers");
    auto stopEntry = entry(end);
    if (!stopEntry) throw PeerError("no header at height " + std::to_string(end));
    Hash256 stop = stopEntry->hash;
    auto [answered, responses] = fanOut<CFHeaders>(
        peers, "cfheaders", [this, start, stop](const PeerPtr& c) { return getCFHeaders(*c, start, stop, requestTimeout); });
    if (answered.empty()) throw PeerError("every peer failed the cfheaders request");
    int32_t filterStart;
    bool anchorSet;
    {
        std::lock_guard lock(chainMutex_);
        filterStart = chain_.filterStart;
        anchorSet = chain_.anchorSet;
    }
    if (start == filterStart && filterStart > 0 && !anchorSet) {
        std::vector<Hash256> votes;
        for (const auto& r : responses) votes.push_back(r.prevFilterHeader);
        Hash256 anchor;
        try {
            anchor = majorityAnchor(votes);
        } catch (const PeerError& e) {
            anchor = votes.front();
            Log::info(std::string("sync: filter peers disagree on the filter header anchor (") + e.what() + "); trusting " + answered.front()->address);
        }
        setFilterAnchor(anchor);
    }
    std::vector<FilterSource> sources;
    for (size_t i = 0; i < answered.size(); ++i) {
        try {
            checkFilterPrev(start, responses[i].prevFilterHeader);
        } catch (const std::exception& e) {
            drop(*answered[i], "cfheaders", e.what());
            continue;
        }
        sources.push_back(FilterSource{answered[i], responses[i].filterHashes});
    }
    if (sources.empty()) throw PeerError("no peer served a filter header chain linking to height " + std::to_string(start - 1));
    std::vector<PeerPtr> sourcePeers;
    for (const auto& s : sources) sourcePeers.push_back(s.peer);
    requirePrimary(sourcePeers);
    return sources;
}

int32_t SyncEngine::syncFilterBatch(const std::vector<FilterSource>& sources, int32_t base, int32_t fStart, int32_t fEnd) {
    // Downloads the filters fStart...fEnd from every source, checks each
    // against the filter hashes its peer committed to, then accepts the
    // filters block by block. It returns the number of filters stored.
    auto stopEntry = entry(fEnd);
    if (!stopEntry) throw PeerError("no header at height " + std::to_string(fEnd));
    Hash256 stop = stopEntry->hash;
    std::unordered_map<const PeerConnection*, std::vector<Hash256>> committed;
    std::vector<PeerPtr> live;
    for (const auto& s : sources) {
        committed[s.peer.get()] = s.hashes;
        if (s.peer->isLive()) live.push_back(s.peer);
    }
    auto [peers, got] = fanOut<std::vector<Bytes>>(live, "cfilters", [&, fStart, stop, base](const PeerPtr& c) {
        std::vector<Bytes> data = getCFilters(*c, fStart, stop);
        const std::vector<Hash256>& hashes = committed.at(c.get());
        for (size_t i = 0; i < data.size(); ++i) {
            size_t index = size_t(fStart - base) + i;
            Hash256 raw = GCSFilter::hash(data[i]);
            if (index >= hashes.size() || raw != hashes[index])
                throw PeerError("filter at height " + std::to_string(fStart + int32_t(i)) + " hashes to " + raw.hex() + ", peer committed to " +
                                (index < hashes.size() ? hashes[index].hex() : std::string("none")));
        }
        return data;
    });
    if (peers.empty()) throw PeerError("every peer failed the cfilters request");
    requirePrimary(peers);
    if (peers.size() == 1 && fStart == base)
        Log::info("sync: filters " + std::to_string(fStart) + "-" + std::to_string(fEnd) + " from " + peers.front()->address +
                  " alone, no second peer to cross-check");
    int mismatches = 0;
    int32_t count = fEnd - fStart + 1;
    const auto& peerList = peers;
    const auto& gotList = got;
    storeFilters(fStart, count, [&](int32_t i) {
        std::vector<Variant> variants = groupVariants(peerList, gotList, size_t(i));
        if (variants.size() > 1) ++mismatches;
        return chooseFilter(fStart + i, variants);
    });
    if (peers.size() > 1)
        Log::info("sync: filters " + std::to_string(fStart) + "-" + std::to_string(fEnd) + " cross-checked with " + std::to_string(peers.size()) +
                  " peers, " + std::to_string(mismatches) + " mismatches");
    return count;
}

std::vector<SyncEngine::Variant> SyncEngine::groupVariants(const std::vector<PeerPtr>& peers, const std::vector<std::vector<Bytes>>& got,
                                                            size_t index) {
    // Groups the filters the peers served for one block by their hash, in the
    // order the peers are listed.
    std::vector<Variant> variants;
    for (size_t i = 0; i < peers.size(); ++i) {
        const Bytes& data = got[i][index];
        Hash256 hash = GCSFilter::hash(data);
        auto v = std::find_if(variants.begin(), variants.end(), [&](const Variant& x) { return x.hash == hash; });
        if (v != variants.end())
            v->peers.push_back(peers[i]);
        else
            variants.push_back(Variant{data, hash, {peers[i]}});
    }
    return variants;
}

std::pair<Bytes, std::optional<Block>> SyncEngine::chooseFilter(int32_t height, const std::vector<Variant>& variants) {
    // Picks the filter of the block at the height. When peers disagree, the
    // full block is downloaded and verified, every variant is checked to
    // contain all of the block's output scripts, and the peers that served
    // an invalid or outvoted filter are disconnected. The verified block is
    // returned too when it had to be downloaded.
    if (variants.size() <= 1) return {variants.front().data, std::nullopt};
    auto e = entry(height);
    Hash256 hash = e ? e->hash : Hash256();
    Log::info("sync: filter mismatch at height " + std::to_string(height) + " (block " + hash.hex() + "): " + describe(variants));
    Block block;
    try {
        block = fetchBlock(height);
    } catch (const CancellationError&) {
        throw;
    } catch (const std::exception& e) {
        throw PeerError("resolve filter mismatch at height " + std::to_string(height) + ": " + e.what());
    }
    size_t winner;
    std::vector<bool> valid;
    try {
        std::tie(winner, valid) = resolveVariants(variants, block);
    } catch (const std::exception& e) {
        for (const auto& v : variants)
            for (const auto& c : v.peers) drop(*c, "filter at height " + std::to_string(height), e.what());
        throw PeerError("height " + std::to_string(height) + ": " + e.what());
    }
    const Variant& chosen = variants[winner];
    std::vector<std::string> addresses;
    for (const auto& c : chosen.peers) addresses.push_back(c->address);
    Log::info("sync: filter mismatch at height " + std::to_string(height) + " resolved against the full block: using " + chosen.hash.hex() +
              " from " + join(addresses, " "));
    for (size_t i = 0; i < variants.size(); ++i) {
        if (i == winner) continue;
        std::string reason = valid[i] ? "filter " + variants[i].hash.hex() + " outvoted by " + chosen.hash.hex()
                                      : "filter " + variants[i].hash.hex() + " is missing output scripts of block " + hash.hex();
        for (const auto& c : variants[i].peers) drop(*c, "filter at height " + std::to_string(height), reason);
    }
    return {chosen.data, std::move(block)};
}

std::pair<size_t, std::vector<bool>> SyncEngine::resolveVariants(const std::vector<Variant>& variants, const Block& block) {
    // Checks every filter variant against the verified block. A valid filter
    // contains every output script of the block; the winner is the valid
    // variant served by the most peers, the earliest listed on a tie.
    Hash256 blockHash = block.hash();
    int winner = -1;
    std::vector<bool> valid(variants.size(), false);
    for (size_t i = 0; i < variants.size(); ++i) {
        const Variant& v = variants[i];
        try {
            valid[i] = filterHasOutputs(v.data, blockHash, block);
        } catch (const std::exception& e) {
            std::vector<std::string> addresses;
            for (const auto& c : v.peers) addresses.push_back(c->address);
            Log::info("sync: filter " + v.hash.hex() + " from " + join(addresses, " ") + " cannot be decoded: " + e.what());
            continue;
        }
        if (valid[i] && (winner < 0 || v.peers.size() > variants[size_t(winner)].peers.size())) winner = int(i);
    }
    if (winner < 0) throw PeerError("no peer served a filter containing all output scripts of block " + blockHash.hex());
    return {size_t(winner), valid};
}

bool SyncEngine::filterHasOutputs(const Bytes& data, const Hash256& blockHash, const Block& block) {
    // Whether the filter contains every output script BIP158 requires: all
    // of them but empty and OP_RETURN scripts. The scripts the block's inputs
    // spend cannot be checked without their previous transactions.
    std::set<Bytes> seen;
    std::vector<Bytes> outputs;
    for (const auto& tx : block.transactions)
        for (const auto& out : tx.outputs)
            if (!out.pkScript.empty() && out.pkScript.front() != Script::opReturn && seen.insert(out.pkScript).second)
                outputs.push_back(out.pkScript);
    std::vector<bool> hits = GCSFilter::matchScripts(data, blockHash, outputs);
    return std::all_of(hits.begin(), hits.end(), [](bool b) { return b; });
}

int32_t SyncEngine::storeFilters(int32_t fStart, int32_t count, const std::function<std::pair<Bytes, std::optional<Block>>(int32_t)>& pick) {
    // Accepts count consecutive filters from fStart, as picked one by one,
    // and stores their rows in one transaction, chained in memory from the
    // stored predecessor of fStart. Rows accepted before an error are still
    // stored. It returns the number stored.
    Hash256 prev = prevFilterHeader(fStart);
    std::vector<StoredFilter> rows;
    std::exception_ptr failure;
    for (int32_t i = 0; i < count; ++i) {
        try {
            auto [data, block] = pick(i);
            StoredFilter row = applyFilter(fStart + i, data, std::move(block), prev);
            prev = row.filterHeader;
            rows.push_back(std::move(row));
        } catch (...) {
            failure = std::current_exception();
            break;
        }
    }
    if (!rows.empty()) {
        try {
            store_.saveFilters(rows);
        } catch (const std::exception& e) {
            throw PeerError("store filters " + std::to_string(fStart) + "-" + std::to_string(fStart + int32_t(rows.size()) - 1) + ": " + e.what());
        }
    }
    if (failure) std::rethrow_exception(failure);
    return int32_t(rows.size());
}

void SyncEngine::checkFilterPrev(int32_t start, const Hash256& got) {
    // Verifies the prev_filter_header of a cfheaders batch against the stored
    // chain of filter headers. Bitcoin Core and btcd send the null hash for
    // the first batch at height 0; the genesis hash is accepted there too.
    // The first batch at the wallet's start height has no stored
    // predecessor: it must match the majority proven anchor when one is set,
    // and is otherwise trusted as the anchor.
    int32_t filterStart;
    bool anchorSet;
    Hash256 anchorPrev;
    {
        std::lock_guard lock(chainMutex_);
        filterStart = chain_.filterStart;
        anchorSet = chain_.anchorSet;
        anchorPrev = chain_.anchorPrev;
    }
    if (start == filterStart && filterStart > 0) {
        if (anchorSet) {
            if (got != anchorPrev) throw PeerError("filter header anchor " + got.hex() + ", want majority-proven " + anchorPrev.hex());
            return;
        }
        std::lock_guard lock(chainMutex_);
        chain_.anchorPrev = got;
        return;
    }
    Hash256 want;
    if (start > 0) {
        auto stored = store_.filterHeader(start - 1);
        if (!stored) throw PeerError("filter header at height " + std::to_string(start - 1) + " not stored");
        want = *stored;
    }
    if (got == want || (start == 0 && got == network_.genesisHash)) return;
    throw PeerError("filter header chain break at " + std::to_string(start) + ": prev " + got.hex() + ", want " + want.hex());
}

Hash256 SyncEngine::prevFilterHeader(int32_t height) {
    // The filter header the filter at the height links to: the anchor at the
    // wallet's start height, the null hash at genesis and the stored header
    // of the previous block otherwise.
    if (height == 0) return Hash256();
    {
        std::lock_guard lock(chainMutex_);
        if (height == chain_.filterStart) return chain_.anchorPrev;
    }
    auto stored = store_.filterHeader(height - 1);
    if (!stored) throw PeerError("filter header at height " + std::to_string(height - 1) + " not stored");
    return *stored;
}

StoredFilter SyncEngine::applyFilter(int32_t height, const Bytes& data, std::optional<Block> block, const Hash256& prev) {
    // Matches the accepted filter of the block at the height against the
    // wallet scripts and returns its row, chained to prev. On a match the
    // block is downloaded, unless it is at hand already, and the wallet
    // transactions in it are stored. The row carries no filter data: filters
    // are pruned right after matching.
    auto e = entry(height);
    if (!e) throw PeerError("no header at height " + std::to_string(height));
    StoredFilter row{height, e->hash, GCSFilter::header(GCSFilter::hash(data), prev), std::nullopt};
    std::vector<WatchScript> watched = watchedScripts();
    std::vector<Bytes> scripts;
    scripts.reserve(watched.size());
    for (const auto& w : watched) scripts.push_back(w.script);
    std::vector<bool> hits;
    try {
        hits = GCSFilter::matchScripts(data, e->hash, scripts);
    } catch (const std::exception& err) {
        throw PeerError("match filter at height " + std::to_string(height) + ": " + err.what());
    }
    std::vector<WatchScript> matched;
    for (size_t i = 0; i < watched.size(); ++i)
        if (hits[i]) matched.push_back(watched[i]);
    for (const auto& w : matched)
        Log::info("match: filter of block " + std::to_string(height) + " (" + e->hash.hex() + ") matches address " + w.address);
    if (!matched.empty() && !block) {
        try {
            block = fetchBlock(height);
        } catch (const CancellationError&) {
            throw;
        } catch (const std::exception& err) {
            throw PeerError("download matched block " + std::to_string(height) + ": " + err.what());
        }
    }
    if (block) {
        int found = processBlock(height, *block);
        if (!matched.empty() && found == 0)
            Log::info("match: block " + std::to_string(height) + " holds no wallet transaction (filter false positive)");
    }
    for (const auto& w : matched) {
        try {
            store_.saveMatch(FilterMatch{height, e->hash, w.address, w.script});
        } catch (const std::exception& err) {
            throw PeerError("store match at height " + std::to_string(height) + ": " + err.what());
        }
    }
    return row;
}

Block SyncEngine::fetchBlock(int32_t height) {
    // Downloads the block at the height from the connected peers in turn, the
    // primary first, and returns the first copy that verifies against the
    // header chain. A peer serving a block that fails verification is
    // disconnected.
    auto e = entry(height);
    if (!e) throw PeerError("no header at height " + std::to_string(height));
    std::vector<PeerPtr> peers = orderedPeers();
    if (peers.empty()) throw PeerError("no connected peers");
    std::string lastError = "no peer answered";
    for (const auto& c : peers) {
        Block block;
        try {
            block = getBlock(*c, e->hash);
        } catch (const std::exception& err) {
            Log::info("peer " + c->address + ": block " + std::to_string(height) + ": " + err.what());
            lastError = err.what();
            if (task::isCancelled()) throw CancellationError();
            continue;
        }
        try {
            block.verify(e->hash);
        } catch (const std::exception& err) {
            drop(*c, "block " + std::to_string(height), err.what());
            lastError = err.what();
            continue;
        }
        Log::info("sync: downloaded block " + std::to_string(height) + " (" + e->hash.hex() + ", " + std::to_string(block.transactions.size()) +
                  " transactions) from " + c->address);
        return block;
    }
    throw PeerError("no peer served block " + e->hash.hex() + ": " + lastError);
}

std::string SyncEngine::describe(const std::vector<Variant>& variants) {
    std::vector<std::string> parts;
    for (const auto& v : variants) {
        std::vector<std::string> addresses;
        for (const auto& c : v.peers) addresses.push_back(c->address);
        parts.push_back(v.hash.hex() + " from " + join(addresses, " "));
    }
    return join(parts, ", ");
}

} // namespace tb
