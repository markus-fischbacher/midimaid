#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/Motif.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

const Scale& scaleOf(const char* id) {
    const Scale* scale = findScale(id);
    REQUIRE(scale != nullptr);
    return *scale;
}

MotifRules minorRules() {
    MotifRules rules;
    rules.scale = &scaleOf("natural_minor");
    return rules; // A minor, tonic triad A C E, base A3 (57), range 55-88, span 7
}

MotifNote note(uint8_t step, uint32_t length, int degree) {
    MotifNote n;
    n.startStep = step;
    n.lengthTicks = length;
    n.degree = static_cast<int8_t>(degree);
    return n;
}

/// Steps (scale index): 0 A3, 1 B3, 2 C4, 3 D4, 4 E4, 5 F4, 6 G4, 7 A4
Motif handMotif() {
    Motif motif;
    motif.bars = 1;
    motif.notes = {note(0, 240, 0), note(2, 240, 1), note(4, 240, 2), note(6, 240, 4), note(8, 480, 2)};
    return motif;
}

} // namespace

TEST_CASE("scaleStepToMidi: steps wrap by octaves (hand computed)", "[motif]") {
    const Scale& scale = scaleOf("natural_minor"); // 0 2 3 5 7 8 10
    CHECK(scaleStepToMidi(scale, 57, 0) == 57);
    CHECK(scaleStepToMidi(scale, 57, 1) == 59);
    CHECK(scaleStepToMidi(scale, 57, 2) == 60);
    CHECK(scaleStepToMidi(scale, 57, 6) == 67);
    CHECK(scaleStepToMidi(scale, 57, 7) == 69);
    CHECK(scaleStepToMidi(scale, 57, 8) == 71);
    CHECK(scaleStepToMidi(scale, 57, -1) == 55);
    CHECK(scaleStepToMidi(scale, 57, -2) == 53);
    CHECK(scaleStepToMidi(scale, 57, -7) == 45);
    CHECK(scaleStepToMidi(scale, 57, -8) == 43);
    const Scale& pentatonic = scaleOf("minor_pentatonic"); // 0 3 5 7 10
    CHECK(scaleStepToMidi(pentatonic, 57, 4) == 67);
    CHECK(scaleStepToMidi(pentatonic, 57, 5) == 69);
    CHECK(scaleStepToMidi(pentatonic, 57, -1) == 55);
}

TEST_CASE("isValidMotif: the hand motif is valid, every rule has a violating case", "[motif]") {
    const MotifRules rules = minorRules();
    REQUIRE(isValidMotif(handMotif(), rules));
    CHECK(weakStepCount(handMotif()) == 2);
    CHECK(weakStepNonChordCount(handMotif(), rules) == 1);

    auto expectInvalid = [&](const char* why, auto change) {
        Motif motif = handMotif();
        change(motif);
        INFO(why);
        CHECK_FALSE(isValidMotif(motif, rules));
    };
    expectInvalid("empty", [](Motif& m) { m.notes.clear(); });
    expectInvalid("three bars", [](Motif& m) { m.bars = 3; });
    expectInvalid("zero bars", [](Motif& m) { m.bars = 0; });
    expectInvalid("step beyond the motif", [](Motif& m) { m.notes.back().startStep = 16; });
    expectInvalid("not sorted", [](Motif& m) { std::swap(m.notes[1], m.notes[2]); });
    expectInvalid("same step twice", [](Motif& m) { m.notes[1].startStep = 0; });
    expectInvalid("length not a multiple of 1/64", [](Motif& m) { m.notes[0].lengthTicks = 100; });
    expectInvalid("length zero", [](Motif& m) { m.notes[0].lengthTicks = 0; });
    expectInvalid("overlap", [](Motif& m) { m.notes[0].lengthTicks = 540; });
    expectInvalid("beyond the end", [](Motif& m) { m.notes.back().lengthTicks = 2040; });
    expectInvalid("velocity offset too high", [](Motif& m) { m.notes[0].velocityOffset = 21; });
    expectInvalid("velocity offset too low", [](Motif& m) { m.notes[0].velocityOffset = -21; });
    expectInvalid("strong step on a non-chord tone", [](Motif& m) { m.notes[2].degree = 3; });
    expectInvalid("no non-chord tone on the weak steps", [](Motif& m) { m.notes[1].degree = 2; });
    expectInvalid("non-chord tone not approached by step", [](Motif& m) { m.notes[1].degree = 3; });
    expectInvalid("span too wide", [](Motif& m) { m.notes[4].degree = 7; });

    MotifRules narrow = rules;
    narrow.range = Range{58, 88};
    CHECK_FALSE(isValidMotif(handMotif(), narrow)); // A3 (57) below the range
    MotifRules noScale = rules;
    noScale.scale = nullptr;
    CHECK_FALSE(isValidMotif(handMotif(), noScale));
}

