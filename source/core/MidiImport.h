#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mm::core {

/// Reading a MIDI clip for the import of a voice or a reference (SPEC 3.18, 3.20). Pure standard library, never
/// throws and never crashes on a broken file.

struct MidiClipNote {
    uint8_t channel = 1; ///< 1-16
    uint8_t pitch = 0;
    uint8_t velocity = 100;
    uint32_t startTick = 0; ///< 960 PPQ from the clip start, exact (not quantised)
    uint32_t lengthTicks = 1;

    bool operator==(const MidiClipNote&) const = default;
};

struct MidiClip {
    std::vector<MidiClipNote> notes; ///< sorted by start, then pitch
    uint32_t lengthTicks = 0;        ///< the end of the clip: the later of the end-of-track events and the last note
    bool onlyFourFour = true;        ///< no time signature event other than 4/4
    uint16_t sourcePpq = 480;
    double bpm = 0; ///< the first tempo event, 0 when there is none
};

enum class MidiReadError {
    None,
    Empty,       ///< no bytes
    NotMidi,     ///< no "MThd" header
    Unsupported, ///< SMPTE time division, format 2
    TooBig,      ///< above the limits below
    Broken,      ///< a track whose events cannot be read
    NoNotes      ///< a valid file without any note
};

struct MidiReadResult {
    MidiReadError error = MidiReadError::None;
    std::string message; ///< what is wrong, for the log (never shown as a user text)
    MidiClip clip;
    bool ok() const { return error == MidiReadError::None; }
};

constexpr size_t kMaxMidiFileBytes = size_t{8} << 20;
constexpr size_t kMaxMidiTracks = 64;
constexpr size_t kMaxMidiNotes = 200000;
constexpr uint64_t kMaxMidiTicks = uint64_t{1} << 31; ///< in the file's own division

/// Reads a Standard MIDI File (format 0 or 1; all tracks are merged, a note on with velocity 0 is a note off, running
/// status, meta events and system exclusive are understood). A note without note off ends with its track. Tolerates a
/// track chunk that is longer than the file (it is read as far as it goes).
MidiReadResult readMidiFile(const uint8_t* data, size_t size);
MidiReadResult readMidiFile(const std::vector<uint8_t>& bytes);

/// The pattern lengths in bars.
constexpr uint32_t kMaxImportBars = 16;

enum class ImportStatus {
    Ok,
    NotFourFour, ///< the clip has a time signature other than 4/4: no import (SPEC 3.18)
    NoNotes
};

/// What an import makes of a clip (SPEC 3.18): only 4/4; the length is rounded up to the next allowed pattern length
/// (1, 2, 4, 8, 16 bars, the rest stays empty); from longer material only the first 16 bars count. Positions and
/// lengths are taken exactly, also triplets.
struct ImportPlan {
    ImportStatus status = ImportStatus::NoNotes;
    bool truncated = false;          ///< the clip is longer than 16 bars: only the first 16 are used
    uint32_t sourceBars = 0;         ///< the clip length in bars, rounded up
    uint32_t lengthBars = 0;         ///< the pattern length: 1, 2, 4, 8 or 16
    size_t droppedNotes = 0;         ///< notes that start after the last bar
    std::vector<MidiClipNote> notes; ///< inside the pattern, a note that reaches over the end is cut there
};

ImportPlan planImport(const MidiClip& clip);

/// The smallest allowed pattern length (1, 2, 4, 8, 16) that holds `bars`; 16 for more.
uint32_t roundUpPatternBars(uint32_t bars);

} // namespace mm::core
