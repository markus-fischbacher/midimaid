#pragma once

#include "core/Constraints.h"
#include "core/Pattern.h"
#include "core/Quality.h"
#include "core/Random.h"
#include "core/StyleProfile.h"
#include "core/VelocityContour.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mm::core {

/// Archetypes (SPEC 3.x, STYLES.md 2-4): the rhythmic and melodic type of a voice. An archetype fills the notes of one
/// voice for the kick grid, the harmony and the style profile of the pattern. Everything is deterministic for a given
/// seed (integer arithmetic and `Pcg32` only). Bass archetypes of this module keep off the kick steps and end before
/// the next kick themselves, so the constraint layer finds nothing to repair (D-113).

/// How strongly the energy pulls an archetype in the automatic choice: dense archetypes become likelier with high
/// energy, calm ones with low energy, neutral ones never change.
enum class ArchetypeDensity { Calm, Neutral, Dense };

struct Archetype {
    std::string_view id;
    VoiceRole role;
    bool ignoresKick; ///< may start on and hold over kicks (`long_tied`); same as `ignoresKickArchetype`
    ArchetypeDensity density;
};

/// All archetypes that have an implementation (v1.0 bass archetypes of the three styles).
std::span<const Archetype> allArchetypes();

/// nullptr for an unknown id or one without implementation.
const Archetype* findArchetype(std::string_view id);

struct ArchetypeSettings {
    int energyPct = 50;     ///< 0-100: density, accent share, octave jumps, velocity level (STYLES.md 1.5)
    int creativityPct = 40; ///< 0-100: variation bars, unusual (chromatic) tones, scatter of the archetype choice
};

/// Chooses an archetype of the profile for `role` at random, weighted by the profile weights moved by the energy
/// (calm: x(150 - energy)/100, dense: x(50 + energy)/100). Ids without an implementation or for another role are
/// skipped. The creativity scatters every weight by a random factor (`archetypeScatterRange`, SPEC 7.6); at 0 (the
/// default) the weights hold exactly and nothing is drawn for it. Empty string if no archetype is left.
std::string chooseArchetype(const StyleProfile& style, VoiceRole role, int energyPct, Pcg32& rng,
                            int creativityPct = 0);

/// The archetype a voice is generated with: the manual choice of the voice (`archetypeAuto == false`, a known id of the
/// right role, no random draw) or else `chooseArchetype`.
std::string resolveArchetype(const Track& track, const StyleProfile& style, int energyPct, Pcg32& rng,
                             int creativityPct = 0);

/// Replaces the notes of voice `voiceIndex` by a new line of the archetype and sets `Track::archetypeId`. The line
/// follows the harmony (`Pattern::context`), the kick grids (`kickTicks`) and the effective range of the voice, and
/// gets the velocity contour of the archetype with the energy applied. Returns false (pattern untouched) for an unknown
/// voice or archetype, a role mismatch, an unknown scale or a range narrower than 12 pitches.
bool generateVoice(Pattern& pattern, size_t voiceIndex, std::string_view archetypeId, const StyleProfile& style,
                   const ArchetypeSettings& settings, Pcg32& rng);

/// The velocity contour of an archetype before the energy is applied (all 100 for an unknown id).
VelocityContour archetypeContour(std::string_view archetypeId);

/// Constraint settings that match the generators: ranges and `ignoresKick` per voice, the kick clearance and the
/// chromatic share of the style profile.
ConstraintSettings constraintSettingsFor(const Pattern& pattern, const StyleProfile& style);

/// Quality context with the `ignoresKick` flags of the voices and the chromatic share of the style profile.
QualityContext qualityContextFor(const Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings);

} // namespace mm::core
