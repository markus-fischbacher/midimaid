#include "core/Transpose.h"

#include <algorithm>

namespace mm::core {

namespace {

constexpr int kAllDegrees = 0xFFF;

int floorDiv(int a, int b) {
    return a / b - ((a % b != 0 && ((a < 0) != (b < 0))) ? 1 : 0);
}

int floorMod(int a, int b) {
    return a - floorDiv(a, b) * b;
}

int maskOf(const TransposeInfo& info) {
    return info.scaleMask == 0 ? kAllDegrees : (info.scaleMask & kAllDegrees);
}

int degreeCount(int mask) {
    int count = 0;
    for (int bit = 0; bit < 12; ++bit) {
        count += (mask >> bit) & 1;
    }
    return count;
}

/// The semitone offset (from the root, 0 to 11) of the degree with this index (0 to count - 1).
int degreeOffset(int mask, int index) {
    for (int bit = 0; bit < 12; ++bit) {
        if ((mask >> bit) & 1) {
            if (index == 0) {
                return bit;
            }
            --index;
        }
    }
    return 0;
}

/// Index of the highest degree at or below `semitone` (0 to 11 above the root).
int degreeIndexBelow(int mask, int semitone) {
    int index = -1;
    for (int bit = 0; bit <= semitone; ++bit) {
        index += (mask >> bit) & 1;
    }
    return std::max(index, 0);
}

/// A pitch as a count of degrees from the root's octave 0, floored; `remainder` is what a pitch off the scale has above
/// its degree.
int absoluteDegree(int pitch, int root, int mask, int* remainder) {
    const int rel = pitch - root;
    const int octave = floorDiv(rel, 12);
    const int within = floorMod(rel, 12);
    const int index = degreeIndexBelow(mask, within);
    if (remainder != nullptr) {
        *remainder = within - degreeOffset(mask, index);
    }
    return octave * degreeCount(mask) + index;
}

int pitchOfDegree(int absolute, int root, int mask, int remainder) {
    const int count = degreeCount(mask);
    const int octave = floorDiv(absolute, count);
    const int index = floorMod(absolute, count);
    return root + octave * 12 + degreeOffset(mask, index) + remainder;
}

/// The pitch after the transposition, before the octave shift and the range.
int shifted(int pitch, int offset, TransposeMode mode, const TransposeInfo& info) {
    if (mode == TransposeMode::Chromatic || info.scaleMask == 0) {
        return pitch + offset;
    }
    const int mask = maskOf(info);
    int remainder = 0;
    const int degree = absoluteDegree(pitch, info.root, mask, &remainder);
    return pitchOfDegree(degree + offset, info.root, mask, remainder);
}

} // namespace

int clampTranspose(int offset) {
    return std::clamp(offset, -kMaxTranspose, kMaxTranspose);
}

int referenceNoteFor(const TransposeSettings& settings, int rootPitchClass) {
    if (settings.referenceNote >= 0) {
        return std::min(settings.referenceNote, 127);
    }
    return 36 + floorMod(rootPitchClass, 12);
}

int offsetForKey(int key, int reference, TransposeMode mode, const TransposeInfo& info) {
    if (mode == TransposeMode::Chromatic || info.scaleMask == 0) {
        return clampTranspose(key - reference);
    }
    const int mask = maskOf(info);
    return clampTranspose(absoluteDegree(key, info.root, mask, nullptr) -
                          absoluteDegree(reference, info.root, mask, nullptr));
}

bool targetsVoice(TransposeTarget target, const TransposeInfo& info) {
    switch (target) {
    case TransposeTarget::Both:
        return true;
    case TransposeTarget::Bass:
        return !info.melody;
    case TransposeTarget::Melody:
        return info.melody;
    }
    return true;
}

int voiceOctaveShift(int offset, TransposeMode mode, const TransposeInfo& info) {
    if (offset == 0) {
        return 0;
    }
    const int high = shifted(info.highest, offset, mode, info);
    const int low = shifted(info.lowest, offset, mode, info);
    if (high > info.rangeHigh && low - 12 >= 0) {
        return -12;
    }
    if (low < info.rangeLow && high + 12 <= 127) {
        return 12;
    }
    return 0;
}

int transposedPitch(int pitch, int offset, TransposeMode mode, const TransposeInfo& info, int voiceShift) {
    if (offset == 0 && voiceShift == 0) {
        return std::clamp(pitch, 0, 127);
    }
    int result = shifted(pitch, offset, mode, info) + voiceShift;
    // A note still outside the range goes into it by octaves (the contour of the voice is kept as far as it goes).
    for (int guard = 0; guard < 11 && result > info.rangeHigh && result - 12 >= 0; ++guard) {
        result -= 12;
    }
    for (int guard = 0; guard < 11 && result < info.rangeLow && result + 12 <= 127; ++guard) {
        result += 12;
    }
    return std::clamp(result, 0, 127);
}

} // namespace mm::core
