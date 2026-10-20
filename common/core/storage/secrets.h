#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "bitcoin/network.h"
#include "storage/store.h"
#include "wallet/hdwallet.h"

namespace tb {

/// SecretStore keeps the wallet's recovery phrase, under an account name:
/// the wallet's account xpub.
class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual std::optional<std::string> secret(const std::string& account) = 0;
    virtual void setSecret(const std::string& secret, const std::string& account, const std::string& label) = 0;
    virtual void deleteSecret(const std::string& account) = 0;
};

class CredentialError : public Error {
public:
    CredentialError(const std::string& what, unsigned long code) : Error(what), code(code) {}
    unsigned long code;
};

/// CredentialSecretStore keeps secrets in the platform's store of the
/// signed-in user's credentials, kept on this computer only: generic
/// credentials of the Windows Credential Manager (windows/core/storage),
/// generic passwords of the macOS login keychain (macos/core/storage), or
/// items of the Secret Service keyring on Linux (linux/core/storage).
class CredentialSecretStore : public SecretStore {
public:
    explicit CredentialSecretStore(std::string service) : service_(std::move(service)) {}

    /// The name of the platform's credential store, as messages show it:
    /// "Windows Credential Manager", "Keychain" or "keyring".
    static const char* storeName();

    std::optional<std::string> secret(const std::string& account) override;
    void setSecret(const std::string& secret, const std::string& account, const std::string& label) override;
    void deleteSecret(const std::string& account) override;

    /// The target name a secret of the account is stored under in the
    /// Credential Manager. The Keychain keeps the service and the account
    /// apart.
    std::string target(const std::string& account) const { return service_ + ":" + account; }

private:
    std::string service_;
};

/// MemorySecretStore keeps secrets in memory, for tests.
class MemorySecretStore : public SecretStore {
public:
    std::optional<std::string> secret(const std::string& account) override;
    void setSecret(const std::string& secret, const std::string& account, const std::string& label) override;
    void deleteSecret(const std::string& account) override;

private:
    std::mutex mutex_;
    std::map<std::string, std::string> secrets_;
};

class WalletError : public Error {
public:
    enum class Kind { wrongNetwork, missingRecoveryPhrase, recoveryPhraseMismatch };

    WalletError(Kind kind, const std::string& what) : Error(what), kind(kind) {}
    static WalletError wrongNetwork(const std::string& stored, const std::string& requested) {
        return {Kind::wrongNetwork, "wallet database is for network \"" + stored + "\", not \"" + requested + "\""};
    }
    static WalletError missingRecoveryPhrase() {
        return {Kind::missingRecoveryPhrase, std::string("the wallet's recovery phrase is not in the ") + CredentialSecretStore::storeName()};
    }
    static WalletError recoveryPhraseMismatch() { return {Kind::recoveryPhraseMismatch, "the recovery phrase belongs to another wallet"}; }

    Kind kind;
};

/// OpenedWallet is the wallet database with the wallet it holds.
struct OpenedWallet {
    std::shared_ptr<Store> store;
    std::shared_ptr<HDWallet> wallet;
    WalletMeta meta;
};

/// WalletLoader opens the wallet database of a network in the data
/// directory, creating the wallet on first run with a fresh 12-word recovery
/// phrase kept in the secret store.
namespace WalletLoader {

/// The label of the credential of the network's recovery phrase.
std::string label(const Network& network);

OpenedWallet open(const std::string& dataDirectory, const Network& network, SecretStore& secrets, int64_t now);

OpenedWallet open(std::shared_ptr<Store> store, const Network& network, SecretStore& secrets, int64_t now);

/// Restores the recovery phrase of the wallet in the database to the secret
/// store, after it was lost: the phrase must derive the stored account
/// xpub.
void restore(const std::string& mnemonic, const std::string& dataDirectory, const Network& network, SecretStore& secrets);

} // namespace WalletLoader

} // namespace tb
