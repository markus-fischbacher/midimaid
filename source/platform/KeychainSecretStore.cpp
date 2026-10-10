#include "platform/SecretStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace mm::platform {

namespace {

/// Owns a CoreFoundation object.
template <typename T> class Cf {
public:
    explicit Cf(T object = nullptr) : object_(object) {}
    ~Cf() {
        if (object_ != nullptr) {
            CFRelease(object_);
        }
    }
    Cf(const Cf&) = delete;
    Cf& operator=(const Cf&) = delete;
    Cf(Cf&& other) noexcept : object_(other.object_) { other.object_ = nullptr; }
    Cf& operator=(Cf&&) = delete;
    T get() const { return object_; }

private:
    T object_;
};

Cf<CFStringRef> cfString(const std::string& text) {
    return Cf<CFStringRef>(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()),
                                                   static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
}

class KeychainSecretStore : public ISecretStore {
public:
    explicit KeychainSecretStore(std::string service) : service_(std::move(service)) {}

    std::optional<std::string> get(const std::string& account) override {
        const auto query = baseQuery(account);
        if (query.get() == nullptr) {
            return std::nullopt;
        }
        CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef found = nullptr;
        if (SecItemCopyMatching(query.get(), &found) != errSecSuccess || found == nullptr) {
            return std::nullopt;
        }
        const Cf<CFDataRef> data(static_cast<CFDataRef>(found));
        return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data.get())),
                           static_cast<size_t>(CFDataGetLength(data.get())));
    }

    bool set(const std::string& account, const std::string& secret) override {
        const auto query = baseQuery(account);
        if (query.get() == nullptr) {
            return false;
        }
        const Cf<CFDataRef> data(
            CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(secret.data()), static_cast<CFIndex>(secret.size())));
        if (data.get() == nullptr) {
            return false;
        }
        const Cf<CFMutableDictionaryRef> update(
            CFDictionaryCreateMutable(nullptr, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFDictionarySetValue(update.get(), kSecValueData, data.get());
        auto status = SecItemUpdate(query.get(), update.get());
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query.get(), kSecValueData, data.get());
            // Only when this computer is unlocked, and never copied to other devices through iCloud.
            CFDictionarySetValue(query.get(), kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
            status = SecItemAdd(query.get(), nullptr);
        }
        return status == errSecSuccess;
    }

    bool remove(const std::string& account) override {
        const auto query = baseQuery(account);
        if (query.get() == nullptr) {
            return false;
        }
        const auto status = SecItemDelete(query.get());
        return status == errSecSuccess || status == errSecItemNotFound;
    }

    bool persistent() const override { return true; }

private:
    Cf<CFMutableDictionaryRef> baseQuery(const std::string& account) const {
        Cf<CFMutableDictionaryRef> query(
            CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        const auto service = cfString(service_);
        const auto name = cfString(account);
        if (query.get() == nullptr || service.get() == nullptr || name.get() == nullptr) {
            return Cf<CFMutableDictionaryRef>();
        }
        CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query.get(), kSecAttrService, service.get());
        CFDictionarySetValue(query.get(), kSecAttrAccount, name.get());
        return query;
    }

    std::string service_;
};

} // namespace

std::unique_ptr<ISecretStore> makeSystemSecretStore(const std::string& service) {
    return std::make_unique<KeychainSecretStore>(service);
}

} // namespace mm::platform
