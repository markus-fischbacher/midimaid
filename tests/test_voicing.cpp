#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/Random.h"
#include "core/Voicing.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

constexpr PitchClass kA = 9;
const Range kStabs = kDefaultStabRange; // 55-79

const Scale& scaleOf(const char* id) {
    const Scale* scale = findScale(id);
    REQUIRE(scale != nullptr);
    return *scale;
}

std::vector<int> pcs(std::vector<int> values) {
    return values;
}

std::vector<PitchClass> tonesOf(const char* scaleId, Chord chord, ChordColor color) {
    return colorTones(scaleOf(scaleId), kA, chord, color);
}

const Chord kI{0, ChordQuality::Minor};
const Chord kFlatVI{8, ChordQuality::Major};
const Chord kFlatVII{10, ChordQuality::Major};

std::vector<int> asInts(const std::vector<PitchClass>& tones) {
    return std::vector<int>(tones.begin(), tones.end());
}

} // namespace

TEST_CASE("chord colour ids", "[voicing]") {
    CHECK(parseChordColorId("min") == ChordColor::Min);
    CHECK(parseChordColorId("fifth") == ChordColor::Fifth);
    CHECK(parseChordColorId("min7") == ChordColor::Min7);
    CHECK(parseChordColorId("min9") == ChordColor::Min9);
    CHECK(parseChordColorId("cluster") == ChordColor::Cluster);
    CHECK_FALSE(parseChordColorId("sus").has_value()); // needs a random choice
    CHECK_FALSE(parseChordColorId("").has_value());
    CHECK_FALSE(parseChordColorId("Min").has_value());
}

TEST_CASE("colour tones in A natural minor (hand computed)", "[voicing]") {
    // A = 9, C = 0, E = 4, G = 7, B = 11, D = 2
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Min)) == pcs({9, 0, 4}));
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Fifth)) == pcs({9, 4}));
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Min7)) == pcs({9, 0, 4, 7}));
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Min9)) == pcs({9, 11, 0, 7})); // A B C G, fifth left out
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Sus2)) == pcs({9, 11, 4}));
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Sus4)) == pcs({9, 2, 4}));
    // no flat second in natural minor: the cluster falls back to root + fifth
    CHECK(asInts(tonesOf("natural_minor", kI, ChordColor::Cluster)) == pcs({9, 4}));
}

TEST_CASE("colour tones: phrygian has the flat second", "[voicing]") {
    // A phrygian: A Bb C D E F G; Bb = 10
    CHECK(asInts(tonesOf("phrygian", kI, ChordColor::Cluster)) == pcs({9, 10, 4}));
    CHECK(asInts(tonesOf("phrygian", kI, ChordColor::Min9)) == pcs({9, 10, 0, 7})); // ninth is the minor second
    CHECK(asInts(tonesOf("phrygian", kI, ChordColor::Sus2)) == pcs({9, 4}));        // no major second: sus tone lost
    CHECK(asInts(tonesOf("phrygian", kI, ChordColor::Sus4)) == pcs({9, 2, 4}));
}

TEST_CASE("colour tones: pentatonic loses ninth and sus2", "[voicing]") {
    // A minor pentatonic: A C D E G
    CHECK(asInts(tonesOf("minor_pentatonic", kI, ChordColor::Min9)) == pcs({9, 0, 4, 7})); // as Min7
    CHECK(asInts(tonesOf("minor_pentatonic", kI, ChordColor::Min7)) == pcs({9, 0, 4, 7}));
    CHECK(asInts(tonesOf("minor_pentatonic", kI, ChordColor::Sus2)) == pcs({9, 4}));
    CHECK(asInts(tonesOf("minor_pentatonic", kI, ChordColor::Sus4)) == pcs({9, 2, 4}));
}

TEST_CASE("colour tones: seventh follows the scale", "[voicing]") {
    // F major in A natural minor: Eb is not in the scale, E (major seventh) is: F A C E
    CHECK(asInts(tonesOf("natural_minor", kFlatVI, ChordColor::Min7)) == pcs({5, 9, 0, 4}));
    // G major: F is in the scale (minor seventh): G B D F
    CHECK(asInts(tonesOf("natural_minor", kFlatVII, ChordColor::Min7)) == pcs({7, 11, 2, 5}));
    // diminished chord keeps its flat fifth: B dim = B D F, + A
    CHECK(asInts(tonesOf("natural_minor", Chord{2, ChordQuality::Diminished}, ChordColor::Min7)) == pcs({11, 2, 5, 9}));
}

