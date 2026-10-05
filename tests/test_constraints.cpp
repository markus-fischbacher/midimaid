#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/KickGrid.h"
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

/// Settings for the tests of the rules that have nothing to do with the kick: no kick avoidance, no clearance.
ConstraintSettings withoutKick(const Pattern& p) {
    ConstraintSettings settings = ConstraintSettings::defaultsFor(p);
    for (auto& voice : settings.voices) {
        voice.ignoresKick = true;
    }
    settings.kickClearanceTicks = 0;
    return settings;
}

ConstraintReport run(Pattern& p, ConstraintSettings settings) {
    return applyConstraints(p, settings);
}

ConstraintReport run(Pattern& p) {
    return applyConstraints(p, withoutKick(p));
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
    auto settings = withoutKick(thirty);
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
    settings = withoutKick(strong);
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
    auto settings = withoutKick(p);
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
    auto settings = withoutKick(triplets);
    settings.acceptTriplets = true;
    run(triplets, settings);
    CHECK(bassNotes(triplets)[0].startTick == 160);

    Pattern free = build();
    settings = withoutKick(free);
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

// ---------------------------------------------------------------------------------------------------------------
// Part 2: kick grid, kick avoidance, kick clearance, slide precedence, intervals and register

namespace {

std::vector<int> stepsOf(std::string_view id) {
    const KickGrid* grid = findKickGrid(id);
    REQUIRE(grid != nullptr);
    return {grid->steps.begin(), grid->steps.end()};
}

Pattern oneBar() {
    return makeEmptyPattern(1, "peak_time"); // A natural minor, chord Am, kick grid 4otf
}

} // namespace

TEST_CASE("kick grids match STYLES.md 1.2", "[core][constraints][kick]") {
    CHECK(stepsOf("4otf") == std::vector<int>{0, 4, 8, 12});
    CHECK(stepsOf("4otf_pickup") == std::vector<int>{0, 4, 8, 12, 15});
    CHECK(stepsOf("halftime") == std::vector<int>{0, 8});
    CHECK(stepsOf("broken_a") == std::vector<int>{0, 6, 8, 12});
    CHECK(stepsOf("broken_b") == std::vector<int>{0, 4, 10, 12});
    CHECK(findKickGrid("custom") == nullptr);
    CHECK(findKickGrid("nope") == nullptr);
}

TEST_CASE("kick positions of a pattern", "[core][constraints][kick]") {
    Pattern four = oneBar();
    CHECK(kickTicks(four) == std::vector<uint32_t>{0, 960, 1920, 2880});

    Pattern pickup = makeEmptyPattern(2, "x");
    pickup.kickGridId = "4otf_pickup";
    CHECK(kickTicks(pickup) == std::vector<uint32_t>{0, 960, 1920, 2880, 3600, 3840, 4800, 5760, 6720, 7440});

    Pattern unknown = oneBar();
    unknown.kickGridId = "something else";
    CHECK(kickTicks(unknown) == kickTicks(oneBar())); // unknown ids fall back to 4otf

    Pattern customWithoutRef = oneBar();
    customWithoutRef.kickGridId = "custom";
    CHECK(kickTicks(customWithoutRef) == kickTicks(oneBar()));
}

TEST_CASE("a phrase can override the kick grid", "[core][constraints][kick]") {
    Pattern p = makeEmptyPattern(8, "x");
    p.phrases = {makePhrase(0, 4, PhraseRole::Main), makePhrase(4, 4, PhraseRole::Breakdown)};
    p.phrases[1].kickGridId = "halftime";
    const auto ticks = kickTicks(p);
    CHECK(std::count_if(ticks.begin(), ticks.end(), [](uint32_t t) { return t < 4 * 3840; }) == 16);
    CHECK(std::count_if(ticks.begin(), ticks.end(), [](uint32_t t) { return t >= 4 * 3840; }) == 8);
    CHECK(std::binary_search(ticks.begin(), ticks.end(), 4u * 3840 + 1920));
    CHECK_FALSE(std::binary_search(ticks.begin(), ticks.end(), 4u * 3840 + 960));
}

TEST_CASE("the custom kick grid comes from the drum reference", "[core][constraints][kick]") {
    Pattern oneBarRef = makeEmptyPattern(2, "x");
    oneBarRef.kickGridId = "custom";
    RhythmReference ref;
    ref.bars = 1;
    ref.kickSteps = (1u << 0) | (1u << 6);
    oneBarRef.rhythmRef = ref;
    CHECK(kickTicks(oneBarRef) == std::vector<uint32_t>{0, 1440, 3840, 3840 + 1440});

    Pattern twoBarRef = makeEmptyPattern(4, "x");
    twoBarRef.kickGridId = "custom";
    ref.bars = 2;
    ref.kickSteps = (1u << 0) | (1u << (16 + 8)); // bar 1 step 0, bar 2 step 8
    twoBarRef.rhythmRef = ref;
    CHECK(kickTicks(twoBarRef) == std::vector<uint32_t>{0, 3840 + 1920, 2 * 3840, 3 * 3840 + 1920});
}

TEST_CASE("bass notes move off the kick steps", "[core][constraints][kick]") {
    Pattern p = twoBars();
    add(p, 0, 33, 960, 120); // a kick step -> one 16th later
    const auto report = applyConstraints(p, ConstraintSettings::defaultsFor(p));
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].startTick == 1200);
    CHECK(bassNotes(p)[0].lengthTicks == 120);
    CHECK(report.kickShifted == 1);
}

