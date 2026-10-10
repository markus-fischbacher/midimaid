#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mm::ai {

struct CompactPattern {
    std::string json;        ///< schema v1 (`AiSchema.h`), one line
    size_t roundedNotes = 0; ///< notes whose start or length is not a whole number of 16ths: the schema rounds them
};

/// The pattern as an answer of schema v1 (SPEC 3.15, 7.3): key, scale, one chord symbol per bar (`"x|y"` for two
/// chords), the phrases and per voice the notes as step, degree, alteration and octave relative to the voice base, with
/// the note ids when `includeIds`. This is what a refinement sends to the AI, so that the AI answers in the same shape.
/// Starts and lengths are in 16ths: a start that is not on the grid is rounded to the nearest 16th, a length to the
/// nearest whole number of 16ths (at least 1), and the note counts in `roundedNotes` (the caller can restore a note
/// that the answer leaves unchanged). Nullopt for an unknown scale or a voice without a base note. `style` gives the
/// ranges (null: the defaults), like `BuildContext::style`.
std::optional<CompactPattern> patternToSchemaJson(const mm::core::Pattern& pattern, const mm::core::StyleProfile* style,
                                                  bool includeIds = true);

/// One chord symbol per bar (`"x|y"` for two chords in a bar), as the schema writes the progression. Nullopt when a
/// half bar has no chord.
std::optional<std::vector<std::string>> progressionSymbols(const mm::core::Pattern& pattern);

/// "A", "C#" ...: the key as the schema writes it (sharps).
std::string rootName(mm::core::PitchClass root);

} // namespace mm::ai
