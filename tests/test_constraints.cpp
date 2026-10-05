#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/PatternValidation.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

constexpr uint32_t kStep = 240; // one 16th

uint32_t endOfNote(const Note& note) {
    return note.startTick + note.lengthTicks;
}

/// Two bars of A natural minor: bar 1 chord Am, bar 2 chord E major (the V chord).
Pattern twoBars() {
    Pattern p = makeEmptyPattern(2, "peak_time");
    p.context.progression = {{{0, ChordQuality::Minor}, 0, 2}, {{7, ChordQuality::Major}, 2, 2}};
    return p;
}

Note& add(Pattern& p, size_t voice, int pitch, uint32_t start, uint32_t length, int velocity = 100) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    note.velocity = static_cast<uint8_t>(velocity);
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

ConstraintReport run(Pattern& p, ConstraintSettings settings) {
    return applyConstraints(p, settings);
}

ConstraintReport run(Pattern& p) {
    return applyConstraints(p, ConstraintSettings::defaultsFor(p));
}

const std::vector<Note>& bassNotes(const Pattern& p) {
    return p.voices[0].notes;
}

const std::vector<Note>& melodyNotes(const Pattern& p) {
    return p.voices[1].notes;
}

} // namespace

TEST_CASE("defaults per role", "[core][constraints]") {
    const auto settings = ConstraintSettings::defaultsFor(makeEmptyPattern(1, "x"));
    REQUIRE(settings.voices.size() == 2);
    CHECK(settings.voices[0].rangeLow == 28);
    CHECK(settings.voices[0].rangeHigh == 52);
    CHECK(settings.voices[1].rangeLow == 55);
    CHECK(settings.voices[1].rangeHigh == 88);
    CHECK(settings.gridTicks == 240);
    CHECK(settings.minNoteTicks == 60);
    CHECK(settings.chromaticPercent == 0);
}

TEST_CASE("every voice gets its own MIDI channel", "[core][constraints][channels]") {
    Pattern p = twoBars();
    Track third;
    third.role = VoiceRole::Melody;
    p.voices.push_back(third);
    p.voices[0].midiChannel = 3;
    p.voices[1].midiChannel = 3;
    p.voices[2].midiChannel = 0;
    const auto report = run(p);
    CHECK(p.voices[0].midiChannel == 3);
    CHECK(p.voices[1].midiChannel == 1); // lowest free channel
    CHECK(p.voices[2].midiChannel == 2);
    CHECK(report.channelsReassigned == 2);

    Pattern fine = twoBars();
    CHECK(run(fine).channelsReassigned == 0);
    CHECK(fine.voices[0].midiChannel == 1);
    CHECK(fine.voices[1].midiChannel == 2);
}

TEST_CASE("velocity is limited to 1-127", "[core][constraints][velocity]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 120, 0);
    add(p, 0, 33, 480, 120, 255);
    add(p, 0, 33, 960 + 480, 120, 90);
    const auto report = run(p);
    CHECK(bassNotes(p)[0].velocity == 1);
    CHECK(bassNotes(p)[1].velocity == 127);
    CHECK(bassNotes(p)[2].velocity == 90);
    CHECK(report.velocityClamped == 2);
}

TEST_CASE("pitches snap to the scale and the sounding chord (chord-scale principle)", "[core][constraints][pitch]") {
    Pattern p = twoBars();
    add(p, 0, 34, 480, 120);        // A#1 in bar 1: snaps down (tie) to A
    add(p, 0, 44, 960 + 480, 120);  // G#2 in bar 1 (Am): not allowed, snaps down to G
    add(p, 0, 44, 3840 + 480, 120); // G#2 in bar 2 (E major chord): chord tone, stays
    const auto report = run(p);
    CHECK(bassNotes(p)[0].pitch == 33);
    CHECK(bassNotes(p)[1].pitch == 43);
    CHECK(bassNotes(p)[2].pitch == 44);
    CHECK(report.pitchSnapped == 2);
}