TEST_CASE("kick avoidance walks past neighbouring kicks and drops at the end", "[core][constraints][kick]") {
    Pattern pickup = twoBars();
    pickup.kickGridId = "4otf_pickup"; // kicks at step 15 of bar 1 and step 0 of bar 2
    add(pickup, 0, 33, 3600, 100);
    applyConstraints(pickup, ConstraintSettings::defaultsFor(pickup));
    REQUIRE(bassNotes(pickup).size() == 1);
    CHECK(bassNotes(pickup)[0].startTick == 3840 + 240);

    Pattern last = oneBar();
    last.kickGridId = "4otf_pickup";
    add(last, 0, 33, 3600, 100); // would have to move into the next pass
    const auto report = applyConstraints(last, ConstraintSettings::defaultsFor(last));
    CHECK(bassNotes(last).empty());
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("a note that would land on another note is dropped instead of moved", "[core][constraints][kick]") {
    Pattern p = twoBars();
    add(p, 0, 33, 960, 120);
    add(p, 0, 36, 1200, 120);
    applyConstraints(p, ConstraintSettings::defaultsFor(p));
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].pitch == 36);
    CHECK(bassNotes(p)[0].startTick == 1200);
}

TEST_CASE("kick avoidance spares locked notes, long_tied bass and other voices", "[core][constraints][kick]") {
    Pattern locked = twoBars();
    add(locked, 0, 33, 960, 120).lock.rhythm = true;
    const auto report = applyConstraints(locked, ConstraintSettings::defaultsFor(locked));
    CHECK(bassNotes(locked)[0].startTick == 960);
    CHECK(report.skippedByLock >= 1);

    Pattern longTied = twoBars();
    add(longTied, 0, 33, 960, 960);
    auto settings = ConstraintSettings::defaultsFor(longTied);
    settings.voices[0].ignoresKick = true;
    const auto tied = applyConstraints(longTied, settings);
    CHECK(bassNotes(longTied)[0].startTick == 960);
    CHECK(bassNotes(longTied)[0].lengthTicks == 960); // holds over the kick at 1920
    CHECK(tied.kickShifted == 0);

    Pattern melody = twoBars();
    add(melody, 1, 60, 960, 240);
    applyConstraints(melody, ConstraintSettings::defaultsFor(melody));
    CHECK(melodyNotes(melody)[0].startTick == 960); // only the bass keeps off the kick
}

TEST_CASE("kick clearance: bass notes end before the next kick", "[core][constraints][kick]") {
    Pattern p = twoBars();
    add(p, 0, 33, 720, 240);  // would end on the kick at 960 -> ends 1/32 before it
    add(p, 0, 33, 1200, 600); // ends at 1800 = 1920 - 120, fits exactly
    const auto report = applyConstraints(p, ConstraintSettings::defaultsFor(p));
    REQUIRE(bassNotes(p).size() == 2);
    CHECK(bassNotes(p)[0].lengthTicks == 120);
    CHECK(bassNotes(p)[1].lengthTicks == 600);
    CHECK(report.clearanceTrimmed == 1);
}

