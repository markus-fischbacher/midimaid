#pragma once

#include "core/Pattern.h"
#include "core/Random.h"
#include "core/Register.h"
#include "core/StyleProfile.h"
#include "core/Theory.h"

#include <optional>
#include <string_view>
#include <vector>

namespace mm::core {

/// Internal part of `core/Archetype`: the melody archetypes (STYLES.md 2-4). Not meant for other modules.
struct MelodyInput {
    const Pattern& pattern;
    const StyleProfile& style;
    std::string_view archetypeId;
    Range range; ///< effective range of the voice (line range, or the voicing range for stabs)
    const Scale& scale;
    int energyPct = 50;
    int creativityPct = 40;
};

/// The notes of a melody archetype, sorted by start and pitch, with ids 0 and velocity 100 plus the micro-variation
/// offset of the motif. They already satisfy the rules of the constraint layer against the bass voice of the pattern
/// (if it has notes), so `applyConstraints` has nothing to repair. nullopt for an unknown archetype or if no
/// valid material could be built (the caller leaves the pattern as it is).
std::optional<std::vector<Note>> generateMelodyNotes(const MelodyInput& input, Pcg32& rng);

} // namespace mm::core
