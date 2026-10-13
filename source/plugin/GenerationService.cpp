#include "plugin/GenerationService.h"

#include "ai/AiRefine.h"
#include "core/PatternGenerator.h"
#include "plugin/AiConnection.h"
#include "plugin/AppLog.h"

#include <chrono>

namespace mm::plugin {

/// Shared with the callbacks that are still queued on the message thread when the service goes away. Only the message
/// thread touches it.
struct GenerationService::State {
    Delivery delivery;
    ReportHandler report;
    bool alive = true;
    int pending = 0;     ///< jobs requested whose delivery has not run yet
    uint64_t latest = 0; ///< number of the newest request
};

namespace {

/// Only a cloud provider costs money (SPEC 7.5): a local one shows the tokens only.
bool providerIsCloud(const std::string& id) {
    const auto models = AiConnection::instance().models(); // a copy: `find` points into it
    const auto* entry = models.find(id);
    return entry != nullptr && entry->cloud;
}

mm::core::GenerationRequest requestFor(const GenerationJob& job, const std::atomic<bool>* cancel) {
    mm::core::GenerationRequest request;
    request.lengthBars = job.settings.lengthBars;
    request.seed = job.seed;
    request.settings.energyPct = job.settings.energyPct;
    request.settings.creativityPct = job.settings.creativityPct;
    request.settings.chromaticPct = job.settings.chromaticPct.value_or(-1);
    request.settings.personalPct = job.settings.personalPct;
    if (job.references.profile.valid) {
        request.profile = job.references.profile;
    }
    if (!job.settings.extraVoices.empty()) { // bass and melody first, then the extra roles (D-250)
        request.voices = {mm::core::VoiceRole::Bass, mm::core::VoiceRole::Melody};
        request.voices.insert(request.voices.end(), job.settings.extraVoices.begin(), job.settings.extraVoices.end());
    }
    request.root = job.settings.root;
    request.scaleId = job.settings.scaleId;
    request.referenceSet = job.references.entries;
    request.history = job.history;
    request.personalProgressions = job.references.progressions;
    request.referenceSetId = job.references.setId;
    request.cancel = cancel;
    return request;
}

const char* outcomeName(mm::ai::AiOutcome outcome) {
    switch (outcome) {
    case mm::ai::AiOutcome::Success:
        return "ok";
    case mm::ai::AiOutcome::ProviderError:
        return "provider-error";
    case mm::ai::AiOutcome::Invalid:
        return "invalid-answer";
    case mm::ai::AiOutcome::Cancelled:
        return "cancelled";
    }
    return "?";
}

/// The log line of one AI request (SPEC 10): metadata only. The texts follow only with "Prompts protokollieren".
void logAiResult(const GenerationJob& job, const mm::ai::AiGenerateResult& result, long long milliseconds) {
    auto& log = AppLog::instance();
    juce::String line = juce::String(job.refine ? "refine" : "generate") +
                        " provider=" + juce::String(job.provider->info().id) + " model=" + juce::String(job.model) +
                        " outcome=" + outcomeName(result.outcome) + " duration=" + juce::String(milliseconds) + "ms" +
                        " requests=" + juce::String(result.requests) + " tokens=" + juce::String(result.inputTokens) +
                        "/" + juce::String(result.outputTokens);
    if (result.outcome == mm::ai::AiOutcome::Success) {
        line += " score=" + juce::String(result.score) + (result.belowMinScore ? " below-min" : "") +
                (result.copyFlagged ? " copy-flagged" : "") +
                (result.repaired ? " repaired" : "") +
                " dropped=" + juce::String(static_cast<int>(result.droppedNotes)) +
                " clamped=" + juce::String(static_cast<int>(result.clampedValues));
    } else if (result.outcome == mm::ai::AiOutcome::ProviderError) {
        line += " status=" + juce::String(static_cast<int>(result.status)) +
                " http=" + juce::String(result.httpStatus) + " message=" + juce::String(result.error);
    } else if (result.outcome == mm::ai::AiOutcome::Invalid) {
        line += " schema-error=" + juce::String(result.error);
    }
    log.write(result.outcome == mm::ai::AiOutcome::Success || result.outcome == mm::ai::AiOutcome::Cancelled
                  ? LogLevel::Info
                  : LogLevel::Warning,
              "ai", line);
    log.writeContent("ai", "system prompt", juce::String::fromUTF8(result.systemPrompt.c_str()));
    log.writeContent("ai", "user prompt", juce::String::fromUTF8(result.userPrompt.c_str()));
    log.writeContent("ai", "answer", juce::String::fromUTF8(result.lastAnswer.c_str()));
}

} // namespace

GenerationService::GenerationService(const mm::core::StyleLibrary& styles, Delivery delivery)
    : styles_(styles), state_(std::make_shared<State>()) {
    state_->delivery = std::move(delivery);
}

GenerationService::~GenerationService() {
    cancel_.cancel();
    pool_.removeAllJobs(true, 10000); // cancel and wait: the worker is quick once the flag is set; a provider ends its
                                      // request when the token is cancelled
    state_->alive = false;
}

bool GenerationService::busy() const {
    return state_->pending > 0;
}

void GenerationService::setReportHandler(ReportHandler handler) {
    state_->report = std::move(handler);
}

void GenerationService::cancel() {
    cancel_.cancel();
}

void GenerationService::request(const GenerationJob& job) {
    cancel_.cancel(); // the running job ends at its next step and delivers nothing
    cancel_ = mm::ai::CancellationToken();
    const uint64_t number = ++state_->latest;
    ++state_->pending;

    const auto* style = styles_.findOrFallback(job.settings.styleId);
    pool_.addJob([state = state_, cancel = cancel_, job, style, number] {
        std::optional<mm::core::Pattern> pattern;
        GenerationReport report;
        if (style != nullptr && !cancel.cancelled()) {
            // the AI is told the chromatic share the musician chose (SPEC 3.8), the generator works with it itself
            const mm::core::StyleProfile effective =
                mm::core::withChromatic(*style, job.settings.chromaticPct.value_or(-1));
            if (job.provider != nullptr && job.templates != nullptr) {
                report.usedAi = true;
                const auto createdUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count();
                const auto started = std::chrono::steady_clock::now();
                mm::ai::AiGenerateResult result;
                if (job.refine) {
                    mm::ai::AiRefineInput input;
                    input.style = &effective;
                    input.templates = job.templates;
                    input.pattern = job.refine->pattern;
                    input.instruction = job.refine->instruction;
                    input.voice = job.refine->voice;
                    input.phrase = job.refine->phrase;
                    input.energyPct = job.settings.energyPct;
                    input.creativityPct = job.settings.creativityPct;
                    input.model = job.model;
                    input.timeoutSeconds = job.timeoutSeconds;
                    input.maxTokens = job.maxTokens;
                    input.createdUnixMs = createdUnixMs;
                    input.referenceSet = job.references.entries;
                    result = mm::ai::refineWithAi(*job.provider, input, cancel);
                } else {
                    mm::ai::AiGenerateInput input;
                    input.style = &effective;
                    input.templates = job.templates;
                    input.prompt.lengthBars = job.settings.lengthBars;
                    input.prompt.root = job.settings.root;
                    input.prompt.scaleId = job.settings.scaleId;
                    input.prompt.energyPct = job.settings.energyPct;
                    input.prompt.creativityPct = job.settings.creativityPct;
                    input.prompt.request = job.settings.prompt;
                    input.model = job.model;
                    input.timeoutSeconds = job.timeoutSeconds;
                    input.maxTokens = job.maxTokens;
                    input.lockedFrom = job.lockedFrom;
                    input.createdUnixMs = createdUnixMs;
                    input.examples = job.examples;
                    if (job.settings.personalPct > 0) { // numbers only, so every provider may get it (D-248)
                        input.prompt.tasteSummary = mm::ai::tasteSummaryText(job.references.profile);
                    }
                    input.referenceSet = job.references.entries;
                    input.referenceSetId = job.references.setId;
                    result = mm::ai::generateWithAi(*job.provider, input, cancel);
                }
                logAiResult(
                    job, result,
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                        .count());
                report.outcome = result.outcome;
                report.status = result.status;
                report.httpStatus = result.httpStatus;
                report.error = result.error;
                report.repaired = result.repaired;
                report.belowMinScore = result.belowMinScore;
                report.copyFlagged = result.copyFlagged;
                report.score = result.score;
                report.usage = AiUsage::instance().record(job.model, providerIsCloud(job.provider->info().id),
                                                          result.inputTokens, result.outputTokens);
                if (result.outcome == mm::ai::AiOutcome::Success && !cancel.cancelled()) {
                    pattern = std::move(result.pattern);
                    if (!job.refine) { // the AI writes bass and melody; the other voices come from the generator (D-250)
                        std::vector<mm::core::VoiceRole> extras = job.settings.extraVoices;
                        if (job.lockedFrom && job.lockedFrom->voices.size() > 2) {
                            extras.clear();
                            for (size_t i = 2; i < job.lockedFrom->voices.size(); ++i) {
                                extras.push_back(job.lockedFrom->voices[i].role);
                            }
                        }
                        const auto request = requestFor(job, cancel.flag());
                        for (size_t i = 0; i < extras.size(); ++i) {
                            mm::core::addVoice(*pattern, extras[i], effective, request, job.seed + i + 1);
                        }
                    }
                    if (job.settings.chromaticPct) { // the pattern remembers it: variations keep to the same share
                        pattern->info.chromaticPct = static_cast<int16_t>(*job.settings.chromaticPct);
                    }
                }
            } else if (!job.refine) { // a refinement has no offline form: without a provider nothing comes out
                const auto request = requestFor(job, cancel.flag());
                auto result = job.lockedFrom ? mm::core::generatePatternAroundLocks(*style, request, *job.lockedFrom)
                                             : mm::core::generatePattern(*style, request);
                if (result.success && !cancel.cancelled()) {
                    pattern = std::move(result.pattern);
                }
            }
        }
        const bool cancelled = cancel.cancelled();
        juce::MessageManager::callAsync(
            [state, job, number, cancelled, report = std::move(report), pattern = std::move(pattern)]() mutable {
                if (!state->alive) {
                    return;
                }
                --state->pending;
                if (cancelled || number != state->latest) {
                    return; // superseded by a newer request, or cancelled
                }
                if (state->report) {
                    state->report(job, report);
                }
                state->delivery(job, std::move(pattern));
            });
    });
}

} // namespace mm::plugin
