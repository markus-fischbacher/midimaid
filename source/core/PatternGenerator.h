#pragma once

#include "core/Archetype.h"
#include "core/CopyProtection.h"
#include "core/Pattern.h"
#include "core/Quality.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// Everything a generation depends on besides the style profile and the seed (SPEC 4, 4.3). Equal requests, profiles
/// and seeds give bit-identical patterns on every platform.
struct GenerationRequest {
    uint32_t lengthBars = 4; ///< 1, 2, 4, 8 or 16
    uint64_t seed = 1;       ///< the user-visible seed
    ArchetypeSettings settings;
    std::optional<PitchClass> root;             ///< fixed root, else drawn
    std::optional<std::string> scaleId;         ///< fixed scale, else drawn by the weights of the style
    std::optional<std::string> kickGridId;      ///< else the default grid of the style
    std::optional<std::string> bassArchetype;   ///< manual choice, else automatic
    std::optional<std::string> melodyArchetype; ///< manual choice, else automatic
    std::vector<VoiceRole> voices;              ///< voice roles in order; empty = bass and melody (at most kMaxVoices)
    std::optional<std::vector<Phrase>> formPlan; ///< fixed phrases (valid for the length), else drawn (FormPlan.h)
    std::vector<ReferenceEntry> referenceSet;   ///< active set for the copy protection (empty = none)
};

/// One candidate, a pure function of `seed`: harmony, style groove, form plan, bass, melody (in this order, so that
/// the melody knows the bass), then the constraint layer. Patterns of up to 4 bars are generated in one piece; patterns
/// of 8 and 16 bars phrase by phrase (D-121): every phrase is a pattern of its own length with the chords, the kick
/// grid and the energy and creativity of its role, generated from a seed of its role (phrases of one role share it,
/// so they repeat their rhythm), and joined at the phrase starts. All draws come from one `Pcg32::fromSeed(seed)`. `info` carries the
/// user seed of the request and `seed` as winner seed. A voice that cannot be generated (unknown scale, range too
/// narrow) stays empty without archetype; `generatePattern` never accepts such a candidate.
Pattern generateCandidate(const StyleProfile& style, const GenerationRequest& request, uint64_t seed);

/// Generates the candidates of the selection (SPEC 4.4, D-88) from `request.seed` and returns the best valid one with
/// quality score and winner seed stored. Hard criteria: every voice generated, copy protection against the reference
/// set. `success == false`: no valid candidate, nothing is returned.
SelectionResult generatePattern(const StyleProfile& style, const GenerationRequest& request);

/// Replays a stored winner seed: exactly that candidate, without selection and without copy protection (D-88). The
/// quality score is set as `generatePattern` does.
Pattern replayWinner(const StyleProfile& style, const GenerationRequest& request, uint64_t winnerSeed);

/// Replaces the notes of one voice by a new line for `seed` (manual archetype of the voice or automatic), keeping the
/// harmony, the kick grid and the other voices. Then the constraint layer runs (it may adjust the other voice to the
/// new line) and the quality score is updated. False, pattern untouched, for an unknown voice, a voice with all three
/// locks or when the voice cannot be generated.
bool regenerateVoice(Pattern& pattern, size_t voiceIndex, const StyleProfile& style, const GenerationRequest& request,
                     uint64_t seed);

/// Replaces bass and melody inside one phrase for `seed`, with the archetypes of the voices and the settings of the
/// role of the phrase. Harmony, form plan and the other phrases stay. Voices with all three locks are kept. Then the
/// constraint layer runs and the quality score is updated. False, pattern untouched, for an unknown or locked phrase,
/// a voice without archetype or when a voice cannot be generated.
bool regeneratePhrase(Pattern& pattern, size_t phraseIndex, const StyleProfile& style, const GenerationRequest& request,
                      uint64_t seed);

} // namespace mm::core
