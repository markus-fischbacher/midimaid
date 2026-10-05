#include "core/Theory.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace mm::core;

namespace {

constexpr PitchClass kA = 9;

const Scale& scale(std::string_view id) {
    const Scale* found = findScale(id);
    REQUIRE(found != nullptr);
    return *found;
}

std::vector<int> intervalsOf(std::string_view id) {
    const auto& s = scale(id);
    return {s.intervals.begin(), s.intervals.end()};
}

} // namespace

TEST_CASE("scale tables have the expected intervals", "[core][theory]") {
    CHECK(intervalsOf("natural_minor") == std::vector<int>{0, 2, 3, 5, 7, 8, 10});
    CHECK(intervalsOf("phrygian") == std::vector<int>{0, 1, 3, 5, 7, 8, 10});
    CHECK(intervalsOf("dorian") == std::vector<int>{0, 2, 3, 5, 7, 9, 10});
    CHECK(intervalsOf("minor_pentatonic") == std::vector<int>{0, 3, 5, 7, 10});
    CHECK(intervalsOf("harmonic_minor") == std::vector<int>{0, 2, 3, 5, 7, 8, 11});
    CHECK(intervalsOf("phrygian_dominant") == std::vector<int>{0, 1, 4, 5, 7, 8, 10});
    CHECK(intervalsOf("locrian") == std::vector<int>{0, 1, 3, 5, 6, 8, 10});
}

TEST_CASE("all scales are ascending within one octave and start at the root", "[core][theory]") {
    CHECK(allScales().size() == 7);
    for (const Scale& s : allScales()) {
        INFO(s.id);
        REQUIRE(s.size() >= 5);
        CHECK(s.intervals.front() == 0);
        CHECK(s.intervals.back() < 12);
        CHECK(std::is_sorted(s.intervals.begin(), s.intervals.end()));
        CHECK(std::adjacent_find(s.intervals.begin(), s.intervals.end()) == s.intervals.end()); // no duplicates
    }
}

TEST_CASE("findScale", "[core][theory]") {
    CHECK(findScale("dorian") != nullptr);
    CHECK(findScale("dorian")->id == "dorian");
    CHECK(findScale("major") == nullptr);
    CHECK(findScale("") == nullptr);
}

TEST_CASE("pitch classes", "[core][theory]") {
    CHECK(pitchClassOf(60) == 0);
    CHECK(pitchClassOf(33) == 9);
    CHECK(pitchClassOf(127) == 7);
    CHECK(pitchClassOf(0) == 0);
    CHECK(pitchClassOf(-1) == 11); // robust for pitches below 0 (relative calculations)
}

TEST_CASE("voice base is the lowest root pitch inside the range", "[core][theory]") {
    CHECK(voiceBase(kA, 28, 52) == 33); // the example of SPEC 7.3: bass, root A
    CHECK(voiceBase(4, 28, 52) == 28);  // E
    for (PitchClass root = 0; root < 12; ++root) {
        const auto base = voiceBase(root, 28, 52);
        REQUIRE(base.has_value());
        CHECK(pitchClassOf(*base) == root);
        CHECK(*base >= 28);
        CHECK(*base <= 39);
    }
    CHECK_FALSE(voiceBase(0, 61, 70).has_value()); // no C between 61 and 70
    CHECK_FALSE(voiceBase(0, 70, 61).has_value()); // empty range
}

TEST_CASE("degrees map to MIDI pitches", "[core][theory]") {
    const auto& minor = scale("natural_minor");
    const int base = 33; // A
    CHECK(degreeToMidi(minor, base, 1, 0, 0) == 33);
    CHECK(degreeToMidi(minor, base, 3, 0, 0) == 36);  // C
    CHECK(degreeToMidi(minor, base, 7, 0, 0) == 43);  // G
    CHECK(degreeToMidi(minor, base, 7, 1, 0) == 44);  // raised leading tone G#
    CHECK(degreeToMidi(minor, base, 2, -1, 0) == 34); // lowered second
    CHECK(degreeToMidi(minor, base, 8, 0, 0) == 45);  // wraps into the next octave
    CHECK(degreeToMidi(minor, base, 15, 0, 0) == 57);
    CHECK(degreeToMidi(minor, base, 1, 0, 1) == 45);
    CHECK(degreeToMidi(minor, base, 1, 0, -1) == 21);
}

TEST_CASE("pentatonic scales have five degrees per octave", "[core][theory]") {
    const auto& pentatonic = scale("minor_pentatonic");
    CHECK(degreeToMidi(pentatonic, 33, 5, 0, 0) == 43);
    CHECK(degreeToMidi(pentatonic, 33, 6, 0, 0) == 45);
    CHECK(degreeToMidi(pentatonic, 33, 7, 0, 0) == 48);
}

TEST_CASE("invalid degrees and out-of-range results are rejected", "[core][theory]") {
    const auto& minor = scale("natural_minor");
    CHECK_FALSE(degreeToMidi(minor, 33, 0, 0, 0).has_value());
    CHECK_FALSE(degreeToMidi(minor, 33, -3, 0, 0).has_value());
    CHECK_FALSE(degreeToMidi(minor, 120, 1, 0, 1).has_value()); // 132 > 127
    CHECK_FALSE(degreeToMidi(minor, 10, 1, 0, -1).has_value()); // -2 < 0
}