TEST_CASE("isValidMotif: non-chord tone at the start or end is not a passing tone", "[motif]") {
    const MotifRules rules = minorRules();
    Motif motif;
    motif.bars = 1;
    motif.notes = {note(1, 240, 1), note(4, 240, 2), note(8, 240, 0)}; // step 1: B first, nothing before it
    CHECK_FALSE(isValidMotif(motif, rules));
    motif.notes = {note(0, 240, 0), note(4, 240, 2), note(9, 240, 1)}; // step 9: B last, nothing after it
    CHECK_FALSE(isValidMotif(motif, rules));
}

TEST_CASE("isValidMotif: the 20 % share is exact", "[motif]") {
    // 5 weak notes need 1 non-chord tone, 6 weak notes need 2 (5 * nonChord >= weak)
    const MotifRules rules = minorRules();
    Motif five;
    five.bars = 2;
    five.notes = {note(0, 240, 0), note(1, 240, 1), note(2, 240, 2), note(3, 240, 2),
                  note(5, 240, 2), note(7, 240, 2), note(8, 240, 2)};
    // weak: steps 1, 2, 3, 5, 7 -> 5 notes, step 1 is B between A and C (passing): 1 of 5 = 20 %
    CHECK(weakStepCount(five) == 5);
    CHECK(weakStepNonChordCount(five, rules) == 1);
    CHECK(isValidMotif(five, rules));
    Motif six = five;
    six.notes.push_back(note(11, 240, 2)); // one more weak note: 1 of 6 < 20 %
    CHECK(weakStepCount(six) == 6);
    CHECK_FALSE(isValidMotif(six, rules));
}

TEST_CASE("realizeMotif: placement, shift and octave folding (hand computed)", "[motif]") {
    const MotifRules rules = minorRules();
    const auto notes = realizeMotif(handMotif(), rules, 0, 3840);
    REQUIRE(notes.size() == 5);
    const int pitches[] = {57, 59, 60, 64, 60};
    const uint32_t starts[] = {3840, 4320, 4800, 5280, 5760};
    const uint32_t lengths[] = {240, 240, 240, 240, 480};
    for (size_t i = 0; i < notes.size(); ++i) {
        CHECK(notes[i].pitch == pitches[i]);
        CHECK(notes[i].startTick == starts[i]);
        CHECK(notes[i].lengthTicks == lengths[i]);
        CHECK(notes[i].velocity == 100);
        CHECK_FALSE(notes[i].accent);
        CHECK(notes[i].id == 0);
    }
    const auto shifted = realizeMotif(handMotif(), rules, 2, 0); // steps 2 3 4 6 4: C D E G E
    const int shiftedPitches[] = {60, 62, 64, 67, 64};
    for (size_t i = 0; i < shifted.size(); ++i) {
        CHECK(shifted[i].pitch == shiftedPitches[i]);
    }
    MotifRules folded = rules;
    folded.range = Range{60, 72};
    const auto low = realizeMotif(handMotif(), folded, -5, 0); // 48 50 52 55 52 fold up to 60 62 64 67 64
    for (size_t i = 0; i < low.size(); ++i) {
        CHECK(low[i].pitch == shiftedPitches[i]);
    }
}

