#pragma once

#include "core/Pattern.h"

#include <optional>
#include <string_view>

namespace mm::core {

/// A pitch range as MIDI numbers, both ends included. Note names exist only in the UI (R-1).
struct Range {
    int low = 0;
    int high = 127;

    int width() const { return high - low + 1; } ///< number of pitches in the range
    bool contains(int pitch) const { return pitch >= low && pitch <= high; }

    bool operator==(const Range&) const = default;
};

// Default ranges of STYLES.md 1.6 (Ableton/Logic naming: bass E0-E2, melody G2-E5, stabs G2-G4).
constexpr Range kDefaultBassRange{28, 52};
constexpr Range kDefaultMelodyRange{55, 88};
constexpr Range kDefaultStabRange{55, 79};

/// A range needs at least 12 pitches so that every pitch class occurs (the voice base of SPEC 7.3 needs the root).
constexpr int kMinRangeWidth = 12;
constexpr int kMaxOctaveOffset = 2;

/// The ranges a style profile sets for the voices; profiles may narrow the defaults. The stab range lives in
/// `Pattern::voicing` (profile: `melody.voicing.range`).
struct RegisterProfile {
    Range bass = kDefaultBassRange;
    Range melody = kDefaultMelodyRange;
};

/// Moves a range by `octaveOffset` octaves (the offset is limited to -2...+2) and keeps it inside MIDI 0-127.
Range applyOctaveOffset(Range range, int octaveOffset);

/// Archetypes that play chords in the voicing range (stabs) instead of the line range of the melody.
bool isVoicingArchetype(std::string_view archetypeId);

/// The range a voice is generated and constrained in: bass range, melody range, or for stab archetypes the voicing
/// range of the pattern, plus the octave offset of the voice (SPEC 4.2). nullopt if the result is narrower than
/// `kMinRangeWidth` pitches.
std::optional<Range> effectiveRange(const RegisterProfile& profile, VoiceRole role, std::string_view archetypeId,
                                    const VoicingSettings& voicing, int octaveOffset);

/// Moves the notes of a voice by whole octaves, keeping the pitch class (a result outside 0-127 moves back by octaves).
/// Notes with a locked pitch (voice or note level) stay. Returns the number of notes that moved.
size_t shiftVoiceByOctaves(Track& track, int octaves);

/// Sets the octave offset of a voice (limited to -2...+2) and moves its notes by the difference in octaves, so the
/// pattern keeps its shape within the shifted range. Pitch locks are respected like in `shiftVoiceByOctaves`.
void changeOctaveOffset(Track& track, int newOffset);

} // namespace mm::core
