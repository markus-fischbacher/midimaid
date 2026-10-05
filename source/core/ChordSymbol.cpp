#include "core/ChordSymbol.h"

#include <array>
#include <cctype>

namespace mm::core {

namespace {

struct Numeral {
    std::string_view upper;
    std::string_view lower;
    int semitones;
};

// Longest first, so "vii" is not read as "vi" followed by "i".
constexpr std::array<Numeral, 7> kNumerals{{
    {"VII", "vii", 11},
    {"III", "iii", 4},
    {"II", "ii", 2},
    {"IV", "iv", 5},
    {"VI", "vi", 9},
    {"V", "v", 7},
    {"I", "i", 0},
}};

constexpr std::array<std::string_view, 12> kCanonicalUpper{"I",  "bII", "II",  "bIII", "III",  "IV",
                                                           "bV", "V",   "bVI", "VI",   "bVII", "VII"};

std::string toLower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c != 'b') {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

} // namespace

std::optional<Chord> parseChordSymbol(std::string_view text) {
    int accidental = 0;
    if (!text.empty() && (text.front() == 'b' || text.front() == '#')) {
        accidental = text.front() == 'b' ? -1 : 1;
        text.remove_prefix(1);
    }

    for (const Numeral& numeral : kNumerals) {
        const bool isUpper = text.substr(0, numeral.upper.size()) == numeral.upper;
        const bool isLower = text.substr(0, numeral.lower.size()) == numeral.lower;
        if (!isUpper && !isLower) {
            continue;
        }
        const std::string_view rest = text.substr(numeral.upper.size());
        ChordQuality quality = isUpper ? ChordQuality::Major : ChordQuality::Minor;
        if (rest == "sus2") {
            quality = ChordQuality::Sus2;
        } else if (rest == "sus4") {
            quality = ChordQuality::Sus4;
        } else if (rest == "o" && isLower) {
            quality = ChordQuality::Diminished;
        } else if (!rest.empty()) {
            return std::nullopt;
        }
        const int offset = ((numeral.semitones + accidental) % 12 + 12) % 12;
        return Chord{static_cast<uint8_t>(offset), quality};
    }
    return std::nullopt;
}

std::string formatChordSymbol(const Chord& chord) {
    const std::string upper(kCanonicalUpper[chord.rootOffset % 12]);
    switch (chord.quality) {
    case ChordQuality::Major:
        return upper;
    case ChordQuality::Minor:
        return toLower(upper);
    case ChordQuality::Diminished:
        return toLower(upper) + "o";
    case ChordQuality::Sus2:
        return upper + "sus2";
    case ChordQuality::Sus4:
        return upper + "sus4";
    }
    return upper;
}

} // namespace mm::core
