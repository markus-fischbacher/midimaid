#pragma once

#include "core/MidiImport.h"
#include "core/Pattern.h"
#include "core/Theory.h"

#include <array>
#include <bitset>
#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// Analysis of a track for the import of a voice (SPEC 3.18) and for references (SPEC 3.20). Pure standard library.
/// Positions are quantised to the 16th grid only for the analysis, never in the material. The results are independent
/// of the key: a clip transposed by k semitones gives the same result with the root moved by k (chords are offsets from
/// the root, the register is stored relative to the lowest tone).

enum class TrackRole { Drums, Chords, Bass, Melody };

struct KeyEstimate {
    PitchClass root = 0;
    std::string scaleId;
    /// 0 to 1: how far the best (root, scale) is ahead of the best other one (relative to its own score).
    double confidence = 0;
};

struct ChordGuess {
    Chord chord; ///< relative to the key root
    uint32_t startHalfBar = 0;
    uint32_t lengthHalfBars = 2; ///< 1 (half a bar) or 2 (a bar)
    double confidence = 0;       ///< 0 to 1; bars without notes carry the chord before them with 0

    bool operator==(const ChordGuess&) const = default;
};

struct RhythmStats {
    std::vector<std::bitset<16>> bars;     ///< per bar: a 16th step that holds a note start
    std::array<int, 16> meanOffsetTicks{}; ///< groove: mean distance of a start from its 16th step, per step
    std::array<int, 16> meanVelocity{};    ///< mean velocity of the notes on a step (0 = no note there)
    double notesPerBar = 0;                ///< density

    bool operator==(const RhythmStats&) const = default;
};

/// The register of a track, stored so that it does not change when the clip is moved to another key: where the lowest
/// tone sits above the nearest root below it, and how far the tones reach from there.
struct RegisterStats {
    int lowOffset = 0;   ///< (lowest pitch - key root) modulo 12: the scale position of the lowest tone
    int span = 0;        ///< highest pitch - lowest pitch, in semitones
    int medianAbove = 0; ///< median pitch - lowest pitch

    bool operator==(const RegisterStats&) const = default;
};

struct TrackAnalysis {
    TrackRole role = TrackRole::Melody;
    double roleConfidence = 1.0; ///< 0 to 1: how sure `guessRole` was (v1.1). Not part of the JSON: it depends on the register, the item keeps it
    KeyEstimate key;
    std::vector<ChordGuess> progression; ///< covers the clip without gaps, per bar or per half bar
    double progressionConfidence = 0;    ///< mean confidence of the chords (unsure progressions count less)
    RhythmStats rhythm;
    RegisterStats pitchRange;
    uint32_t lengthBars = 0;
    size_t noteCount = 0;
};

struct AnalysisSettings {
    /// The scales that can be chosen (ids of `allScales()`), normally those of the style (SPEC 3.20). Empty: all.
    std::vector<std::string> candidateScales;
    /// The weight of each candidate in the same order (the weights of the style): with too little evidence the scale
    /// the style uses most wins. Missing weights count as 1.
    std::vector<int> scaleWeights;
};

/// The most likely key and scale: a pitch-class profile weighted by duration (the lowest note of each bar and the first
/// and last note count more) is rated by its likelihood under every root and candidate scale (tones of the scale share
/// the probability, the root and the fifth more; tones outside cost; a scale with more tones spreads thinner, so a
/// five-tone melody gives the pentatonic scale), times the weight of the scale in the style. A clip without notes gives
/// confidence 0. `confidence` is 1 - exp(-margin / 3) for the margin to the next best (root, scale), in nats.
KeyEstimate estimateKey(const std::vector<MidiClipNote>& notes, const AnalysisSettings& settings = {});

/// Drums (channel 10, or only typical drum notes), chords (mostly three or more tones at once), bass (one tone at a
/// time and low), else melody. A suggestion: the musician can correct it.
TrackRole suggestRole(const std::vector<MidiClipNote>& notes);

/// The role with a confidence from 0 to 1 (SPEC 3.20, v1.1, D-239). The role is that of `suggestRole`. Channel 10: the
/// share of its notes; chords: the share of onsets with three or more tones; a kit on another channel: 0.6 (0.75 from
/// 16 notes); a bass line: 0.5 at the limit of its register (median pitch 55), 1 an octave below; a melody: the surer
/// of "tones overlap" (0.6 for one overlap, 1 from a fifth of the notes) and "lies high" (0.5 at the limit, 1 an octave
/// above it). An empty track is a melody with confidence 0. A heuristic, not a probability.
struct RoleGuess {
    TrackRole role = TrackRole::Melody;
    double confidence = 0;
};
RoleGuess guessRole(const std::vector<MidiClipNote>& notes);

/// Below this confidence the view marks the role as unsure.
constexpr double kUnsureRoleConfidence = 0.7;

/// The chord of every bar, or of every half bar when the two halves clearly differ (SPEC 3.18). Taken from the
/// tones the bar holds, the lowest ones counting more; a bass line without thirds gets the quality its scale gives.
std::vector<ChordGuess> estimateProgression(const std::vector<MidiClipNote>& notes, uint32_t lengthBars,
                                            const KeyEstimate& key);

/// The same for several tracks that play together (SPEC 3.18, mehrspurig): the track that lies lowest gives the roots,
/// all tracks together the quality. With one merged track the lowest tones of the mix stand in for the bass.
std::vector<ChordGuess> estimateProgressionOf(const std::vector<std::vector<MidiClipNote>>& tracks, uint32_t lengthBars,
                                              const KeyEstimate& key);

/// Everything above for one track. `lengthBars` is the length of the clip (from `planImport`).
TrackAnalysis analyzeTrack(const std::vector<MidiClipNote>& notes, uint32_t lengthBars,
                           const AnalysisSettings& settings = {});

/// Several tracks together (SPEC 3.18: with more than one imported voice the key comes from all of them).
KeyEstimate estimateKeyOf(const std::vector<std::vector<MidiClipNote>>& tracks, const AnalysisSettings& settings = {});

/// The analysis as JSON, key independent (chords as offsets, register relative to the lowest tone), for the cache of
/// references (SPEC 3.20). `analysisFromJson` returns nullopt for anything that is not such a document.
std::string analysisToJson(const TrackAnalysis& analysis);
std::optional<TrackAnalysis> analysisFromJson(const std::string& text);

} // namespace mm::core
