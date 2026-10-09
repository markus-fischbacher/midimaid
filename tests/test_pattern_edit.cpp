#include "core/EditGrid.h"
#include "core/PatternEdit.h"
#include "core/PatternValidation.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

using namespace mm::core;

namespace {

/// Two bars, bass and melody empty.
Pattern base() {
    return makeEmptyPattern(2, "edit");
}

const Note* find(const Pattern& pattern, size_t voice, uint32_t id) {
    for (const auto& note : pattern.voices[voice].notes) {
        if (note.id == id) {
            return &note;
        }
    }
    return nullptr;
}

std::vector<uint32_t> ids(std::initializer_list<uint32_t> list) {
    return list;
}

} // namespace

TEST_CASE("addNote inserts in order with a new id and marks the pattern as edited", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 960, 240, 100);
    const auto b = addNote(pattern, 0, 47, 0, 240, 90);
    const auto c = addNote(pattern, 0, 48, 960, 120, 80); // same tick as `a`: after it
    REQUIRE((a != 0 && b != 0 && c != 0));
    CHECK(std::set<uint32_t>{a, b, c}.size() == 3);
    REQUIRE(pattern.voices[0].notes.size() == 3);
    CHECK(pattern.voices[0].notes[0].id == b);
    CHECK(pattern.voices[0].notes[1].id == a);
    CHECK(pattern.voices[0].notes[2].id == c);
    CHECK(pattern.info.source == "edit");
    CHECK(validatePattern(pattern).empty());
}

TEST_CASE("addNote refuses what does not fit and changes nothing", "[pattern-edit]") {
    auto pattern = base();
    const auto reference = pattern;
    CHECK(addNote(pattern, 2, 45, 0, 240, 100) == 0);  // no such voice
    CHECK(addNote(pattern, 0, 128, 0, 240, 100) == 0); // pitch
    CHECK(addNote(pattern, 0, 45, 0, 0, 100) == 0);    // length
    CHECK(addNote(pattern, 0, 45, 0, 240, 0) == 0);    // velocity
    CHECK(addNote(pattern, 0, 45, 0, 240, 128) == 0);
    CHECK(addNote(pattern, 0, 45, 7680 - 100, 240, 100) == 0); // past the end
    CHECK(pattern == reference);
    CHECK(addNote(pattern, 0, 45, 7680 - 240, 240, 100) != 0); // exactly to the end
}

TEST_CASE("removeNotes removes the named notes only", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    const auto b = addNote(pattern, 0, 47, 240, 240, 100);
    const auto m = addNote(pattern, 1, 60, 0, 240, 100);
    CHECK(removeNotes(pattern, 0, ids({a, m, 999})) == 1); // `m` is in the other voice
    CHECK(pattern.voices[0].notes.size() == 1);
    CHECK(find(pattern, 0, b) != nullptr);
    CHECK(find(pattern, 1, m) != nullptr);
    const auto reference = pattern;
    CHECK(removeNotes(pattern, 0, ids({a})) == 0);
    CHECK(removeNotes(pattern, 9, ids({b})) == 0);
    CHECK(pattern == reference);
}

TEST_CASE("moveNotes moves the block rigidly and cuts the move at the ends", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 120, 240, 240, 100);
    const auto b = addNote(pattern, 0, 60, 960, 480, 100);
    // Free move.
    CHECK(moveNotes(pattern, 0, ids({a, b}), 240, -10) == 2);
    CHECK(find(pattern, 0, a)->startTick == 480);
    CHECK(find(pattern, 0, a)->pitch == 110);
    CHECK(find(pattern, 0, b)->startTick == 1200);
    CHECK(find(pattern, 0, b)->pitch == 50);
    // Too far left: the first note stops at 0, the second keeps its distance to it.
    CHECK(moveNotes(pattern, 0, ids({a, b}), -100000, 0) == 2);
    CHECK(find(pattern, 0, a)->startTick == 0);
    CHECK(find(pattern, 0, b)->startTick == 720);
    // Too far right: the last note ends at the pattern end.
    CHECK(moveNotes(pattern, 0, ids({a, b}), 100000, 0) == 2);
    CHECK(find(pattern, 0, b)->startTick + find(pattern, 0, b)->lengthTicks == 7680);
    CHECK(find(pattern, 0, b)->startTick - find(pattern, 0, a)->startTick == 720);
    // Pitch: the block stops when the top note reaches 127.
    CHECK(moveNotes(pattern, 0, ids({a, b}), 0, 100) == 2);
    CHECK(find(pattern, 0, a)->pitch == 127);
    CHECK(find(pattern, 0, b)->pitch == 127 - 60);
    CHECK(validatePattern(pattern).empty());
}