TEST_CASE("kick clearance settings", "[core][constraints][kick]") {
    auto lengthWith = [](uint32_t clearance) {
        Pattern p = twoBars();
        add(p, 0, 33, 480, 480);
        auto settings = ConstraintSettings::defaultsFor(p);
        settings.kickClearanceTicks = clearance;
        applyConstraints(p, settings);
        return bassNotes(p)[0].lengthTicks;
    };
    CHECK(lengthWith(0) == 480);   // off
    CHECK(lengthWith(60) == 420);  // 1/64
    CHECK(lengthWith(120) == 360); // 1/32
    CHECK(lengthWith(240) == 240); // 1/16
}

TEST_CASE("the clearance has priority over the note length", "[core][constraints][kick]") {
    Pattern p = twoBars();
    auto settings = ConstraintSettings::defaultsFor(p);
    settings.gridTicks = 0;
    add(p, 0, 33, 840, 120);   // starts right in the clearance zone: nothing is left, dropped
    add(p, 0, 33, 1440, 200);  // 1440..1640 is fine
    add(p, 0, 33, 2000, 1000); // ends at 3000, limit is 2760 -> 760 ticks left
    const auto report = applyConstraints(p, settings);
    REQUIRE(bassNotes(p).size() == 2);
    CHECK(bassNotes(p)[0].startTick == 1440);
    CHECK(bassNotes(p)[0].lengthTicks == 200);
    CHECK(bassNotes(p)[1].startTick == 2000);
    CHECK(bassNotes(p)[1].lengthTicks == 760);
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("a note with less than the minimum left before the kick is dropped", "[core][constraints][kick]") {
    Pattern p = twoBars();
    auto settings = ConstraintSettings::defaultsFor(p);
    settings.gridTicks = 0;
    add(p, 0, 33, 1790, 200); // limit 1800: only 10 ticks left
    add(p, 0, 33, 1700, 100); // 1700..1800 ends exactly at the limit
    const auto report = applyConstraints(p, settings);
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].startTick == 1700);
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("the clearance also holds before the first kick of the next loop pass", "[core][constraints][kick]") {
    Pattern p = oneBar();
    add(p, 0, 33, 3360, 480); // reaches the end of the pattern where the next pass starts with a kick
    applyConstraints(p, ConstraintSettings::defaultsFor(p));
    REQUIRE(bassNotes(p).size() == 1);
    CHECK(bassNotes(p)[0].lengthTicks == 360);
}

TEST_CASE("kick clearance spares long_tied and locked notes", "[core][constraints][kick]") {
    Pattern longTied = twoBars();
    add(longTied, 0, 33, 480, 960);
    auto settings = ConstraintSettings::defaultsFor(longTied);
    settings.voices[0].ignoresKick = true;
    applyConstraints(longTied, settings);
    CHECK(bassNotes(longTied)[0].lengthTicks == 960);

    Pattern locked = twoBars();
    add(locked, 0, 33, 480, 960).lock.rhythm = true;
    const auto report = applyConstraints(locked, ConstraintSettings::defaultsFor(locked));
    CHECK(bassNotes(locked)[0].lengthTicks == 960);
    CHECK(report.skippedByLock >= 1);
}

TEST_CASE("kick clearance beats a slide that reaches over a kick", "[core][constraints][kick][slide]") {
    Pattern over = twoBars();
    add(over, 0, 33, 480, 240).slide = true;
    add(over, 0, 36, 1200, 240); // the slide would reach 1260 and cross the kick at 960
    const auto report = applyConstraints(over, ConstraintSettings::defaultsFor(over));
    CHECK_FALSE(bassNotes(over)[0].slide);
    CHECK(report.kickSlidesRemoved == 1);

    Pattern clear = twoBars();
    add(clear, 0, 33, 1200, 240).slide = true;
    add(clear, 0, 36, 1440, 240); // reaches 1500: no kick in between
    const auto fine = applyConstraints(clear, ConstraintSettings::defaultsFor(clear));
    CHECK(bassNotes(clear)[0].slide);
    CHECK(fine.kickSlidesRemoved == 0);
}

