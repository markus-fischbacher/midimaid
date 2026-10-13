#include "core/Probability.h"

#include <algorithm>
#include <numeric>

namespace mm::core {

size_t exportPassCount(const std::vector<PatternNote>& notes, uint32_t lengthTicks, uint32_t maxBars) {
    size_t lcm = 1;
    bool chances = false;
    for (const PatternNote& note : notes) {
        if (note.condB > 1) {
            lcm = std::lcm(lcm, static_cast<size_t>(std::min<uint8_t>(note.condB, 8)));
        }
        chances = chances || (note.chance < 100 && note.chance > 0);
    }
    size_t passes = lcm > 1 ? lcm : (chances ? 4 : 1);
    const size_t bars = std::max<size_t>((static_cast<size_t>(std::max<uint32_t>(lengthTicks, 1)) + 3839) / 3840, 1);
    const size_t most = std::max<size_t>(maxBars / bars, 1);
    return std::min(passes, most);
}

std::vector<PatternNote> resolvePasses(const std::vector<PatternNote>& notes, uint32_t lengthTicks, uint64_t seed,
                                       size_t passes) {
    std::vector<PatternNote> result;
    for (size_t pass = 0; pass < passes; ++pass) {
        for (const PatternNote& note : notes) {
            if (!soundsInPass(note, seed, pass)) {
                continue;
            }
            PatternNote out = note;
            if (note.slideTargetId != 0 && note.plainLengthTicks != 0) {
                const bool targetSounds = std::any_of(notes.begin(), notes.end(), [&](const PatternNote& other) {
                    return other.noteId == note.slideTargetId && soundsInPass(other, seed, pass);
                });
                if (!targetSounds) {
                    out.lengthTicks = note.plainLengthTicks;
                }
            }
            out.startTick = static_cast<uint32_t>(pass * lengthTicks + note.startTick);
            out.chance = 100;
            out.condA = out.condB = 1;
            out.slideTargetId = 0;
            out.plainLengthTicks = 0;
            result.push_back(out);
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const PatternNote& a, const PatternNote& b) { return a.startTick < b.startTick; });
    return result;
}

std::vector<PatternNote> notesForExport(const std::vector<PatternNote>& notes, uint32_t& lengthTicks, uint64_t seed) {
    const bool probabilistic = std::any_of(notes.begin(), notes.end(), [](const PatternNote& note) {
        return note.chance < 100 || note.condB > 1;
    });
    if (!probabilistic) {
        return notes;
    }
    const size_t passes = exportPassCount(notes, lengthTicks);
    auto resolved = resolvePasses(notes, lengthTicks, seed, passes);
    lengthTicks = static_cast<uint32_t>(lengthTicks * passes);
    return resolved;
}

} // namespace mm::core