TEST_CASE("colour tones: fifth of a diminished chord is the flat fifth", "[voicing]") {
    CHECK(asInts(tonesOf("natural_minor", Chord{2, ChordQuality::Diminished}, ChordColor::Fifth)) == pcs({11, 5}));
}

TEST_CASE("pickChordColor: weights, sus and unknown ids", "[voicing]") {
    {
        Pcg32 rng = Pcg32::fromSeed(1);
        const std::vector<WeightedId> only{{"min9", 5}};
        for (int i = 0; i < 20; ++i) {
            CHECK(pickChordColor(only, rng) == ChordColor::Min9);
        }
    }
    {
        Pcg32 rng = Pcg32::fromSeed(2);
        const std::vector<WeightedId> junk{{"banana", 10}, {"min", 0}};
        CHECK_FALSE(pickChordColor(junk, rng).has_value());
        CHECK_FALSE(pickChordColor({}, rng).has_value());
        const std::vector<WeightedId> withJunk{{"banana", 1000}, {"fifth", 1}};
        for (int i = 0; i < 50; ++i) {
            CHECK(pickChordColor(withJunk, rng) == ChordColor::Fifth); // unknown ids never win
        }
    }
    {
        Pcg32 rng = Pcg32::fromSeed(3);
        const std::vector<WeightedId> sus{{"sus", 1}};
        int sus2 = 0, sus4 = 0;
        for (int i = 0; i < 400; ++i) {
            const auto color = pickChordColor(sus, rng);
            REQUIRE(color.has_value());
            REQUIRE((*color == ChordColor::Sus2 || *color == ChordColor::Sus4));
            (*color == ChordColor::Sus2 ? sus2 : sus4)++;
        }
        CHECK(sus2 > 120);
        CHECK(sus4 > 120);
    }
    {
        // roughly the weights 40/30/20/10
        Pcg32 rng = Pcg32::fromSeed(4);
        const std::vector<WeightedId> peak{{"min", 40}, {"fifth", 30}, {"min7", 20}, {"sus", 10}};
        int counts[7] = {};
        for (int i = 0; i < 4000; ++i) {
            counts[static_cast<int>(*pickChordColor(peak, rng))]++;
        }
        CHECK(counts[static_cast<int>(ChordColor::Min)] > 1400);
        CHECK(counts[static_cast<int>(ChordColor::Min)] < 1800);
        CHECK(counts[static_cast<int>(ChordColor::Min9)] == 0);
        CHECK(counts[static_cast<int>(ChordColor::Sus2)] + counts[static_cast<int>(ChordColor::Sus4)] > 300);
    }
}

TEST_CASE("pickChordColor: deterministic per seed", "[voicing]") {
    const std::vector<WeightedId> colors{{"min", 40}, {"sus", 30}, {"cluster", 30}};
    Pcg32 a = Pcg32::fromSeed(77);
    Pcg32 b = Pcg32::fromSeed(77);
    for (int i = 0; i < 100; ++i) {
        CHECK(pickChordColor(colors, a) == pickChordColor(colors, b));
    }
}

TEST_CASE("voiceChord: first chord sits near the centre of the range (hand computed)", "[voicing]") {
    const auto tones = tonesOf("natural_minor", kI, ChordColor::Min); // A C E
    // second inversion E4 A4 C5: low + high = 136, the range centre doubled is 134
    CHECK(voiceChord(tones, kStabs, false, nullptr) == std::vector<int>{64, 69, 72});
    // with doubling: C4 E4 A4 C5 (low + high = 132)
    CHECK(voiceChord(tones, kStabs, true, nullptr) == std::vector<int>{60, 64, 69, 72});
}

TEST_CASE("voiceChord: two-tone colours get the octave doubling", "[voicing]") {
    const auto fifth = tonesOf("natural_minor", kI, ChordColor::Fifth); // A E
    // E4 A4 E5 (low + high 140) beats A3 E4 A4 (126): both are 6 and 8 away from 134
    CHECK(voiceChord(fifth, kStabs, false, nullptr) == std::vector<int>{64, 69, 76});
    CHECK(voiceChord(fifth, kStabs, true, nullptr) == std::vector<int>{64, 69, 76});
}

