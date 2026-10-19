#include "test_macos.h"

#include "crypto/hashes.h"
#include "storage/secrets.h"

namespace tb::test {

void MacTests::keychain() {
    // A secret round-trips through the login keychain under a service name
    // of its own, and is deleted again. TWOTBIT_SKIP_KEYCHAIN skips it where
    // there is no unlocked login keychain, as on a build machine.
    if (qEnvironmentVariableIsSet("TWOTBIT_SKIP_KEYCHAIN")) QSKIP("TWOTBIT_SKIP_KEYCHAIN is set");
    CredentialSecretStore secrets("com.pin2t.twotbit.test-" + toHex(Hashes::randomBytes(4)));
    std::string account = "vpub-test-" + toHex(Hashes::randomBytes(4));
    EXPECT(!secrets.secret(account), "nothing stored yet");
    secrets.setSecret(vectorMnemonic, account, "Twotbit test credential");
    EXPECT(secrets.secret(account) == std::optional<std::string>(vectorMnemonic), "stored");
    secrets.setSecret("other words", account, "Twotbit test credential");
    EXPECT(secrets.secret(account) == std::optional<std::string>("other words"), "replaced");
    secrets.deleteSecret(account);
    EXPECT(!secrets.secret(account), "deleted");
    secrets.deleteSecret(account);
}

} // namespace tb::test
