#pragma once

#include "core/Pattern.h"
#include "core/Quality.h"

#include <cstdint>
#include <vector>

namespace mm::core {

/// Copy protection against the active reference set (SPEC 3.20, D-87). The same similarity metric serves the
/// repetition protection of v1.1 (STYLES.md 1.14), which is not part of v1.0.

/// More than 85 % similarity to a distinctive entry rejects a candidate (a similarity of exactly 85 % is allowed).
constexpr int kCopyThresholdPermille = 850;

/// Distinctive entries need at least this many pitch classes and this many pitch changes per two bars.
constexpr int kDistinctiveMinPitchClasses = 3;
constexpr int kDistinctiveMinChangesPerTwoBars = 4;

/// A voice as a loop of whole bars: the unit of the comparison.
struct Material {
    VoiceRole role = VoiceRole::Melody;
    uint32_t bars = 1;
    std::vector<Note> notes;
};

/// An entry of the reference set. Import and analysis fill it later; `active` is the switch of the set.
struct ReferenceEntry {
    VoiceRole role = VoiceRole::Melody;
    uint32_t bars = 1;
    std::vector<Note> notes;
    bool active = true;
};

Material materialOf(const Pattern& pattern, size_t voice);
Material materialOf(const ReferenceEntry& entry);

/// One onset of the line: the step on the 16th grid, the pitch of the line (lowest tone of a bass step, highest tone
/// otherwise) and the interval in semitones to the previous onset. The interval of the first onset is taken from the
/// last one (the material is a loop); a single onset has interval 0.
struct LineOnset {
    int step = 0;
    int pitch = 0;
    int interval = 0;

    bool operator==(const LineOnset&) const = default;
};

/// The onsets of the material, ascending by step. Starts are rounded to the nearest 16th (halfway goes up); a start
/// that rounds to the end of the material wraps to the beginning. Several notes on a step give one onset.
std::vector<LineOnset> lineOf(const Material& material);

/// Similarity of two materials, 0-1000: onsets on the same step with the same interval, divided by the larger
/// number of onsets. The shorter material is compared with windows of its length in the longer one, which start on
/// every bar (loops: windows wrap around the end); the most similar window counts. 0 if one of them has no notes.
int similarityPermille(const Material& a, const Material& b);

/// Only distinctive entries are protected: at least 3 pitch classes and at least 4 pitch changes of the line per two
/// bars. A rolling root-fifth-octave line is common property.
bool isDistinctive(const ReferenceEntry& entry);

struct CopyCheck {
    bool violated = false;
    size_t voice = 0; ///< voice of the pattern with the highest similarity
    size_t entry = 0; ///< entry of the set it resembles
    int permille = 0; ///< that similarity (0 if nothing was compared)
};

/// Compares every voice with notes to every active, distinctive entry of the same role. `violated` if the highest
/// similarity is above `kCopyThresholdPermille`.
CopyCheck checkCopyProtection(const Pattern& pattern, const std::vector<ReferenceEntry>& set);

/// The check as a hard criterion for `selectBest` (true = candidate allowed). Holds a copy of the set.
HardCheck copyProtectionCheck(std::vector<ReferenceEntry> set);

} // namespace mm::core
