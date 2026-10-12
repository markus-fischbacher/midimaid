#pragma once

#include "core/GrooveLibrary.h"
#include "core/Pattern.h"
#include "core/PlaybackPattern.h"
#include "core/StyleProfile.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm::core {

/// What the engine plays for one voice: the output stage (SPEC 4.2a) applied with the settings of the style, reduced
/// to the notes of voice `voiceIndex`. A slide that reaches past the pattern end is cut at it. Sorted by start tick.
/// A voice index beyond the pattern's voices gives no notes. `octaveShift` is the instance's own octave (stage 1).
struct PlaybackNotes {
    std::vector<PatternNote> notes;
    uint32_t lengthTicks = 0;
    TransposeInfo transpose; ///< key, range and extremes of the voice for the live transposition (SPEC 3.16)
};

PlaybackNotes renderVoiceForPlayback(const Pattern& pattern, const StyleProfile& style, size_t voiceIndex,
                                     int octaveShift = 0, const GrooveLibrary* grooves = nullptr);

} // namespace mm::core