TEST_CASE("realizeMotif: accent, velocity offset and invalid rules", "[motif]") {
    MotifRules rules = minorRules();
    Motif motif = handMotif();
    motif.notes[1].accent = true;
    motif.notes[2].velocityOffset = -20;
    motif.notes[3].velocityOffset = 20;
    const auto notes = realizeMotif(motif, rules, 0, 0);
    CHECK(notes[1].accent);
    CHECK(notes[2].velocity == 80);
    CHECK(notes[3].velocity == 120);
    rules.range = Range{60, 65}; // narrower than an octave
    CHECK(realizeMotif(motif, rules, 0, 0).empty());
    rules.scale = nullptr;
    CHECK(realizeMotif(motif, rules, 0, 0).empty());
}

TEST_CASE("generateMotif: parameter checks", "[motif]") {
    Pcg32 rng(1, 1);
    const MotifRules rules = minorRules();
    MotifParams params;
    CHECK(generateMotif(params, rules, rng).has_value());
    MotifParams threeBars = params;
    threeBars.bars = 3;
    CHECK_FALSE(generateMotif(threeBars, rules, rng).has_value());
    MotifParams zeroBars = params;
    zeroBars.bars = 0;
    CHECK_FALSE(generateMotif(zeroBars, rules, rng).has_value());
    MotifParams noNotes = params;
    noNotes.minNotes = 0;
    CHECK_FALSE(generateMotif(noNotes, rules, rng).has_value());
    MotifParams reversed = params;
    reversed.minNotes = 5;
    reversed.maxNotes = 4;
    CHECK_FALSE(generateMotif(reversed, rules, rng).has_value());
    MotifRules noScale = rules;
    noScale.scale = nullptr;
    CHECK_FALSE(generateMotif(params, noScale, rng).has_value());
    // six notes in one bar need two weak steps, and a span of 0 cannot hold a non-chord tone
    MotifRules flat = rules;
    flat.maxSpanSemitones = 0;
    MotifParams six = params;
    six.minNotes = six.maxNotes = 6;
    CHECK_FALSE(generateMotif(six, flat, rng).has_value());
}

TEST_CASE("property: generated motifs satisfy every rule on all scales", "[motif][property]") {
    const auto scales = allScales();
    int generated = 0;
    int total = 0;
    for (const Scale& scale : scales) {
        for (uint8_t bars = 1; bars <= 2; ++bars) {
            for (uint64_t seed = 0; seed < 60; ++seed) {
                MotifRules rules;
                rules.scale = &scale;
                MotifParams params;
                params.bars = bars;
                params.minNotes = 3;
                params.maxNotes = 6;
                Pcg32 rng = Pcg32::fromSeed(seed * 7 + bars);
                Pcg32 again = Pcg32::fromSeed(seed * 7 + bars);
                const auto motif = generateMotif(params, rules, rng);
                ++total;
                CHECK(motif == generateMotif(params, rules, again)); // deterministic
                if (!motif) {
                    continue;
                }
                ++generated;
                CHECK(isValidMotif(*motif, rules));
                CHECK(motif->bars == bars);
                CHECK(motif->notes.size() >= 3);
                CHECK(motif->notes.size() <= 6);
                CHECK(weakStepNonChordCount(*motif, rules) * 5 >= weakStepCount(*motif));
                for (const Note& n : realizeMotif(*motif, rules, 0, 0)) {
                    CHECK(rules.range.contains(n.pitch));
                    CHECK(inScale(scale, rules.keyRoot, n.pitch));
                }
            }
        }
    }
    CHECK(generated * 100 >= total * 95); // the rules are met almost always
}

