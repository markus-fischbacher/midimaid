#pragma once

#include "core/PlaybackPattern.h"

#include <cstdint>
#include <vector>

namespace mm::core {

/// A note of the read-only roll as a rectangle in unit coordinates of the view: x and width along the pattern (0 to 1),
/// y from the top (high pitch) and height as one pitch row (0 to 1).
struct RollRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint8_t velocity = 100;

    bool operator==(const RollRect&) const = default;
};

struct RollLayout {
    std::vector<RollRect> notes;
    int lowPitch = 0;  ///< lowest pitch row shown
    int highPitch = 0; ///< highest pitch row shown
    uint32_t bars = 1; ///< bar lines to draw (length in bars, rounded up)
};

/// Lays out the notes of one voice for the roll of the voice UI (SPEC 8.1). The shown pitch range covers all notes
/// plus one row above and below and is at least `minRows` rows high, centred on the notes (so a one-note pattern does
/// not fill the view). It stays within 0 to 127. Empty notes give an empty layout around middle C. Pure function.
RollLayout layoutRoll(const std::vector<PatternNote>& notes, uint32_t lengthTicks, int minRows = 12);

} // namespace mm::core
