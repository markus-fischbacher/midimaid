#include "core/ChordSymbol.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace mm::core;

namespace {

constexpr ChordQuality kMajor = ChordQuality::Major;
constexpr ChordQuality kMinor = ChordQuality::Minor;
constexpr ChordQuality kDim = ChordQuality::Diminished;
constexpr ChordQuality kSus2 = ChordQuality::Sus2;
constexpr ChordQuality kSus4 = ChordQuality::Sus4;

} // namespace

TEST_CASE("chord symbols of STYLES.md 1.17 (examples in A minor)", "[core][chord]") {
    struct Case {
        const char* text;
        uint8_t offset;
        ChordQuality quality;
    };
    const std::vector<Case> cases = {
        {"i", 0, kMinor},     // Am
        {"iv", 5, kMinor},    // Dm
        {"V", 7, kMajor},     // E
        {"bII", 1, kMajor},   // Bb
        {"bIII", 3, kMajor},  // C
        {"bVI", 8, kMajor},   // F
        {"bVII", 10, kMajor}, // G
        {"I", 0, kMajor},     {"ii", 2, kMinor},      {"II", 2, kMajor},   {"iii", 4, kMinor}, {"III", 4, kMajor},
        {"IV", 5, kMajor},    {"v", 7, kMinor},       {"vi", 9, kMinor},   {"VI", 9, kMajor},  {"vii", 11, kMinor},
        {"VII", 11, kMajor},  {"viio", 11, kDim},     {"iio", 2, kDim},    {"ivo", 5, kDim},   {"Isus2", 0, kSus2},
        {"isus4", 0, kSus4},  {"bIIIsus2", 3, kSus2}, {"Vsus4", 7, kSus4}, {"#IV", 6, kMajor}, {"#iv", 6, kMinor},
        {"bV", 6, kMajor},    {"#I", 1, kMajor},      {"bI", 11, kMajor}, // wraps below the root
        {"#VII", 0, kMajor},                                              // wraps above the octave
        {"bii", 1, kMinor},
    };
    for (const Case& c : cases) {
        INFO(c.text);
        const auto chord = parseChordSymbol(c.text);
        REQUIRE(chord.has_value());
        CHECK(chord->rootOffset == c.offset);
        CHECK(chord->quality == c.quality);
    }
}

TEST_CASE("invalid chord symbols are rejected", "[core][chord]") {
    for (const char* text : {"",
                             "b",
                             "#",
                             "bb",
                             "##VI",
                             "b#VI",
                             "#bVI",
                             "Vi",
                             "iV",
                             "IIII",
                             "viiii",
                             "VIII",
                             "viii",
                             "VIIo",
                             "ioo",
                             "isus",
                             "isus3",
                             "Isus",
                             "Isus4x",
                             "sus4",
                             " I",
                             "I ",
                             "i|bVII",
                             "1",
                             "x",
                             "o",
                             "oi",
                             "iosus4",
                             "Iosus2",
                             "bIo",
                             "\xe2\x99\xad"
                             "VI",
                             "VI\xc2\xb0",
                             "I-V",
                             "I7",
                             "i7",
                             "bVI7",
                             "IV/V"}) {
        INFO("'" << text << "'");
        CHECK_FALSE(parseChordSymbol(text).has_value());
    }
}

TEST_CASE("chord symbols are formatted canonically", "[core][chord]") {
    CHECK(formatChordSymbol({0, kMinor}) == "i");
    CHECK(formatChordSymbol({0, kMajor}) == "I");
    CHECK(formatChordSymbol({1, kMajor}) == "bII");
    CHECK(formatChordSymbol({3, kMajor}) == "bIII");
    CHECK(formatChordSymbol({5, kMinor}) == "iv");
    CHECK(formatChordSymbol({6, kMajor}) == "bV"); // never #IV
    CHECK(formatChordSymbol({7, kMajor}) == "V");
    CHECK(formatChordSymbol({8, kMajor}) == "bVI");
    CHECK(formatChordSymbol({10, kMajor}) == "bVII");
    CHECK(formatChordSymbol({11, kDim}) == "viio");
    CHECK(formatChordSymbol({3, kSus2}) == "bIIIsus2");
    CHECK(formatChordSymbol({0, kSus4}) == "Isus4");
    CHECK(formatChordSymbol({12, kMinor}) == "i"); // the offset is taken modulo 12
}

TEST_CASE("every chord survives a format and parse round trip", "[core][chord]") {
    for (uint8_t offset = 0; offset < 12; ++offset) {
        for (const ChordQuality quality : {kMajor, kMinor, kDim, kSus2, kSus4}) {
            const Chord chord{offset, quality};
            const std::string text = formatChordSymbol(chord);
            INFO(text);
            const auto parsed = parseChordSymbol(text);
            REQUIRE(parsed.has_value());
            REQUIRE(*parsed == chord);
            REQUIRE(formatChordSymbol(*parsed) == text);
        }
    }
}

TEST_CASE("sharp spellings normalise to the canonical flat spelling", "[core][chord]") {
    CHECK(formatChordSymbol(*parseChordSymbol("#IV")) == "bV");
    CHECK(formatChordSymbol(*parseChordSymbol("#iv")) == "bv" + std::string());
    CHECK(formatChordSymbol(*parseChordSymbol("#I")) == "bII");
}

TEST_CASE("the progressions of the style profiles parse", "[core][chord]") {
    // STYLES.md 2-4: i-bVII, i-bVI, i-iv, i-bVI-bIII-bVII, i-iv-bVI-V, i-bVII-bVI-bVII, i-bVI-iv-bVII, i-bII
    for (const char* text : {"i", "bVII", "bVI", "iv", "bIII", "V", "bII"}) {
        INFO(text);
        CHECK(parseChordSymbol(text).has_value());
    }
    CHECK(*parseChordSymbol("bVI") == Chord{8, kMajor});
    CHECK(*parseChordSymbol("V") == Chord{7, kMajor});
}
