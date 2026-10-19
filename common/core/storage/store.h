#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bitcoin/primitives.h"
#include "storage/sqlite.h"
#include "wallet/hdwallet.h"

namespace tb {

/// WalletMeta is the single wallet metadata row: the network, the account
/// xpub, when the wallet was created and the next receive index. The
/// recovery phrase lives in the Windows Credential Manager or the Keychain; the mnemonic
/// column holds it only for the local test networks and in a database
/// brought over from bitfyn, until it is moved there.
struct WalletMeta {
    std::string mnemonic;
    std::string xpub;
    std::string network;
    int64_t createdAt = 0;
    uint32_t nextIndex = 0;

    friend bool operator==(const WalletMeta&, const WalletMeta&) = default;
};

/// StoredHeader is one validated block header at its height.
struct StoredHeader {
    int32_t height = 0;
    BlockHeader header;

    friend bool operator==(const StoredHeader&, const StoredHeader&) = default;
};

/// StoredFilter is the BIP158 filter row of one block: its chained filter
/// header and, until it is pruned after matching, the filter itself.
struct StoredFilter {
    int32_t height = 0;
    Hash256 blockHash;
    Hash256 filterHeader;
    std::optional<Bytes> data;
};

/// FilterMatch records one wallet script that matched a block filter.
struct FilterMatch {
    int32_t height = 0;
    Hash256 blockHash;
    std::string address;
    Bytes script;
};

/// StoredTransaction is one confirmed wallet transaction in its serialized
/// form.
struct StoredTransaction {
    Hash256 txid;
    int32_t height = 0;
    Hash256 blockHash;
    Bytes raw;

    friend bool operator==(const StoredTransaction&, const StoredTransaction&) = default;
};

/// PendingTransaction is one unconfirmed wallet transaction with the unix
/// time it was first seen.
struct PendingTransaction {
    Hash256 txid;
    Bytes raw;
    int64_t seenAt = 0;
};

/// StoredAddress is one derived address with its derivation path and public
/// key.
struct StoredAddress {
    uint32_t index = 0;
    std::string path;
    std::string address;
    Bytes pubkey;
};

/// RescanJob is a queued rescan of the blocks from a height on for a wallet
/// address that was watched after blocks paying it were scanned.
struct RescanJob {
    std::string address;
    int32_t from = 0;

    friend bool operator==(const RescanJob&, const RescanJob&) = default;
};

/// PeerRecord is one known peer with its connection statistics.
struct PeerRecord {
    std::string host;
    uint16_t port = 0;
    uint64_t services = 0;
    int64_t latencyMs = 0;
    int64_t okCount = 0;
    int64_t failCount = 0;

    friend bool operator==(const PeerRecord&, const PeerRecord&) = default;
};

/// Rate is the price of one bitcoin in US cents at a unix time.
struct Rate {
    int64_t time = 0;
    int64_t cents = 0;

    friend bool operator==(const Rate&, const Rate&) = default;
};

/// Store keeps the wallet in an SQLite database, with the same schema as
/// bitfyn's and the macOS app's: the wallet metadata and addresses, the
/// validated header chain, filter headers and matches, wallet transactions,
/// known peers, rates and settings. Calls are serialized; it is safe to use
/// from any thread.
class Store {
public:
    /// The settings key of the pinned peer's host:port.
    static constexpr const char* settingPinnedPeer = "pinnedPeer";

    /// Opens the database at the path, creating it and its directory as
    /// needed.
    static std::unique_ptr<Store> open(const std::string& path);

    /// Opens an existing database only to read it, changing nothing of the
    /// file, as to read the headers of another wallet's database.
    static std::unique_ptr<Store> openImmutable(const std::string& path);

    const std::string& path() const { return path_; }

    /// Closes the database; the store is not used afterwards.
    void close();

    // Wallet metadata and addresses

    /// Inserts the wallet metadata row unless a wallet is stored already.
    void saveMeta(const std::string& mnemonic, const std::string& xpub, const std::string& network, int64_t createdAt);
    /// The wallet metadata, or nothing when no wallet is stored yet.
    std::optional<WalletMeta> meta();
    /// Replaces the stored mnemonic, empty once it moved to the Credential
    /// Manager.
    void setMnemonic(const std::string& mnemonic);
    void updateNextIndex(uint32_t index);
    /// Stores a derived receive address. Idempotent per index.
    void addAddress(const DerivedAddress& a) { addAddress("receive", a); }
    /// Stores a derived change address. Idempotent per index.
    void addChangeAddress(const DerivedAddress& a) { addAddress("change", a); }
    std::vector<StoredAddress> addresses() { return addresses("receive"); }
    std::vector<StoredAddress> changeAddresses() { return addresses("change"); }