TEST_CASE("voiceChord: previous voicing decides by least movement (hand computed)", "[voicing]") {
    const std::vector<int> a{64, 69, 72}; // A minor
    const auto flatVI = voiceChord(tonesOf("natural_minor", kFlatVI, ChordColor::Min), kStabs, false, &a);
    CHECK(flatVI == std::vector<int>{65, 69, 72}); // A and C stay, E moves up one
    CHECK(maxVoiceMovement(a, *flatVI) == 1);

    const auto flatVII = voiceChord(tonesOf("natural_minor", kFlatVII, ChordColor::Min), kStabs, false, &*flatVI);
    CHECK(flatVII == std::vector<int>{67, 71, 74}); // G B D
    CHECK(maxVoiceMovement(*flatVI, *flatVII) == 2);
}

TEST_CASE("voiceChord: common tones stay put", "[voicing]") {
    const std::vector<int> prev{60, 64, 69};                                                                // C E A
    const auto next = voiceChord(tonesOf("natural_minor", kFlatVI, ChordColor::Min), kStabs, false, &prev); // F A C
    REQUIRE(next.has_value());
    CHECK(*next == std::vector<int>{60, 65, 69}); // C and A stay, E -> F
}

TEST_CASE("voiceChord: chords of different size", "[voicing]") {
    const std::vector<int> triad{64, 69, 72};
    const auto seventh = voiceChord(tonesOf("natural_minor", kI, ChordColor::Min7), kStabs, false, &triad);
    REQUIRE(seventh.has_value());
    CHECK(seventh->size() == 4);
    // the triad tones stay, the new tone G sits next to them
    for (const int pitch : triad) {
        CHECK(std::find(seventh->begin(), seventh->end(), pitch) != seventh->end());
    }
    CHECK(maxVoiceMovement(triad, *seventh) <= 2);
}

TEST_CASE("voiceChord: empty previous behaves like none, ties prefer narrow then low", "[voicing]") {
    const auto tones = tonesOf("natural_minor", kI, ChordColor::Min);
    const std::vector<int> empty;
    CHECK(voiceChord(tones, kStabs, false, &empty) == voiceChord(tones, kStabs, false, nullptr));
    // identical previous voicing: the same voicing again (cost 0)
    const std::vector<int> prev{60, 64, 69};
    CHECK(voiceChord(tones, kStabs, false, &prev) == prev);
}

TEST_CASE("voiceChord: failure cases", "[voicing]") {
    const auto tones = tonesOf("natural_minor", kI, ChordColor::Min);
    CHECK_FALSE(voiceChord(tones, Range{60, 63}, false, nullptr).has_value()); // nothing fits
    CHECK_FALSE(voiceChord({}, kStabs, false, nullptr).has_value());
    const std::vector<PitchClass> single{9};
    CHECK_FALSE(voiceChord(single, kStabs, true, nullptr).has_value()); // 1 tone + doubling = 2 notes
    const std::vector<PitchClass> five{0, 2, 4, 5, 7};
    CHECK_FALSE(voiceChord(five, kStabs, false, nullptr).has_value());
    // doubling does not fit but the plain voicing would
    const auto fifth = tonesOf("natural_minor", kI, ChordColor::Fifth);
    CHECK_FALSE(voiceChord(fifth, Range{69, 75}, false, nullptr).has_value());
    // exactly fits: A4 E5 A5 in 69-81
    CHECK(voiceChord(fifth, Range{69, 81}, false, nullptr) == std::vector<int>{69, 76, 81});
    // range ends are inclusive
    CHECK(voiceChord(tones, Range{57, 64}, false, nullptr) == std::vector<int>{57, 60, 64});
}

TEST_CASE("voiceChord: duplicate and out-of-octave pitch classes are normalised", "[voicing]") {
    const std::vector<PitchClass> messy{9, 9, 0, 4, 4};
    CHECK(voiceChord(messy, kStabs, false, nullptr) ==
          voiceChord(tonesOf("natural_minor", kI, ChordColor::Min), kStabs, false, nullptr));
}

