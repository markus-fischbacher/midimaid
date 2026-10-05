#include "PatternFixtures.h"
#include "core/CopyProtection.h"
#include "core/Quality.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <utility>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

constexpr uint32_t kStep = 240;

using Onsets = std::vector<std::pair<int, int>>; // (step, pitch)

std::vector<Note> notesOf(const Onsets& onsets) {
    std::vector<Note> notes;
    uint32_t id = 1;
    for (const auto& [step, pitch] : onsets) {
        Note note;
        note.id = id++;
        note.pitch = static_cast<uint8_t>(pitch);
        note.startTick = static_cast<uint32_t>(step) * kStep;
        note.lengthTicks = kStep;
        notes.push_back(note);
    }
    return notes;
}

Material material(const Onsets& onsets, uint32_t bars = 1, VoiceRole role = VoiceRole::Melody) {
    return Material{role, bars, notesOf(onsets)};
}

ReferenceEntry entry(const Onsets& onsets, uint32_t bars = 1, VoiceRole role = VoiceRole::Melody) {
    return ReferenceEntry{role, bars, notesOf(onsets), true};
}

Onsets transposed(Onsets onsets, int semitones) {
    for (auto& onset : onsets) {
        onset.second += semitones;
    }
    return onsets;
}

/// 20 onsets on steps 0-19 with varied pitches: a distinctive two-bar line
Onsets twentyOnsets() {
    Onsets onsets;
    for (int i = 0; i < 20; ++i) {
        onsets.emplace_back(i, 50 + (i * 7) % 13);
    }
    return onsets;
}

Pattern patternWith(size_t voice, const Onsets& onsets, uint32_t bars = 1) {
    Pattern p = makeEmptyPattern(bars, "x");
    p.voices[voice].notes = notesOf(onsets);
    return p;
}

} // namespace

// -- line ----------------------------------------------------------------------------------------------------------

TEST_CASE("line: starts snap to the nearest 16th, halfway goes up, the end wraps", "[copy]") {
    Material m;
    m.bars = 1;
    auto at = [&](uint32_t tick, int pitch) {
        Note n;
        n.startTick = tick;
        n.pitch = static_cast<uint8_t>(pitch);
        m.notes.push_back(n);
    };
    at(119, 60);                // step 0
    at(120, 62);                // step 1 (halfway)
    at(360, 64);                // step 2
    at(kTicksPerBar - 121, 65); // step 15
    const auto line = lineOf(m);
    REQUIRE(line.size() == 4);
    CHECK(line[0].step == 0);
    CHECK(line[1].step == 1);
    CHECK(line[2].step == 2);
    CHECK(line[3].step == 15);

    Material wrap;
    wrap.bars = 1;
    Note n;
    n.startTick = kTicksPerBar - 120; // rounds to the end of the bar: step 0 of the loop
    n.pitch = 60;
    wrap.notes.push_back(n);
    CHECK(lineOf(wrap)[0].step == 0);
}

TEST_CASE("line: chords give the highest tone, the bass the lowest", "[copy]") {
    const Onsets chord{{4, 60}, {4, 67}, {4, 64}};
    CHECK(lineOf(material(chord, 1, VoiceRole::Melody))[0].pitch == 67);
    CHECK(lineOf(material(chord, 1, VoiceRole::Bass))[0].pitch == 60);
    // notes that snap to the same step form one onset
    Material close;
    close.bars = 1;
    Note a;
    a.startTick = 0;
    a.pitch = 70;
    Note b;
    b.startTick = 100;
    b.pitch = 72;
    close.notes = {a, b};
    CHECK(lineOf(close).size() == 1);
    CHECK(lineOf(close)[0].pitch == 72);
    close.role = VoiceRole::Bass;
    CHECK(lineOf(close)[0].pitch == 70);
}

