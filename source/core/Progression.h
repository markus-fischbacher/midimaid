#pragma once

#include "core/Pattern.h"
#include "core/Random.h"
#include "core/StyleProfile.h"

#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// A progression of 2 to 4 chords (relative to the key root) with a weight: what the musician's own tracks use
/// (`personalProgressions`, SPEC 3.20).
struct WeightedProgression {
    std::vector<Chord> chords;
    int weight = 0;

    bool operator==(const WeightedProgression&) const = default;
};

/// Weight of one progression of the style profile against the weights of the personal ones (D-182).
constexpr int kStyleProgressionWeight = 20;

/// The progressions a mode can use (STYLES.md 1.17, D-119). `static` has the single chord `i`. `two_chord` and
/// `four_chord` use the progressions of the profile with exactly that many chords; if there is none, they are derived
/// from the others (O-25): two chords take the first two chords of any progression, four chords repeat a two-chord
/// progression as A B A B or extend a three-chord progression as A B C B. Unknown modes give an empty list, as does a
/// profile without any usable progression.
std::vector<std::vector<Chord>> progressionsForMode(const HarmonyProfile& harmony, const std::string& mode);

/// The chord lengths in bars (1, 2, 4, 8 or 16) a progression of `chordCount` chords can use in a pattern of
/// `lengthBars`: those of the profile range in which the progression fits once (`chordCount * length <= lengthBars`).
/// If none fits, the longest standard length that does (also below the profile minimum). Never empty; at least 1.
std::vector<uint32_t> fittingChordLengths(const HarmonyProfile& harmony, size_t chordCount, uint32_t lengthBars);

/// Draws the chord events of a pattern: gapless over exactly `lengthBars * 2` half bars. Draw order (fixed, D-88):
/// mode by weight, progression uniformly among those of the mode, chord length uniformly among the fitting ones.
/// A pattern of one bar has half-bar chords and at most two of them. The sequence repeats until the pattern ends.
/// `static` and profiles without usable progressions give one chord `i` for the whole pattern.
///
/// `personal` (SPEC 3.20, D-182) joins the progressions of the mode: they fit the mode like those of the profile (a
/// longer one is cut, a shorter one extended, `progressionsForMode`), and the draw is by weight: every progression of
/// the profile counts `kStyleProgressionWeight`, the personal ones their weight, together at most as much as the
/// profile's (so at most half of the draws). Without personal progressions the draws are those of before.
std::vector<ChordEvent> generateProgression(const StyleProfile& style, uint32_t lengthBars, Pcg32& rng,
                                            const std::vector<WeightedProgression>& personal = {});

/// The whole harmonic context: scale by the weights of the profile and a root drawn uniformly from the 12 pitch
/// classes, plus the progression. A given root or scale replaces the drawn value; the draws are made anyway, so
/// fixing one of them does not change the others for the same seed.
HarmonicContext makeHarmonicContext(const StyleProfile& style, uint32_t lengthBars, Pcg32& rng,
                                    std::optional<PitchClass> root = std::nullopt,
                                    std::optional<std::string> scaleId = std::nullopt,
                                    const std::vector<WeightedProgression>& personal = {});

/// Replaces `pattern.context` with a freshly generated one for the length of the pattern.
void applyHarmony(Pattern& pattern, const StyleProfile& style, Pcg32& rng,
                  std::optional<PitchClass> root = std::nullopt, std::optional<std::string> scaleId = std::nullopt,
                  const std::vector<WeightedProgression>& personal = {});

} // namespace mm::core
