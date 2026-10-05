#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace mm::core {

/// Pitch class, 0-11, 0 = C. Pitches elsewhere in the code are MIDI numbers; note names exist only in the UI.
using PitchClass = uint8_t;

PitchClass pitchClassOf(int midi);

/// A scale as semitone intervals above the root: ascending, first entry 0, all within one octave.
struct Scale {
    std::string_view id;
    std::span<const uint8_t> intervals;

    size_t size() const { return intervals.size(); }
};

/// All scales of the style profiles (STYLES.md): natural_minor, phrygian, dorian, minor_pentatonic, harmonic_minor,
/// phrygian_dominant, locrian.
std::span<const Scale> allScales();

/// nullptr for an unknown id.
const Scale* findScale(std::string_view id);

enum class ChordQuality { Major, Minor, Diminished, Sus2, Sus4 };

/// Chord symbol as in STYLES.md 1.17: root as semitones above the key's root, plus quality. Sevenths and other
/// options come from the chord colours of the voicing, not from the chord.
struct Chord {
    uint8_t rootOffset = 0; // 0-11
    ChordQuality quality = ChordQuality::Minor;
};

/// The three triad intervals of a quality, relative to the chord root.
std::array<uint8_t, 3> chordIntervals(ChordQuality quality);

/// Pitch classes of the chord for a key with the given root.
std::array<PitchClass, 3> chordPitchClasses(PitchClass keyRoot, const Chord& chord);

/// The voice base: the lowest pitch with the key's pitch class inside [low, high] (SPEC 7.3). nullopt if none.
std::optional<int> voiceBase(PitchClass root, int low, int high);

/// Scale degree to MIDI pitch (SPEC 7.3). `degree` counts from 1 and wraps into higher octaves (a pentatonic scale
/// has 5 degrees per octave), `alt` is -1, 0 or +1 semitones, `octave` is relative to `base` (the voice base).
/// nullopt for degree < 1 or a result outside 0-127.
std::optional<int> degreeToMidi(const Scale& scale, int base, int degree, int alt, int octave);

bool inScale(const Scale& scale, PitchClass root, int midi);
bool isChordTone(PitchClass root, const Chord& chord, int midi);

/// Chord-scale principle (D-57): a pitch is allowed if it is in the scale or a tone of the sounding chord.
bool isAllowed(const Scale& scale, PitchClass root, const std::optional<Chord>& chord, int midi);

enum class TieBreak { Down, Up };

/// Snaps to the nearest allowed pitch. At equal distance `tieBreak` decides. nullopt only if nothing allowed lies
/// within the MIDI range nearby (cannot happen for the scales above).
std::optional<int> quantize(const Scale& scale, PitchClass root, const std::optional<Chord>& chord, int midi,
                            TieBreak tieBreak = TieBreak::Down);

} // namespace mm::core