TEST_CASE("line: intervals to the previous onset, the first one cyclic", "[copy]") {
    const auto line = lineOf(material({{8, 62}, {0, 60}, {4, 64}})); // unsorted input
    REQUIRE(line.size() == 3);
    CHECK(line[0] == LineOnset{0, 60, -2}); // 60 - 62 from the last onset
    CHECK(line[1] == LineOnset{4, 64, 4});
    CHECK(line[2] == LineOnset{8, 62, -2});
    CHECK(lineOf(material({{3, 60}}))[0].interval == 0);
    CHECK(lineOf(material({})).empty());
    Material noBars = material({{0, 60}});
    noBars.bars = 0;
    CHECK(lineOf(noBars).empty());
}

// -- similarity ----------------------------------------------------------------------------------------------------

TEST_CASE("similarity: identical, transposed and different in length or velocity", "[copy]") {
    const Onsets x{{0, 60}, {4, 64}, {8, 67}, {12, 64}};
    CHECK(similarityPermille(material(x), material(x)) == 1000);
    CHECK(similarityPermille(material(x), material(transposed(x, 7))) == 1000);
    CHECK(similarityPermille(material(x), material(transposed(x, -12))) == 1000);
    Material other = material(x);
    for (Note& n : other.notes) {
        n.lengthTicks = 60;
        n.velocity = 30;
    }
    CHECK(similarityPermille(material(x), other) == 1000);
}

TEST_CASE("similarity: partial matches (hand computed)", "[copy]") {
    const Onsets a{{0, 60}, {4, 64}, {8, 67}, {12, 64}};
    // intervals of a: -4 +4 +3 -3; b changes the pitch of steps 4 and 8: -4 +2 +5 -3 -> two of four match
    const Onsets b{{0, 60}, {4, 62}, {8, 67}, {12, 64}};
    CHECK(similarityPermille(material(a), material(b)) == 500);
    // the same pitches one step later: no onset shares a step
    const Onsets shifted{{1, 60}, {5, 64}, {9, 67}, {13, 64}};
    CHECK(similarityPermille(material(a), material(shifted)) == 0);
}

TEST_CASE("similarity: the larger number of onsets is the denominator", "[copy]") {
    const Onsets a{{0, 60}, {1, 62}, {2, 59}, {3, 64}};
    // b continues a with four more onsets and ends on the pitch a ends on, so the first interval stays equal
    const Onsets b{{0, 60}, {1, 62}, {2, 59}, {3, 64}, {4, 66}, {5, 67}, {6, 68}, {7, 64}};
    CHECK(similarityPermille(material(a), material(b)) == 500);
    CHECK(similarityPermille(material(b), material(a)) == 500);
}

TEST_CASE("similarity: a short material is searched in every bar of a longer one", "[copy]") {
    const Onsets a{{0, 60}, {4, 64}, {8, 67}, {12, 64}};
    Onsets b{{0, 50}, {8, 52}};                       // bar 0
    b.insert(b.end(), {{16 + 4, 55}, {16 + 15, 64}}); // bar 1, ends on the pitch a ends on
    for (const auto& [step, pitch] : a) {             // bar 2 holds a
        b.emplace_back(32 + step, pitch);
    }
    b.emplace_back(48 + 2, 70); // bar 3
    CHECK(similarityPermille(material(a, 1), material(b, 4)) == 1000);
    CHECK(similarityPermille(material(b, 4), material(a, 1)) == 1000); // symmetric
}

TEST_CASE("similarity: loops compare rotated by whole bars", "[copy]") {
    const Onsets x{{0, 60}, {4, 64}, {8, 67}, {12, 64}};
    const Onsets y{{2, 69}, {6, 65}, {10, 62}};
    Onsets xy = x;
    for (const auto& [step, pitch] : y) {
        xy.emplace_back(16 + step, pitch);
    }
    Onsets yx = y;
    for (const auto& [step, pitch] : x) {
        yx.emplace_back(16 + step, pitch);
    }
    // the same two bars played from the other bar
    CHECK(similarityPermille(material(xy, 2), material(yx, 2)) == 1000);
}

