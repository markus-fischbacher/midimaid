#include "core/Register.h"

#include <algorithm>

namespace mm::core {

namespace {

int clampOffset(int octaveOffset) {
    return std::clamp(octaveOffset, -kMaxOctaveOffset, kMaxOctaveOffset);
}

} // namespace

Range applyOctaveOffset(Range range, int octaveOffset) {
    const int shift = 12 * clampOffset(octaveOffset);
    return {std::clamp(range.low + shift, 0, 127), std::clamp(range.high + shift, 0, 127)};
}

bool isVoicingArchetype(std::string_view archetypeId) {
    return archetypeId == "stabs" || archetypeId == "aggro_stabs";
}

bool ignoresKickArchetype(std::string_view archetypeId) {
    return archetypeId == "long_tied";
}

std::optional<Range> effectiveRange(const RegisterProfile& profile, VoiceRole role, std::string_view archetypeId,
                                    const VoicingSettings& voicing, int octaveOffset) {
    Range base = profile.bass;
    if (role == VoiceRole::Melody) {
        base = isVoicingArchetype(archetypeId) ? Range{voicing.lowNote, voicing.highNote} : profile.melody;
    }
    const Range shifted = applyOctaveOffset(base, octaveOffset);
    if (shifted.width() < kMinRangeWidth) {
        return std::nullopt;
    }
    return shifted;
}

size_t shiftVoiceByOctaves(Track& track, int octaves) {
    if (octaves == 0 || track.lock.pitch) {
        return 0;
    }
    size_t moved = 0;
    for (Note& note : track.notes) {
        if (note.lock.pitch) {
            continue;
        }
        int pitch = note.pitch + 12 * octaves;
        while (pitch > 127) {
            pitch -= 12;
        }
        while (pitch < 0) {
            pitch += 12;
        }
        if (pitch != note.pitch) {
            note.pitch = static_cast<uint8_t>(pitch);
            ++moved;
        }
    }
    return moved;
}

void changeOctaveOffset(Track& track, int newOffset) {
    const int target = clampOffset(newOffset);
    shiftVoiceByOctaves(track, target - track.octaveOffset);
    track.octaveOffset = static_cast<int8_t>(target);
}

} // namespace mm::core