TEST_CASE("voiceProgression: i - bVI - bVII - i with voice leading", "[voicing]") {
    const Chord chords[] = {kI, kFlatVI, kFlatVII, kI};
    const ChordColor colors[] = {ChordColor::Min, ChordColor::Min, ChordColor::Min, ChordColor::Min};
    const bool doubling[] = {false, false, false, false};
    const auto voicings = voiceProgression(scaleOf("natural_minor"), kA, chords, colors, doubling, kStabs);
    REQUIRE(voicings.has_value());
    REQUIRE(voicings->size() == 4);
    CHECK((*voicings)[0] == std::vector<int>{64, 69, 72});
    CHECK((*voicings)[1] == std::vector<int>{65, 69, 72});
    CHECK((*voicings)[2] == std::vector<int>{67, 71, 74});
    for (size_t i = 1; i < voicings->size(); ++i) {
        CHECK(maxVoiceMovement((*voicings)[i - 1], (*voicings)[i]) <= 2);
    }
}

TEST_CASE("voiceProgression: size mismatch and impossible chords", "[voicing]") {
    const Chord chords[] = {kI, kFlatVI};
    const ChordColor colors[] = {ChordColor::Min};
    const bool doubling[] = {false, false};
    CHECK_FALSE(voiceProgression(scaleOf("natural_minor"), kA, chords, colors, doubling, kStabs).has_value());
    const ChordColor two[] = {ChordColor::Min, ChordColor::Min};
    const bool one[] = {false};
    CHECK_FALSE(voiceProgression(scaleOf("natural_minor"), kA, chords, two, one, kStabs).has_value());
    CHECK_FALSE(voiceProgression(scaleOf("natural_minor"), kA, chords, two, doubling, Range{60, 63}).has_value());
    CHECK(voiceProgression(scaleOf("natural_minor"), kA, {}, {}, {}, kStabs)->empty());
}

TEST_CASE("maxVoiceMovement", "[voicing]") {
    CHECK(maxVoiceMovement({60, 64, 67}, {60, 64, 67}) == 0);
    CHECK(maxVoiceMovement({60, 64, 67}, {60, 65, 67}) == 1);
    CHECK(maxVoiceMovement({60, 64, 67}, {72}) == 12); // the tone 60 is 12 away from the only tone 72
    CHECK(maxVoiceMovement({}, {60}) == 0);
    CHECK(maxVoiceMovement({60}, {}) == 0);
}

TEST_CASE("property: voicings are valid close positions inside the range", "[voicing][property]") {
    Pcg32 rng(11, 3);
    const auto scales = allScales();
    for (int round = 0; round < 400; ++round) {
        const Scale& scale = scales[rng.bounded(static_cast<uint32_t>(scales.size()))];
        const PitchClass root = static_cast<PitchClass>(rng.bounded(12));
        const int low = 40 + static_cast<int>(rng.bounded(30));
        const Range range{low, low + 12 + static_cast<int>(rng.bounded(20))};
        const size_t count = 1 + rng.bounded(6);
        std::vector<Chord> chords;
        std::vector<ChordColor> colors;
        std::unique_ptr<bool[]> doublingFlags(new bool[count]);
        for (size_t i = 0; i < count; ++i) {
            chords.push_back(Chord{static_cast<uint8_t>(rng.bounded(12)), static_cast<ChordQuality>(rng.bounded(5))});
            colors.push_back(static_cast<ChordColor>(rng.bounded(7)));
            doublingFlags[i] = rng.chance(40);
        }
        const auto voicings =
            voiceProgression(scale, root, chords, colors, std::span<const bool>(doublingFlags.get(), count), range);
        if (!voicings) {
            continue;
        }
        REQUIRE(voicings->size() == count);
        for (size_t i = 0; i < count; ++i) {
            const std::vector<int>& v = (*voicings)[i];
            const auto tones = colorTones(scale, root, chords[i], colors[i]);
            REQUIRE(v.size() >= 3);
            REQUIRE(v.size() <= 5);
            CHECK(std::is_sorted(v.begin(), v.end()));
            CHECK(std::adjacent_find(v.begin(), v.end()) == v.end());
            CHECK(v.front() >= range.low);
            CHECK(v.back() <= range.high);
            // close position: the core tones lie within one octave, the doubling sits exactly 12 above the lowest
            const size_t core = tones.size();
            CHECK(v[core - 1] - v.front() < 12);
            if (v.size() > core) {
                CHECK(v.back() == v.front() + 12);
            }
            // every pitch class belongs to the colour and every colour tone appears
            for (const int pitch : v) {
                CHECK(std::find(tones.begin(), tones.end(), pitchClassOf(pitch)) != tones.end());
            }
            for (const PitchClass pc : tones) {
                CHECK(std::any_of(v.begin(), v.end(), [&](int pitch) { return pitchClassOf(pitch) == pc; }));
            }
            // colour tones are allowed by the chord-scale principle, so the constraint layer keeps them
            for (const int pitch : v) {
                CHECK(isAllowed(scale, root, chords[i], pitch));
            }
        }
        // deterministic
        CHECK(voicings ==
              voiceProgression(scale, root, chords, colors, std::span<const bool>(doublingFlags.get(), count), range));
    }
}