TEST_CASE("a slide reaching exactly onto a kick step counts as reaching over it", "[core][constraints][kick][slide]") {
    // Without clearance a note may start right before a kick; the slide overlap (1/64) then decides.
    auto removed = [](uint32_t targetStart) {
        Pattern p = twoBars();
        auto settings = ConstraintSettings::defaultsFor(p);
        settings.gridTicks = 0;
        settings.kickClearanceTicks = 0;
        add(p, 0, 33, 720, 100).slide = true;
        add(p, 0, 36, targetStart, 60);
        applyConstraints(p, settings);
        return !bassNotes(p)[0].slide;
    };
    CHECK(removed(900));       // 900 + 1/64 = 960 = the kick
    CHECK_FALSE(removed(898)); // 898 + 60 = 958 < 960
}

TEST_CASE("the last slide of a pattern is checked against the kick of the next pass",
          "[core][constraints][kick][slide]") {
    Pattern p = oneBar();
    add(p, 0, 36, 480, 240);
    add(p, 0, 33, 3360, 240).slide = true; // slides into the first note of the next pass
    const auto report = applyConstraints(p, ConstraintSettings::defaultsFor(p));
    CHECK_FALSE(bassNotes(p)[1].slide); // the way crosses the kick at the pattern end
    CHECK(report.kickSlidesRemoved == 1);
}

TEST_CASE("long_tied bass keeps its slides over kicks", "[core][constraints][kick][slide]") {
    Pattern p = twoBars();
    add(p, 0, 33, 480, 240).slide = true;
    add(p, 0, 36, 1200, 240);
    auto settings = ConstraintSettings::defaultsFor(p);
    settings.voices[0].ignoresKick = true;
    applyConstraints(p, settings);
    CHECK(bassNotes(p)[0].slide);
}

TEST_CASE("tense intervals against the bass on strong steps are avoided", "[core][constraints][intervals]") {
    auto melodyPitch = [](ConstraintSettings (*tune)(ConstraintSettings), const char* scale, int bass, int melody) {
        Pattern p = oneBar();
        p.context.scaleId = scale;
        add(p, 0, bass, 0, 960);
        add(p, 1, melody, 0, 480);
        auto settings = withoutKick(p);
        settings = tune(settings);
        applyConstraints(p, settings);
        REQUIRE(melodyNotes(p).size() == 1);
        return static_cast<int>(melodyNotes(p)[0].pitch);
    };
    auto plain = [](ConstraintSettings s) { return s; };
    CHECK(melodyPitch(plain, "natural_minor", 40, 65) == 64);  // minor second (E-F) -> E
    CHECK(melodyPitch(plain, "locrian", 33, 63) == 62);        // tritone (A-D#) -> D
    CHECK(melodyPitch(plain, "harmonic_minor", 33, 68) == 69); // major seventh (A-G#) -> A
    CHECK(melodyPitch(plain, "natural_minor", 33, 64) == 64);  // a fifth-ish consonance (E over A) stays
}

TEST_CASE("tense intervals are allowed with 30 % chromatic share or in a harsh style",
          "[core][constraints][intervals]") {
    auto melodyPitch = [](int chromatic, bool harsh) {
        Pattern p = oneBar();
        add(p, 0, 40, 0, 960);
        add(p, 1, 65, 0, 480);
        auto settings = withoutKick(p);
        settings.chromaticPercent = chromatic;
        settings.harshStyle = harsh;
        applyConstraints(p, settings);
        return static_cast<int>(melodyNotes(p)[0].pitch);
    };
    CHECK(melodyPitch(0, false) == 64);
    CHECK(melodyPitch(29, false) == 64);
    CHECK(melodyPitch(30, false) == 65);
    CHECK(melodyPitch(0, true) == 65);
}

TEST_CASE("intervals are only checked on strong steps", "[core][constraints][intervals]") {
    Pattern weak = oneBar();
    add(weak, 0, 40, 0, 1920);
    add(weak, 1, 65, 240, 240); // sounds from step 1 to step 2: no strong step inside
    applyConstraints(weak, withoutKick(weak));
    CHECK(melodyNotes(weak)[0].pitch == 65);

    Pattern across = oneBar();
    add(across, 0, 40, 0, 1920);
    add(across, 1, 65, 240, 960); // sounds across the strong step at 960
    applyConstraints(across, withoutKick(across));
    CHECK(melodyNotes(across)[0].pitch == 64);
}