TEST_CASE("chromatic share: only a few passing tones on weak steps stay", "[core][constraints][pitch]") {
    auto makeBass = [] {
        Pattern p = twoBars();
        for (uint32_t step : {1u, 2u, 3u, 5u, 6u, 7u, 9u, 10u, 11u, 13u}) {
            add(p, 0, 34, step * kStep, 120); // A#1 on weak steps of bar 1
        }
        return p;
    };
    auto countOf = [](const Pattern& p, int pitch) {
        return std::count_if(bassNotes(p).begin(), bassNotes(p).end(), [&](const Note& n) { return n.pitch == pitch; });
    };

    Pattern none = makeBass();
    CHECK(run(none).pitchSnapped == 10); // 0 %: everything snaps
    CHECK(countOf(none, 34) == 0);

    Pattern thirty = makeBass();
    auto settings = ConstraintSettings::defaultsFor(thirty);
    settings.chromaticPercent = 30;
    run(thirty, settings);
    CHECK(countOf(thirty, 34) == 3); // floor(30 % of 10 notes), the earliest ones
    CHECK(bassNotes(thirty)[0].pitch == 34);
    CHECK(bassNotes(thirty)[1].pitch == 34);
    CHECK(bassNotes(thirty)[2].pitch == 34);
    CHECK(bassNotes(thirty)[3].pitch == 33);

    Pattern strong = twoBars(); // strong steps never keep a chromatic tone
    add(strong, 0, 34, 960, 120);
    add(strong, 0, 34, 1920, 120);
    settings = ConstraintSettings::defaultsFor(strong);
    settings.chromaticPercent = 100;
    run(strong, settings);
    CHECK(countOf(strong, 34) == 0);
}

TEST_CASE("register: pitches fold by octaves into the range", "[core][constraints][register]") {
    Pattern p = twoBars();
    add(p, 0, 60, 0, 120);   // C3: above 52, nearest C in range is 48
    add(p, 0, 24, 480, 120); // C1: below 28, nearest C in range is 36
    add(p, 0, 20, 960, 120); // G#0: folds to 32, then snaps to G (31)
    const auto report = run(p);
    CHECK(bassNotes(p)[0].pitch == 48);
    CHECK(bassNotes(p)[1].pitch == 36);
    CHECK(bassNotes(p)[2].pitch == 31);
    CHECK(report.registerFolded == 3);

    Pattern melody = twoBars();
    add(melody, 1, 36, 0, 240);    // below 55
    add(melody, 1, 100, 480, 240); // above 88
    run(melody);
    CHECK(melodyNotes(melody)[0].pitch == 60);
    CHECK(melodyNotes(melody)[1].pitch == 88 - ((88 - pitchClassOf(100)) % 12));
}

TEST_CASE("register: a range without the pitch class drops the note", "[core][constraints][register]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 240);   // A: no A between 40 and 42
    add(p, 0, 40, 480, 240); // E: fits
    auto settings = ConstraintSettings::defaultsFor(p);
    settings.voices[0] = {40, 42};
    const auto report = run(p, settings);
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].pitch == 40);
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("grid: note starts snap to the nearest grid position", "[core][constraints][grid]") {
    Pattern p = twoBars();
    add(p, 0, 33, 250, 120);  // -> 240
    add(p, 0, 33, 610, 120);  // lower 480, upper 720: 130 vs 110 -> 720
    add(p, 0, 33, 1080, 120); // tie between 960 and 1200 -> earlier (960)
    const auto report = run(p);
    CHECK(bassNotes(p)[0].startTick == 240);
    CHECK(bassNotes(p)[1].startTick == 720);
    CHECK(bassNotes(p)[2].startTick == 960);
    CHECK(report.gridSnapped == 3);
}