TEST_CASE("constraint layer keeps voiced stabs untouched", "[voicing]") {
    for (const char* scaleId : {"natural_minor", "phrygian", "dorian", "minor_pentatonic", "harmonic_minor",
                                "phrygian_dominant", "locrian"}) {
        for (int c = 0; c < 7; ++c) {
            const auto color = static_cast<ChordColor>(c);
            Pattern p = makeEmptyPattern(1, "x");
            p.context.scaleId = scaleId;
            p.voices[1].archetypeId = "stabs";
            const auto tones = colorTones(scaleOf(scaleId), p.context.root, p.context.progression[0].chord, color);
            const auto voicing = voiceChord(tones, kStabs, false, nullptr);
            REQUIRE(voicing.has_value());
            for (const int pitch : *voicing) {
                Note note;
                note.id = allocateNoteId(p);
                note.pitch = static_cast<uint8_t>(pitch);
                note.startTick = 0;
                note.lengthTicks = 960;
                p.voices[1].notes.push_back(note);
            }
            applyConstraints(p, ConstraintSettings::defaultsFor(p));
            INFO(scaleId << " colour " << c);
            std::vector<int> after;
            for (const Note& n : p.voices[1].notes) {
                after.push_back(n.pitch);
            }
            CHECK(after == *voicing);
        }
    }
}

TEST_CASE("colour tones: scales with both sevenths and both seconds prefer the minor seventh and the major second",
          "[voicing]") {
    static const uint8_t intervals[] = {0, 1, 2, 3, 5, 7, 10, 11};
    const Scale wide{"wide", intervals};
    // A C E, seventh G (not G#), ninth B (not Bb): A, B, C, G
    CHECK(asInts(colorTones(wide, kA, kI, ChordColor::Min7)) == pcs({9, 0, 4, 7}));
    CHECK(asInts(colorTones(wide, kA, kI, ChordColor::Min9)) == pcs({9, 11, 0, 7}));
    CHECK(asInts(colorTones(wide, kA, kI, ChordColor::Cluster)) == pcs({9, 10, 4}));
}

TEST_CASE("voiceChord: ties go to the narrower voicing, then to the lower one", "[voicing]") {
    // A C E in 50-75: A3 C4 E4 and C4 E4 A4 are both 4 away from the centre; the narrower one wins
    const auto tones = tonesOf("natural_minor", kI, ChordColor::Min);
    CHECK(voiceChord(tones, Range{50, 75}, false, nullptr) == std::vector<int>{57, 60, 64});
    // C, C# in 61-84 (octave doubling): C#4 C5 C#5 and C5 C#5 C6 are both 12 wide and 11 from the centre; the lower
    // wins
    const std::vector<PitchClass> cluster{0, 1};
    CHECK(voiceChord(cluster, Range{61, 84}, false, nullptr) == std::vector<int>{61, 72, 73});
}

TEST_CASE("voiceChord: movement counts in both directions (hand computed)", "[voicing]") {
    // Bb B Eb after the voicing C#4 F5: B3 Eb4 Bb4 costs 2+2+9 (tones to the previous) + 2+7 (previous to the tones)
    // = 22; looking at one direction only would pick Bb3 B3 Eb4 (7 + 16) or Eb4 Bb4 B4 (counting the other way).
    const std::vector<PitchClass> tones{10, 11, 3};
    const std::vector<int> previous{61, 77};
    CHECK(voiceChord(tones, kStabs, false, &previous) == std::vector<int>{59, 63, 70});
}

TEST_CASE("maxVoiceMovement: both directions count", "[voicing]") {
    CHECK(maxVoiceMovement({60}, {60, 72}) == 12);
    CHECK(maxVoiceMovement({60, 72}, {60}) == 12);
}
