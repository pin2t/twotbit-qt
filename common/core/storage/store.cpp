#include "storage/store.h"

#include <ctime>
#include <filesystem>

#include "util/log.h"

namespace tb {

namespace {

const char* schema = R"(
create table if not exists meta (
    id        integer primary key check (id = 1),
    mnemonic  blob    not null,
    xpub      text    not null,
    network   text    not null,
    createdAt integer not null,
    nextIndex integer not null default 0
);
create table if not exists addresses (
    type           text    not null check (type in ('receive', 'change')),
    idx            integer not null,
    derivationPath text    not null unique,
    address        text    not null unique,
    pubkey         blob    not null,
    createdAt      integer not null,
    primary key (type, idx)
);
create table if not exists headers (
    height     integer primary key,
    hash       blob not null unique,
    prevHash   blob not null,
    merkleRoot blob not null,
    version    integer not null,
    timestamp  integer not null,
    bits       integer not null,
    nonce      integer not null
);
create table if not exists cfilters (
    height       integer primary key,
    blockHash    blob not null,
    filterHeader blob not null,
    filterData   blob
);
create table if not exists matches (
    height    integer not null,
    blockHash blob    not null,
    address   text    not null,
    script    blob    not null,
    primary key (height, address)
);
create table if not exists transactions (
    txid      blob    primary key,
    height    integer not null,
    blockHash blob    not null,
    raw       blob    not null
);
create table if not exists pending (
    txid   blob    primary key,
    raw    blob    not null,
    seenAt integer not null
);
create table if not exists peers (
    host      text    not null,
    port      integer not null,
    services  integer not null default 0,
    latencyMs integer not null default 0,
    okCount   integer not null default 0,
    failCount integer not null default 0,
    primary key (host, port)
);
create table if not exists rates (
    ts    integer primary key,
    cents integer not null
);
create table if not exists rescans (
    address    text    primary key,
    fromHeight integer not null
);
create table if not exists settings (
    key   text primary key,
    value text not null
);
)";

const char* insertHeader = "insert or replace into headers (height, hash, prevHash, merkleRoot, version, timestamp, bits, nonce) values (?, ?, ?, ?, ?, ?, ?, ?)";
const char* insertFilter = "insert or replace into cfilters (height, blockHash, filterHeader, filterData) values (?, ?, ?, ?)";

int64_t now() { return int64_t(std::time(nullptr)); }

} // namespace

std::unique_ptr<Store> Store::open(const std::string& path) {
    std::filesystem::path dir = pathOf(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);
    auto db = std::make_unique<SQLiteDatabase>(path);
    try {
        db->execute("pragma journal_mode = wal; pragma synchronous = normal;");
        db->execute(schema);
    } catch (const SQLiteError& e) {
        if (e.code == 26) throw SQLiteError(26, "cannot read the wallet database (encrypted or not a database): " + e.message);
        throw;
    }
    return std::unique_ptr<Store>(new Store(std::move(db), path));
}

std::unique_ptr<Store> Store::openImmutable(const std::string& path) {
    return std::unique_ptr<Store>(new Store(std::make_unique<SQLiteDatabase>(path, true), path));
}

void Store::close() {
    std::lock_guard lock(mutex_);
    db_->close();
}

void Store::saveMeta(const std::string& mnemonic, const std::string& xpub, const std::string& network, int64_t createdAt) {
    with([&](SQLiteDatabase& db) {
        db.run("insert or ignore into meta (id, mnemonic, xpub, network, createdAt, nextIndex) values (1, ?, ?, ?, ?, 0)",
               {bytesOf(mnemonic), xpub, network, createdAt});
    });
}

std::optional<WalletMeta> Store::meta() {
    return with([&](SQLiteDatabase& db) {
        std::optional<WalletMeta> meta;
        db.run("select mnemonic, xpub, network, createdAt, nextIndex from meta where id = 1", {}, [&](const SQLiteDatabase::Row& row) {
            meta = WalletMeta{stringOf(row.blob(0).value_or(Bytes())), row.text(1), row.text(2), row.integer(3), uint32_t(row.integer(4))};
        });
        return meta;
    });
}

