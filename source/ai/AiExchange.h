#pragma once

#include "ai/AiGeneration.h"

#include <functional>

namespace mm::ai {

/// What turning one answer into a pattern gave: a finished pattern, or the problem for the repair request.
struct ExchangeAttempt {
    bool ok = false;
    std::string error;
    mm::core::Pattern pattern;
    size_t droppedNotes = 0;
    size_t clampedValues = 0;
};

using MakeAttempt = std::function<ExchangeAttempt(const std::string& answer)>;

/// The shared request loop of "Generate" and "Refine" (SPEC 7.3, 7.9): asks the provider, hands each answer to
/// `makeAttempt`, and sends exactly one repair request that names the problem when the answer was unusable. A provider
/// error ends it at once; a cancelled token ends it with `Cancelled`. A pattern that worked is rated with the style
/// (SPEC 4.4). `prompt` and `request` are filled by the caller (the request without the user text of the repair).
AiGenerateResult runExchange(IAiProvider& provider, const Prompt& prompt, AiRequest request,
                             const mm::core::StyleProfile& style, int energyPct, int creativityPct,
                             const MakeAttempt& makeAttempt, const CancellationToken& token);

} // namespace mm::ai