TEST_CASE("generateMotif: different seeds give different motifs", "[motif]") {
    const MotifRules rules = minorRules();
    MotifParams params;
    std::set<std::vector<int>> shapes;
    for (uint64_t seed = 0; seed < 50; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto motif = generateMotif(params, rules, rng);
        REQUIRE(motif.has_value());
        std::vector<int> shape;
        for (const MotifNote& n : motif->notes) {
            shape.push_back(n.startStep * 100 + n.degree);
        }
        shapes.insert(shape);
    }
    CHECK(shapes.size() >= 30);
}

TEST_CASE("generateMotif: span limit and range are respected", "[motif]") {
    MotifRules rules = minorRules();
    rules.maxSpanSemitones = 4;
    rules.range = Range{57, 69};
    MotifParams params;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto motif = generateMotif(params, rules, rng);
        if (!motif) {
            continue;
        }
        int low = 127;
        int high = 0;
        for (const MotifNote& n : motif->notes) {
            const int pitch = scaleStepToMidi(*rules.scale, rules.basePitch, n.degree);
            low = std::min(low, pitch);
            high = std::max(high, pitch);
        }
        CHECK(high - low <= 4);
        CHECK(low >= 57);
        CHECK(high <= 69);
    }
}

TEST_CASE("generateMotif: pickup notes sit before the bar line", "[motif]") {
    const MotifRules rules = minorRules();
    MotifParams always;
    always.minNotes = 4;
    always.maxNotes = 5;
    always.pickupPercent = 100;
    MotifParams never = always;
    never.pickupPercent = 100;
    never.allowPickup = false; // the switch beats the percentage
    int withPickup = 0;
    int withoutPickup = 0;
    int built = 0;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        Pcg32 a = Pcg32::fromSeed(seed);
        Pcg32 b = Pcg32::fromSeed(seed);
        const auto yes = generateMotif(always, rules, a);
        const auto no = generateMotif(never, rules, b);
        if (!yes || !no) {
            continue;
        }
        ++built;
        withPickup += yes->notes.back().startStep == 15 ? 1 : 0;
        withoutPickup += no->notes.back().startStep == 15 ? 1 : 0;
    }
    CHECK(built >= 90);
    CHECK(withPickup == built);       // a pickup always puts a note on the last step
    CHECK(withoutPickup * 2 < built); // otherwise the last step is rarer
}

TEST_CASE("planRepetitions: hypnotic varies every fourth occurrence", "[motif]") {
    Pcg32 rng(5, 5);
    CHECK(planRepetitions(0, RepetitionStyle::Hypnotic, rng).empty());
    CHECK(planRepetitions(3, RepetitionStyle::Hypnotic, rng) == std::vector<bool>{false, false, false});
    CHECK(planRepetitions(4, RepetitionStyle::Hypnotic, rng) == std::vector<bool>{false, false, false, true});
    CHECK(planRepetitions(8, RepetitionStyle::Hypnotic, rng) ==
          std::vector<bool>{false, false, false, true, false, false, false, true});
    CHECK(planRepetitions(9, RepetitionStyle::Hypnotic, rng).back() == false);
}

