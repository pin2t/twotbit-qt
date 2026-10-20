#include "storage/secrets.h"

#include <libsecret/secret.h>

namespace tb {

namespace {

/// The schema of the recovery phrases' items: the service and the account
/// kept apart, as the Keychain keeps them.
const SecretSchema* schema() {
    static const SecretSchema s = {
        "com.pin2t.twotbit.RecoveryPhrase",
        SECRET_SCHEMA_NONE,
        {
            {"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
    };
    return &s;
}

CredentialError secretError(GError* error) {
    std::string text = error->message ? error->message : "error";
    unsigned long code = static_cast<unsigned long>(error->code);
    g_error_free(error);
    return CredentialError("Secret Service: " + text + " (" + std::to_string(code) + ")", code);
}

} // namespace

const char* CredentialSecretStore::storeName() { return "keyring"; }

// The items go to the default collection of the Secret Service, the login
// keyring that GNOME Keyring unlocks as the user signs in, or KWallet on KDE.
// The calls wait for the service, and for its unlock prompt when the keyring
// is locked, so they run off the GUI thread as the app's are.

std::optional<std::string> CredentialSecretStore::secret(const std::string& account) {
    GError* error = nullptr;
    gchar* found = secret_password_lookup_nonpageable_sync(schema(), nullptr, &error, "service", service_.c_str(), "account", account.c_str(), nullptr);
    if (error) throw secretError(error);
    if (!found) return std::nullopt;
    std::string out(found);
    secret_password_free(found);
    return out;
}

void CredentialSecretStore::setSecret(const std::string& secret, const std::string& account, const std::string& label) {
    GError* error = nullptr;
    // An item with the same attributes is replaced.
    secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, label.c_str(), secret.c_str(), nullptr, &error, "service", service_.c_str(),
                               "account", account.c_str(), nullptr);
    if (error) throw secretError(error);
}

void CredentialSecretStore::deleteSecret(const std::string& account) {
    GError* error = nullptr;
    // False without an error when there was nothing to delete.
    secret_password_clear_sync(schema(), nullptr, &error, "service", service_.c_str(), "account", account.c_str(), nullptr);
    if (error) throw secretError(error);
}

} // namespace tb