TEST_CASE("grid: triplets stay only if accepted, a grid of 0 leaves starts alone", "[core][constraints][grid]") {
    auto build = [] {
        Pattern p = twoBars();
        add(p, 0, 33, 160, 100);
        return p;
    };
    Pattern off = build();
    run(off);
    CHECK(bassNotes(off)[0].startTick == 240);

    Pattern triplets = build();
    auto settings = ConstraintSettings::defaultsFor(triplets);
    settings.acceptTriplets = true;
    run(triplets, settings);
    CHECK(bassNotes(triplets)[0].startTick == 160);

    Pattern free = build();
    settings = ConstraintSettings::defaultsFor(free);
    settings.gridTicks = 0;
    run(free, settings);
    CHECK(bassNotes(free)[0].startTick == 160);
}

TEST_CASE("notes stay inside the pattern", "[core][constraints][grid]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 1, 60, 3600, 500); // reaches beyond the end -> clipped to 240
    add(p, 1, 64, 3830, 100); // snaps to the end -> uses the grid position before it (a chord with the first)
    add(p, 1, 67, 3840, 240); // starts at the end -> dropped
    add(p, 1, 67, 4000, 240); // starts after the end -> dropped
    const auto report = run(p);
    REQUIRE(melodyNotes(p).size() == 2);
    CHECK(melodyNotes(p)[0].startTick == 3600);
    CHECK(melodyNotes(p)[0].lengthTicks == 240);
    CHECK(melodyNotes(p)[1].startTick == 3600);
    CHECK(melodyNotes(p)[1].lengthTicks == 100);
    CHECK(report.droppedNotes == 2);
    CHECK(report.clippedToEnd == 1);
}

TEST_CASE("equal pitches never overlap on one channel", "[core][constraints][overlap]") {
    Pattern p = twoBars();
    add(p, 1, 60, 0, 960);
    add(p, 1, 60, 480, 480); // the first note is shortened to end where the second starts
    add(p, 1, 64, 0, 960);   // a different pitch may overlap (melody is polyphonic)
    run(p);
    const auto& notes = melodyNotes(p);
    REQUIRE(notes.size() == 3);
    for (const Note& note : notes) {
        if (note.pitch == 60 && note.startTick == 0) {
            CHECK(note.lengthTicks == 480);
        }
        if (note.pitch == 64) {
            CHECK(note.lengthTicks == 960);
        }
    }
}

TEST_CASE("duplicates at the same start collapse into one note", "[core][constraints][overlap]") {
    Pattern p = twoBars();
    add(p, 1, 60, 0, 240);
    add(p, 1, 60, 0, 480); // same pitch, same start: one note with the longer length
    add(p, 1, 64, 0, 240); // a chord tone stays
    const auto report = run(p);
    REQUIRE(melodyNotes(p).size() == 2);
    CHECK(melodyNotes(p)[0].pitch == 60);
    CHECK(melodyNotes(p)[0].lengthTicks == 480);
    CHECK(melodyNotes(p)[0].id == 1); // the first note keeps its id
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("the bass is monophonic", "[core][constraints][overlap]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 600);
    add(p, 0, 36, 240, 240); // the first note ends where this one starts
    add(p, 0, 33, 960, 240);
    add(p, 0, 36, 960, 240); // same start: the lower pitch stays
    const auto report = run(p);
    REQUIRE(bassNotes(p).size() == 3);
    CHECK(bassNotes(p)[0].lengthTicks == 240);
    CHECK(bassNotes(p)[2].pitch == 33);
    CHECK(report.trimmedOverlaps == 1);
    for (size_t i = 0; i + 1 < bassNotes(p).size(); ++i) {
        CHECK(endOfNote(bassNotes(p)[i]) <= bassNotes(p)[i + 1].startTick);
    }
}

TEST_CASE("slides are removed from chords and ratchets", "[core][constraints][slide]") {
    Pattern chord = twoBars();
    add(chord, 1, 60, 0, 480).slide = true;
    add(chord, 1, 64, 240, 480); // overlaps the sliding note
    const auto report = run(chord);
    CHECK_FALSE(melodyNotes(chord)[0].slide);
    CHECK(report.slidesRemoved == 1);

    Pattern ratchet = twoBars();
    Note& n = add(ratchet, 0, 33, 0, 240);
    n.slide = true;
    n.ratchet = 2;
    add(ratchet, 0, 36, 480, 240);
    run(ratchet);
    CHECK_FALSE(bassNotes(ratchet)[0].slide);

    Pattern intoChord = twoBars();
    add(intoChord, 1, 60, 0, 240).slide = true;
    add(intoChord, 1, 64, 240, 240); // the slide target is a chord
    add(intoChord, 1, 67, 240, 240);
    run(intoChord);
    CHECK_FALSE(melodyNotes(intoChord)[0].slide);
}

TEST_CASE("slides in monophonic sections stay", "[core][constraints][slide]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 240).slide = true;
    add(p, 0, 36, 240, 240);              // different pitch: a real slide
    add(p, 0, 33, 960, 240).slide = true; // last note: slides into the first note of the next pass
    const auto report = run(p);
    CHECK(bassNotes(p)[0].slide);
    CHECK(bassNotes(p)[2].slide);
    CHECK(report.slidesRemoved == 0);
    CHECK(report.tiesMerged == 0);
}

TEST_CASE("a slide onto the same pitch becomes a tie", "[core][constraints][slide]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 240).slide = true;
    add(p, 0, 33, 240, 240);
    const auto report = run(p);
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].id == 1);
    CHECK(bassNotes(p)[0].lengthTicks == 480);
    CHECK_FALSE(bassNotes(p)[0].slide);
    CHECK(report.tiesMerged == 1);
}

