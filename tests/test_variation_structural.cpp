#include "core/PatternEdit.h"
#include "core/Variation.h"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>

using namespace mm::core;

namespace {

/// A minor natural, one Am chord, melody range 55-88 (the fixture voice of every test below).
struct Rig {
    Pattern pattern;
    VariationVoice voice;

    explicit Rig(uint32_t bars, VoiceRole role = VoiceRole::Melody) : pattern(makeEmptyPattern(bars, "x")) {
        pattern.context.root = 9;
        pattern.context.scaleId = "natural_minor";
        voice.scale = findScale("natural_minor");
        REQUIRE(voice.scale != nullptr);
        voice.harmony = &pattern.context;
        voice.rangeLow = 55;
        voice.rangeHigh = 88;
        voice.patternEnd = bars * kTicksPerBar;
        voice.role = role;
        voice.phrases = &pattern.phrases;
    }

    std::vector<Note>& notes() { return pattern.voices[1].notes; }
    void add(uint32_t start, uint8_t pitch, uint32_t length = 240, uint8_t velocity = 100) {
        REQUIRE(addNote(pattern, 1, pitch, start, length, velocity) != 0);
    }
};

const Note* byId(const std::vector<Note>& notes, uint32_t id) {
    for (const Note& note : notes) {
        if (note.id == id) {
            return &note;
        }
    }
    return nullptr;
}

bool noOverlap(const std::vector<Note>& notes, uint32_t end) {
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].startTick + notes[i].lengthTicks > (i + 1 < notes.size() ? notes[i + 1].startTick : end)) {
            return false;
        }
        if (i + 1 < notes.size() && notes[i].startTick >= notes[i + 1].startTick) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("structural edits per strength", "[variation]") {
    CHECK(structuralEditCount(0) == 0);
    CHECK(structuralEditCount(kStructuralFromPct - 1) == 0);
    CHECK(structuralEditCount(kStructuralFromPct) == 1);
    CHECK(structuralEditCount(59) == 1);
    CHECK(structuralEditCount(60) == 2);
    CHECK(structuralEditCount(89) == 2);
    CHECK(structuralEditCount(90) == 3);
    CHECK(structuralEditCount(100) == 3);
}

TEST_CASE("shifting the rhythm moves notes by one sixteenth and keeps them apart", "[variation][structural]") {
    bool moved = false;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig(1);
        for (uint32_t i = 0; i < 6; ++i) {
            rig.add(i * 480, 60 + static_cast<uint8_t>(i), 480);
        }
        rig.notes()[2].lock.rhythm = true;
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        if (!shiftRhythm(rig.notes(), rig.voice, rng)) {
            CHECK(rig.notes() == before);
            continue;
        }
        moved = true;
        CHECK(rig.notes().size() == before.size());
        CHECK(noOverlap(rig.notes(), rig.voice.patternEnd));
        size_t shifted = 0;
        for (const Note& note : before) {
            const Note* now = byId(rig.notes(), note.id);
            REQUIRE(now != nullptr);
            CHECK(now->pitch == note.pitch);
            CHECK(now->lengthTicks <= note.lengthTicks);
            const int64_t d = static_cast<int64_t>(now->startTick) - note.startTick;
            CHECK((d == 0 || d == 240 || d == -240));
            shifted += d != 0 ? 1 : 0;
            if (note.lock.rhythm) {
                CHECK(*now == note);
            }
        }
        CHECK(shifted >= 1);
        CHECK(shifted <= 3);
    }
    CHECK(moved);
}

TEST_CASE("a note never shifts out of the pattern", "[variation][structural]") {
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Rig rig(1);
        rig.add(0, 60, 240);
        rig.add(3600, 62, 240); // the last sixteenth of the bar
        Pcg32 rng = Pcg32::fromSeed(seed);
        shiftRhythm(rig.notes(), rig.voice, rng);
        for (const Note& note : rig.notes()) {
            CHECK(note.startTick + note.lengthTicks <= rig.voice.patternEnd);
        }
    }
}

