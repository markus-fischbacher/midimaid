#include "plugin/AiBackend.h"

namespace mm::plugin {

AiBackend& AiBackend::instance() {
    static AiBackend backend;
    return backend;
}

void AiBackend::set(AiBackendSettings settings) {
    const std::lock_guard<std::mutex> lock(mutex_);
    settings_ = std::move(settings);
}

AiBackendSettings AiBackend::get() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

bool AiBackend::active() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return settings_.provider != nullptr;
}

ScopedAiBackend::ScopedAiBackend(AiBackendSettings settings) : previous_(AiBackend::instance().get()) {
    AiBackend::instance().set(std::move(settings));
}

ScopedAiBackend::~ScopedAiBackend() {
    AiBackend::instance().set(std::move(previous_));
}

} // namespace mm::plugin
