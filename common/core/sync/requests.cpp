#include "sync/engine.h"

namespace tb {

std::vector<BlockHeader> SyncEngine::getHeaders(PeerConnection& c, const std::vector<Hash256>& locator) {
    // Requests the headers following the block locator.
    c.headers.drain();
    TimePoint started = Clock::now();
    c.send(msg::GetHeaders{locator, Hash256()});
    try {
        std::vector<BlockHeader> headers = c.headers.receive(requestTimeout, "headers");
        record(c, true, started);
        return headers;
    } catch (...) {
        record(c, false, started);
        throw;
    }
}

CFHeaders SyncEngine::getCFHeaders(PeerConnection& c, int32_t start, const Hash256& stop, Duration timeout) {
    // Sends one getcfheaders request and checks the response's filter type,
    // stop hash and number of filter hashes.
    c.cfHeaders.drain();
    TimePoint started = Clock::now();
    c.send(msg::GetCFHeaders{uint32_t(start), stop});
    CFHeaders response;
    try {
        response = c.cfHeaders.receive(timeout, "cfheaders");
        record(c, true, started);
    } catch (...) {
        record(c, false, started);
        throw;
    }
    if (response.filterType != Messages::basicFilter) throw PeerError("unexpected filter type " + std::to_string(response.filterType));
    if (response.stopHash != stop) throw PeerError("stop hash " + response.stopHash.hex() + ", want " + stop.hex());
    int64_t want = int64_t(heightOf(stop)) - start + 1;
    if (int64_t(response.filterHashes.size()) != want)
        throw PeerError("got " + std::to_string(response.filterHashes.size()) + " filter headers for " + std::to_string(want) + " blocks");
    return response;
}

std::vector<Bytes> SyncEngine::getCFilters(PeerConnection& c, int32_t start, const Hash256& stop) {
    // Downloads the filters of the blocks start...stop in height order. Every
    // filter must belong to a block of the range and arrive once.
    c.cfilters.drain();
    int32_t end = heightOf(stop);
    if (end < start) throw PeerError("stop block " + stop.hex() + " not in the chain");
    TimePoint started = Clock::now();
    c.send(msg::GetCFilters{uint32_t(start), stop});
    std::vector<std::optional<Bytes>> out(size_t(end - start + 1));
    try {
        for (size_t i = 0; i < out.size(); ++i) {
            CFilter response = c.cfilters.receive(requestTimeout, "cfilter");
            if (response.filterType != Messages::basicFilter) throw PeerError("unexpected filter type " + std::to_string(response.filterType));
            int32_t height = heightOf(response.blockHash);
            if (height < start || height > end || out[size_t(height - start)])
                throw PeerError("unexpected cfilter for block " + response.blockHash.hex());
            out[size_t(height - start)] = std::move(response.data);
        }
    } catch (...) {
        record(c, false, started);
        throw;
    }
    record(c, true, started);
    std::vector<Bytes> filters;
    filters.reserve(out.size());
    for (auto& f : out) filters.push_back(f ? std::move(*f) : Bytes());
    return filters;
}

Block SyncEngine::getBlock(PeerConnection& c, const Hash256& hash) {
    // Downloads the full block, with its witness data when the peer serves
    // it. The block is not verified here.
    c.blockReplies.drain();
    InvType kind = c.services().contains(ServiceFlags::witness) ? InvType::witnessBlock : InvType::block;
    TimePoint started = Clock::now();
    c.send(msg::GetData{{InvVect(kind, hash)}});
    TimePoint deadline = started + requestTimeout;
    while (true) {
        Duration left = deadline - Clock::now();
        try {
            if (left <= Duration::zero()) throw TimeoutError();
            PeerConnection::BlockReply reply = c.blockReplies.receive(left, "block");
            if (reply.block && reply.block->hash() == hash) {
                record(c, true, started);
                return std::move(*reply.block);
            }
            if (std::any_of(reply.notFound.begin(), reply.notFound.end(), [&](const InvVect& iv) { return iv.hash == hash; })) {
                record(c, false, started);
                throw PeerError("block " + hash.hex() + " not found");
            }
        } catch (const TimeoutError&) {
            record(c, false, started);
            throw TimeoutError("no block response within " + formatDuration(requestTimeout));
        }
    }
}

} // namespace tb
