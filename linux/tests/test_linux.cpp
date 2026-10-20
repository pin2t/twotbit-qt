#include "test_linux.h"

#include "crypto/hashes.h"
#include "storage/secrets.h"

namespace tb::test {

void LinuxTests::keyring() {
    // A secret round-trips through the Secret Service keyring under a
    // service name of its own, and is deleted again. TWOTBIT_SKIP_KEYRING
    // skips it where no keyring runs, as on a build machine without a
    // desktop session.
    if (qEnvironmentVariableIsSet("TWOTBIT_SKIP_KEYRING")) QSKIP("TWOTBIT_SKIP_KEYRING is set");
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