TEST_CASE("swapping tones trades two pitches under one chord or jumps at least a third", "[variation][structural]") {
    int swaps = 0;
    int jumps = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Rig rig(1);
        const uint8_t pitches[] = {57, 60, 64, 67, 69, 72};
        for (uint32_t i = 0; i < 6; ++i) {
            rig.add(i * 480, pitches[i], 240);
        }
        rig.notes()[0].lock.pitch = true;
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(swapTones(rig.notes(), rig.voice, rng));
        std::vector<const Note*> changed;
        for (const Note& note : before) {
            const Note* now = byId(rig.notes(), note.id);
            REQUIRE(now != nullptr);
            CHECK(now->startTick == note.startTick);
            if (now->pitch != note.pitch) {
                changed.push_back(now);
            }
        }
        REQUIRE((changed.size() == 1 || changed.size() == 2));
        CHECK(byId(rig.notes(), before[0].id)->pitch == 57); // the locked pitch stays
        if (changed.size() == 2) {
            ++swaps;
            CHECK(changed[0]->pitch == byId(before, changed[1]->id)->pitch);
            CHECK(changed[1]->pitch == byId(before, changed[0]->id)->pitch);
        } else {
            ++jumps;
            const int d = std::abs(static_cast<int>(changed[0]->pitch) - byId(before, changed[0]->id)->pitch);
            CHECK(d >= 3);
            CHECK(d <= 9);
            CHECK(isAllowed(*rig.voice.scale, 9, rig.pattern.context.progression.front().chord, changed[0]->pitch));
            CHECK(changed[0]->pitch >= 55);
            CHECK(changed[0]->pitch <= 88);
        }
    }
    CHECK(swaps > 10);
    CHECK(jumps > 10);
}

TEST_CASE("an octave jump moves one note by exactly an octave inside the range", "[variation][structural]") {
    int up = 0;
    int down = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig(1);
        rig.add(0, 57, 240);
        rig.add(480, 64, 240);
        rig.add(960, 88, 240); // at the top: can only go down
        rig.notes()[1].lock.pitch = true;
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(octaveJump(rig.notes(), rig.voice, rng));
        int changed = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            const int d = static_cast<int>(rig.notes()[i].pitch) - before[i].pitch;
            if (d != 0) {
                ++changed;
                CHECK(std::abs(d) == 12);
                CHECK(rig.notes()[i].pitch >= 55);
                CHECK(rig.notes()[i].pitch <= 88);
                (d > 0 ? up : down) += 1;
            }
        }
        CHECK(changed == 1);
        CHECK(rig.notes()[1] == before[1]);
    }
    CHECK(up > 5);
    CHECK(down > 5);
}

TEST_CASE("an octave jump finds nothing when no note can move", "[variation][structural]") {
    Rig rig(1);
    rig.add(0, 60, 240);
    rig.voice.rangeLow = 60;
    rig.voice.rangeHigh = 71; // narrower than two octaves around the note
    const auto before = rig.notes();
    Pcg32 rng = Pcg32::fromSeed(1);
    CHECK_FALSE(octaveJump(rig.notes(), rig.voice, rng));
    CHECK(rig.notes() == before);
    rig.notes()[0].lock.pitch = true;
    rig.voice.rangeHigh = 88;
    CHECK_FALSE(octaveJump(rig.notes(), rig.voice, rng));
}

TEST_CASE("density adds on free steps and removes weak notes only", "[variation][structural]") {
    int added = 0;
    int removed = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Rig rig(2, VoiceRole::Bass);
        for (uint32_t i = 0; i < 8; ++i) {
            rig.add(i * 960 + (i % 2 == 0 ? 0 : 480), static_cast<uint8_t>(57 + i), 240,
                    static_cast<uint8_t>(70 + 5 * i));
        }
        rig.notes()[1].accent = true;
        rig.notes()[3].lock.rhythm = true;
        rig.voice.rangeLow = 40;
        rig.voice.rangeHigh = 70;
        const auto before = rig.notes();
        const uint32_t nextId = rig.pattern.nextNoteId;
        Pcg32 rng = Pcg32::fromSeed(seed);
        if (!changeDensity(rig.notes(), rig.pattern, {}, rig.voice, rng)) {
            CHECK(rig.notes() == before);
            continue;
        }
        CHECK(noOverlap(rig.notes(), rig.voice.patternEnd));
        CHECK(rig.notes().size() >= 2);
        std::set<uint32_t> ids;
        for (const Note& note : rig.notes()) {
            CHECK(ids.insert(note.id).second);
            const Note* old = byId(before, note.id);
            if (old == nullptr) {
                ++added;
                CHECK(note.id >= nextId);
                CHECK(note.startTick % 240 == 0);
                CHECK(note.lengthTicks >= 60);
                CHECK(note.pitch >= 40);
                CHECK(note.pitch <= 70);
                CHECK_FALSE(note.accent);
                // The new note takes the velocity of the note before it (the first one when it comes first).
                const Note* model = &before.front();
                for (const Note& candidate : before) {
                    if (candidate.startTick < note.startTick) {
                        model = &candidate;
                    }
                }
                CHECK(note.velocity == model->velocity);
            } else {
                CHECK(old->startTick == note.startTick);
                CHECK(old->pitch == note.pitch);
                if (old->lock.rhythm) {
                    CHECK(*old == note);
                }
            }
        }
        for (const Note& note : before) {
            if (byId(rig.notes(), note.id) == nullptr) {
                ++removed;
                CHECK_FALSE(note.accent);
                CHECK_FALSE(note.lock.rhythm);
                CHECK(note.startTick % 960 != 0); // a bass note on the beat stays
            }
        }
    }
    CHECK(added > 20);
    CHECK(removed > 20);
}

