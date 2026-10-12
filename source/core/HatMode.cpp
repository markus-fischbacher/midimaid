#include "core/HatMode.h"

#include <algorithm>
#include <set>
#include <vector>

namespace mm::core {

namespace {

constexpr uint32_t kStepTicks = 240;

bool isHatStep(const RhythmReference& ref, uint32_t tick) {
    const uint32_t steps = std::max<uint32_t>(ref.bars, 1) * 16;
    return ref.hatSteps.test((tick / kStepTicks) % steps);
}

} // namespace

std::string_view toString(HatMode mode) {
    switch (mode) {
    case HatMode::With:
        return "with";
    case HatMode::Against:
        return "against";
    default:
        return "free";
    }
}

HatMode parseHatMode(std::string_view text) {
    return text == "with" ? HatMode::With : text == "against" ? HatMode::Against : HatMode::Free;
}

void applyHatMode(Pattern& pattern, HatMode mode) {
    if (mode == HatMode::Free || !pattern.rhythmRef.has_value() || pattern.rhythmRef->bars < 1 ||
        pattern.rhythmRef->hatSteps.none()) {
        return;
    }
    const RhythmReference& ref = *pattern.rhythmRef;
    const uint32_t patternEnd = pattern.lengthBars * kTicksPerBar;
    const bool wantHat = mode == HatMode::With;
    for (Track& track : pattern.voices) {
        if (track.role != VoiceRole::Melody || track.lock.rhythm) {
            continue;
        }
        std::set<uint32_t> occupied;
        for (const Note& note : track.notes) {
            occupied.insert(note.startTick);
        }
        std::vector<uint32_t> starts(occupied.begin(), occupied.end());
        for (const uint32_t start : starts) {
            if (start % kStepTicks != 0 || start % (kStepTicks * 4) == 0 || isHatStep(ref, start) == wantHat) {
                continue; // off the grid, on a beat, or already where it should be
            }
            bool movable = true;
            for (const Note& note : track.notes) {
                movable = movable && (note.startTick != start || (!note.slide && !note.lock.rhythm));
            }
            if (!movable) {
                continue;
            }
            for (const int64_t step : {int64_t{-1}, int64_t{1}}) {
                const int64_t target = static_cast<int64_t>(start) + step * kStepTicks;
                if (target < 0 || target >= static_cast<int64_t>(patternEnd) ||
                    occupied.count(static_cast<uint32_t>(target)) != 0 ||
                    isHatStep(ref, static_cast<uint32_t>(target)) != wantHat) {
                    continue;
                }
                for (Note& note : track.notes) {
                    if (note.startTick == start) {
                        note.startTick = static_cast<uint32_t>(target);
                    }
                }
                occupied.erase(start);
                occupied.insert(static_cast<uint32_t>(target));
                break;
            }
        }
        std::stable_sort(track.notes.begin(), track.notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    }
}

} // namespace mm::core