void Store::setMnemonic(const std::string& mnemonic) {
    with([&](SQLiteDatabase& db) { db.run("update meta set mnemonic = ? where id = 1", {bytesOf(mnemonic)}); });
}

void Store::updateNextIndex(uint32_t index) {
    with([&](SQLiteDatabase& db) { db.run("update meta set nextIndex = ? where id = 1", {int64_t(index)}); });
}

void Store::addAddress(const std::string& kind, const DerivedAddress& a) {
    with([&](SQLiteDatabase& db) {
        db.run("insert or ignore into addresses (type, idx, derivationPath, address, pubkey, createdAt) values (?, ?, ?, ?, ?, ?)",
               {kind, int64_t(a.index), a.path, a.address, a.pubkey, now()});
    });
}

std::vector<StoredAddress> Store::addresses(const std::string& kind) {
    return with([&](SQLiteDatabase& db) {
        std::vector<StoredAddress> out;
        db.run("select idx, derivationPath, address, pubkey from addresses where type = ? order by idx", {kind},
               [&](const SQLiteDatabase::Row& row) {
                   out.push_back(StoredAddress{uint32_t(row.integer(0)), row.text(1), row.text(2), row.blob(3).value_or(Bytes())});
               });
        return out;
    });
}

void Store::saveHeaders(const std::vector<StoredHeader>& headers) {
    if (headers.empty()) return;
    with([&](SQLiteDatabase& db) {
        db.transaction([&] {
            for (const auto& h : headers) {
                db.run(insertHeader, {int64_t(h.height), h.header.hash().data(), h.header.prevBlock.data(), h.header.merkleRoot.data(),
                                      int64_t(h.header.version), int64_t(h.header.timestamp), int64_t(h.header.bits), int64_t(h.header.nonce)});
            }
        });
    });
}

void Store::forEachHeader(const std::function<void(const StoredHeader&)>& body) {
    with([&](SQLiteDatabase& db) {
        db.run("select height, prevHash, merkleRoot, version, timestamp, bits, nonce from headers order by height", {},
               [&](const SQLiteDatabase::Row& row) {
                   BlockHeader header{int32_t(row.integer(3)), row.hash(1), row.hash(2), uint32_t(row.integer(4)), uint32_t(row.integer(5)),
                                      uint32_t(row.integer(6))};
                   body(StoredHeader{int32_t(row.integer(0)), header});
               });
    });
}

size_t Store::headerCount() {
    return with([&](SQLiteDatabase& db) { return size_t(db.scalar("select count(*) from headers").value_or(0)); });
}

void Store::deleteHeaders(int32_t above) {
    with([&](SQLiteDatabase& db) { db.run("delete from headers where height > ?", {int64_t(above)}); });
}

void Store::saveFilters(const std::vector<StoredFilter>& filters) {
    if (filters.empty()) return;
    with([&](SQLiteDatabase& db) {
        db.transaction([&] {
            for (const auto& f : filters) {
                SQLValue data = f.data ? SQLValue(*f.data) : SQLValue(nullptr);
                db.run(insertFilter, {int64_t(f.height), f.blockHash.data(), f.filterHeader.data(), data});
            }
        });
    });
}

void Store::deleteFilters(int32_t above) {
    with([&](SQLiteDatabase& db) { db.run("delete from cfilters where height > ?", {int64_t(above)}); });
}

void Store::pruneFilterData(int32_t from) {
    with([&](SQLiteDatabase& db) { db.run("update cfilters set filterData = null where height >= ? and filterData is not null", {int64_t(from)}); });
}

int32_t Store::filterResumeHeight(int32_t from) {
    return with([&](SQLiteDatabase& db) {
        auto highest = db.scalar("select max(height) from cfilters where height >= ?", {int64_t(from)});
        if (!highest || int32_t(*highest) < from) return from;
        return int32_t(*highest) + 1;
    });
}