TEST_CASE("density never goes below two notes and respects a locked rhythm", "[variation][structural]") {
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Rig rig(1);
        rig.add(240, 60, 240);
        rig.add(720, 62, 240);
        rig.add(1200, 64, 240);
        Pcg32 rng = Pcg32::fromSeed(seed);
        for (int i = 0; i < 6; ++i) {
            changeDensity(rig.notes(), rig.pattern, {}, rig.voice, rng);
        }
        CHECK(rig.notes().size() >= 2);
        CHECK(noOverlap(rig.notes(), rig.voice.patternEnd));
    }
    Rig rig(1);
    rig.add(240, 60, 240);
    rig.add(720, 62, 240);
    rig.add(1200, 64, 240);
    const auto before = rig.notes();
    Pcg32 rng = Pcg32::fromSeed(1);
    LockFlags locked;
    locked.rhythm = true;
    CHECK_FALSE(changeDensity(rig.notes(), rig.pattern, locked, rig.voice, rng));
    CHECK(rig.notes() == before);
}

TEST_CASE("a note added next to a long one shortens it", "[variation][structural]") {
    bool shortened = false;
    for (uint64_t seed = 1; seed <= 60 && !shortened; ++seed) {
        Rig rig(1);
        rig.add(0, 60, 1920); // half a bar long
        rig.add(3000, 64, 240);
        rig.notes()[1].lock.rhythm = true;
        Pcg32 rng = Pcg32::fromSeed(seed);
        if (changeDensity(rig.notes(), rig.pattern, {}, rig.voice, rng)) {
            CHECK(noOverlap(rig.notes(), rig.voice.patternEnd));
            shortened = shortened || rig.notes()[0].lengthTicks < 1920;
        }
    }
    CHECK(shortened);
}

TEST_CASE("mirroring and playing backwards change pitches only", "[variation][structural]") {
    int mirrored = 0;
    int backwards = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig(1);
        const uint8_t pitches[] = {64, 67, 71, 69};
        for (uint32_t i = 0; i < 4; ++i) {
            rig.add(i * 960, pitches[i], 480);
        }
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(invertOrMirror(rig.notes(), rig.voice, rng));
        std::vector<int> now;
        for (size_t i = 0; i < before.size(); ++i) {
            CHECK(rig.notes()[i].startTick == before[i].startTick);
            CHECK(rig.notes()[i].lengthTicks == before[i].lengthTicks);
            CHECK(rig.notes()[i].id == before[i].id);
            now.push_back(rig.notes()[i].pitch);
        }
        const std::vector<int> backward{69, 71, 67, 64};
        if (now == backward) {
            ++backwards;
            continue;
        }
        ++mirrored;
        CHECK(now[0] == 64); // the first note is the pivot
        for (size_t i = 0; i < now.size(); ++i) {
            CHECK(now[i] >= 55);
            CHECK(now[i] <= 88);
            CHECK(isAllowed(*rig.voice.scale, 9, rig.pattern.context.progression.front().chord, now[i]));
        }
        // 67 and 71 lie above the pivot, the mirror image lies below it.
        CHECK(now[1] < 64);
        CHECK(now[2] < now[1]);
    }
    CHECK(mirrored > 10);
    CHECK(backwards > 10);
}

