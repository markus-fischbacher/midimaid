#pragma once

#include "ai/AiGeneration.h"
#include "core/InstanceSettings.h"
#include "core/Pattern.h"
#include "core/StyleLibrary.h"

#include <atomic>
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>
#include <optional>
#include <string>

namespace mm::plugin {

/// What one generation needs. Copied into the job; the worker touches nothing of the instance.
struct GenerationJob {
    mm::core::GenerationSettings settings;
    uint64_t seed = 1;
    int slot = 0; ///< 0-based target slot, kept from the moment of the request
    /// The pattern of the slot when a voice of it is locked: the locked voices, the harmony and the length stay
    /// (SPEC 3.5, `generatePatternAroundLocks`). Empty: a new pattern.
    std::optional<mm::core::Pattern> lockedFrom;
    /// With a provider the job asks the AI (SPEC 7); without, the offline generators run. Fixed at the request, so
    /// a later change of the backend does not touch a job that is running or waits for a retry.
    std::shared_ptr<mm::ai::IAiProvider> provider;
    const mm::ai::PromptTemplates* templates = nullptr;
    std::string model;
    int timeoutSeconds = 30;
    int maxTokens = 4096;
};

/// How a job ended, for the editor (SPEC 7.9). Without AI only `usedAi == false` is meaningful.
struct GenerationReport {
    bool usedAi = false;
    mm::ai::AiOutcome outcome = mm::ai::AiOutcome::Success; ///< AI jobs: how the pipeline ended
    mm::ai::AiStatus status = mm::ai::AiStatus::Ok;         ///< `ProviderError`: the provider's status
    int httpStatus = 0;
    std::string error; ///< a readable cause; never contains a key
    bool repaired = false;
    bool belowMinScore = false;
    int score = 0;
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

    /// Called on the message thread right before the delivery, with how the job ended.
    using ReportHandler = std::function<void(const GenerationJob&, const GenerationReport&)>;
    void setReportHandler(ReportHandler handler);

    /// Message thread. Cancels a running job and starts this one.
    void request(const GenerationJob& job);
    /// Message thread. Cancels a running job: nothing is delivered for it.
    void cancel();
    /// True from the request until its result was delivered (or dropped). Message thread.
    bool busy() const;

private:
    struct State;
    class Worker;

    const mm::core::StyleLibrary& styles_;
    std::shared_ptr<State> state_;
    mm::ai::CancellationToken cancel_;
    juce::ThreadPool pool_{1};
};

} // namespace mm::plugin
