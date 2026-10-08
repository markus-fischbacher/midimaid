#pragma once

#include "core/InstanceSettings.h"
#include "core/Pattern.h"
#include "core/StyleLibrary.h"

#include <atomic>
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>
#include <optional>

namespace mm::plugin {

/// What one generation needs. Copied into the job; the worker touches nothing of the instance.
struct GenerationJob {
    mm::core::GenerationSettings settings;
    uint64_t seed = 1;
    int slot = 0; ///< 0-based target slot, kept from the moment of the request
};

/// Runs generations on a background thread (CLAUDE.md: threading). One worker; a new request cancels the running
/// one, so only the latest request delivers. The result is handed over on the message thread. The destructor cancels
/// and waits, and nothing is delivered afterwards.
class GenerationService {
public:
    /// Called on the message thread with the job and the pattern, or no pattern when nothing valid came out.
    using Delivery = std::function<void(const GenerationJob&, std::optional<mm::core::Pattern>)>;

    GenerationService(const mm::core::StyleLibrary& styles, Delivery delivery);
    ~GenerationService();

    /// Message thread. Cancels a running job and starts this one.
    void request(const GenerationJob& job);
    /// True from the request until its result was delivered (or dropped). Message thread.
    bool busy() const;

private:
    struct State;
    class Worker;

    const mm::core::StyleLibrary& styles_;
    std::shared_ptr<State> state_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    juce::ThreadPool pool_{1};
};

} // namespace mm::plugin