TEST_CASE("similarity: empty material", "[copy]") {
    const Onsets x{{0, 60}, {4, 64}};
    CHECK(similarityPermille(material(x), material({})) == 0);
    CHECK(similarityPermille(material({}), material(x)) == 0);
    CHECK(similarityPermille(material({}), material({})) == 0);
}

// -- distinctive ---------------------------------------------------------------------------------------------------

namespace {

/// Consecutive steps with the given pitches.
Onsets line(const std::vector<int>& pitches) {
    Onsets onsets;
    for (size_t i = 0; i < pitches.size(); ++i) {
        onsets.emplace_back(static_cast<int>(i), pitches[i]);
    }
    return onsets;
}

} // namespace

TEST_CASE("distinctive: three pitch classes and four changes per two bars", "[copy]") {
    CHECK_FALSE(isDistinctive(entry(line({57, 60, 64, 64, 64}), 2)));             // 2 changes
    CHECK_FALSE(isDistinctive(entry(line({57, 60, 64, 64, 60}), 2)));             // 3 changes
    CHECK(isDistinctive(entry(line({57, 60, 64, 60, 57}), 2)));                   // 4 changes
    CHECK_FALSE(isDistinctive(entry(line({57, 60, 64, 60, 57, 60, 64, 60}), 4))); // 7 changes in 4 bars
    CHECK(isDistinctive(entry(line({57, 60, 64, 60, 57, 60, 64, 60, 57}), 4)));   // 8
    CHECK(isDistinctive(entry(line({57, 60, 64}), 1)));                           // 2 changes in one bar
    CHECK_FALSE(isDistinctive(entry(line({57, 57, 60, 60}), 1)));                 // 1 change, two pitch classes
}

TEST_CASE("distinctive: pitch classes count over all notes, changes over the line", "[copy]") {
    // a chord on step 0 gives three pitch classes but one onset
    Onsets chordOnly{{0, 57}, {0, 60}, {0, 64}};
    CHECK_FALSE(isDistinctive(entry(chordOnly, 1)));
    // chord, then 72 and 64: line 64 72 64 has two changes, pitch classes A C E
    Onsets mixed = chordOnly;
    mixed.emplace_back(4, 72);
    CHECK_FALSE(isDistinctive(entry(mixed, 1))); // line 64 72: one change
    mixed.emplace_back(8, 64);
    CHECK(isDistinctive(entry(mixed, 1)));
}

TEST_CASE("distinctive: common property is not protected", "[copy]") {
    // rolling root-fifth-octave bass: two pitch classes however many changes
    CHECK_FALSE(isDistinctive(entry(line({33, 40, 45, 33, 40, 45, 33, 40}), 1, VoiceRole::Bass)));
    CHECK_FALSE(isDistinctive(entry(line({57, 64, 57, 64, 57, 64}), 2)));
    CHECK_FALSE(isDistinctive(entry({}, 1)));
    ReferenceEntry noBars = entry(line({57, 60, 64, 60, 57}), 2);
    noBars.bars = 0;
    CHECK_FALSE(isDistinctive(noBars));
}

// -- check ---------------------------------------------------------------------------------------------------------