TEST_CASE("chains of slides onto the same pitch merge completely", "[core][constraints][slide]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 240).slide = true;
    add(p, 0, 33, 240, 240).slide = true;
    add(p, 0, 33, 480, 240);
    add(p, 0, 36, 960, 240);
    const auto report = run(p);
    REQUIRE(bassNotes(p).size() == 2);
    CHECK(bassNotes(p)[0].lengthTicks == 720);
    CHECK(bassNotes(p)[0].id == 1);
    CHECK(report.tiesMerged == 2);
}

TEST_CASE("a tie also bridges a gap between the two notes", "[core][constraints][slide]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 120).slide = true;
    add(p, 0, 33, 480, 240);
    run(p);
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].lengthTicks == 720);
}

TEST_CASE("notes shorter than the minimum are dropped", "[core][constraints][length]") {
    Pattern p = twoBars();
    add(p, 0, 33, 0, 59);
    add(p, 0, 33, 480, 60);
    const auto report = run(p);
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].startTick == 480);
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("a fully locked voice stays exactly as it is", "[core][constraints][locks]") {
    Pattern p = twoBars();
    p.voices[0].lock = {true, true, true};
    add(p, 0, 100, 250, 99999, 200); // everything wrong
    add(p, 0, 100, 250, 5, 0);
    add(p, 1, 34, 0, 240); // the melody is processed as usual
    p.voices[0].midiChannel = 1;
    const Track lockedBefore = p.voices[0];
    run(p);
    CHECK(p.voices[0] == lockedBefore);
    CHECK(melodyNotes(p)[0].pitch == 57); // folded into 55-88 and snapped to the A minor scale: 34 -> 58 -> 57
}

TEST_CASE("locked dimensions are never changed, the skipped fix is counted", "[core][constraints][locks]") {
    Pattern p = twoBars();
    Note& rhythmLocked = add(p, 0, 34, 250, 99999);
    rhythmLocked.lock.rhythm = true;
    Note& pitchLocked = add(p, 0, 34, 960 + 480, 120);
    pitchLocked.lock.pitch = true;
    Note& velocityLocked = add(p, 0, 33, 2400, 120, 0);
    velocityLocked.lock.velocity = true;
    Note& shortLocked = add(p, 0, 33, 2880, 10);
    shortLocked.lock.rhythm = true;
    const auto report = run(p);

    const auto& notes = bassNotes(p);
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].startTick == 250); // rhythm untouched
    CHECK(notes[0].lengthTicks == 99999);
    CHECK(notes[0].pitch == 33); // pitch of an unlocked dimension was still snapped
    CHECK(notes[1].pitch == 34); // pitch locked: stays out of scale
    CHECK(notes[2].velocity == 0);
    CHECK(notes[3].lengthTicks == 10); // not dropped although too short
    CHECK(report.skippedByLock >= 4);
}