TEST_CASE("moveNotes keeps the notes sorted and does nothing when nothing moves", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    const auto b = addNote(pattern, 0, 47, 480, 240, 100);
    CHECK(moveNotes(pattern, 0, ids({a}), 960, 0) == 1);
    CHECK(pattern.voices[0].notes[0].id == b);
    CHECK(pattern.voices[0].notes[1].id == a);
    const auto reference = pattern;
    CHECK(moveNotes(pattern, 0, ids({a}), 0, 0) == 0);
    CHECK(moveNotes(pattern, 0, ids({}), 10, 1) == 0);
    CHECK(moveNotes(pattern, 0, ids({a}), 100000, 0) == 1); // cut to the end: moves a little
    CHECK(moveNotes(pattern, 0, ids({a}), 100000, 0) == 0); // already there
    (void)reference;
}

TEST_CASE("length: set and resize are cut to 1 tick and the pattern end", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 7680 - 480, 240, 100);
    const auto b = addNote(pattern, 0, 47, 0, 240, 100);
    CHECK(setLength(pattern, 0, ids({a}), 5000) == 1);
    CHECK(find(pattern, 0, a)->lengthTicks == 480);
    CHECK(setLength(pattern, 0, ids({a}), 480) == 0); // unchanged
    CHECK(setLength(pattern, 0, ids({a, b}), 0) == 2);
    CHECK(find(pattern, 0, a)->lengthTicks == 1);
    CHECK(resizeNotes(pattern, 0, ids({b}), 240) == 1);
    CHECK(find(pattern, 0, b)->lengthTicks == 241);
    CHECK(resizeNotes(pattern, 0, ids({b}), -100000) == 1);
    CHECK(find(pattern, 0, b)->lengthTicks == 1);
    CHECK(resizeNotes(pattern, 0, ids({b}), 0) == 0);
    CHECK(validatePattern(pattern).empty());
}

TEST_CASE("velocities are set per note and cut into 1 to 127", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    const auto b = addNote(pattern, 0, 47, 240, 240, 100);
    const std::vector<VelocityChange> changes = {{a, 60}, {b, 200}, {999, 10}};
    CHECK(setVelocities(pattern, 0, changes) == 2);
    CHECK(find(pattern, 0, a)->velocity == 60);
    CHECK(find(pattern, 0, b)->velocity == 127);
    const std::vector<VelocityChange> zero = {{a, 0}};
    CHECK(setVelocities(pattern, 0, zero) == 1);
    CHECK(find(pattern, 0, a)->velocity == 1);
    CHECK(setVelocities(pattern, 0, zero) == 0);
    CHECK(setVelocities(pattern, 5, zero) == 0);
}

TEST_CASE("slide and accent are flags of their own, a slide takes the ratchet away", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    pattern.voices[0].notes[0].ratchet = 3;
    CHECK(setSlide(pattern, 0, ids({a}), true) == 1);
    CHECK(find(pattern, 0, a)->slide);
    CHECK(find(pattern, 0, a)->ratchet == 1);
    CHECK(validatePattern(pattern).empty());
    CHECK(setSlide(pattern, 0, ids({a}), true) == 0);
    CHECK(setAccent(pattern, 0, ids({a}), true) == 1);
    CHECK(find(pattern, 0, a)->accent);
    CHECK(find(pattern, 0, a)->velocity == 100); // the accent is not part of the velocity
    CHECK(setAccent(pattern, 0, ids({a}), false) == 1);
    CHECK(setSlide(pattern, 0, ids({a}), false) == 1);
    CHECK_FALSE(find(pattern, 0, a)->slide);
}

TEST_CASE("an unchanged edit leaves the pattern including its source untouched", "[pattern-edit]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    pattern.info.source = "algorithm";
    const auto reference = pattern;
    CHECK(setAccent(pattern, 0, ids({a}), false) == 0);
    CHECK(setLength(pattern, 0, ids({a}), 240) == 0);
    CHECK(pattern == reference);
}