TEST_CASE("check: the 85 % boundary (17 of 20 allowed, 18 of 20 rejected)", "[copy]") {
    const Onsets original = twentyOnsets();
    const std::vector<ReferenceEntry> set{entry(original, 2)};
    REQUIRE(isDistinctive(set[0]));

    // the last onsets move to free steps without changing the order: their intervals stay, only the steps differ
    auto moved = [&](int count) {
        Onsets copy = original;
        for (int i = 0; i < count; ++i) {
            copy[static_cast<size_t>(19 - i)].first += 10;
        }
        return copy;
    };
    const CopyCheck zero = checkCopyProtection(patternWith(1, original, 2), set);
    CHECK(zero.violated);
    CHECK(zero.permille == 1000);
    const CopyCheck one = checkCopyProtection(patternWith(1, moved(1), 2), set);
    CHECK(one.violated);
    CHECK(one.permille == 950);
    const CopyCheck two = checkCopyProtection(patternWith(1, moved(2), 2), set);
    CHECK(two.violated);
    CHECK(two.permille == 900);
    const CopyCheck three = checkCopyProtection(patternWith(1, moved(3), 2), set);
    CHECK_FALSE(three.violated); // exactly 85 % is allowed
    CHECK(three.permille == 850);
    CHECK_FALSE(checkCopyProtection(patternWith(1, moved(4), 2), set).violated);
}

TEST_CASE("check: transposed copies are caught, only voices of the same role compare", "[copy]") {
    const Onsets original = twentyOnsets();
    const std::vector<ReferenceEntry> set{entry(original, 2, VoiceRole::Melody)};
    CHECK(checkCopyProtection(patternWith(1, transposed(original, 5), 2), set).violated);
    // the same notes in the bass voice: the entry is a melody, nothing is compared
    const CopyCheck bass = checkCopyProtection(patternWith(0, original, 2), set);
    CHECK_FALSE(bass.violated);
    CHECK(bass.permille == 0);
    // a bass entry compares with the bass voice
    const std::vector<ReferenceEntry> bassSet{entry(original, 2, VoiceRole::Bass)};
    CHECK(checkCopyProtection(patternWith(0, original, 2), bassSet).violated);
    CHECK_FALSE(checkCopyProtection(patternWith(1, original, 2), bassSet).violated);
}

TEST_CASE("check: inactive and common entries are ignored", "[copy]") {
    const Onsets original = twentyOnsets();
    ReferenceEntry inactive = entry(original, 2);
    inactive.active = false;
    CHECK_FALSE(checkCopyProtection(patternWith(1, original, 2), {inactive}).violated);
    // a copy of a common line (two pitch classes) passes
    const Onsets common = line({57, 64, 57, 64, 57, 64, 57, 64});
    CHECK_FALSE(checkCopyProtection(patternWith(1, common, 1), {entry(common, 1)}).violated);
    CHECK_FALSE(checkCopyProtection(patternWith(1, original, 2), {}).violated);
}

TEST_CASE("check: reports the most similar entry and voice", "[copy]") {
    const Onsets original = twentyOnsets();
    Onsets partly = original;
    for (int i = 0; i < 8; ++i) {
        partly[static_cast<size_t>(19 - i)].first += 10; // 12 of 20 match: 600
    }
    Onsets different;
    for (int i = 0; i < 12; ++i) {
        different.emplace_back(i * 2, 40 + (i * 5) % 11);
    }
    const std::vector<ReferenceEntry> set{entry(different, 2), entry(partly, 2), entry(original, 2)};
    Pattern p = patternWith(1, original, 2);
    p.voices[0].notes = notesOf({{0, 33}}); // a bass voice that matches nothing
    const CopyCheck result = checkCopyProtection(p, set);
    CHECK(result.violated);
    CHECK(result.voice == 1);
    CHECK(result.entry == 2);
    CHECK(result.permille == 1000);

    // the less similar one when the exact copy is missing
    const CopyCheck partial = checkCopyProtection(p, {set[0], set[1]});
    CHECK_FALSE(partial.violated);
    CHECK(partial.entry == 1);
    CHECK(partial.permille == 600);
}

TEST_CASE("check: voices without notes are skipped", "[copy]") {
    const Pattern empty = makeEmptyPattern(2, "x");
    const CopyCheck result = checkCopyProtection(empty, {entry(twentyOnsets(), 2)});
    CHECK_FALSE(result.violated);
    CHECK(result.permille == 0);
}