TEST_CASE("planRepetitions: standard repeats two to four times before a variation", "[motif]") {
    for (uint64_t seed = 0; seed < 50; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        CHECK(planRepetitions(1, RepetitionStyle::Standard, rng) == std::vector<bool>{false});
        CHECK(planRepetitions(2, RepetitionStyle::Standard, rng) == std::vector<bool>{false, false});
    }
    int counts[3] = {}; // repetitions 2, 3, 4
    Pcg32 rng(9, 9);
    const std::vector<bool> plan = planRepetitions(6000, RepetitionStyle::Standard, rng);
    REQUIRE_FALSE(plan[0]);
    REQUIRE_FALSE(plan[1]);
    size_t last = 0;
    bool first = true;
    for (size_t i = 0; i < plan.size(); ++i) {
        if (!plan[i]) {
            continue;
        }
        const size_t repetitions = first ? i : i - last - 1;
        REQUIRE(repetitions >= 2);
        REQUIRE(repetitions <= 4);
        ++counts[repetitions - 2];
        last = i;
        first = false;
    }
    const int total = counts[0] + counts[1] + counts[2];
    CHECK(counts[1] * 100 > total * 53); // about 60 %
    CHECK(counts[1] * 100 < total * 67);
    CHECK(counts[0] * 100 > total * 19); // about 25 %
    CHECK(counts[0] * 100 < total * 31);
    CHECK(counts[2] * 100 > total * 9); // about 15 %
    CHECK(counts[2] * 100 < total * 21);
}

TEST_CASE("varyMotif: micro variation changes one attribute of one note", "[motif]") {
    const MotifRules rules = minorRules();
    const Motif source = handMotif();
    for (uint64_t seed = 0; seed < 80; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto varied = varyMotif(source, MotifVariation::Micro, rules, rng);
        REQUIRE(varied.has_value());
        REQUIRE(varied->notes.size() == source.notes.size());
        CHECK(isValidMotif(*varied, rules));
        int changedNotes = 0;
        for (size_t i = 0; i < source.notes.size(); ++i) {
            const MotifNote& a = source.notes[i];
            const MotifNote& b = varied->notes[i];
            CHECK(a.startStep == b.startStep);
            CHECK(a.degree == b.degree);
            if (a != b) {
                ++changedNotes;
                const int attributes =
                    (a.lengthTicks != b.lengthTicks) + (a.accent != b.accent) + (a.velocityOffset != b.velocityOffset);
                CHECK(attributes == 1);
            }
        }
        CHECK(changedNotes == 1);
    }
}

TEST_CASE("varyMotif: invert mirrors around the first note and repairs strong steps (hand computed)", "[motif]") {
    MotifRules rules = minorRules();
    rules.range = Range{40, 88};
    rules.maxSpanSemitones = 12;
    Motif source;
    source.bars = 1;
    source.notes = {note(0, 240, 0), note(4, 240, 2), note(8, 240, 4), note(12, 240, 2)}; // A C E C
    REQUIRE(isValidMotif(source, rules));
    Pcg32 rng(1, 1);
    // mirrored 0 -2 -4 -2 (A F D F); F and D are no chord tones: repaired to E (-3) and C (-5): A E C E
    const auto inverted = varyMotif(source, MotifVariation::Invert, rules, rng);
    REQUIRE(inverted.has_value());
    CHECK(inverted->notes[0].degree == 0);
    CHECK(inverted->notes[1].degree == -3);
    CHECK(inverted->notes[2].degree == -5);
    CHECK(inverted->notes[3].degree == -3);
    // the hand motif cannot be inverted: its passing tone would no longer be approached by step
    const MotifRules plain = minorRules();
    CHECK_FALSE(varyMotif(handMotif(), MotifVariation::Invert, plain, rng).has_value());
}

TEST_CASE("varyMotif: new ending moves only the last note to another chord tone", "[motif]") {
    MotifRules rules = minorRules();
    Motif source;
    source.bars = 1;
    source.notes = {note(0, 240, 0), note(4, 240, 2), note(8, 240, 4), note(12, 240, 2)};
    for (uint64_t seed = 0; seed < 40; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto varied = varyMotif(source, MotifVariation::NewEnding, rules, rng);
        REQUIRE(varied.has_value());
        for (size_t i = 0; i + 1 < source.notes.size(); ++i) {
            CHECK(varied->notes[i] == source.notes[i]);
        }
        const int degree = varied->notes.back().degree;
        CHECK((degree == 0 || degree == 4)); // chord tones within 4 steps of C (2), except C itself
        CHECK(varied->notes.back().startStep == 12);
    }
}

