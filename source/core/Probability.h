#pragma once

#include "core/PlaybackPattern.h"
#include "core/Random.h"

#include <cstdint>
#include <vector>

namespace mm::core {

/// Probability and conditions per note (SPEC 3.19, v1.1, D-233). Everything here is integer arithmetic without
/// allocation, so the audio thread uses it as it is; the same functions resolve the passes of an export.

/// The deterministic number behind the decision of a note in a pass: a hash of the pattern seed, the note id and the
/// pass (the song grid counts them from the start of the song, SPEC 6.1).
inline uint64_t passHash(uint64_t seed, uint32_t noteId, uint64_t pass) {
    uint64_t state = seed ^ (static_cast<uint64_t>(noteId) * 0x9E3779B97F4A7C15ULL) ^ (pass * 0xD1B54A32D192ED03ULL);
    return splitMix64(state);
}

/// True if a note with these values sounds in `pass`: the condition A:B (the A-th of every B passes, A counted from 1,
/// B from 1 to 8) and the chance (percent) both hold. A condition 1:1 and a chance of 100 always do.
inline bool soundsInPass(uint8_t chance, uint8_t condA, uint8_t condB, uint64_t seed, uint32_t noteId, uint64_t pass) {
    if (condB > 1) {
        const uint64_t a = condA < 1 ? 1 : (condA > condB ? condB : condA);
        if (pass % condB != a - 1) {
            return false;
        }
    }
    if (chance >= 100) {
        return true;
    }
    return chance > 0 && passHash(seed, noteId, pass) % 100 < chance;
}

inline bool soundsInPass(const PatternNote& note, uint64_t seed, uint64_t pass) {
    return soundsInPass(note.chance, note.condA, note.condB, seed, note.noteId, pass);
}

/// How many passes an export has to cover to show every combination: the least common multiple of the B of all
/// conditions, 4 for a pattern with chances only (a few draws), 1 without either; never more bars than `maxBars`.
size_t exportPassCount(const std::vector<PatternNote>& notes, uint32_t lengthTicks, uint32_t maxBars = 64);

/// The notes of `passes` passes in a row, each pass resolved (`soundsInPass`, a slide whose target stays out of the pass
/// ends plain); the starts move by the pass length. Sorted by start tick. The pattern length of the result is
/// `passes * lengthTicks`.
std::vector<PatternNote> resolvePasses(const std::vector<PatternNote>& notes, uint32_t lengthTicks, uint64_t seed,
                                       size_t passes);

/// What an export writes for the notes of one voice: the notes as they are when none has a chance or a condition,
/// otherwise the resolved passes (`exportPassCount`, `resolvePasses`), with `lengthTicks` grown to match.
std::vector<PatternNote> notesForExport(const std::vector<PatternNote>& notes, uint32_t& lengthTicks, uint64_t seed);

} // namespace mm::core
