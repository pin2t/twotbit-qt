#include "storage/secrets.h"

#include "util/log.h"
#include "util/strings.h"
#include "wallet/bip39.h"

namespace tb {

std::optional<std::string> MemorySecretStore::secret(const std::string& account) {
    std::lock_guard lock(mutex_);
    auto it = secrets_.find(account);
    if (it == secrets_.end()) return std::nullopt;
    return it->second;
}

void MemorySecretStore::setSecret(const std::string& secret, const std::string& account, const std::string&) {
    std::lock_guard lock(mutex_);
    secrets_[account] = secret;
}

void MemorySecretStore::deleteSecret(const std::string& account) {
    std::lock_guard lock(mutex_);
    secrets_.erase(account);
}

namespace WalletLoader {

std::string label(const Network& network) { return "Twotbit recovery phrase (" + network.displayName() + ")"; }

namespace {

std::string databasePath(const std::string& dataDirectory, const Network& network) {
    return utf8Of(pathOf(dataDirectory) / pathOf(network.databaseFileName()));
}

} // namespace

OpenedWallet open(const std::string& dataDirectory, const Network& network, SecretStore& secrets, int64_t now) {
    std::shared_ptr<Store> store = Store::open(databasePath(dataDirectory, network));
    try {
        return open(store, network, secrets, now);
    } catch (...) {
        store->close();
        throw;
    }
}

OpenedWallet open(std::shared_ptr<Store> store, const Network& network, SecretStore& secrets, int64_t now) {
    bool credentials = network.keepsPhraseInCredentials();
    auto meta = store->meta();
    if (!meta) {
        std::string mnemonic = BIP39::generateMnemonic();
        auto wallet = std::make_shared<HDWallet>(mnemonic, network);
        if (credentials) secrets.setSecret(mnemonic, wallet->accountXPub(), label(network));
        store->saveMeta(credentials ? "" : mnemonic, wallet->accountXPub(), network.displayName(), now);
        Log::info("wallet created on " + network.displayName() + ", account " + wallet->accountXPub());
        return OpenedWallet{store, wallet, *store->meta()};
    }
    const Network* stored = Network::named(meta->network);
    if (!stored || *stored != network) throw WalletError::wrongNetwork(meta->network, network.displayName());
    if (!meta->mnemonic.empty()) {
        auto wallet = std::make_shared<HDWallet>(meta->mnemonic, network);
        if (!credentials) return OpenedWallet{store, wallet, *meta};
        secrets.setSecret(meta->mnemonic, meta->xpub, label(network));
        store->setMnemonic("");
        Log::info("wallet recovery phrase moved from the database to the Credential Manager");
        return OpenedWallet{store, wallet, *store->meta()};
    }
    auto mnemonic = secrets.secret(meta->xpub);
    if (!mnemonic) throw WalletError::missingRecoveryPhrase();
    auto wallet = std::make_shared<HDWallet>(*mnemonic, network);
    if (wallet->accountXPub() != meta->xpub) throw WalletError::recoveryPhraseMismatch();
    return OpenedWallet{store, wallet, *meta};
}

void restore(const std::string& mnemonic, const std::string& dataDirectory, const Network& network, SecretStore& secrets) {
    std::unique_ptr<Store> store = Store::open(databasePath(dataDirectory, network));
    struct Closer {
        Store& s;
        ~Closer() { s.close(); }
    } closer{*store};
    auto meta = store->meta();
    if (!meta) return;
    std::string normalized = lower(join(words(mnemonic), " "));
    HDWallet wallet(normalized, network);
    if (wallet.accountXPub() != meta->xpub) throw WalletError::recoveryPhraseMismatch();
    secrets.setSecret(normalized, meta->xpub, label(network));
}

} // namespace WalletLoader

} // namespace tb
