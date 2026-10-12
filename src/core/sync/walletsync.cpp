#include <algorithm>
#include <ctime>
#include <map>

#include "bitcoin/script.h"
#include "crypto/hashes.h"
#include "sync/engine.h"

namespace tb {

namespace {

int64_t unixNow() { return int64_t(std::time(nullptr)); }

} // namespace

void SyncEngine::loadWallet() {
    std::lock_guard lock(walletMutex_);
    loadWalletLocked(wallet_);
}

void SyncEngine::loadWalletLocked(WalletState& w) {
    // Rebuilds the coins, the inputs spent by pending transactions, the
    // history and the balances; the wallet lock is held.
    std::vector<StoredTransaction> stored = store_.transactions();
    std::vector<PendingTransaction> pending = store_.pendingTransactions();
    std::vector<Transaction> confirmed;
    std::vector<int32_t> heights;
    std::vector<int64_t> times;
    w.confirmedIDs.clear();
    for (const auto& t : stored) {
        Transaction tx;
        try {
            tx = Transaction::parse(t.raw);
        } catch (const std::exception& e) {
            throw PeerError("decode stored tx " + t.txid.hex() + ": " + e.what());
        }
        confirmed.push_back(std::move(tx));
        heights.push_back(t.height);
        times.push_back(blockTime(t.height));
        w.confirmedIDs.insert(t.txid);
    }
    std::vector<Transaction> unconfirmed;
    w.pendingIDs.clear();
    w.pendingSpends.clear();
    for (const auto& p : pending) {
        Transaction tx;
        try {
            tx = Transaction::parse(p.raw);
        } catch (const std::exception& e) {
            throw PeerError("decode pending tx " + p.txid.hex() + ": " + e.what());
        }
        for (const auto& in : tx.inputs) w.pendingSpends[in.previousOutPoint] = p.txid;
        unconfirmed.push_back(std::move(tx));
        times.push_back(p.seenAt);
        w.pendingIDs.insert(p.txid);
    }
    std::vector<Transaction> all = confirmed;
    all.insert(all.end(), unconfirmed.begin(), unconfirmed.end());
    w.outpoints.clear();
    w.usedAddrs.clear();
    w.firstPaid.clear();
    std::unordered_map<OutPoint, Coin, OutPointHasher> unspent;
    for (size_t n = 0; n < all.size(); ++n) {
        const Transaction& tx = all[n];
        Hash256 txid = tx.txid();
        for (size_t i = 0; i < tx.outputs.size(); ++i) {
            const TxOut& out = tx.outputs[i];
            auto idx = w.scriptIndex.find(out.pkScript);
            if (idx == w.scriptIndex.end()) continue;
            const WatchScript& script = w.scripts[idx->second];
            OutPoint op{txid, uint32_t(i)};
            w.outpoints[op] = script.address;
            w.usedAddrs.insert(script.address);
            bool isConfirmed = n < confirmed.size();
            if (isConfirmed) {
                auto first = w.firstPaid.find(script.address);
                if (first == w.firstPaid.end() || heights[n] < first->second) w.firstPaid[script.address] = heights[n];
            }
            unspent[op] = Coin{op, out.value, out.pkScript, script.address, script.path, isConfirmed, times[n]};
        }
    }
    for (const auto& tx : all)
        for (const auto& in : tx.inputs) unspent.erase(in.previousOutPoint);
    w.coins.clear();
    w.coins.reserve(unspent.size());
    for (auto& [op, coin] : unspent) w.coins.push_back(std::move(coin));
    std::sort(w.coins.begin(), w.coins.end(), [](const Coin& a, const Coin& b) {
        if (a.value != b.value) return a.value > b.value;
        return a.outPoint.description() < b.outPoint.description();
    });
    w.pendingTxs = unconfirmed;
    w.history = buildHistory(all, times, confirmed.size(), w, network_);
    const auto& index = w.scriptIndex;
    std::tie(w.confirmedBalance, w.pendingBalance) =
        computeBalance(confirmed, unconfirmed, [&](const Bytes& script) { return index.count(script) > 0; });
}

int64_t SyncEngine::blockTime(int32_t height) const {
    auto e = entry(height);
    return e ? int64_t(e->time) : 0;
}

std::pair<int64_t, int64_t> SyncEngine::computeBalance(const std::vector<Transaction>& confirmed, const std::vector<Transaction>& unconfirmed,
                                                       const IsMine& isMine) {
    int64_t settled = unspentValue(confirmed, isMine);
    std::vector<Transaction> all = confirmed;
    all.insert(all.end(), unconfirmed.begin(), unconfirmed.end());
    return {settled, unspentValue(all, isMine) - settled};
}

int64_t SyncEngine::unspentValue(const std::vector<Transaction>& txs, const IsMine& isMine) {
    std::unordered_map<OutPoint, int64_t, OutPointHasher> coins;
    for (const auto& tx : txs) {
        Hash256 txid = tx.txid();
        for (size_t i = 0; i < tx.outputs.size(); ++i)
            if (isMine(tx.outputs[i].pkScript)) coins[OutPoint{txid, uint32_t(i)}] = tx.outputs[i].value;
    }
    for (const auto& tx : txs)
        for (const auto& in : tx.inputs) coins.erase(in.previousOutPoint);
    int64_t sum = 0;
    for (const auto& [op, value] : coins) sum += value;
    return sum;
}

bool SyncEngine::isUsed(const std::string& address) const {
    std::lock_guard lock(walletMutex_);
    return wallet_.usedAddrs.count(address) > 0;
}

std::vector<Coin> SyncEngine::coins() const {
    std::lock_guard lock(walletMutex_);
    return wallet_.coins;
}

std::vector<WalletTransaction> SyncEngine::history() const {
    std::lock_guard lock(walletMutex_);
    return wallet_.history;
}

std::pair<int64_t, int64_t> SyncEngine::walletBalance() const {
    std::lock_guard lock(walletMutex_);
    return {wallet_.confirmedBalance, wallet_.pendingBalance};
}

std::vector<WatchScript> SyncEngine::watchedScripts() const {
    std::lock_guard lock(walletMutex_);
    return wallet_.scripts;
}

void SyncEngine::watch(const DerivedAddress& address) {
    Bytes script = address.script();
    bool watched = false;
    std::optional<int32_t> paid;
    std::string failure;
    {
        std::lock_guard lock(walletMutex_);
        WalletState& w = wallet_;
        if (!w.initialized || w.scriptIndex.count(script)) return;
        w.scriptIndex[script] = w.scripts.size();
        w.scripts.push_back(WatchScript{address.address, address.path, script});
        watched = true;
        try {
            loadWalletLocked(w);
            auto first = w.firstPaid.find(address.address);
            if (first != w.firstPaid.end()) {
                paid = first->second;
                store_.addRescan(RescanJob{address.address, first->second});
            }
        } catch (const std::exception& e) {
            failure = e.what();
        }
    }
    if (!watched) return;
    Log::info("sync: watching address " + address.address);
    if (!failure.empty()) Log::info("sync: watch address " + address.address + ": " + failure);
    if (!paid) {
        reloadBlooms();
        return;
    }
    Log::info("sync: address " + address.address + " was paid in block " + std::to_string(*paid) + " before it was watched, rescan queued");
    walletChanged();
    wake_.signal();
}

bool SyncEngine::describeTx(const Transaction& tx, const WalletState& w, const std::string& place) const {
    // Logs how a transaction touches the wallet and reports whether it does;
    // the wallet lock is held.
    Hash256 txid = tx.txid();
    bool relevant = false;
    for (const auto& in : tx.inputs) {
        auto it = w.outpoints.find(in.previousOutPoint);
        if (it == w.outpoints.end()) continue;
        relevant = true;
        Log::info("match: tx " + txid.hex() + " " + place + " spends " + in.previousOutPoint.description() + " of " + it->second);
    }
    for (const auto& out : tx.outputs) {
        auto idx = w.scriptIndex.find(out.pkScript);
        if (idx == w.scriptIndex.end()) continue;
        relevant = true;
        Log::info("match: tx " + txid.hex() + " " + place + " pays " + std::to_string(out.value) + " sat to " + w.scripts[idx->second].address);
    }
    return relevant;
}

int SyncEngine::processBlock(int32_t height, const Block& block) {
    // Stores every transaction of the verified block paying a wallet script
    // or spending a wallet coin. A pending transaction it confirms leaves the
    // pending set, and a pending transaction conflicting with one of its
    // transactions is dropped. It returns the number of wallet transactions
    // found.
    Hash256 blockHash = block.hash();
    int found = 0;
    bool changed = false;
    std::exception_ptr failure;
    {
        std::lock_guard lock(walletMutex_);
        WalletState& w = wallet_;
        try {
            for (const auto& tx : block.transactions) {
                Hash256 txid = tx.txid();
                for (const auto& in : tx.inputs) {
                    auto other = w.pendingSpends.find(in.previousOutPoint);
                    if (other == w.pendingSpends.end() || other->second == txid) continue;
                    Hash256 conflicting = other->second;
                    try {
                        store_.deletePending(conflicting);
                    } catch (const std::exception& e) {
                        throw PeerError("drop pending tx " + conflicting.hex() + ": " + e.what());
                    }
                    w.pendingSpends.erase(other);
                    changed = true;
                    Log::info("pending: tx " + conflicting.hex() + " dropped, it conflicts with tx " + txid.hex() + " in block " +
                              std::to_string(height));
                }
                if (!describeTx(tx, w, "in block " + std::to_string(height))) continue;
                try {
                    store_.saveTransaction(StoredTransaction{txid, height, blockHash, tx.serialized()});
                } catch (const std::exception& e) {
                    throw PeerError("store tx " + txid.hex() + ": " + e.what());
                }
                if (w.pendingIDs.count(txid)) {
                    try {
                        store_.deletePending(txid);
                    } catch (const std::exception& e) {
                        throw PeerError("confirm pending tx " + txid.hex() + ": " + e.what());
                    }
                    Log::info("pending: tx " + txid.hex() + " confirmed in block " + std::to_string(height));
                }
                ++found;
                changed = true;
            }
            if (changed) loadWalletLocked(w);
        } catch (...) {
            failure = std::current_exception();
        }
    }
    if (changed) walletChanged();
    if (failure) std::rethrow_exception(failure);
    return found;
}

void SyncEngine::requestTxs(PeerConnection& c, const std::vector<InvVect>& list) {
    // Asks the peer for the announced transactions not seen before, without
    // witness data: the inputs and outputs are all the wallet checks.
    std::vector<InvVect> wanted;
    {
        std::lock_guard lock(walletMutex_);
        WalletState& w = wallet_;
        for (const auto& iv : list) {
            if (!iv.is(InvType::tx) && !iv.is(InvType::witnessTx)) continue;
            if (w.seenTxs.count(iv.hash) || w.confirmedIDs.count(iv.hash) || w.pendingIDs.count(iv.hash)) continue;
            if (w.seenTxs.size() >= seenLimit) w.seenTxs.clear();
            w.seenTxs.insert(iv.hash);
            wanted.emplace_back(InvType::tx, iv.hash);
        }
    }
    if (!wanted.empty()) c.send(msg::GetData{wanted});
}

void SyncEngine::acceptPendingTx(PeerConnection& c, const Transaction& tx) {
    // Stores a relayed unconfirmed transaction when it pays a wallet script
    // or spends a wallet coin; bloom filter false positives are ignored.
    Hash256 txid = tx.txid();
    bool accepted = false;
    std::string failure;
    {
        std::lock_guard lock(walletMutex_);
        WalletState& w = wallet_;
        if (w.confirmedIDs.count(txid) || w.pendingIDs.count(txid) || !describeTx(tx, w, "unconfirmed, from " + c.address + ",")) return;
        try {
            store_.savePending(PendingTransaction{txid, tx.serialized(), unixNow()});
            loadWalletLocked(w);
            accepted = true;
        } catch (const std::exception& e) {
            failure = e.what();
        }
    }
    if (accepted)
        walletChanged();
    else if (!failure.empty())
        Log::info("pending: store tx " + txid.hex() + ": " + failure);
}

void SyncEngine::expirePending() {
    // Drops the unconfirmed transactions older than pendingExpiry.
    int n;
    try {
        n = store_.deletePendingBefore(unixNow() - pendingExpiry);
    } catch (const std::exception& e) {
        throw PeerError(std::string("expire pending transactions: ") + e.what());
    }
    if (n <= 0) return;
    Log::info("pending: " + std::to_string(n) + " unconfirmed transactions expired after 14 days");
    loadWallet();
    walletChanged();
}

void SyncEngine::walletChanged() {
    // Reports the new balance and refreshes the bloom filters of the
    // connected peers with the current wallet coins.
    auto [confirmed, pending] = walletBalance();
    Log::info("balance: " + std::to_string(confirmed) + " sat confirmed, " + (pending >= 0 ? "+" : "") + std::to_string(pending) + " sat pending");
    reloadBlooms();
    emitStatus();
}

size_t SyncEngine::broadcast(const Transaction& tx) {
    std::vector<PeerPtr> peers = livePeers(poolPeers());
    if (peers.empty()) throw PeerError("not connected to any peer");
    Hash256 txid = tx.txid();
    try {
        std::lock_guard lock(walletMutex_);
        WalletState& w = wallet_;
        store_.savePending(PendingTransaction{txid, tx.serialized(), unixNow()});
        w.seenTxs.insert(txid);
        describeTx(tx, w, "sent,");
        loadWalletLocked(w);
    } catch (const std::exception& e) {
        throw PeerError("record tx " + txid.hex() + ": " + e.what());
    }
    for (const auto& c : peers) c->send(msg::Tx{tx});
    Log::info("send: tx " + txid.hex() + " broadcast to " + std::to_string(peers.size()) + " peers");
    walletChanged();
    return peers.size();
}

void SyncEngine::rebroadcast(PeerConnection& c) {
    // Sends the pending transactions spending wallet coins, the wallet's own
    // unconfirmed payments, to a newly connected peer.
    std::vector<Transaction> own;
    {
        std::lock_guard lock(walletMutex_);
        for (const auto& tx : wallet_.pendingTxs)
            if (std::any_of(tx.inputs.begin(), tx.inputs.end(), [&](const TxIn& in) { return wallet_.outpoints.count(in.previousOutPoint) > 0; }))
                own.push_back(tx);
    }
    for (const auto& tx : own) c.send(msg::Tx{tx});
    if (!own.empty()) Log::info("send: " + std::to_string(own.size()) + " pending transactions sent again to " + c.address);
}

BloomFilter SyncEngine::walletBloom() const {
    // The BIP37 filter of the wallet: the key hash of every wallet address,
    // which peers match in output scripts, and every wallet coin, matched in
    // the inputs spending it. Peers must not add to it themselves: each false
    // positive would add an unrelated transaction's outputs until the filter
    // matches everything. The wallet reloads it instead whenever its coins
    // change.
    std::lock_guard lock(walletMutex_);
    uint32_t elements = uint32_t(wallet_.scripts.size() + wallet_.outpoints.size());
    uint32_t tweak = uint32_t(Hashes::randomUInt64());
    BloomFilter filter(std::max(elements, bloomMinElements), tweak, bloomFPRate, BloomFilter::updateNone);
    for (const auto& s : wallet_.scripts) filter.add(ByteView(s.script).subspan(2));
    for (const auto& [op, address] : wallet_.outpoints) filter.add(op);
    return filter;
}

bool SyncEngine::loadBloom(PeerConnection& c) {
    // Sends the wallet filter to a peer serving BIP37 filters and asks it for
    // the matching transactions already in its mempool; from then on it
    // announces every matching transaction it relays.
    if (!c.services().contains(ServiceFlags::bloom)) return false;
    BloomFilter filter = walletBloom();
    size_t size = filter.data().size();
    uint32_t funcs = filter.hashFuncs();
    c.send(msg::FilterLoad{std::move(filter)});
    c.send(msg::MemPool{});
    Log::info("peer " + c.address + ": bloom filter loaded (" + std::to_string(size) + " bytes, " + std::to_string(funcs) + " hash functions)");
    return true;
}

void SyncEngine::reloadBlooms() {
    for (const auto& c : livePeers(poolPeers()))
        if (c->services().contains(ServiceFlags::bloom)) c->send(msg::FilterLoad{walletBloom()});
}

void SyncEngine::countRelayed(PeerConnection& c, const Transaction& tx) {
    int64_t n, bytes;
    {
        std::lock_guard lock(relayMutex_);
        relay_.txs += 1;
        relay_.bytes += int64_t(tx.serialized().size());
        n = relay_.txs;
        bytes = relay_.bytes;
    }
    if (n % relayLogEvery == 0)
        Log::info("tx relay: " + std::to_string(n) + " relayed transactions checked (" + std::to_string(bytes / 1024) + " KB), the latest from " +
                  c.address);
}

bool SyncEngine::relaySource(const PeerConnection& c, const std::vector<PeerPtr>& peers) {
    // Whether the transactions the peer announces are fetched, given the
    // connected peers with the primary first. While any peer serves bloom
    // filters only those are listened to, as they announce just the
    // transactions matching the wallet's filter. Without any, every relayed
    // transaction is downloaded from the primary and checked by the wallet
    // itself: full transaction bandwidth, but it works with every node and
    // reveals nothing about the wallet.
    bool anyBloom = std::any_of(peers.begin(), peers.end(), [](const PeerPtr& p) { return p->services().contains(ServiceFlags::bloom); });
    if (anyBloom) return c.services().contains(ServiceFlags::bloom);
    return !peers.empty() && peers.front().get() == &c;
}

std::string SyncEngine::describeRelay(const std::vector<PeerPtr>& peers) {
    size_t bloom = size_t(std::count_if(peers.begin(), peers.end(), [](const PeerPtr& p) { return p->services().contains(ServiceFlags::bloom); }));
    if (bloom > 0) return "bloom filters on " + std::to_string(bloom) + " peer" + (bloom == 1 ? "" : "s");
    if (!peers.empty()) return "no peer serves bloom filters, downloading all relayed transactions from " + peers.front()->address;
    return "no peers";
}

void SyncEngine::logRelayMode() {
    std::string mode = describeRelay(orderedPeers());
    bool changed;
    {
        std::lock_guard lock(relayMutex_);
        changed = relay_.mode != mode;
        relay_.mode = mode;
    }
    if (changed) Log::info("tx relay: " + mode);
}

std::vector<WalletTransaction> SyncEngine::buildHistory(const std::vector<Transaction>& txs, const std::vector<int64_t>& times, size_t confirmed,
                                                        const WalletState& w, const Network& network) {
    std::unordered_map<Hash256, const Transaction*, Hash256Hasher> byID;
    for (const auto& tx : txs) byID[tx.txid()] = &tx;
    std::vector<WalletTransaction> out;
    out.reserve(txs.size());
    for (size_t n = 0; n < txs.size(); ++n) {
        const Transaction& tx = txs[n];
        WalletTransaction entry{tx.txid(), times[n], n < confirmed, 0, 0, {}, {}};
        bool funded = true;
        int64_t fee = 0;
        for (const auto& in : tx.inputs) {
            auto [address, value] = inputAddress(in, byID, w, network);
            funded = funded && address.mine;
            fee += value;
            if (address.mine) entry.net -= value;
            entry.inputs.push_back(std::move(address));
        }
        for (const auto& output : tx.outputs) {
            TxAddress address = outputAddress(output.pkScript, w, network);
            fee -= output.value;
            if (address.mine) entry.net += output.value;
            entry.outputs.push_back(std::move(address));
        }
        if (funded) entry.fee = fee;
        out.push_back(std::move(entry));
    }
    std::stable_sort(out.begin(), out.end(), [](const WalletTransaction& a, const WalletTransaction& b) {
        if (a.confirmed != b.confirmed) return !a.confirmed;
        return a.time > b.time;
    });
    return out;
}

TxAddress SyncEngine::outputAddress(const Bytes& script, const WalletState& w, const Network& network) {
    auto idx = w.scriptIndex.find(script);
    if (idx != w.scriptIndex.end()) return TxAddress{w.scripts[idx->second].address, true};
    return TxAddress{Address::of(script, network).value_or(""), false};
}

std::pair<TxAddress, int64_t> SyncEngine::inputAddress(const TxIn& input, const std::unordered_map<Hash256, const Transaction*, Hash256Hasher>& byID,
                                                       const WalletState& w, const Network& network) {
    const OutPoint& prev = input.previousOutPoint;
    if (prev.isCoinbase()) return {TxAddress{coinbaseInput, false}, 0};
    auto it = byID.find(prev.hash);
    if (it != byID.end() && prev.index < it->second->outputs.size()) {
        const TxOut& out = it->second->outputs[prev.index];
        return {outputAddress(out.pkScript, w, network), out.value};
    }
    return {TxAddress{spentAddress(input.signatureScript, input.witness, network), false}, 0};
}

std::string SyncEngine::spentAddress(const Bytes& sigScript, const std::vector<Bytes>& witness, const Network& network) {
    std::vector<Bytes> pushes;
    try {
        pushes = Script::pushedData(sigScript);
    } catch (const DecodeError&) {
        return "";
    }
    if (!witness.empty()) {
        if (pushes.size() == 1) return Address::p2sh(Hashes::hash160(pushes[0]), network);
        if (!sigScript.empty()) return "";
        const Bytes& last = witness.back();
        if (witness.size() == 2 && Script::isPubKey(last)) return Address::segwit(0, Hashes::hash160(last), network);
        if (witness.size() >= 2 && !Script::isControlBlock(last)) return Address::segwit(0, Hashes::sha256(last), network);
        return "";
    }
    if (pushes.size() == 2 && Script::isPubKey(pushes[1])) return Address::p2pkh(Hashes::hash160(pushes[1]), network);
    if (pushes.size() >= 2) return Address::p2sh(Hashes::hash160(pushes.back()), network);
    return "";
}

} // namespace tb
