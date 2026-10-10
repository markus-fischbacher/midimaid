#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace mm::platform {

/// The place where API keys are kept (SPEC 8.5, CLAUDE.md: security). Keys live only in the keychain of the operating
/// system, never in the plugin state, a log, an error message or a test file. No clear-text fallback: where there is
/// no keychain, the key lives in memory for the session only. `account` is the provider id.
/// The calls block (the keychain talks to a system service): call them from a background thread.
class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    /// The stored secret, or nothing when there is none or it cannot be read.
    virtual std::optional<std::string> get(const std::string& account) = 0;
    /// Stores or replaces the secret. False when it could not be stored.
    virtual bool set(const std::string& account, const std::string& secret) = 0;
    /// Removes the secret. True when there is none afterwards.
    virtual bool remove(const std::string& account) = 0;
    /// True when the secret survives the session (a real keychain); false for the memory store.
    virtual bool persistent() const = 0;
};

/// Keeps the secrets in memory: for the tests and for systems without a keychain (the key has to be entered again in
/// the next session, SPEC 8.5).
class MemorySecretStore : public ISecretStore {
public:
    std::optional<std::string> get(const std::string& account) override;
    bool set(const std::string& account, const std::string& secret) override;
    bool remove(const std::string& account) override;
    bool persistent() const override { return false; }

private:
    std::mutex mutex_;
    std::map<std::string, std::string> secrets_;
};

/// The keychain of this system under the service name `service`; the memory store where there is none.
std::unique_ptr<ISecretStore> makeSystemSecretStore(const std::string& service = "com.klirrwerk.midimaid");

} // namespace mm::platform
