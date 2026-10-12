#pragma once

#include <cstdint>

namespace mm::core {

/// Live transposition (SPEC 3.16, v1.1): the pure part. Nothing here allocates or locks: the audio thread calls it for
/// every note that starts.

enum class TransposeMode { Chromatic, Diatonic };
enum class TransposeBehavior { Hold, Momentary };
enum class TransposeTiming { NextNote, NextBeat, NextBar };
enum class TransposeTarget { Both, Bass, Melody };

constexpr int kMaxTranspose = 12;

/// The settings of the keyboard transposition (SPEC 3.16, 8.4). `referenceNote` < 0 means "the key of the pattern, in
/// the bass register": MIDI 36 + root (C2 to B2, D-215). `channel` 0 accepts every MIDI channel, 1 to 16 only that one.
struct TransposeSettings {
    TransposeMode mode = TransposeMode::Chromatic;
    TransposeBehavior behavior = TransposeBehavior::Hold;
    TransposeTiming timing = TransposeTiming::NextNote;
    TransposeTarget target = TransposeTarget::Both;
    int referenceNote = -1;
    int channel = 0;
    bool exportTransposed = true; ///< a file dragged out holds the notes as they sound now (SPEC 3.16)
    bool operator==(const TransposeSettings&) const = default;
};

/// What the engine knows about the voice it plays, to transpose its notes (the part of the pattern the output stage
/// has not used): the key, the range of the voice and the extremes of the rendered notes.
struct TransposeInfo {
    uint8_t root = 0;        ///< pitch class of the key
    uint16_t scaleMask = 0;  ///< bit n set: the degree root + n is in the scale; 0 = unknown (diatonic acts as chromatic)
    uint8_t rangeLow = 0;    ///< range of the voice (the constraint layer's)
    uint8_t rangeHigh = 127;
    uint8_t lowest = 0;      ///< lowest and highest pitch of the rendered notes
    uint8_t highest = 127;
    bool melody = false;     ///< the voice has the melody role (the target of the transposition)
    bool operator==(const TransposeInfo&) const = default;
};

int clampTranspose(int offset);

/// The note a reference of `settings` resolves to: the given one, or 36 + root.
int referenceNoteFor(const TransposeSettings& settings, int rootPitchClass);

/// The transposition a key stands for: its distance to the reference in semitones (chromatic) or in degrees of the
/// scale (diatonic; a key off the scale counts as the degree below), limited to +-12.
int offsetForKey(int key, int reference, TransposeMode mode, const TransposeInfo& info);

/// Whether the transposition applies to a voice with `info`.
bool targetsVoice(TransposeTarget target, const TransposeInfo& info);

/// SPEC 3.16: when the transposition takes the voice out of its range, the whole voice moves an octave to keep its
/// contour: -12, 0 or +12 (added to every pitch after the transposition).
int voiceOctaveShift(int offset, TransposeMode mode, const TransposeInfo& info);

/// One pitch after the transposition by `offset` (semitones or degrees by `mode`) and the octave shift of the voice;
/// a pitch that is still outside the range goes into it by octaves, one by one. Always 0 to 127.
int transposedPitch(int pitch, int offset, TransposeMode mode, const TransposeInfo& info, int voiceShift);

} // namespace mm::core