TEST_CASE("varyMotif: shifting a note moves it by one step", "[motif]") {
    const MotifRules rules = minorRules();
    Motif source;
    source.bars = 1;
    source.notes = {note(0, 240, 0), note(4, 240, 2), note(9, 240, 4), note(12, 240, 2)};
    for (uint64_t seed = 0; seed < 40; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto varied = varyMotif(source, MotifVariation::ShiftNote, rules, rng);
        REQUIRE(varied.has_value());
        CHECK(isValidMotif(*varied, rules));
        int moved = 0;
        for (size_t i = 0; i < source.notes.size(); ++i) {
            const int delta = varied->notes[i].startStep - source.notes[i].startStep;
            CHECK(std::abs(delta) <= 1);
            CHECK(varied->notes[i].degree == source.notes[i].degree);
            moved += delta != 0 ? 1 : 0;
        }
        CHECK(moved == 1);
    }
}

TEST_CASE("varyMotif: transposition keeps the rules and changes the motif", "[motif]") {
    const MotifRules rules = minorRules();
    Motif source;
    source.bars = 1;
    source.notes = {note(0, 240, 0), note(4, 240, 2), note(8, 240, 4), note(12, 240, 2)};
    int found = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto varied = varyMotif(source, MotifVariation::Transpose, rules, rng);
        if (!varied) {
            continue;
        }
        ++found;
        CHECK(*varied != source);
        CHECK(isValidMotif(*varied, rules));
        for (size_t i = 0; i < source.notes.size(); ++i) {
            CHECK(varied->notes[i].startStep == source.notes[i].startStep);
        }
    }
    CHECK(found > 0);
}

TEST_CASE("varyMotif: empty motif and impossible variations", "[motif]") {
    Pcg32 rng(1, 1);
    const MotifRules rules = minorRules();
    Motif empty;
    CHECK_FALSE(varyMotif(empty, MotifVariation::Micro, rules, rng).has_value());
    // a single note on step 0 cannot shift (no step before it is free... it can move to step 1, but that is weak and
    // has no neighbours); nothing valid exists
    Motif single;
    single.bars = 1;
    single.notes = {note(0, 240, 0)};
    CHECK_FALSE(varyMotif(single, MotifVariation::ShiftNote, rules, rng).has_value());
    CHECK_FALSE(varyMotif(single, MotifVariation::Invert, rules, rng).has_value()); // mirrored: identical
}

TEST_CASE("buildMotifSequence: blocks follow the plan and every block is valid", "[motif]") {
    const MotifRules rules = minorRules();
    const Motif source = handMotif();
    for (uint64_t seed = 0; seed < 30; ++seed) {
        for (const RepetitionStyle style : {RepetitionStyle::Standard, RepetitionStyle::Hypnotic}) {
            Pcg32 rng = Pcg32::fromSeed(seed);
            Pcg32 planRng = Pcg32::fromSeed(seed);
            const auto sequence = buildMotifSequence(source, rules, 16, style, rng);
            const auto plan = planRepetitions(16, style, planRng);
            REQUIRE(sequence.size() == 16);
            for (size_t i = 0; i < sequence.size(); ++i) {
                CHECK(isValidMotif(sequence[i], rules));
                if (!plan[i]) {
                    CHECK(sequence[i] == source);
                }
            }
            CHECK(sequence[0] == source);
            CHECK(sequence[1] == source);
            CHECK(repetitionPermille(sequence) >= 600);
        }
    }
}

