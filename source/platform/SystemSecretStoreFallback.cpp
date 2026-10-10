#include "platform/SecretStore.h"

namespace mm::platform {

// No keychain on this system yet (SPEC 8.5: no clear-text fallback): the key stays in memory for the session.
std::unique_ptr<ISecretStore> makeSystemSecretStore(const std::string&) {
    return std::make_unique<MemorySecretStore>();
}

} // namespace mm::platform