TEST_CASE("random edit series keep the pattern valid, sorted and the ids stable", "[pattern-edit][series]") {
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        auto pattern = makeEmptyPattern(1 + (seed % 4) * 0 + (seed % 2), "edit"); // 1 or 2 bars
        std::set<uint32_t> everUsed;
        for (int step = 0; step < 150; ++step) {
            const auto voice = static_cast<size_t>(rng.bounded(2));
            auto& notes = pattern.voices[voice].notes;
            std::vector<uint32_t> chosen;
            for (const auto& note : notes) {
                if (rng.chance(30)) {
                    chosen.push_back(note.id);
                }
            }
            const uint32_t end = pattern.lengthBars * kTicksPerBar;
            switch (rng.bounded(8)) {
            case 0:
            case 1: {
                const auto id = addNote(pattern, voice, static_cast<uint8_t>(rng.bounded(128)), rng.bounded(end),
                                        1 + rng.bounded(1200), static_cast<uint8_t>(1 + rng.bounded(127)));
                if (id != 0) {
                    CHECK(everUsed.insert(id).second); // never reused
                }
                break;
            }
            case 2:
                removeNotes(pattern, voice, chosen);
                break;
            case 3:
                moveNotes(pattern, voice, chosen, rng.range(-2000, 2000), rng.range(-30, 30));
                break;
            case 4:
                setLength(pattern, voice, chosen, rng.bounded(3000));
                break;
            case 5:
                resizeNotes(pattern, voice, chosen, rng.range(-1000, 1000));
                break;
            case 6:
                setSlide(pattern, voice, chosen, rng.chance(50));
                break;
            default:
                setAccent(pattern, voice, chosen, rng.chance(50));
                break;
            }
            const auto issues = validatePattern(pattern);
            INFO("seed " << seed << " step " << step);
            REQUIRE(issues.empty());
        }
        std::set<uint32_t> present;
        for (const auto& track : pattern.voices) {
            for (const auto& note : track.notes) {
                CHECK(present.insert(note.id).second);
                CHECK(everUsed.count(note.id) == 1);
            }
        }
    }
}

TEST_CASE("the edit grid has the tick sizes of the spec", "[edit-grid]") {
    CHECK(EditGrid{4, false}.ticks() == 960);
    CHECK(EditGrid{8, false}.ticks() == 480);
    CHECK(EditGrid{16, false}.ticks() == 240);
    CHECK(EditGrid{32, false}.ticks() == 120);
    CHECK(EditGrid{4, true}.ticks() == 640);
    CHECK(EditGrid{8, true}.ticks() == 320);
    CHECK(EditGrid{16, true}.ticks() == 160); // a 16th triplet, SPEC 3.5
    CHECK(EditGrid{32, true}.ticks() == 80);
    CHECK(EditGrid{7, false}.ticks() == 240); // an invalid division counts as 16
    CHECK(isValidDivision(8));
    CHECK_FALSE(isValidDivision(64));
}

TEST_CASE("snapTick goes to the nearest grid point, halfway to the earlier one", "[edit-grid]") {
    const EditGrid sixteenth{16, false};
    CHECK(snapTick(0, sixteenth) == 0);
    CHECK(snapTick(119, sixteenth) == 0);
    CHECK(snapTick(120, sixteenth) == 0);
    CHECK(snapTick(121, sixteenth) == 240);
    CHECK(snapTick(3839, sixteenth) == 3840);
    const EditGrid triplet{16, true};
    CHECK(snapTick(100, triplet) == 160);
    CHECK(snapTick(80, triplet) == 0);
    CHECK(snapTick(330, triplet) == 320);
}

TEST_CASE("snapPitch keeps the pitch chromatically and finds the nearest scale tone", "[edit-grid]") {
    const Scale* minor = findScale("natural_minor"); // A minor: A B C D E F G
    REQUIRE(minor != nullptr);
    CHECK(snapPitch(61, PitchSnap::Chromatic, 9, *minor) == 61);
    CHECK(snapPitch(200, PitchSnap::Chromatic, 9, *minor) == 127);
    CHECK(snapPitch(-5, PitchSnap::Chromatic, 9, *minor) == 0);
    CHECK(snapPitch(60, PitchSnap::Scale, 9, *minor) == 60); // C is in A minor
    CHECK(snapPitch(61, PitchSnap::Scale, 9, *minor) == 60); // C#: between C and D, snaps down
    CHECK(snapPitch(63, PitchSnap::Scale, 9, *minor) == 62); // D#: between D and E, down
    CHECK(snapPitch(66, PitchSnap::Scale, 9, *minor) == 65); // F#: between F and G, down
    const Scale* pentatonic = findScale("minor_pentatonic"); // A C D E G
    REQUIRE(pentatonic != nullptr);
    CHECK(snapPitch(65, PitchSnap::Scale, 9, *pentatonic) == 64); // F: E is 1 below, G 2 above
    CHECK(snapPitch(66, PitchSnap::Scale, 9, *pentatonic) == 67); // F#: G is 1 above
    CHECK(snapPitch(0, PitchSnap::Scale, 0, *minor) == 0);
    CHECK(snapPitch(127, PitchSnap::Scale, 0, *minor) <= 127);
}