TEST_CASE("buildMotifSequence: hypnotic variations are micro variations", "[motif]") {
    const MotifRules rules = minorRules();
    const Motif source = handMotif();
    Pcg32 rng(3, 3);
    const auto sequence = buildMotifSequence(source, rules, 8, RepetitionStyle::Hypnotic, rng);
    REQUIRE(sequence.size() == 8);
    for (const size_t index : {size_t{3}, size_t{7}}) {
        REQUIRE(sequence[index] != source);
        for (size_t i = 0; i < source.notes.size(); ++i) {
            CHECK(sequence[index].notes[i].startStep == source.notes[i].startStep);
            CHECK(sequence[index].notes[i].degree == source.notes[i].degree);
        }
    }
}

TEST_CASE("buildMotifSequence: phrase shorter than the motif or not a multiple", "[motif]") {
    const MotifRules rules = minorRules();
    Motif two = handMotif();
    two.bars = 2;
    Pcg32 rng(1, 1);
    CHECK(buildMotifSequence(two, rules, 1, RepetitionStyle::Standard, rng).empty());
    CHECK(buildMotifSequence(two, rules, 3, RepetitionStyle::Standard, rng).empty());
    CHECK(buildMotifSequence(two, rules, 4, RepetitionStyle::Standard, rng).size() == 2);
    Motif none = handMotif();
    none.bars = 0;
    CHECK(buildMotifSequence(none, rules, 4, RepetitionStyle::Standard, rng).empty());
}

TEST_CASE("repetitionPermille", "[motif]") {
    const Motif a = handMotif();
    Motif b = a;
    b.notes[0].accent = true;
    CHECK(repetitionPermille({}) == 0);
    CHECK(repetitionPermille({a}) == 1000);
    CHECK(repetitionPermille({a, a, a, b}) == 750);
    CHECK(repetitionPermille({b, a, a, a}) == 250);
    CHECK(repetitionPermille({a, b}) == 500);
}

TEST_CASE("constraint layer keeps realised motifs untouched", "[motif]") {
    for (const char* scaleId : {"natural_minor", "dorian", "phrygian", "minor_pentatonic"}) {
        for (uint64_t seed = 0; seed < 25; ++seed) {
            Pattern p = makeEmptyPattern(2, "x");
            p.context.scaleId = scaleId;
            MotifRules rules;
            rules.scale = &scaleOf(scaleId);
            rules.keyRoot = p.context.root;
            Pcg32 rng = Pcg32::fromSeed(seed);
            const auto motif = generateMotif(MotifParams{}, rules, rng);
            REQUIRE(motif.has_value());
            std::vector<Note> expected;
            for (uint32_t bar = 0; bar < 2; ++bar) {
                for (Note n : realizeMotif(*motif, rules, 0, bar * kTicksPerBar)) {
                    n.id = allocateNoteId(p);
                    p.voices[1].notes.push_back(n);
                    expected.push_back(n);
                }
            }
            applyConstraints(p, ConstraintSettings::defaultsFor(p));
            INFO(scaleId << " seed " << seed);
            REQUIRE(p.voices[1].notes.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                CHECK(p.voices[1].notes[i].pitch == expected[i].pitch);
                CHECK(p.voices[1].notes[i].startTick == expected[i].startTick);
                CHECK(p.voices[1].notes[i].lengthTicks == expected[i].lengthTicks);
            }
        }
    }
}

TEST_CASE("isValidMotif: strong steps need chord tones on their own", "[motif]") {
    const MotifRules rules = minorRules();
    Motif motif;
    motif.bars = 1;
    motif.notes = {note(0, 240, 0), note(4, 240, 4), note(8, 240, 0)}; // A E A, no weak steps
    REQUIRE(isValidMotif(motif, rules));
    motif.notes[1].degree = 3; // D on a strong step; no weak note is involved
    CHECK_FALSE(isValidMotif(motif, rules));
}

TEST_CASE("isValidMotif: a non-chord tone must also be left by step", "[motif]") {
    const MotifRules rules = minorRules();
    Motif motif = handMotif();
    motif.notes[2].degree = 4; // B (step 1) is approached from A by step but left for E: a leap of three steps
    CHECK_FALSE(isValidMotif(motif, rules));
}