std::optional<Hash256> Store::filterHeader(int32_t height) {
    return with([&](SQLiteDatabase& db) {
        std::optional<Hash256> out;
        db.run("select filterHeader from cfilters where height = ?", {int64_t(height)}, [&](const SQLiteDatabase::Row& row) { out = row.hash(0); });
        return out;
    });
}

size_t Store::filterCount() {
    return with([&](SQLiteDatabase& db) { return size_t(db.scalar("select count(*) from cfilters").value_or(0)); });
}

void Store::saveMatch(const FilterMatch& m) {
    with([&](SQLiteDatabase& db) {
        db.run("insert or ignore into matches (height, blockHash, address, script) values (?, ?, ?, ?)",
               {int64_t(m.height), m.blockHash.data(), m.address, m.script});
    });
}

std::vector<FilterMatch> Store::matches() {
    return with([&](SQLiteDatabase& db) {
        std::vector<FilterMatch> out;
        db.run("select height, blockHash, address, script from matches order by height, address", {}, [&](const SQLiteDatabase::Row& row) {
            out.push_back(FilterMatch{int32_t(row.integer(0)), row.hash(1), row.text(2), row.blob(3).value_or(Bytes())});
        });
        return out;
    });
}

void Store::deleteMatches(int32_t above) {
    with([&](SQLiteDatabase& db) { db.run("delete from matches where height > ?", {int64_t(above)}); });
}

void Store::saveTransaction(const StoredTransaction& t) {
    with([&](SQLiteDatabase& db) {
        db.run("insert or replace into transactions (txid, height, blockHash, raw) values (?, ?, ?, ?)",
               {t.txid.data(), int64_t(t.height), t.blockHash.data(), t.raw});
    });
}

std::vector<StoredTransaction> Store::transactions() {
    return with([&](SQLiteDatabase& db) {
        std::vector<StoredTransaction> out;
        db.run("select txid, height, blockHash, raw from transactions order by height, rowid", {}, [&](const SQLiteDatabase::Row& row) {
            out.push_back(StoredTransaction{row.hash(0), int32_t(row.integer(1)), row.hash(2), row.blob(3).value_or(Bytes())});
        });
        return out;
    });
}

void Store::deleteTransactions(int32_t above) {
    with([&](SQLiteDatabase& db) { db.run("delete from transactions where height > ?", {int64_t(above)}); });
}

void Store::savePending(const PendingTransaction& p) {
    with([&](SQLiteDatabase& db) {
        db.run("insert or ignore into pending (txid, raw, seenAt) values (?, ?, ?)", {p.txid.data(), p.raw, p.seenAt});
    });
}

std::vector<PendingTransaction> Store::pendingTransactions() {
    return with([&](SQLiteDatabase& db) {
        std::vector<PendingTransaction> out;
        db.run("select txid, raw, seenAt from pending order by seenAt, rowid", {}, [&](const SQLiteDatabase::Row& row) {
            out.push_back(PendingTransaction{row.hash(0), row.blob(1).value_or(Bytes()), row.integer(2)});
        });
        return out;
    });
}

void Store::deletePending(const Hash256& txid) {
    with([&](SQLiteDatabase& db) { db.run("delete from pending where txid = ?", {txid.data()}); });
}

int Store::deletePendingBefore(int64_t unix) {
    return with([&](SQLiteDatabase& db) {
        db.run("delete from pending where seenAt < ?", {unix});
        return db.changes();
    });
}

void Store::addRescan(const RescanJob& r) {
    with([&](SQLiteDatabase& db) {
        db.run("insert into rescans (address, fromHeight) values (?, ?) "
               "on conflict (address) do update set fromHeight = min(fromHeight, excluded.fromHeight)",
               {r.address, int64_t(r.from)});
    });
}