TEST_CASE("a voice-level lock on one dimension protects all its notes", "[core][constraints][locks]") {
    Pattern p = twoBars();
    p.voices[0].lock.rhythm = true;
    add(p, 0, 34, 250, 100);
    add(p, 0, 33, 260, 100);
    run(p);
    REQUIRE(bassNotes(p).size() == 2);
    CHECK(bassNotes(p)[0].startTick == 250);
    CHECK(bassNotes(p)[1].startTick == 260);
    CHECK(bassNotes(p)[0].pitch == 33); // pitch is not locked
}

TEST_CASE("the result is valid, deterministic and a fixed point", "[core][constraints][property]") {
    for (uint64_t seed = 0; seed < 400; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        Pattern pattern = randomPattern(rng);
        for (Track& track : pattern.voices) {
            track.lock = {};
            for (Note& note : track.notes) {
                note.lock = {};
            }
        }
        auto settings = ConstraintSettings::defaultsFor(pattern);
        static const uint32_t grids[] = {240, 120, 0};
        static const int chromatic[] = {0, 10, 35, 100};
        settings.gridTicks = grids[rng.bounded(3)];
        settings.acceptTriplets = rng.chance(50);
        settings.chromaticPercent = chromatic[rng.bounded(4)];

        Pattern first = pattern;
        const auto report = applyConstraints(first, settings);
        INFO("seed " << seed);
        REQUIRE(report.converged);
        REQUIRE(report.passes <= 6);
        REQUIRE(validatePattern(first).empty());

        Pattern again = first; // a second run is a no-op
        const auto second = applyConstraints(again, settings);
        REQUIRE(second.total() == 0);
        REQUIRE(again == first);

        Pattern copy = pattern; // deterministic
        applyConstraints(copy, settings);
        REQUIRE(copy == first);
    }
}

