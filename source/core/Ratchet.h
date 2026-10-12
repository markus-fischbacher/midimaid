#pragma once

#include "core/Pattern.h"
#include "core/Random.h"

#include <string>

namespace mm::core {

/// Ratchets (STYLES.md 1.13, D-227): one note is repeated 2-4 times inside its own length (the output stage resolves
/// them, SPEC 4.2a stage 5). The generators set them sparingly: only in `acid_siren` and `roll16_aggressive`, at most
/// one per bar. The chance of a bar rises with the energy; Hard/Industrial uses them more (STYLES.md 1.16: 40-70 %).
/// Returns 0 for every other archetype. Starting values for the listening test.
int ratchetChancePct(const std::string& archetypeId, const std::string& styleId, int energyPct);

/// The most repeats a note of `lengthTicks` can take: every repeat keeps at least `minNoteTicks` (1 = no ratchet).
int maxRatchetFor(uint32_t lengthTicks, uint32_t minNoteTicks = 60);

/// Draws ratchets for the bars [fromBar, toBar) of one voice from its own seed stream: per bar one draw by
/// `ratchetChancePct` of the voice's archetype, then one eligible note (single, no slide, not a slide target, free
/// rhythm lock, at least 120 ticks long) and 2 to `maxRatchetFor` repeats. Bars that already have a ratchet (a turnaround)
/// are skipped. Voices with a locked rhythm stay as they are. The constraint layer runs afterwards.
void applyRatchets(Pattern& pattern, size_t voice, const std::string& styleId, int energyPct, uint64_t seed,
                   uint32_t fromBar, uint32_t toBar);

} // namespace mm::core
