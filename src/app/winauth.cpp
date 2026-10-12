#include "winauth.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <QWidget>

#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <wincred.h>
#include <winstring.h>

#include <string>

#include "util/log.h"

#ifndef CRED_PACK_PROTECTED_CREDENTIALS
#define CRED_PACK_PROTECTED_CREDENTIALS 0x1
#endif

namespace WinAuth {

namespace {

// The WinRT interfaces of Windows.Security.Credentials.UI.UserConsentVerifier
// the check needs, declared by hand as MinGW has no header for them.

const IID iidAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID iidVerifierStatics = {0xAF4F3F91, 0x564C, 0x4DDC, {0xB8, 0xB8, 0x97, 0x34, 0x47, 0x62, 0x7C, 0x8C}};
const IID iidVerifierInterop = {0x39E050C3, 0x4E74, 0x441A, {0x8D, 0xC0, 0xB8, 0x11, 0x04, 0xDF, 0x94, 0x9C}};

struct IAsyncInfoABI : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* code) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

/// IAsyncOperation of an enumeration, whose result is an INT32.
struct IAsyncOperationEnumABI : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(INT32* result) = 0;
};

struct IUserConsentVerifierStaticsABI : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CheckAvailabilityAsync(IAsyncOperationEnumABI** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestVerificationAsync(HSTRING message, IAsyncOperationEnumABI** result) = 0;
};

struct IUserConsentVerifierInteropABI : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestVerificationForWindowAsync(HWND window, HSTRING message, REFIID riid, void** operation) = 0;
};

/// UserConsentVerifierAvailability.Available and
/// UserConsentVerificationResult.Verified.
constexpr INT32 available = 0;
constexpr INT32 verified = 0;

enum class Outcome { confirmed, refused, unavailable };

template <class T>
struct Releaser {
    T* p = nullptr;
    ~Releaser() {
        if (p) p->Release();
    }
};

class HString {
public:
    explicit HString(const std::wstring& text) { WindowsCreateString(text.c_str(), UINT32(text.size()), &h_); }
    ~HString() {
        if (h_) WindowsDeleteString(h_);
    }
    HSTRING get() const { return h_; }

private:
    HSTRING h_ = nullptr;
};

/// Waits for the operation to complete while the window keeps painting,
/// and returns its INT32 result.
bool await(IAsyncOperationEnumABI* operation, INT32* result) {
    Releaser<IAsyncInfoABI> info;
    if (FAILED(operation->QueryInterface(iidAsyncInfo, reinterpret_cast<void**>(&info.p)))) return false;
    int status = 0;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (FAILED(info.p->get_Status(&status)) || status != 0) loop.quit();
    });
    if (SUCCEEDED(info.p->get_Status(&status)) && status == 0) {
        poll.start(50);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return status == 1 && SUCCEEDED(operation->GetResults(result));
}

Outcome windowsHello(HWND window, const QString& reason) {
    HString name(L"Windows.Security.Credentials.UI.UserConsentVerifier");
    Releaser<IUserConsentVerifierStaticsABI> statics;
    if (FAILED(RoGetActivationFactory(name.get(), iidVerifierStatics, reinterpret_cast<void**>(&statics.p)))) return Outcome::unavailable;
    Releaser<IAsyncOperationEnumABI> check;
    INT32 availability = -1;
    if (FAILED(statics.p->CheckAvailabilityAsync(&check.p)) || !await(check.p, &availability) || availability != available)
        return Outcome::unavailable;
    HString message(reason.toStdWString());
    Releaser<IAsyncOperationEnumABI> request;
    Releaser<IUserConsentVerifierInteropABI> interop;
    QUuid iid(pinterfaceIID("pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};enum(Windows.Security.Credentials.UI.UserConsentVerificationResult;i4))"));
    bool asked = SUCCEEDED(statics.p->QueryInterface(iidVerifierInterop, reinterpret_cast<void**>(&interop.p))) &&
                 SUCCEEDED(interop.p->RequestVerificationForWindowAsync(window, message.get(), GUID(iid), reinterpret_cast<void**>(&request.p)));
    if (!asked && FAILED(statics.p->RequestVerificationAsync(message.get(), &request.p))) return Outcome::unavailable;
    INT32 result = -1;
    if (!await(request.p, &result)) return Outcome::unavailable;
    return result == verified ? Outcome::confirmed : Outcome::refused;
}