TEST_CASE("after the constraints every rule holds", "[core][constraints][property]") {
    for (uint64_t seed = 1000; seed < 1400; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        Pattern pattern = randomPattern(rng);
        for (Track& track : pattern.voices) {
            track.lock = {};
            for (Note& note : track.notes) {
                note.lock = {};
            }
        }
        auto settings = ConstraintSettings::defaultsFor(pattern);
        static const uint32_t grids[] = {240, 120, 0};
        static const int chromatic[] = {0, 10, 35, 100};
        settings.gridTicks = grids[rng.bounded(3)];
        settings.acceptTriplets = rng.chance(50);
        settings.chromaticPercent = chromatic[rng.bounded(4)];
        applyConstraints(pattern, settings);
        INFO("seed " << seed);

        const Scale* scale = findScale(pattern.context.scaleId);
        REQUIRE(scale != nullptr);
        std::vector<uint8_t> channels;
        for (size_t v = 0; v < pattern.voices.size(); ++v) {
            const Track& track = pattern.voices[v];
            const auto range = settings.voices[v];
            channels.push_back(track.midiChannel);
            size_t outOfScale = 0;
            for (size_t i = 0; i < track.notes.size(); ++i) {
                const Note& note = track.notes[i];
                REQUIRE(note.pitch >= range.rangeLow);
                REQUIRE(note.pitch <= range.rangeHigh);
                REQUIRE(note.velocity >= 1);
                REQUIRE(note.velocity <= 127);
                REQUIRE(note.lengthTicks >= settings.minNoteTicks);
                REQUIRE(endOfNote(note) <= pattern.lengthBars * kTicksPerBar);
                if (settings.gridTicks > 0) {
                    REQUIRE((note.startTick % settings.gridTicks == 0 ||
                             (settings.acceptTriplets && note.startTick % 160 == 0)));
                }
                if (i > 0) {
                    REQUIRE(track.notes[i - 1].startTick <= note.startTick);
                }
                const uint32_t halfBar = note.startTick / (kTicksPerBar / 2);
                std::optional<Chord> chord;
                for (const ChordEvent& event : pattern.context.progression) {
                    if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
                        chord = event.chord;
                    }
                }
                if (!isAllowed(*scale, pattern.context.root, chord, note.pitch)) {
                    ++outOfScale;
                    REQUIRE(note.startTick % 960 != 0); // a chromatic tone only on a weak step
                }
                for (size_t j = i + 1; j < track.notes.size(); ++j) {
                    const Note& other = track.notes[j];
                    const bool overlap = other.startTick < endOfNote(note) && endOfNote(other) > note.startTick;
                    if (other.pitch == note.pitch) {
                        REQUIRE_FALSE(overlap);
                    }
                    if (track.role == VoiceRole::Bass) {
                        REQUIRE_FALSE(overlap);
                    }
                }
                if (note.slide) {
                    // the slide target (first note starting at or after the end) is a single note of another pitch
                    const Note* target = nullptr;
                    size_t targetGroup = 0;
                    for (const Note& other : track.notes) {
                        if (other.startTick >= endOfNote(note) &&
                            (target == nullptr || other.startTick < target->startTick)) {
                            target = &other;
                        }
                    }
                    if (target != nullptr) {
                        for (const Note& other : track.notes) {
                            targetGroup += other.startTick == target->startTick ? 1 : 0;
                        }
                        REQUIRE(targetGroup == 1);
                        REQUIRE(target->pitch != note.pitch);
                    }
                    REQUIRE(note.ratchet == 1);
                    for (size_t j = 0; j < track.notes.size(); ++j) {
                        if (j != i) {
                            const Note& other = track.notes[j];
                            REQUIRE_FALSE((other.startTick < endOfNote(note) && endOfNote(other) > note.startTick));
                        }
                    }
                }
            }
            REQUIRE(outOfScale <= static_cast<size_t>(settings.chromaticPercent) * track.notes.size() / 100);
        }
        std::sort(channels.begin(), channels.end());
        REQUIRE(std::adjacent_find(channels.begin(), channels.end()) == channels.end());
    }
}

TEST_CASE("locks hold over random patterns", "[core][constraints][property]") {
    for (uint64_t seed = 2000; seed < 2300; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        const Pattern original = randomPattern(rng); // random locks on tracks and notes
        Pattern changed = original;
        applyConstraints(changed, ConstraintSettings::defaultsFor(changed));
        INFO("seed " << seed);
        for (size_t v = 0; v < original.voices.size(); ++v) {
            const Track& before = original.voices[v];
            const Track& after = changed.voices[v];
            if (before.lock.pitch && before.lock.rhythm && before.lock.velocity) {
                REQUIRE(after.notes == before.notes);
                continue;
            }
            std::map<uint32_t, const Note*> byId;
            for (const Note& note : after.notes) {
                byId[note.id] = &note;
            }
            for (const Note& note : before.notes) {
                const bool pitchLocked = before.lock.pitch || note.lock.pitch;
                const bool rhythmLocked = before.lock.rhythm || note.lock.rhythm;
                const bool velocityLocked = before.lock.velocity || note.lock.velocity;
                const auto it = byId.find(note.id);
                if (rhythmLocked) {
                    REQUIRE(it != byId.end()); // never dropped
                    REQUIRE(it->second->startTick == note.startTick);
                    REQUIRE(it->second->lengthTicks == note.lengthTicks);
                    REQUIRE(it->second->slide == note.slide);
                }
                if (it != byId.end()) {
                    if (pitchLocked) {
                        REQUIRE(it->second->pitch == note.pitch);
                    }
                    if (velocityLocked) {
                        REQUIRE(it->second->velocity == note.velocity);
                    }
                }
            }
        }
    }
}
