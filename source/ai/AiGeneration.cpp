#include "ai/AiGeneration.h"

#include "ai/AiExchange.h"
#include "ai/PatternCompact.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/Quality.h"

#include <algorithm>
#include <set>

namespace mm::ai {

namespace {

using namespace mm::core;

const char* roleName(VoiceRole role) {
    return role == VoiceRole::Bass ? "bass" : "melody";
}

/// Reads one answer and turns it into a finished pattern, or says what was wrong with it.
ExchangeAttempt makePattern(const std::string& text, const AiGenerateInput& input, const std::string& providerId) {
    ExchangeAttempt attempt;
    const auto parsed = parseAiResponse(text);
    if (!parsed.ok) {
        attempt.error = parsed.error;
        return attempt;
    }
    AiDraft draft = parsed.draft;
    const Pattern* locked = input.lockedFrom ? &*input.lockedFrom : nullptr;
    BuildContext context;
    context.lengthBars = locked != nullptr ? locked->lengthBars : input.prompt.lengthBars;
    context.styleId = input.style->id;
    context.style = input.style;
    context.energyPct = input.prompt.energyPct;
    context.creativityPct = input.prompt.creativityPct;
    context.prompt = input.prompt.request;
    context.promptVersion = kPromptVersion;
    context.providerId = providerId;
    context.modelId = input.model;
    context.rawResponse = text;
    context.createdUnixMs = input.createdUnixMs;
    if (locked != nullptr) {
        // The new voices are written for the harmony of the locked ones, whatever the answer says.
        const auto symbols = progressionSymbols(*locked);
        if (!symbols) {
            attempt.error = "the locked pattern has no usable harmony";
            return attempt;
        }
        draft.root = rootName(locked->context.root);
        draft.scale = locked->context.scaleId;
        draft.progression = *symbols;
    }
    const auto built = buildPattern(draft, context);
    attempt.droppedNotes = built.droppedNotes;
    attempt.clampedValues = built.clampedValues;
    if (!built.ok) {
        attempt.error = built.error;
        return attempt;
    }
    Pattern pattern = built.pattern;

    if (locked != nullptr) {
        pattern.phrases = locked->phrases;
        pattern.kickGridId = locked->kickGridId;
        pattern.context = locked->context;
        pattern.voicing = locked->voicing;
        uint32_t next = std::max(pattern.nextNoteId, locked->nextNoteId);
        for (size_t i = 0; i < pattern.voices.size() && i < locked->voices.size(); ++i) {
            if (isVoiceLocked(locked->voices[i])) {
                pattern.voices[i] = locked->voices[i];
            } else {
                for (auto& note : pattern.voices[i].notes) {
                    note.id = next++; // the answer's ids are new, and must not meet the ids of the locked notes
                }
            }
        }
        pattern.nextNoteId = next;
    }

    applyConstraints(pattern, constraintSettingsFor(pattern, *input.style));
    for (const auto& track : pattern.voices) {
        if (track.notes.empty() && !(locked != nullptr && isVoiceLocked(track))) {
            attempt.error = std::string("voices: the ") + roleName(track.role) + " voice has no notes";
            return attempt;
        }
    }
    attempt.pattern = std::move(pattern);
    attempt.ok = true;
    return attempt;
}

} // namespace

AiGenerateResult generateWithAi(IAiProvider& provider, const AiGenerateInput& input, const CancellationToken& token) {
    AiGenerateResult result;
    if (input.style == nullptr || input.templates == nullptr) {
        result.error = "no style or no prompt templates";
        return result;
    }
    GeneratePromptInput promptInput = input.prompt;
    promptInput.style = input.style;
    if (input.lockedFrom) {
        const Pattern& locked = *input.lockedFrom;
        promptInput.lengthBars = locked.lengthBars;
        promptInput.root = locked.context.root;
        promptInput.scaleId = locked.context.scaleId;
        for (const auto& track : locked.voices) {
            if (isVoiceLocked(track)) {
                promptInput.lockedRoles.push_back(roleName(track.role));
            }
        }
        promptInput.fixedProgression = progressionSymbols(locked).value_or(std::vector<std::string>{});
    }
    const auto prompt = buildGeneratePrompt(*input.templates, promptInput);
    if (!prompt) {
        result.error = "the prompt could not be built";
        return result;
    }

    AiRequest request;
    request.model = input.model;
    request.schemaJson = schemaV1Json();
    request.temperature = std::clamp(input.prompt.creativityPct, 0, 100) / 100.0;
    request.maxTokens = input.maxTokens;
    request.timeoutSeconds = input.timeoutSeconds;
    const std::string providerId = provider.info().id;
    return runExchange(
        provider, *prompt, request, *input.style, input.prompt.energyPct, input.prompt.creativityPct,
        [&](const std::string& answer) { return makePattern(answer, input, providerId); }, token);
}

AiGenerateResult runExchange(IAiProvider& provider, const Prompt& prompt, AiRequest request,
                             const mm::core::StyleProfile& style, int energyPct, int creativityPct,
                             const MakeAttempt& makeAttempt, const CancellationToken& token) {
    using namespace mm::core;
    AiGenerateResult result;
    result.systemPrompt = prompt.system;
    result.userPrompt = prompt.user;
    request.systemPrompt = prompt.system;
    request.userPrompt = prompt.user;
    std::string problem;
    for (int attemptNumber = 0; attemptNumber < 2; ++attemptNumber) {
        if (token.cancelled()) {
            result.outcome = AiOutcome::Cancelled;
            return result;
        }
        if (attemptNumber == 1) {
            request.userPrompt = prompt.user + "\n\nYour previous answer could not be used: " + problem +
                                 "\nAnswer again with the complete JSON object only, following the schema exactly.";
        }
        const AiResult answer = provider.generate(request, token);
        ++result.requests;
        result.inputTokens += answer.inputTokens;
        result.outputTokens += answer.outputTokens;
        if (answer.status == AiStatus::Cancelled || token.cancelled()) {
            result.outcome = AiOutcome::Cancelled;
            return result;
        }
        if (!answer.ok()) {
            result.outcome = AiOutcome::ProviderError;
            result.status = answer.status;
            result.httpStatus = answer.httpStatus;
            result.error = answer.message;
            return result;
        }
        result.lastAnswer = answer.text;
        ExchangeAttempt attempt = makeAttempt(answer.text);
        if (attempt.ok) {
            result.outcome = AiOutcome::Success;
            result.repaired = attemptNumber == 1;
            result.droppedNotes = attempt.droppedNotes;
            result.clampedValues = attempt.clampedValues;
            ArchetypeSettings settings;
            settings.energyPct = energyPct;
            settings.creativityPct = creativityPct;
            const auto scores = scoreCriteria(attempt.pattern, qualityContextFor(attempt.pattern, style, settings));
            result.score = std::clamp(overallScore(scores, style.quality, creativityPct), 0, 100);
            attempt.pattern.qualityScore = static_cast<uint8_t>(result.score);
            result.belowMinScore = result.score < style.quality.minScore;
            result.pattern = std::move(attempt.pattern);
            return result;
        }
        problem = attempt.error;
    }
    result.outcome = AiOutcome::Invalid;
    result.error = problem;
    return result;
}

} // namespace mm::ai
