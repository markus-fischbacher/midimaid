#pragma once

#include "core/Archetype.h"
#include "core/Pattern.h"
#include "core/Random.h"
#include "core/StyleProfile.h"
#include "core/Theory.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace mm::core {

/// Variation engine (SPEC 3.6, D-155): small algorithmic changes to the notes of a pattern. Deterministic for a given
/// seed (integer arithmetic and `Pcg32` only), no selection among candidates and no repetition protection: a subtle
/// variation is similar to its source by definition (STYLES.md 1.14).
struct VariationRequest {
    uint64_t seed = 0;
    int strengthPct = 30;          ///< 0-100: share of the notes that change and how far (see `variationEditCount`)
    std::optional<size_t> voice{}; ///< only this voice; nullopt = every voice that is not locked
};

/// What the operators know about the voice they work on.
struct VariationVoice {
    const Scale* scale = nullptr;
    const HarmonicContext* harmony = nullptr;
    int rangeLow = 0;
    int rangeHigh = 127;
    uint32_t patternEnd = 0;
    VoiceRole role = VoiceRole::Bass;
    const std::vector<Phrase>* phrases = nullptr;
    const std::vector<Note>* original = nullptr; ///< the voice before this variation (same indices while D1 only edits)
};

enum class StructuralOperator { ShiftRhythm, SwapTones, OctaveJump, Density, InvertOrMirror, CallResponse };

/// The structural operators (SPEC 3.6, D-156). Each works on the notes of one voice (sorted by start), returns whether
/// it changed something and leaves the notes untouched otherwise. They only touch what the locks allow (note level
/// and the `lock` of the voice); none of them leaves the pattern, the range of the voice or makes notes overlap.
/// Shifts the start of 1 to 3 notes by +-1/16.
bool shiftRhythm(std::vector<Note>& notes, const VariationVoice& voice, Pcg32& rng);
/// Two notes under the same chord trade pitches, or one note jumps at least 3 semitones to another allowed pitch.
bool swapTones(std::vector<Note>& notes, const VariationVoice& voice, Pcg32& rng);
/// One note moves an octave up or down inside the range.
bool octaveJump(std::vector<Note>& notes, const VariationVoice& voice, Pcg32& rng);
/// Adds or removes 1 or 2 notes: added ones on a free 1/16 step (weak steps first, pitch next to the predecessor,
/// new ids from `pattern`), removed ones are weak notes (never an accent, a locked note, a bass note on the beat); a
/// voice keeps at least 2 notes.
bool changeDensity(std::vector<Note>& notes, Pattern& pattern, const LockFlags& lock, const VariationVoice& voice,
                   Pcg32& rng);
/// Mirrors the pitches of one bar or phrase around its first note (snapped to scale or chord, folded into the
/// range), or plays them backwards over the same rhythm.
bool invertOrMirror(std::vector<Note>& notes, const VariationVoice& voice, Pcg32& rng);
/// Exchanges the content of the bars 2k and 2k+1 of one pair; needs 2 bars or more.
bool swapCallAndResponse(std::vector<Note>& notes, const VariationVoice& voice, Pcg32& rng);
/// One operator with the locks and the role of the voice applied: inversion and call & response only for melody
/// voices; rhythm operators not with a locked rhythm, pitch operators not with a locked pitch.
bool applyStructural(StructuralOperator op, std::vector<Note>& notes, Pattern& pattern, const LockFlags& lock,
                     const VariationVoice& voice, Pcg32& rng);

/// Upper bound of the share of notes one variation edits, reached at strength 100 (subtle operators only, D-155).
constexpr int kSubtleMaxSharePct = 30;

/// From this strength on structural operators join the subtle ones (D-156).
constexpr int kStructuralFromPct = 25;

/// How many structural operators run per voice at `strengthPct`: none below `kStructuralFromPct`, then 1 (below 60),
/// 2 (below 90) and 3.
size_t structuralEditCount(int strengthPct);

/// How many notes of a voice of `noteCount` notes a variation changes at `strengthPct` (before the constraint layer):
/// the share grows linearly up to `kSubtleMaxSharePct`, rounded to the nearest note, at least 1 for a strength above 0
/// and a voice with notes, 0 otherwise. An operator that touches several notes (velocity contour) may overshoot by up
/// to 3.
size_t variationEditCount(size_t noteCount, int strengthPct);

/// Varies the voices of `pattern` for `request`. Subtle operators always run: note length, velocity contour, accent
/// position and replacing single notes by a neighbouring scale or chord tone. From `kStructuralFromPct` on
/// `structuralEditCount` structural operators follow per voice (shift rhythm, swap tones, octave jump, density,
/// inversion or reversal, call and response). Locked voices (all three locks) and
/// locked dimensions of single notes stay exactly as they are, also for an explicitly requested voice. Surviving notes
/// keep their ids. Afterwards the constraint layer runs, the quality score is updated and `info.source` becomes
/// "variation". Returns the number of changed notes; 0 means the pattern is untouched (strength 0, unknown voice,
/// everything locked or nothing that could change).
size_t applyVariation(Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings,
                      const VariationRequest& request);

} // namespace mm::core
