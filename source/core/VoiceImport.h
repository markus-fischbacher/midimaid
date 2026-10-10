#pragma once

#include "core/MidiImport.h"
#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <optional>

namespace mm::core {

/// Takes an imported clip into a pattern as a voice (SPEC 3.18). Pure standard library.
///
/// The clip replaces the voice `voice` of `current` (or of a new pattern when `current` is empty). It is locked in
/// every dimension, has no archetype, keeps its ticks, pitches and velocities exactly and plays without groove (the
/// groove of the producer is in the ticks already). The key, scale and progression of the pattern come from the
/// analysis of the imported voice together with the other locked voices of the same length. The other voices are
/// empty afterwards (they would no longer fit the harmony): `generatePatternAroundLocks` fills them. With another
/// length than `current` the pattern is built anew: style groove, form plan, the roles and channels of `current`, no
/// locks.
/// nullopt for a voice that does not exist, an empty plan or a plan that is not ok.
std::optional<Pattern> importVoice(const StyleProfile& style, const std::optional<Pattern>& current, size_t voice,
                                   const ImportPlan& plan);

/// The key of the pattern as the musician corrects it: the progression is derived again from the locked voices with
/// the given root and scale (not guessed). The locked voices and the other voices stay. False, pattern untouched, for
/// an unknown scale or when no voice is locked.
bool correctImportedKey(Pattern& pattern, PitchClass root, const std::string& scaleId);

} // namespace mm::core
