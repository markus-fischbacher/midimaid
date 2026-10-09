#pragma once

#include "core/Archetype.h"
#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <optional>

namespace mm::core {

/// Variation engine (SPEC 3.6, D-155): small algorithmic changes to the notes of a pattern. Deterministic for a given
/// seed (integer arithmetic and `Pcg32` only), no selection among candidates and no repetition protection: a subtle
/// variation is similar to its source by definition (STYLES.md 1.14).
struct VariationRequest {
    uint64_t seed = 0;
    int strengthPct = 30;          ///< 0-100: share of the notes that change and how far (see `variationEditCount`)
    std::optional<size_t> voice{}; ///< only this voice; nullopt = every voice that is not locked
};

/// Upper bound of the share of notes one variation edits, reached at strength 100 (subtle operators only, D-155).
constexpr int kSubtleMaxSharePct = 30;

/// How many notes of a voice of `noteCount` notes a variation changes at `strengthPct` (before the constraint layer):
/// the share grows linearly up to `kSubtleMaxSharePct`, rounded to the nearest note, at least 1 for a strength above 0
/// and a voice with notes, 0 otherwise. An operator that touches several notes (velocity contour) may overshoot by up
/// to 3.
size_t variationEditCount(size_t noteCount, int strengthPct);

/// Varies the voices of `pattern` for `request` with the subtle operators: note length, velocity contour, accent
/// position and replacing single notes by a neighbouring scale or chord tone. Locked voices (all three locks) and
/// locked dimensions of single notes stay exactly as they are, also for an explicitly requested voice. Surviving notes
/// keep their ids. Afterwards the constraint layer runs, the quality score is updated and `info.source` becomes
/// "variation". Returns the number of changed notes; 0 means the pattern is untouched (strength 0, unknown voice,
/// everything locked or nothing that could change).
size_t applyVariation(Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings,
                      const VariationRequest& request);

} // namespace mm::core
