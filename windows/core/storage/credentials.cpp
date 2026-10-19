#include "storage/secrets.h"

#include <windows.h>
#include <wincred.h>

#include "util/strings.h"

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

const char* CredentialSecretStore::storeName() { return "Windows Credential Manager"; }

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

} // namespace tb
