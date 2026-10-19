#include "storage/secrets.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <cstring>

namespace tb {

namespace {

/// CFRef releases a Core Foundation object it owns.
template <class T>
class CFRef {
public:
    explicit CFRef(T ref = nullptr) : ref_(ref) {}
    ~CFRef() {
        if (ref_) CFRelease(ref_);
    }
    CFRef(const CFRef&) = delete;
    CFRef& operator=(const CFRef&) = delete;
    T get() const { return ref_; }
    T* out() { return &ref_; }

private:
    T ref_;
};

CFStringRef string(const std::string& utf8) {
    return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(utf8.data()), CFIndex(utf8.size()), kCFStringEncodingUTF8, false);
}

std::string utf8(CFStringRef text) {
    if (!text) return {};
    CFIndex length = CFStringGetLength(text);
    CFIndex size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(size_t(size), '\0');
    if (!CFStringGetCString(text, out.data(), size, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

CredentialError keychainError(OSStatus status) {
    CFRef<CFStringRef> message(SecCopyErrorMessageString(status, nullptr));
    std::string text = utf8(message.get());
    return CredentialError("Keychain: " + (text.empty() ? std::string("error") : text) + " (" + std::to_string(status) + ")",
                           static_cast<unsigned long>(status));
}

/// The query of the generic password of the account in the service, as the
/// Swift app's KeychainSecretStore makes it, so both find the same items.
CFMutableDictionaryRef query(const std::string& service, const std::string& account) {
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRef<CFStringRef> s(string(service)), a(string(account));
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, s.get());
    CFDictionarySetValue(q, kSecAttrAccount, a.get());
    return q;
}

} // namespace

const char* CredentialSecretStore::storeName() { return "Keychain"; }

std::optional<std::string> CredentialSecretStore::secret(const std::string& account) {
    CFRef<CFMutableDictionaryRef> q(query(service_, account));
    CFDictionarySetValue(q.get(), kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q.get(), kSecMatchLimit, kSecMatchLimitOne);
    CFRef<CFTypeRef> item;
    OSStatus status = SecItemCopyMatching(q.get(), item.out());
    if (status == errSecItemNotFound) return std::nullopt;
    if (status != errSecSuccess) throw keychainError(status);
    if (!item.get() || CFGetTypeID(item.get()) != CFDataGetTypeID()) throw keychainError(errSecDecode);
    auto data = static_cast<CFDataRef>(item.get());
    const char* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(data));
    return std::string(bytes, bytes + CFDataGetLength(data));
}

void CredentialSecretStore::setSecret(const std::string& secret, const std::string& account, const std::string& label) {
    CFRef<CFDataRef> data(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(secret.data()), CFIndex(secret.size())));
    CFRef<CFMutableDictionaryRef> q(query(service_, account));
    CFRef<CFMutableDictionaryRef> update(CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    CFDictionarySetValue(update.get(), kSecValueData, data.get());
    OSStatus status = SecItemUpdate(q.get(), update.get());
    if (status == errSecSuccess) return;
    if (status != errSecItemNotFound) throw keychainError(status);
    CFRef<CFStringRef> name(string(label));
    CFDictionarySetValue(q.get(), kSecValueData, data.get());
    CFDictionarySetValue(q.get(), kSecAttrLabel, name.get());
    CFDictionarySetValue(q.get(), kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
    status = SecItemAdd(q.get(), nullptr);
    if (status != errSecSuccess) throw keychainError(status);
}

void CredentialSecretStore::deleteSecret(const std::string& account) {
    CFRef<CFMutableDictionaryRef> q(query(service_, account));
    OSStatus status = SecItemDelete(q.get());
    if (status != errSecSuccess && status != errSecItemNotFound) throw keychainError(status);
}

} // namespace tb
