#pragma once

#include "core/PlaybackPattern.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mm::core {

struct MidiFileOptions {
    double bpm = 120.0; ///< written as tempo event (the current DAW tempo, SPEC 3.4)
    uint8_t timeSigNumerator = 4;
    uint8_t timeSigDenominator = 4; ///< power of two
    std::string trackName;
};

/// Serialises a pattern as a Standard MIDI File (format 0, 960 PPQ) with tempo, time signature and track name.
/// The end-of-track event sits at the pattern end, so the clip length matches the pattern length.
std::vector<uint8_t> writeMidiFile(const PatternView& pattern, const MidiFileOptions& options);

} // namespace mm::core