    // Headers

    /// Stores headers in one transaction, replacing rows at the same heights.
    void saveHeaders(const std::vector<StoredHeader>& headers);
    /// Streams every stored header in height order.
    void forEachHeader(const std::function<void(const StoredHeader&)>& body);
    size_t headerCount();
    /// Removes every header above the height.
    void deleteHeaders(int32_t above);

    // Filters

    /// Stores filter rows in one transaction.
    void saveFilters(const std::vector<StoredFilter>& filters);
    /// Removes every filter row above the height, as a fork is rewound.
    void deleteFilters(int32_t above);
    /// Drops filter data kept at or above the height, left behind by a crash
    /// before pruning; the chained headers stay.
    void pruneFilterData(int32_t from);
    /// The height the next filter download starts at: one past the highest
    /// stored filter row at or after from. Rows are contiguous from the
    /// wallet's start height, so that is the first one missing.
    int32_t filterResumeHeight(int32_t from);
    /// The stored filter header at the height, if any.
    std::optional<Hash256> filterHeader(int32_t height);
    size_t filterCount();

    // Matches

    void saveMatch(const FilterMatch& m);
    std::vector<FilterMatch> matches();
    void deleteMatches(int32_t above);

    // Transactions

    /// Stores a confirmed wallet transaction, replacing an earlier copy.
    void saveTransaction(const StoredTransaction& t);
    /// Every confirmed wallet transaction in block order.
    std::vector<StoredTransaction> transactions();
    void deleteTransactions(int32_t above);
    /// Stores an unconfirmed transaction; one seen before keeps its first
    /// seen time.
    void savePending(const PendingTransaction& p);
    /// Every unconfirmed wallet transaction, oldest first.
    std::vector<PendingTransaction> pendingTransactions();
    void deletePending(const Hash256& txid);
    /// Removes the unconfirmed transactions first seen before the unix time
    /// and returns how many were removed.
    int deletePendingBefore(int64_t unix);

    // Rescans

    /// Queues a rescan of the address; one queued already keeps the lower
    /// height.
    void addRescan(const RescanJob& r);
    std::vector<RescanJob> rescans();
    /// Removes a finished rescan, unless it was queued again from a lower
    /// height meanwhile.
    void deleteRescan(const RescanJob& r);

    // Peers

    /// Records a known peer address. Idempotent.
    void savePeer(const std::string& host, uint16_t port);
    /// Stores the services and handshake latency of a connected peer.
    void upsertPeer(const std::string& host, uint16_t port, uint64_t services, int64_t latencyMs);
    /// Stores the advertised services of a gossiped peer.
    void updatePeerServices(const std::string& host, uint16_t port, uint64_t services);
    /// Counts one successful or failed request against a peer and stores its
    /// latency.
    void recordPeerResult(const std::string& host, uint16_t port, bool ok, int64_t latencyMs);
    std::vector<PeerRecord> peers();

    // Settings

    /// The value stored for the key, or "" when it is not set.
    std::string setting(const std::string& key);
    /// Stores the value for the key; an empty value removes the setting.
    void setSetting(const std::string& key, const std::string& value);

    // Rates

    /// Fills the rates table with the rates, only while it is empty, so
    /// rates recorded since are never overwritten. It reports whether they
    /// were inserted.
    bool seedRates(const std::vector<Rate>& rates);
    void addRate(const Rate& r);
    /// The most recent rate, or nothing while none is stored.
    std::optional<Rate> latestRate();

private:
    Store(std::unique_ptr<SQLiteDatabase> db, std::string path) : db_(std::move(db)), path_(std::move(path)) {}

    void addAddress(const std::string& kind, const DerivedAddress& a);
    std::vector<StoredAddress> addresses(const std::string& kind);

    template <class F>
    auto with(F body) -> decltype(body(std::declval<SQLiteDatabase&>())) {
        std::lock_guard lock(mutex_);
        return body(*db_);
    }

    std::mutex mutex_;
    std::unique_ptr<SQLiteDatabase> db_;
    std::string path_;
};

} // namespace tb