Outcome windowsPassword(HWND window, const QString& reason) {
    std::wstring message = reason.toStdWString();
    CREDUI_INFOW info{};
    info.cbSize = sizeof info;
    info.hwndParent = window;
    info.pszCaptionText = L"Twotbit";
    info.pszMessageText = message.c_str();
    DWORD authError = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        ULONG package = 0;
        LPVOID out = nullptr;
        ULONG outSize = 0;
        BOOL save = FALSE;
        DWORD rc = CredUIPromptForWindowsCredentialsW(&info, authError, &package, nullptr, 0, &out, &outSize, &save, CREDUIWIN_ENUMERATE_CURRENT_USER);
        if (rc == ERROR_CANCELLED) return Outcome::refused;
        if (rc != ERROR_SUCCESS) return Outcome::unavailable;
        WCHAR user[CREDUI_MAX_USERNAME_LENGTH + 1] = {}, domain[CREDUI_MAX_DOMAIN_TARGET_LENGTH + 1] = {}, pass[CREDUI_MAX_PASSWORD_LENGTH + 1] = {};
        DWORD userLength = CREDUI_MAX_USERNAME_LENGTH, domainLength = CREDUI_MAX_DOMAIN_TARGET_LENGTH, passLength = CREDUI_MAX_PASSWORD_LENGTH;
        BOOL unpacked = CredUnPackAuthenticationBufferW(CRED_PACK_PROTECTED_CREDENTIALS, out, outSize, user, &userLength, domain, &domainLength, pass,
                                                        &passLength);
        SecureZeroMemory(out, outSize);
        CoTaskMemFree(out);
        if (!unpacked) return Outcome::unavailable;
        CRED_PROTECTION_TYPE protection;
        if (CredIsProtectedW(pass, &protection) && protection != CredUnprotected) {
            WCHAR plain[CREDUI_MAX_PASSWORD_LENGTH + 1] = {};
            DWORD plainLength = CREDUI_MAX_PASSWORD_LENGTH;
            if (CredUnprotectW(FALSE, pass, passLength, plain, &plainLength)) {
                SecureZeroMemory(pass, sizeof pass);
                wcsncpy(pass, plain, CREDUI_MAX_PASSWORD_LENGTH);
                SecureZeroMemory(plain, sizeof plain);
            }
        }
        std::wstring name(user), realm(domain);
        if (realm.empty()) {
            size_t slash = name.find(L'\\');
            if (slash != std::wstring::npos) {
                realm = name.substr(0, slash);
                name = name.substr(slash + 1);
            }
        }
        bool empty = pass[0] == 0;
        HANDLE token = nullptr;
        BOOL ok = LogonUserW(name.c_str(), realm.empty() ? L"." : realm.c_str(), pass, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token);
        DWORD error = GetLastError();
        SecureZeroMemory(pass, sizeof pass);
        if (ok) {
            CloseHandle(token);
            return Outcome::confirmed;
        }
        if (error == ERROR_LOGON_FAILURE) {
            authError = error;
            continue;
        }
        // An account without a password may not log on but is the owner's;
        // other accounts Windows cannot check this way are not refused.
        if (error == ERROR_ACCOUNT_RESTRICTION && empty) return Outcome::confirmed;
        tb::Log::info("settings: password check unavailable, error " + std::to_string(error));
        return Outcome::unavailable;
    }
    return Outcome::refused;
}

} // namespace

QString pinterfaceIID(const QString& signature) {
    static const unsigned char ns[16] = {0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0, 0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee};
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    sha1.addData(QByteArrayView(reinterpret_cast<const char*>(ns), 16));
    sha1.addData(signature.toUtf8());
    QByteArray h = sha1.result();
    auto byte = [&](int i) { return uint32_t(uint8_t(h[i])); };
    uint32_t data1 = byte(0) << 24 | byte(1) << 16 | byte(2) << 8 | byte(3);
    uint16_t data2 = uint16_t(byte(4) << 8 | byte(5));
    uint16_t data3 = uint16_t(((byte(6) << 8 | byte(7)) & 0x0fff) | 0x5000);
    QUuid uuid(data1, data2, data3, uchar((byte(8) & 0x3f) | 0x80), uchar(byte(9)), uchar(byte(10)), uchar(byte(11)), uchar(byte(12)), uchar(byte(13)),
               uchar(byte(14)), uchar(byte(15)));
    return uuid.toString(QUuid::WithBraces);
}

bool confirmOwner(QWidget* parent, const QString& reason) {
    QWidget* top = parent ? parent->window() : nullptr;
    HWND window = top ? reinterpret_cast<HWND>(top->winId()) : nullptr;
    if (top) top->setEnabled(false);
    Outcome outcome = windowsHello(window, reason);
    if (outcome == Outcome::unavailable) outcome = windowsPassword(window, reason);
    if (top) {
        top->setEnabled(true);
        top->activateWindow();
    }
    return outcome != Outcome::refused;
}

} // namespace WinAuth