TEST_CASE("chord intervals and pitch classes", "[core][theory]") {
    CHECK(chordIntervals(ChordQuality::Major) == std::array<uint8_t, 3>{0, 4, 7});
    CHECK(chordIntervals(ChordQuality::Minor) == std::array<uint8_t, 3>{0, 3, 7});
    CHECK(chordIntervals(ChordQuality::Diminished) == std::array<uint8_t, 3>{0, 3, 6});
    CHECK(chordIntervals(ChordQuality::Sus2) == std::array<uint8_t, 3>{0, 2, 7});
    CHECK(chordIntervals(ChordQuality::Sus4) == std::array<uint8_t, 3>{0, 5, 7});

    // In A minor: V = E major (E, G#, B), bVI = F major (F, A, C), i = A minor (A, C, E)
    CHECK(chordPitchClasses(kA, {7, ChordQuality::Major}) == std::array<PitchClass, 3>{4, 8, 11});
    CHECK(chordPitchClasses(kA, {8, ChordQuality::Major}) == std::array<PitchClass, 3>{5, 9, 0});
    CHECK(chordPitchClasses(kA, {0, ChordQuality::Minor}) == std::array<PitchClass, 3>{9, 0, 4});
}

TEST_CASE("chord-scale principle: the leading tone over the V chord of natural minor", "[core][theory]") {
    const auto& minor = scale("natural_minor");
    const int gSharp = 44;
    CHECK_FALSE(isAllowed(minor, kA, std::nullopt, gSharp));
    CHECK_FALSE(isAllowed(minor, kA, Chord{0, ChordQuality::Minor}, gSharp));
    CHECK(isAllowed(minor, kA, Chord{7, ChordQuality::Major}, gSharp));
    CHECK(quantize(minor, kA, std::nullopt, gSharp) == 43);                      // snaps down to G
    CHECK(quantize(minor, kA, Chord{7, ChordQuality::Major}, gSharp) == gSharp); // stays
}

TEST_CASE("chord-scale principle: the minor sixth of a bVI chord over minor pentatonic", "[core][theory]") {
    const auto& pentatonic = scale("minor_pentatonic"); // A C D E G
    const int f = 53;
    CHECK_FALSE(isAllowed(pentatonic, kA, std::nullopt, f));
    CHECK(isAllowed(pentatonic, kA, Chord{8, ChordQuality::Major}, f));
    CHECK(quantize(pentatonic, kA, std::nullopt, f) == 52); // E
    CHECK(quantize(pentatonic, kA, Chord{8, ChordQuality::Major}, f) == f);
}

TEST_CASE("quantize tie break", "[core][theory]") {
    const auto& minor = scale("natural_minor");
    const int aSharp = 34; // exactly between A (33) and B (35)
    CHECK(quantize(minor, kA, std::nullopt, aSharp) == 33);
    CHECK(quantize(minor, kA, std::nullopt, aSharp, TieBreak::Down) == 33);
    CHECK(quantize(minor, kA, std::nullopt, aSharp, TieBreak::Up) == 35);
}

TEST_CASE("quantize near the edges of the MIDI range", "[core][theory]") {
    const auto& minor = scale("natural_minor");
    // C minor-ish: in A natural minor, MIDI 0 is C (in scale), 127 is G (in scale)
    CHECK(quantize(minor, kA, std::nullopt, 0) == 0);
    CHECK(quantize(minor, kA, std::nullopt, 127) == 127);
    CHECK(quantize(minor, 1, std::nullopt, 0).has_value());
}

TEST_CASE("quantize properties over all roots, scales and chords", "[core][theory]") {
    const std::vector<std::optional<Chord>> chords = {std::nullopt,
                                                      Chord{0, ChordQuality::Minor},
                                                      Chord{7, ChordQuality::Major},
                                                      Chord{8, ChordQuality::Major},
                                                      Chord{1, ChordQuality::Diminished},
                                                      Chord{5, ChordQuality::Sus2},
                                                      Chord{3, ChordQuality::Sus4}};

    for (const Scale& s : allScales()) {
        for (PitchClass root = 0; root < 12; ++root) {
            for (const auto& chord : chords) {
                for (int midi = 0; midi <= 127; ++midi) {
                    for (const auto tie : {TieBreak::Down, TieBreak::Up}) {
                        const auto result = quantize(s, root, chord, midi, tie);
                        REQUIRE(result.has_value());
                        // result is allowed
                        REQUIRE(isAllowed(s, root, chord, *result));
                        // allowed pitches stay where they are
                        if (isAllowed(s, root, chord, midi)) {
                            REQUIRE(*result == midi);
                        }
                        // nothing allowed is strictly closer
                        const int distance = std::abs(*result - midi);
                        for (int other = std::max(0, midi - distance + 1); other <= std::min(127, midi + distance - 1);
                             ++other) {
                            REQUIRE_FALSE(isAllowed(s, root, chord, other));
                        }
                        // the tie break picks the lower or the upper neighbour when both are equally near
                        const int below = midi - distance;
                        const int above = midi + distance;
                        const bool bothOk = distance > 0 && below >= 0 && above <= 127 &&
                                            isAllowed(s, root, chord, below) && isAllowed(s, root, chord, above);
                        if (bothOk) {
                            REQUIRE(*result == (tie == TieBreak::Down ? below : above));
                        }
                    }
                }
            }
        }
    }
}
