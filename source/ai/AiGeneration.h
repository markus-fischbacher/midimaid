#pragma once

#include "ai/AiSchema.h"
#include "ai/AiTypes.h"
#include "ai/PromptBuilder.h"

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <optional>
#include <string>

namespace mm::ai {

struct AiGenerateInput {
    const mm::core::StyleProfile* style = nullptr;
    const PromptTemplates* templates = nullptr;
    /// What the musician asked for (style pointer and the locked fields are filled in by `generateWithAi`).
    GeneratePromptInput prompt;
    std::string model;
    int timeoutSeconds = 30;
    int maxTokens = 4096;
    /// The pattern of the slot when a voice of it is locked (SPEC 3.5): its locked voices, harmony, phrases and length
    /// stay, the answer only writes the other voices.
    std::optional<mm::core::Pattern> lockedFrom;
    int64_t createdUnixMs = 0;
};

enum class AiOutcome {
    Success,       ///< a valid pattern
    ProviderError, ///< the provider failed: `status` and `message` say how (no repair is tried)
    Invalid,       ///< the answer was unusable, also after the one repair request: `error` says why
    Cancelled
};

struct AiGenerateResult {
    AiOutcome outcome = AiOutcome::Invalid;
    mm::core::Pattern pattern; ///< on `Success`: constrained, rated, with `info` filled in
    std::string error;         ///< `Invalid`: path and reason of the problem; `ProviderError`: the provider's message
    AiStatus status = AiStatus::Ok; ///< `ProviderError`: the status of the provider
    int httpStatus = 0;
    bool repaired = false;       ///< the first answer was unusable and the second one worked
    int score = 0;               ///< the quality rating (0-100) of the pattern
    bool belowMinScore = false;  ///< the score is under the minimum of the style (SPEC 4.4): shown, not refused
    int requests = 0;            ///< how many requests were sent (1 or 2)
    int inputTokens = 0;
    int outputTokens = 0;
    size_t droppedNotes = 0;
    size_t clampedValues = 0;
};

/// "Generate" with an AI provider (SPEC 7, 7.4): builds the prompt, asks the provider, reads the answer, builds the
/// pattern, runs the constraint layer and the quality rating (SPEC 4.2, 4.4). An unusable answer (no JSON, wrong
/// structure, no usable notes, a voice that is empty after the constraint layer) gets exactly one repair request that
/// names the problem (SPEC 7.3); a provider error gets none (the caller decides: retry, offline, cancel, SPEC 7.9).
/// Blocks; runs on a background thread of the caller. A cancelled token ends it with `Cancelled`, also while the
/// provider is working, and nothing is returned.
///
/// With `lockedFrom` the key, scale, progression and length of that pattern are used, the answer writes the other
/// voices, and the locked voices, the phrases and the kick grid are put back unchanged (ids renumbered so that none
/// is used twice).
AiGenerateResult generateWithAi(IAiProvider& provider, const AiGenerateInput& input, const CancellationToken& token);

} // namespace mm::ai
