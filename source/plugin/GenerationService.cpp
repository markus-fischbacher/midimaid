#include "plugin/GenerationService.h"

#include "core/PatternGenerator.h"

namespace mm::plugin {

/// Shared with the callbacks that are still queued on the message thread when the service goes away. Only the message
/// thread touches it.
struct GenerationService::State {
    Delivery delivery;
    bool alive = true;
    int pending = 0;     ///< jobs requested whose delivery has not run yet
    uint64_t latest = 0; ///< number of the newest request
};

namespace {

mm::core::GenerationRequest requestFor(const GenerationJob& job, const std::atomic<bool>* cancel) {
    mm::core::GenerationRequest request;
    request.lengthBars = job.settings.lengthBars;
    request.seed = job.seed;
    request.settings.energyPct = job.settings.energyPct;
    request.settings.creativityPct = job.settings.creativityPct;
    request.root = job.settings.root;
    request.scaleId = job.settings.scaleId;
    request.cancel = cancel;
    return request;
}

} // namespace

GenerationService::GenerationService(const mm::core::StyleLibrary& styles, Delivery delivery)
    : styles_(styles), state_(std::make_shared<State>()) {
    state_->delivery = std::move(delivery);
}

GenerationService::~GenerationService() {
    if (cancel_) {
        cancel_->store(true);
    }
    pool_.removeAllJobs(true, 10000); // cancel and wait: the worker is quick once the flag is set
    state_->alive = false;
}

bool GenerationService::busy() const {
    return state_->pending > 0;
}

void GenerationService::request(const GenerationJob& job) {
    if (cancel_) {
        cancel_->store(true); // the running job ends at its next candidate and delivers nothing
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const uint64_t number = ++state_->latest;
    ++state_->pending;

    const auto* style = styles_.findOrFallback(job.settings.styleId);
    pool_.addJob([state = state_, cancel = cancel_, job, style, number] {
        std::optional<mm::core::Pattern> pattern;
        if (style != nullptr && !cancel->load()) {
            auto result = mm::core::generatePattern(*style, requestFor(job, cancel.get()));
            if (result.success && !cancel->load()) {
                pattern = std::move(result.pattern);
            }
        }
        const bool cancelled = cancel->load();
        juce::MessageManager::callAsync([state, job, number, cancelled, pattern = std::move(pattern)]() mutable {
            if (!state->alive) {
                return;
            }
            --state->pending;
            if (cancelled || number != state->latest) {
                return; // superseded by a newer request
            }
            state->delivery(job, std::move(pattern));
        });
    });
}

} // namespace mm::plugin