TEST_CASE("copyProtectionCheck holds a copy of the set", "[copy]") {
    const Onsets original = twentyOnsets();
    std::vector<ReferenceEntry> set{entry(original, 2)};
    const HardCheck check = copyProtectionCheck(set);
    set.clear(); // the check no longer depends on the caller's set
    CHECK_FALSE(check(patternWith(1, original, 2)));
    CHECK(check(patternWith(1, line({60, 62, 64, 65, 67}), 2)));
}

// -- selection -----------------------------------------------------------------------------------------------------

TEST_CASE("selection: copies of a reference entry are rejected", "[copy]") {
    const Onsets original = twentyOnsets();
    const std::vector<ReferenceEntry> set{entry(original, 2)};
    QualityProfile profile;
    profile.minScore = 0;
    profile.motif = profile.range = profile.density = profile.intervals = profile.rhythmVariety = 0;
    profile.kick = 1;

    // candidate 3 of round 1 has the best score but copies the entry
    constexpr uint64_t init = 99;
    const uint64_t copySeed = candidateSeed(init, 0, 3);
    const uint64_t goodSeed = candidateSeed(init, 0, 5);
    CandidateGenerator generate = [&](uint64_t seed) {
        Pattern p = makeEmptyPattern(2, "x");
        const int hits = seed == copySeed ? 0 : seed == goodSeed ? 1 : 4; // kick score 100, 75, 0
        const uint32_t kickSteps[] = {0, 4, 8, 12};
        const uint32_t freeSteps[] = {1, 2, 3, 5};
        for (int i = 0; i < 4; ++i) {
            Note n;
            n.id = static_cast<uint32_t>(i + 1);
            n.pitch = 33;
            n.startTick = (i < hits ? kickSteps[i] : freeSteps[i - hits]) * kStep;
            n.lengthTicks = kStep;
            p.voices[0].notes.push_back(n);
        }
        if (seed == copySeed) {
            p.voices[1].notes = notesOf(original);
        }
        return p;
    };
    const SelectionResult result = selectBest(generate, init, profile, QualityContext{}, copyProtectionCheck(set));
    REQUIRE(result.success);
    CHECK(result.seed == goodSeed);
    CHECK(result.score == 75);
    // without the protection the copy would win
    CHECK(selectBest(generate, init, profile, QualityContext{}).seed == copySeed);

    // when every candidate copies the entry, nothing is taken
    CandidateGenerator allCopies = [&](uint64_t) { return patternWith(1, original, 2); };
    const SelectionResult none = selectBest(allCopies, init, profile, QualityContext{}, copyProtectionCheck(set));
    CHECK_FALSE(none.success);
    CHECK(none.candidates == 24);
}

TEST_CASE("property: similarity is symmetric, bounded and 1000 for a line with itself", "[copy][property]") {
    Pcg32 rng(41, 2);
    for (int round = 0; round < 200; ++round) {
        auto randomMaterial = [&]() {
            Material m;
            m.role = rng.chance(50) ? VoiceRole::Bass : VoiceRole::Melody;
            m.bars = 1 + rng.bounded(4);
            const int count = 1 + static_cast<int>(rng.bounded(12));
            for (int i = 0; i < count; ++i) {
                Note n;
                n.pitch = static_cast<uint8_t>(30 + rng.bounded(40));
                n.startTick = rng.bounded(m.bars * kTicksPerBar);
                n.lengthTicks = 120;
                m.notes.push_back(n);
            }
            return m;
        };
        const Material a = randomMaterial();
        Material b = randomMaterial();
        b.role = a.role;
        const int ab = similarityPermille(a, b);
        CHECK(ab >= 0);
        CHECK(ab <= 1000);
        CHECK(ab == similarityPermille(b, a));
        CHECK(similarityPermille(a, a) == 1000);
        Material up = a;
        for (Note& n : up.notes) {
            n.pitch = static_cast<uint8_t>(n.pitch + 3);
        }
        CHECK(similarityPermille(a, up) == 1000);
    }
}
