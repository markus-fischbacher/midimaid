#pragma once

#include "core/Random.h"
#include "core/Register.h"
#include "core/StyleProfile.h"
#include "core/Theory.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mm::core {

/// Voicings of stabs and chords (STYLES.md 1.10): close position, voice leading over chord changes. Chord memory
/// ([v1.1]) is not part of v1.0; the voice leading applies everywhere.

/// Chord colours of the style profiles. The profile id "sus" stands for `Sus2` or `Sus4` (see `pickChordColor`).
enum class ChordColor { Min, Fifth, Min7, Min9, Sus2, Sus4, Cluster };

/// nullopt for unknown ids and for "sus", which needs a random choice.
std::optional<ChordColor> parseChordColorId(std::string_view id);

/// Draws a colour from the weighted profile list (one draw for the weight, one more `chance(50)` for "sus": sus2 or
/// sus4). Entries with unknown ids never win. nullopt if no entry can be drawn.
std::optional<ChordColor> pickChordColor(std::span<const WeightedId> colors, Pcg32& rng);

/// Pitch classes of a chord in a colour, root first, then upwards by role (no duplicates). Colour tones that are not
/// in the scale (seventh, ninth, sus tone, flat second) are chosen from the scale or left out, because the constraint
/// layer would snap them away otherwise:
///   Min      triad of the chord symbol          Fifth    root + fifth of the chord
///   Min7     triad + seventh                    Min9     root, third, seventh, ninth (fifth left out);
///   Sus2     root, 2, fifth                              without ninth: as Min7
///   Sus4     root, 4, fifth                     Cluster  root, flat second, fifth
/// The seventh is the minor seventh above the chord root if it is in the scale, otherwise the major seventh; the
/// ninth is the major second, otherwise the minor second. "Third" and "fifth" are the second and third triad tone of
/// the chord symbol (a sus chord keeps its sus tone). A colour whose extra tone is not available loses that tone.
std::vector<PitchClass> colorTones(const Scale& scale, PitchClass keyRoot, const Chord& chord, ChordColor color);

/// Close-position voicing inside `range`: every inversion at every octave position, all pitches ascending and within
/// one octave (a close position), optionally plus the lowest pitch an octave higher. Voicings with fewer than 3 tones
/// get the octave doubling automatically (power chord). Result has 3-5 pitches, ascending.
///
/// Choice: without `previous` the voicing nearest to the centre of the range wins; with `previous` (the voicing of
/// the chord before) the one with the least movement wins: the sum of the distances from every tone to the nearest
/// tone of the other voicing, in both directions, so common tones count 0 and chords of different size work.
/// Ties go to the narrower voicing, then to the lower one. nullopt if no voicing fits in the range.
std::optional<std::vector<int>> voiceChord(std::span<const PitchClass> tones, Range range, bool doubleLowest,
                                           const std::vector<int>* previous);

/// Voicings for a whole progression with voice leading from chord to chord. `colors` and `doubleLowest` have one
/// entry per chord. nullopt if sizes differ or a chord cannot be voiced.
std::optional<std::vector<std::vector<int>>> voiceProgression(const Scale& scale, PitchClass keyRoot,
                                                              std::span<const Chord> chords,
                                                              std::span<const ChordColor> colors,
                                                              std::span<const bool> doubleLowest, Range range);

/// Largest distance (semitones) from any tone of one voicing to the nearest tone of the other, both directions.
/// 0 for identical voicings and for empty input.
int maxVoiceMovement(const std::vector<int>& from, const std::vector<int>& to);

} // namespace mm::core
