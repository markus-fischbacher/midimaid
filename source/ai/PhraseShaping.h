#pragma once

#include "core/Archetype.h"
#include "core/Pattern.h"
#include "core/StyleProfile.h"

namespace mm::ai {

/// A pattern over 8 bars comes from the AI as a motif that is repeated over the pattern (SPEC 3.7, 7.3). This gives
/// each phrase the character of its role, deterministically (no AI, no random state):
/// - main: the motif as it is;
/// - variation: the subtle and structural variation engine on the phrase (strength 35);
/// - answer: the variation engine on the melody only (strength 60: call and response, inversion, octave jumps);
/// - build: a velocity crescendo over the phrase (up to +20) and an accent on the offbeats of its last bar;
/// - breakdown: every second note step of a bar goes (the first stays) and the velocity drops by 12.
/// Locks are not looked at (the AI builds a new pattern; locked voices are put back by the caller afterwards).
/// The constraint layer runs after this, like for every pattern.
void shapePhrasesByRole(mm::core::Pattern& pattern, const mm::core::StyleProfile& style,
                        const mm::core::ArchetypeSettings& settings);

constexpr int kVariationPhraseStrength = 35;
constexpr int kAnswerPhraseStrength = 60;
constexpr int kBuildVelocityRise = 20;
constexpr int kBreakdownVelocityDrop = 12;

} // namespace mm::ai