TEST_CASE("inversion needs two free notes and no pitch lock", "[variation][structural]") {
    Rig rig(1);
    rig.add(0, 64, 480);
    Pcg32 rng = Pcg32::fromSeed(1);
    CHECK_FALSE(invertOrMirror(rig.notes(), rig.voice, rng)); // one note
    rig.add(960, 67, 480);
    rig.notes()[1].lock.pitch = true;
    const auto before = rig.notes();
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Pcg32 other = Pcg32::fromSeed(seed);
        CHECK_FALSE(invertOrMirror(rig.notes(), rig.voice, other)); // a locked pitch stops it
    }
    CHECK(rig.notes() == before);
}

TEST_CASE("call and response exchange the bars of one pair", "[variation][structural]") {
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Rig rig(4);
        const uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
        for (uint32_t bar = 0; bar < 4; ++bar) {
            rig.add(bar * kTicksPerBar, pitches[bar * 2], 480);
            rig.add(bar * kTicksPerBar + 960 + 240 * bar, pitches[bar * 2 + 1], 240);
        }
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(swapCallAndResponse(rig.notes(), rig.voice, rng));
        REQUIRE(rig.notes().size() == before.size());
        int movedBars = 0;
        for (const Note& note : before) {
            const Note* now = byId(rig.notes(), note.id);
            REQUIRE(now != nullptr);
            const int64_t d = static_cast<int64_t>(now->startTick) - note.startTick;
            CHECK((d == 0 || d == kTicksPerBar || d == -static_cast<int64_t>(kTicksPerBar)));
            CHECK(now->pitch == note.pitch);
            CHECK(now->lengthTicks == note.lengthTicks);
            movedBars += d != 0 ? 1 : 0;
        }
        CHECK(movedBars == 4); // two bars with two notes each
        for (size_t i = 0; i + 1 < rig.notes().size(); ++i) {
            CHECK(rig.notes()[i].startTick < rig.notes()[i + 1].startTick);
        }
    }
}

TEST_CASE("call and response: nothing to swap", "[variation][structural]") {
    Rig one(1);
    one.add(0, 60, 480);
    Pcg32 rng = Pcg32::fromSeed(1);
    CHECK_FALSE(swapCallAndResponse(one.notes(), one.voice, rng)); // one bar has no pair

    Rig same(2);
    same.add(0, 60, 480);
    same.add(kTicksPerBar, 60, 480);
    const auto before = same.notes();
    CHECK_FALSE(swapCallAndResponse(same.notes(), same.voice, rng)); // identical bars
    CHECK(same.notes() == before);

    Rig locked(2);
    locked.add(0, 60, 480);
    locked.add(kTicksPerBar, 64, 480);
    locked.notes()[1].lock.pitch = true;
    const auto lockedBefore = locked.notes();
    CHECK_FALSE(swapCallAndResponse(locked.notes(), locked.voice, rng));
    CHECK(locked.notes() == lockedBefore);
}

TEST_CASE("inversion and call and response are for melody voices only", "[variation][structural]") {
    for (const auto op : {StructuralOperator::InvertOrMirror, StructuralOperator::CallResponse}) {
        for (uint64_t seed = 1; seed <= 10; ++seed) {
            Rig bass(2, VoiceRole::Bass);
            bass.add(0, 60, 480);
            bass.add(960, 67, 480);
            bass.add(kTicksPerBar, 64, 480);
            const auto before = bass.notes();
            Pcg32 rng = Pcg32::fromSeed(seed);
            CHECK_FALSE(applyStructural(op, bass.notes(), bass.pattern, {}, bass.voice, rng));
            CHECK(bass.notes() == before);
        }
        Rig melody(2, VoiceRole::Melody);
        melody.add(0, 60, 480);
        melody.add(960, 67, 480);
        melody.add(kTicksPerBar, 64, 480);
        bool worked = false;
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Pcg32 rng = Pcg32::fromSeed(seed);
            auto copy = melody.notes();
            worked = applyStructural(op, copy, melody.pattern, {}, melody.voice, rng) || worked;
        }
        CHECK(worked);
    }
}