TEST_CASE("duplicateNotes copies the selection right behind itself, rounded up to the grid", "[pattern-edit][duplicate]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    const auto b = addNote(pattern, 0, 48, 480, 240, 90);
    const auto other = addNote(pattern, 0, 50, 2000, 120, 70); // not selected
    REQUIRE((a != 0 && b != 0 && other != 0));
    pattern.voices[0].notes[0].accent = true;
    pattern.voices[0].notes[1].slide = true;
    pattern.voices[0].notes[1].lock.pitch = true;
    pattern.info.source = "algorithm";
    std::vector<uint32_t> added;
    const auto selection = ids({a, b});
    // The selection spans 0 to 720; the next multiple of a beat (960) is the offset.
    REQUIRE(duplicateNotes(pattern, 0, selection, 960, &added) == 2);
    CHECK(pattern.info.source == "edit");
    REQUIRE(added.size() == 2);
    const Note* copyA = find(pattern, 0, added[0]);
    const Note* copyB = find(pattern, 0, added[1]);
    REQUIRE((copyA != nullptr && copyB != nullptr));
    CHECK((copyA->startTick == 960 && copyA->pitch == 45 && copyA->lengthTicks == 240 && copyA->velocity == 100));
    CHECK(copyA->accent);
    CHECK((copyB->startTick == 1440 && copyB->pitch == 48 && copyB->velocity == 90));
    CHECK((copyB->slide && copyB->lock.pitch));
    CHECK(std::set<uint32_t>{a, b, other, added[0], added[1]}.size() == 5);
    CHECK(find(pattern, 0, a)->startTick == 0); // the originals stay
    CHECK(find(pattern, 0, other)->startTick == 2000);
    CHECK(std::is_sorted(pattern.voices[0].notes.begin(), pattern.voices[0].notes.end(),
                         [](const Note& x, const Note& y) { return x.startTick < y.startTick; }));
    CHECK(validatePattern(pattern).empty());
    CHECK(pattern.voices[1].notes.empty());
}

TEST_CASE("duplicateNotes: grid 0 means one tick, a single note moves by its own length", "[pattern-edit][duplicate]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 100, 240, 100);
    const auto b = addNote(pattern, 0, 47, 300, 100, 100);
    std::vector<uint32_t> added;
    REQUIRE(duplicateNotes(pattern, 0, ids({a}), 0, &added) == 1);
    CHECK(find(pattern, 0, added[0])->startTick == 340);
    REQUIRE(duplicateNotes(pattern, 0, ids({a, b}), 0, &added) == 2); // span 100 to 400 = 300
    CHECK(find(pattern, 0, added[0])->startTick == 400);
    CHECK(find(pattern, 0, added[1])->startTick == 600);
}

TEST_CASE("duplicateNotes drops the copies that do not fit and changes nothing when none does",
          "[pattern-edit][duplicate]") {
    auto pattern = base(); // 7680 ticks
    const auto early = addNote(pattern, 0, 45, 0, 240, 100);
    const auto late = addNote(pattern, 0, 47, 6000, 240, 100);
    std::vector<uint32_t> added;
    REQUIRE(duplicateNotes(pattern, 0, ids({early, late}), 1, &added) == 1); // offset 6240: only `early` fits
    REQUIRE(added.size() == 1);
    CHECK(find(pattern, 0, added[0])->startTick == 6240);
    CHECK(pattern.voices[0].notes.size() == 3);
    // The end of a copy may touch the end of the pattern, not pass it.
    auto edge = base();
    const auto first = addNote(edge, 0, 45, 0, 3840, 100);
    REQUIRE(duplicateNotes(edge, 0, ids({first}), 1) == 1); // 3840 + 3840 = 7680
    auto none = base();
    const auto wide = addNote(none, 0, 45, 0, 4000, 100);
    const auto before = none;
    added = {99};
    CHECK(duplicateNotes(none, 0, ids({wide}), 1, &added) == 0);
    CHECK(added.empty());
    CHECK(none == before);
}

TEST_CASE("duplicateNotes ignores unknown voices and ids and leaves the pattern untouched", "[pattern-edit][duplicate]") {
    auto pattern = base();
    const auto a = addNote(pattern, 0, 45, 0, 240, 100);
    pattern.info.source = "algorithm";
    const auto before = pattern;
    CHECK(duplicateNotes(pattern, 5, ids({a}), 960) == 0);
    CHECK(duplicateNotes(pattern, 0, ids({a + 100}), 960) == 0);
    CHECK(duplicateNotes(pattern, 1, ids({a}), 960) == 0); // `a` lives in the other voice
    CHECK(duplicateNotes(pattern, 0, {}, 960) == 0);
    CHECK(pattern == before);
}
