#include "storage/secrets.h"

#include <windows.h>
#include <wincred.h>

#include "util/log.h"
#include "util/strings.h"
#include "wallet/bip39.h"

namespace tb {

namespace {

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
    return out;
}

std::string systemMessage(DWORD code) {
    wchar_t* text = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                             reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out;
    if (n && text) {
        int len = WideCharToMultiByte(CP_UTF8, 0, text, int(n), nullptr, 0, nullptr, nullptr);
        out.resize(size_t(len));
        WideCharToMultiByte(CP_UTF8, 0, text, int(n), out.data(), len, nullptr, nullptr);
        LocalFree(text);
    }
    return trim(out);
}

CredentialError credentialError(DWORD code) {
    std::string message = systemMessage(code);
    return CredentialError("Credential Manager: " + (message.empty() ? "error" : message) + " (" + std::to_string(code) + ")", code);
}

} // namespace

std::optional<std::string> CredentialSecretStore::secret(const std::string& account) {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(wide(target(account)).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) return std::nullopt;
        throw credentialError(code);
    }
    const char* blob = reinterpret_cast<const char*>(credential->CredentialBlob);
    std::string out(blob, blob + credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return out;
}

void CredentialSecretStore::setSecret(const std::string& secret, const std::string& account, const std::string& label) {
    std::wstring targetName = wide(target(account));
    std::wstring userName = wide(account);
    std::wstring comment = wide(label);
    std::string blob = secret;
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = targetName.data();
    credential.Comment = comment.data();
    credential.CredentialBlobSize = DWORD(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = userName.data();
    BOOL ok = CredWriteW(&credential, 0);
    DWORD code = GetLastError();
    SecureZeroMemory(blob.data(), blob.size());
    if (!ok) throw credentialError(code);
}

void CredentialSecretStore::deleteSecret(const std::string& account) {
    if (!CredDeleteW(wide(target(account)).c_str(), CRED_TYPE_GENERIC, 0)) {
        DWORD code = GetLastError();
        if (code != ERROR_NOT_FOUND) throw credentialError(code);
    }
}

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
