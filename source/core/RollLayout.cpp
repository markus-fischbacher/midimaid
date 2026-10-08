#include "core/RollLayout.h"

#include "core/Pattern.h"

#include <algorithm>

namespace mm::core {

RollLayout layoutRoll(const std::vector<PatternNote>& notes, uint32_t lengthTicks, int minRows) {
    RollLayout layout;
    const uint32_t length = std::max<uint32_t>(lengthTicks, 1);
    layout.bars = std::max<uint32_t>((length + kTicksPerBar - 1) / kTicksPerBar, 1);
    minRows = std::clamp(minRows, 1, 128);

    int low = 60;
    int high = 60;
    if (!notes.empty()) {
        low = 127;
        high = 0;
        for (const PatternNote& note : notes) {
            low = std::min<int>(low, note.pitch);
            high = std::max<int>(high, note.pitch);
        }
    }
    low = std::max(low - 1, 0);
    high = std::min(high + 1, 127);
    // Widen to the minimum height around the centre, then slide back into 0 to 127.
    const int missing = minRows - (high - low + 1);
    if (missing > 0) {
        low -= missing / 2;
        high += missing - missing / 2;
        if (low < 0) {
            high -= low;
            low = 0;
        }
        if (high > 127) {
            low -= high - 127;
            high = 127;
        }
        low = std::max(low, 0);
    }
    layout.lowPitch = low;
    layout.highPitch = high;

    const float rows = static_cast<float>(high - low + 1);
    for (const PatternNote& note : notes) {
        RollRect rect;
        rect.x = static_cast<float>(note.startTick) / static_cast<float>(length);
        rect.width = static_cast<float>(note.lengthTicks) / static_cast<float>(length);
        rect.y = static_cast<float>(high - note.pitch) / rows;
        rect.height = 1.0f / rows;
        rect.velocity = note.velocity;
        layout.notes.push_back(rect);
    }
    return layout;
}

} // namespace mm::core