TEST_CASE("locked dimensions keep the operators away", "[variation][structural]") {
    LockFlags rhythm;
    rhythm.rhythm = true;
    LockFlags pitch;
    pitch.pitch = true;
    for (const auto op :
         {StructuralOperator::ShiftRhythm, StructuralOperator::Density, StructuralOperator::CallResponse}) {
        Rig rig(2);
        rig.add(0, 60, 480);
        rig.add(1920, 64, 240);
        rig.add(kTicksPerBar, 67, 480);
        const auto before = rig.notes();
        for (uint64_t seed = 1; seed <= 10; ++seed) {
            Pcg32 rng = Pcg32::fromSeed(seed);
            CHECK_FALSE(applyStructural(op, rig.notes(), rig.pattern, rhythm, rig.voice, rng));
        }
        CHECK(rig.notes() == before);
    }
    for (const auto op : {StructuralOperator::SwapTones, StructuralOperator::OctaveJump,
                          StructuralOperator::InvertOrMirror, StructuralOperator::CallResponse}) {
        Rig rig(2);
        rig.add(0, 60, 480);
        rig.add(1920, 64, 240);
        rig.add(kTicksPerBar, 67, 480);
        const auto before = rig.notes();
        for (uint64_t seed = 1; seed <= 10; ++seed) {
            Pcg32 rng = Pcg32::fromSeed(seed);
            CHECK_FALSE(applyStructural(op, rig.notes(), rig.pattern, pitch, rig.voice, rng));
        }
        CHECK(rig.notes() == before);
    }
}

TEST_CASE("a locked long note is never shortened to make room", "[variation][structural]") {
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig(1);
        rig.add(0, 60, 3000); // reaches over most free steps
        rig.add(3600, 64, 240);
        rig.notes()[0].lock.rhythm = true;
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        changeDensity(rig.notes(), rig.pattern, {}, rig.voice, rng);
        CHECK(*byId(rig.notes(), before[0].id) == before[0]);
        CHECK(noOverlap(rig.notes(), rig.voice.patternEnd));
    }
}

TEST_CASE("an added note fits the gap it is put into", "[variation][structural]") {
    int added = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Rig rig(1);
        rig.add(250, 60, 10);  // the step at 240 has 10 ticks of room: too little
        rig.add(600, 62, 100); // the step at 480 has 120 ticks
        rig.add(1500, 64, 100);
        rig.add(3000, 65, 100);
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        changeDensity(rig.notes(), rig.pattern, {}, rig.voice, rng);
        CHECK(noOverlap(rig.notes(), rig.voice.patternEnd));
        for (const Note& note : rig.notes()) {
            if (byId(before, note.id) == nullptr) {
                ++added;
                CHECK(note.lengthTicks >= 60);
            }
        }
    }
    CHECK(added > 20);
}

TEST_CASE("a mirror image below the range folds up by octaves", "[variation][structural]") {
    int checked = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Rig rig(1);
        const uint8_t pitches[] = {57, 72, 70, 66}; // mirrored around 57 they fall to the low 40s
        for (uint32_t i = 0; i < 4; ++i) {
            rig.add(i * 960, pitches[i], 480);
        }
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(invertOrMirror(rig.notes(), rig.voice, rng));
        if (rig.notes()[0].pitch != 66) { // not the retrograde
            ++checked;
            for (const Note& note : rig.notes()) {
                CHECK(note.pitch >= 55);
                CHECK(note.pitch <= 88);
                CHECK(isAllowed(*rig.voice.scale, 9, rig.pattern.context.progression.front().chord, note.pitch));
            }
        }
    }
    CHECK(checked > 10);
}

TEST_CASE("a bar that does not change under inversion or reversal is no change", "[variation][structural]") {
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Rig rig(1);
        for (uint32_t i = 0; i < 4; ++i) {
            rig.add(i * 960, 64, 480); // the same pitch four times: mirror and retrograde leave it as it is
        }
        const auto before = rig.notes();
        Pcg32 rng = Pcg32::fromSeed(seed);
        CHECK_FALSE(invertOrMirror(rig.notes(), rig.voice, rng));
        CHECK(rig.notes() == before);
    }
}

TEST_CASE("call and response take both pairs of four bars and stop at a locked rhythm", "[variation][structural]") {
    bool first = false;
    bool second = false;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Rig rig(4);
        for (uint32_t bar = 0; bar < 4; ++bar) {
            rig.add(bar * kTicksPerBar, static_cast<uint8_t>(60 + bar), 480);
        }
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(swapCallAndResponse(rig.notes(), rig.voice, rng));
        first = first || rig.notes()[0].pitch == 61;
        second = second || rig.notes()[2].pitch == 63;
    }
    CHECK(first);
    CHECK(second);
    Rig locked(2);
    locked.add(0, 60, 480);
    locked.add(kTicksPerBar, 64, 480);
    locked.notes()[1].lock.rhythm = true;
    Pcg32 rng = Pcg32::fromSeed(1);
    CHECK_FALSE(swapCallAndResponse(locked.notes(), locked.voice, rng));
}
