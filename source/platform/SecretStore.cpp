#include "platform/SecretStore.h"

namespace mm::platform {

std::optional<std::string> MemorySecretStore::get(const std::string& account) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = secrets_.find(account);
    if (it == secrets_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool MemorySecretStore::set(const std::string& account, const std::string& secret) {
    const std::lock_guard<std::mutex> lock(mutex_);
    secrets_[account] = secret;
    return true;
}

bool MemorySecretStore::remove(const std::string& account) {
    const std::lock_guard<std::mutex> lock(mutex_);
    secrets_.erase(account);
    return true;
}

} // namespace mm::platform
