#include "core/Theory.h"

#include <algorithm>

namespace mm::core {

namespace {

constexpr std::array<uint8_t, 7> kNaturalMinor{0, 2, 3, 5, 7, 8, 10};
constexpr std::array<uint8_t, 7> kPhrygian{0, 1, 3, 5, 7, 8, 10};
constexpr std::array<uint8_t, 7> kDorian{0, 2, 3, 5, 7, 9, 10};
constexpr std::array<uint8_t, 5> kMinorPentatonic{0, 3, 5, 7, 10};
constexpr std::array<uint8_t, 7> kHarmonicMinor{0, 2, 3, 5, 7, 8, 11};
constexpr std::array<uint8_t, 7> kPhrygianDominant{0, 1, 4, 5, 7, 8, 10};
constexpr std::array<uint8_t, 7> kLocrian{0, 1, 3, 5, 6, 8, 10};

const std::array<Scale, 7> kScales{{
    {"natural_minor", kNaturalMinor},
    {"phrygian", kPhrygian},
    {"dorian", kDorian},
    {"minor_pentatonic", kMinorPentatonic},
    {"harmonic_minor", kHarmonicMinor},
    {"phrygian_dominant", kPhrygianDominant},
    {"locrian", kLocrian},
}};

int mod12(int value) {
    return ((value % 12) + 12) % 12;
}

bool inMidiRange(int midi) {
    return midi >= 0 && midi <= 127;
}

} // namespace

PitchClass pitchClassOf(int midi) {
    return static_cast<PitchClass>(mod12(midi));
}

std::span<const Scale> allScales() {
    return kScales;
}

const Scale* findScale(std::string_view id) {
    for (const Scale& scale : kScales) {
        if (scale.id == id) {
            return &scale;
        }
    }
    return nullptr;
}

std::array<uint8_t, 3> chordIntervals(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::Major:
        return {0, 4, 7};
    case ChordQuality::Minor:
        return {0, 3, 7};
    case ChordQuality::Diminished:
        return {0, 3, 6};
    case ChordQuality::Sus2:
        return {0, 2, 7};
    case ChordQuality::Sus4:
        return {0, 5, 7};
    }
    return {0, 3, 7};
}

std::array<PitchClass, 3> chordPitchClasses(PitchClass keyRoot, const Chord& chord) {
    const auto intervals = chordIntervals(chord.quality);
    std::array<PitchClass, 3> result{};
    for (size_t i = 0; i < intervals.size(); ++i) {
        result[i] = static_cast<PitchClass>(mod12(keyRoot + chord.rootOffset + intervals[i]));
    }
    return result;
}

std::optional<int> voiceBase(PitchClass root, int low, int high) {
    for (int pitch = std::max(low, 0); pitch <= std::min(high, 127); ++pitch) {
        if (pitchClassOf(pitch) == root) {
            return pitch;
        }
    }
    return std::nullopt;
}

std::optional<int> degreeToMidi(const Scale& scale, int base, int degree, int alt, int octave) {
    if (degree < 1 || scale.size() == 0) {
        return std::nullopt;
    }
    const int count = static_cast<int>(scale.size());
    const int index = degree - 1;
    const int pitch =
        base + 12 * octave + 12 * (index / count) + scale.intervals[static_cast<size_t>(index % count)] + alt;
    if (!inMidiRange(pitch)) {
        return std::nullopt;
    }
    return pitch;
}

bool inScale(const Scale& scale, PitchClass root, int midi) {
    const int relative = mod12(midi - root);
    return std::find(scale.intervals.begin(), scale.intervals.end(), static_cast<uint8_t>(relative)) !=
           scale.intervals.end();
}

bool isChordTone(PitchClass root, const Chord& chord, int midi) {
    const auto pitchClasses = chordPitchClasses(root, chord);
    return std::find(pitchClasses.begin(), pitchClasses.end(), pitchClassOf(midi)) != pitchClasses.end();
}

bool isAllowed(const Scale& scale, PitchClass root, const std::optional<Chord>& chord, int midi) {
    return inScale(scale, root, midi) || (chord.has_value() && isChordTone(root, *chord, midi));
}

std::optional<int> quantize(const Scale& scale, PitchClass root, const std::optional<Chord>& chord, int midi,
                            TieBreak tieBreak) {
    for (int distance = 0; distance <= 12; ++distance) {
        const int below = midi - distance;
        const int above = midi + distance;
        const bool belowOk = inMidiRange(below) && isAllowed(scale, root, chord, below);
        const bool aboveOk = inMidiRange(above) && isAllowed(scale, root, chord, above);
        if (belowOk && aboveOk) {
            return tieBreak == TieBreak::Down ? below : above; // distance 0: both equal
        }
        if (belowOk) {
            return below;
        }
        if (aboveOk) {
            return above;
        }
    }
    return std::nullopt;
}

} // namespace mm::core