std::vector<RescanJob> Store::rescans() {
    return with([&](SQLiteDatabase& db) {
        std::vector<RescanJob> out;
        db.run("select address, fromHeight from rescans order by fromHeight, address", {},
               [&](const SQLiteDatabase::Row& row) { out.push_back(RescanJob{row.text(0), int32_t(row.integer(1))}); });
        return out;
    });
}

void Store::deleteRescan(const RescanJob& r) {
    with([&](SQLiteDatabase& db) { db.run("delete from rescans where address = ? and fromHeight >= ?", {r.address, int64_t(r.from)}); });
}

void Store::savePeer(const std::string& host, uint16_t port) {
    with([&](SQLiteDatabase& db) { db.run("insert or ignore into peers (host, port) values (?, ?)", {host, int64_t(port)}); });
}

void Store::upsertPeer(const std::string& host, uint16_t port, uint64_t services, int64_t latencyMs) {
    with([&](SQLiteDatabase& db) {
        db.run("insert into peers (host, port, services, latencyMs, okCount, failCount) values (?, ?, ?, ?, 0, 0) "
               "on conflict(host, port) do update set services = excluded.services, latencyMs = excluded.latencyMs",
               {host, int64_t(port), int64_t(services), latencyMs});
    });
}

void Store::updatePeerServices(const std::string& host, uint16_t port, uint64_t services) {
    with([&](SQLiteDatabase& db) {
        db.run("insert into peers (host, port, services) values (?, ?, ?) "
               "on conflict(host, port) do update set services = excluded.services",
               {host, int64_t(port), int64_t(services)});
    });
}

void Store::recordPeerResult(const std::string& host, uint16_t port, bool ok, int64_t latencyMs) {
    with([&](SQLiteDatabase& db) {
        db.run("insert into peers (host, port, latencyMs, okCount, failCount) values (?, ?, ?, ?, ?) "
               "on conflict(host, port) do update set latencyMs = excluded.latencyMs, "
               "okCount = okCount + excluded.okCount, failCount = failCount + excluded.failCount",
               {host, int64_t(port), latencyMs, int64_t(ok ? 1 : 0), int64_t(ok ? 0 : 1)});
    });
}

std::vector<PeerRecord> Store::peers() {
    return with([&](SQLiteDatabase& db) {
        std::vector<PeerRecord> out;
        db.run("select host, port, services, latencyMs, okCount, failCount from peers", {}, [&](const SQLiteDatabase::Row& row) {
            out.push_back(PeerRecord{row.text(0), uint16_t(row.integer(1)), uint64_t(row.integer(2)), row.integer(3), row.integer(4), row.integer(5)});
        });
        return out;
    });
}

std::string Store::setting(const std::string& key) {
    return with([&](SQLiteDatabase& db) {
        std::string value;
        db.run("select value from settings where key = ?", {key}, [&](const SQLiteDatabase::Row& row) { value = row.text(0); });
        return value;
    });
}

void Store::setSetting(const std::string& key, const std::string& value) {
    with([&](SQLiteDatabase& db) {
        if (value.empty())
            db.run("delete from settings where key = ?", {key});
        else
            db.run("insert or replace into settings (key, value) values (?, ?)", {key, value});
    });
}

bool Store::seedRates(const std::vector<Rate>& rates) {
    return with([&](SQLiteDatabase& db) {
        return db.transaction([&] {
            if (db.scalar("select exists(select 1 from rates)") != 0) return false;
            for (const auto& r : rates) db.run("insert or replace into rates (ts, cents) values (?, ?)", {r.time, r.cents});
            return true;
        });
    });
}

void Store::addRate(const Rate& r) {
    with([&](SQLiteDatabase& db) { db.run("insert or replace into rates (ts, cents) values (?, ?)", {r.time, r.cents}); });
}

std::optional<Rate> Store::latestRate() {
    return with([&](SQLiteDatabase& db) {
        std::optional<Rate> rate;
        db.run("select ts, cents from rates order by ts desc limit 1", {}, [&](const SQLiteDatabase::Row& row) {
            rate = Rate{row.integer(0), row.integer(1)};
        });
        return rate;
    });
}

} // namespace tb
