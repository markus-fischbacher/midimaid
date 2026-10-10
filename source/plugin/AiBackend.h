#pragma once

#include "ai/AiTypes.h"

#include <memory>
#include <mutex>
#include <string>

namespace mm::plugin {

/// The AI provider that "Generate" uses, shared by every instance in the process (SPEC 7.1). Without a provider the
/// plugin generates offline and never touches the network (CLAUDE.md: security). Message thread sets it, any thread
/// may read a copy; never the audio thread.
struct AiBackendSettings {
    std::shared_ptr<mm::ai::IAiProvider> provider; ///< empty: offline only
    std::string model;
    int timeoutSeconds = 30;
    int maxTokens = 4096;
};

class AiBackend {
public:
    static AiBackend& instance();

    void set(AiBackendSettings settings);
    AiBackendSettings get() const;
    bool active() const;

private:
    mutable std::mutex mutex_;
    AiBackendSettings settings_;
};

/// RAII helper for tests: sets the backend and puts the previous one back.
class ScopedAiBackend {
public:
    explicit ScopedAiBackend(AiBackendSettings settings);
    ~ScopedAiBackend();
    ScopedAiBackend(const ScopedAiBackend&) = delete;
    ScopedAiBackend& operator=(const ScopedAiBackend&) = delete;

private:
    AiBackendSettings previous_;
};

} // namespace mm::plugin
