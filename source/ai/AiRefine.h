#pragma once

#include "ai/AiGeneration.h"

#include <optional>
#include <string>

namespace mm::ai {

struct AiRefineInput {
    const mm::core::StyleProfile* style = nullptr;
    const PromptTemplates* templates = nullptr;
    mm::core::Pattern pattern; ///< the pattern to refine, exactly as the slot holds it
    std::string instruction;   ///< the musician's wish ("fewer notes", "last bar higher")
    /// The scope (SPEC 3.15): one voice (index into `Pattern::voices`) or one phrase (index into `Pattern::phrases`);
    /// neither: the whole pattern. A voice wins over a phrase.
    std::optional<size_t> voice;
    std::optional<size_t> phrase;
    int energyPct = 50;
    int creativityPct = 40;
    std::string model;
    int timeoutSeconds = 30;
    int maxTokens = 4096;
    int64_t createdUnixMs = 0;
};

/// How many earlier refinements the pattern remembers and sends as context (SPEC 3.15).
constexpr size_t kRefineHistorySize = 5;

/// "Refine" with an AI provider (SPEC 3.15, 7): sends the pattern with its note ids, the scope, the locks and the last
/// refinements, reads the answer like `generateWithAi` (one repair request for an unusable answer, none for a provider
/// error, cancel at any time) and merges it into the pattern:
/// - key, scale, progression, phrases, kick grid and everything outside the notes stay as they are;
/// - a note whose id comes back unchanged keeps its exact ticks (the schema rounds to 16ths, so the answer alone would
///   move off-grid notes); a changed note takes the answer's values; an unknown or repeated id makes a new note;
/// - voices outside the scope, locked voices and (for a phrase) the bars outside it are put back unchanged;
/// - then the constraint layer and the quality rating run, and the instruction joins `refineHistory` (the last 5).
/// `result.pattern` is the new pattern on `Success`; `info.source` is "refine".
AiGenerateResult refineWithAi(IAiProvider& provider, const AiRefineInput& input, const CancellationToken& token);

/// The merge step alone (for tests): `answer` is what the builder made of the answer (ids that were known are kept,
/// new notes have ids from `current.nextNoteId` up), `rounded` is `current` as the schema writes it. Returns the
/// refined pattern before the constraint layer, with `current`'s context, phrases and settings.
mm::core::Pattern mergeRefinement(const mm::core::Pattern& current, const mm::core::Pattern& rounded,
                                  const mm::core::Pattern& answer, std::optional<size_t> voice,
                                  std::optional<size_t> phrase);

} // namespace mm::ai