TEST_CASE("register: at a simultaneous attack the melody is 12 semitones above the bass",
          "[core][constraints][intervals]") {
    Pattern p = oneBar();
    add(p, 0, 45, 0, 960);
    add(p, 1, 52, 0, 480); // 7 semitones above
    auto settings = withoutKick(p);
    settings.voices[1] = {40, 88};
    applyConstraints(p, settings);
    CHECK(melodyNotes(p)[0].pitch == 57);

    Pattern later = oneBar(); // not simultaneous: no register rule
    add(later, 0, 45, 0, 960);
    add(later, 1, 52, 240, 240);
    settings = withoutKick(later);
    settings.voices[1] = {40, 88};
    applyConstraints(later, settings);
    CHECK(melodyNotes(later)[0].pitch == 52);

    Pattern narrow = oneBar(); // the range leaves no valid pitch: the note is dropped
    add(narrow, 0, 45, 0, 960);
    add(narrow, 1, 48, 0, 480);
    settings = withoutKick(narrow);
    settings.voices[1] = {40, 50};
    const auto report = applyConstraints(narrow, settings);
    CHECK(melodyNotes(narrow).empty());
    CHECK(report.droppedNotes == 1);
}

TEST_CASE("every non-bass voice yields to the bass, the bass never yields", "[core][constraints][intervals]") {
    Pattern p = oneBar();
    Track second;
    second.role = VoiceRole::Melody;
    second.midiChannel = 3;
    p.voices.push_back(second);
    add(p, 0, 40, 0, 960);
    add(p, 1, 65, 0, 480);
    add(p, 2, 77, 0, 480); // F again, two octaves higher
    auto settings = withoutKick(p);
    settings.voices.push_back({55, 88, true});
    applyConstraints(p, settings);
    CHECK(bassNotes(p)[0].pitch == 40);
    CHECK(melodyNotes(p)[0].pitch == 64);
    CHECK(p.voices[2].notes[0].pitch == 76);
}

TEST_CASE("interval fixes respect locks", "[core][constraints][intervals]") {
    Pattern pitchLocked = oneBar();
    add(pitchLocked, 0, 40, 0, 960);
    add(pitchLocked, 1, 65, 0, 480).lock.pitch = true;
    const auto report = applyConstraints(pitchLocked, withoutKick(pitchLocked));
    CHECK(melodyNotes(pitchLocked)[0].pitch == 65);
    CHECK(report.skippedByLock >= 1);

    Pattern lockedBass = oneBar(); // a locked (imported) bass still sets the rules for the melody
    lockedBass.voices[0].lock = {true, true, true};
    add(lockedBass, 0, 40, 0, 960);
    add(lockedBass, 1, 65, 0, 480);
    applyConstraints(lockedBass, withoutKick(lockedBass));
    CHECK(bassNotes(lockedBass)[0].pitch == 40);
    CHECK(melodyNotes(lockedBass)[0].pitch == 64);

    Pattern unfixable = oneBar();
    add(unfixable, 0, 45, 0, 960);
    add(unfixable, 1, 48, 0, 480).lock.rhythm = true;
    auto settings = withoutKick(unfixable);
    settings.voices[1] = {40, 50};
    applyConstraints(unfixable, settings);
    CHECK(melodyNotes(unfixable).size() == 1); // locked: not dropped
}

TEST_CASE("without a bass voice there are no interval rules", "[core][constraints][intervals]") {
    Pattern p = oneBar();
    p.voices.erase(p.voices.begin());
    add(p, 0, 65, 0, 480);
    applyConstraints(p, withoutKick(p));
    CHECK(p.voices[0].notes[0].pitch == 65);
}

