#include "test_windows.h"

#include "crypto/hashes.h"
#include "storage/secrets.h"
#include "winauth.h"

namespace tb::test {

void WindowsTests::credentialManager() {
    // A secret round-trips through the Windows Credential Manager under a
    // service name of its own, and is deleted again.
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

void WindowsTests::pinterfaceIID() {
    // IAsyncOperation<UserConsentVerificationResult>, whose IID the Windows
    // SDK headers list.
    QString iid = WinAuth::pinterfaceIID(
        "pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};enum(Windows.Security.Credentials.UI.UserConsentVerificationResult;i4))");
    EXPECT_EQ(iid.toStdString(), std::string("{fd596ffd-2318-558f-9dbe-d21df43764a5}"), "IID");
}

} // namespace tb::test