TEST_CASE("kick and interval rules hold over random patterns", "[core][constraints][property]") {
    static const char* grids[] = {"4otf", "4otf_pickup", "halftime", "broken_a", "broken_b", "custom"};
    for (uint64_t seed = 3000; seed < 3500; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        Pattern pattern = randomPattern(rng);
        for (Track& track : pattern.voices) {
            track.lock = {};
            for (Note& note : track.notes) {
                note.lock = {};
            }
        }
        pattern.kickGridId = grids[rng.bounded(6)];
        for (Phrase& phrase : pattern.phrases) {
            phrase.kickGridId = rng.chance(30) ? std::optional<std::string>(grids[rng.bounded(6)]) : std::nullopt;
        }
        auto settings = ConstraintSettings::defaultsFor(pattern);
        static const uint32_t clearances[] = {0, 60, 120, 240};
        static const uint32_t overlaps[] = {0, 60, 120};
        static const int chromatic[] = {0, 10, 35};
        settings.kickClearanceTicks = clearances[rng.bounded(4)];
        settings.slideOverlapTicks = overlaps[rng.bounded(3)];
        settings.chromaticPercent = chromatic[rng.bounded(3)];
        settings.harshStyle = rng.chance(30);
        for (auto& voice : settings.voices) {
            voice.ignoresKick = rng.chance(25);
        }
        INFO("seed " << seed);

        const auto report = applyConstraints(pattern, settings);
        REQUIRE(report.converged);
        REQUIRE(validatePattern(pattern).empty());
        Pattern again = pattern;
        REQUIRE(applyConstraints(again, settings).total() == 0);
        REQUIRE(again == pattern);

        const auto kicks = kickTicks(pattern);
        const uint32_t endTick = pattern.lengthBars * kTicksPerBar;
        const bool tension = settings.chromaticPercent >= 30 || settings.harshStyle;
        const Track* firstBass = nullptr;
        for (const Track& track : pattern.voices) {
            if (track.role == VoiceRole::Bass) {
                firstBass = &track;
                break;
            }
        }

        for (size_t v = 0; v < pattern.voices.size(); ++v) {
            const Track& track = pattern.voices[v];
            if (track.role == VoiceRole::Bass && !settings.voices[v].ignoresKick && !kicks.empty()) {
                uint32_t firstStart = endTick;
                for (const Note& note : track.notes) {
                    firstStart = std::min(firstStart, note.startTick);
                }
                for (const Note& note : track.notes) {
                    REQUIRE_FALSE(std::binary_search(kicks.begin(), kicks.end(), note.startTick));
                    const auto next = std::upper_bound(kicks.begin(), kicks.end(), note.startTick);
                    const uint32_t nextKick = next != kicks.end() ? *next : kicks.front() + endTick;
                    if (settings.kickClearanceTicks > 0) {
                        REQUIRE(static_cast<int64_t>(endOfNote(note)) <=
                                static_cast<int64_t>(nextKick) - static_cast<int64_t>(settings.kickClearanceTicks));
                    }
                    if (note.slide) {
                        uint32_t targetStart = endTick + firstStart;
                        for (const Note& other : track.notes) {
                            if (other.startTick >= endOfNote(note)) {
                                targetStart = std::min(targetStart, other.startTick);
                            }
                        }
                        const uint32_t reach = targetStart + settings.slideOverlapTicks;
                        for (uint32_t copy = 0; copy < 2; ++copy) {
                            for (const uint32_t kick : kicks) {
                                const uint32_t tick = kick + copy * endTick;
                                REQUIRE_FALSE((tick > note.startTick && tick <= reach));
                            }
                        }
                    }
                }
            }
            if (track.role != VoiceRole::Bass && firstBass != nullptr) {
                for (const Note& note : track.notes) {
                    for (const Note& bassNote : firstBass->notes) {
                        if (bassNote.startTick == note.startTick) {
                            REQUIRE(note.pitch >= bassNote.pitch + 12);
                        }
                    }
                    if (!tension) {
                        const uint32_t firstStrong = (note.startTick + 959) / 960 * 960;
                        for (uint32_t tick = firstStrong; tick < endOfNote(note); tick += 960) {
                            for (const Note& bassNote : firstBass->notes) {
                                if (bassNote.startTick <= tick && tick < endOfNote(bassNote)) {
                                    const int interval = ((note.pitch - bassNote.pitch) % 12 + 12) % 12;
                                    REQUIRE(interval != 1);
                                    REQUIRE(interval != 6);
                                    REQUIRE(interval != 11);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
