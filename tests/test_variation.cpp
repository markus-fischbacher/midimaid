#include "PatternFixtures.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/Variation.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;

namespace {

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

// The strongest variation that still uses the subtle operators only (D-156).
constexpr int kLastSubtlePct = kStructuralFromPct - 1;

const char* const kStyles[] = {"peak_time", "melodic_techno", "hard_industrial"};

Pattern source(const StyleProfile& style, uint32_t bars, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    const auto result = generatePattern(style, request);
    REQUIRE(result.success);
    return result.pattern;
}

VariationRequest requestOf(uint64_t seed, int strength, std::optional<size_t> voice = std::nullopt) {
    VariationRequest request;
    request.seed = seed;
    request.strengthPct = strength;
    request.voice = voice;
    return request;
}

/// How many notes of `after` differ from the note with the same id in `before` (new notes count).
size_t differing(const Track& before, const Track& after) {
    std::map<uint32_t, const Note*> byId;
    for (const Note& note : before.notes) {
        byId[note.id] = &note;
    }
    size_t count = 0;
    for (const Note& note : after.notes) {
        const auto it = byId.find(note.id);
        count += (it == byId.end() || !(*it->second == note)) ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("the number of edits grows with the strength", "[variation]") {
    CHECK(variationEditCount(0, 50) == 0);
    CHECK(variationEditCount(16, 0) == 0);
    CHECK(variationEditCount(16, -5) == 0);
    CHECK(variationEditCount(16, 1) == 1); // at least one edit above 0
    CHECK(variationEditCount(100, 100) == 50);
    CHECK(variationEditCount(100, 200) == 50); // the strength is capped at 100
    CHECK(variationEditCount(100, 50) == 15);
    CHECK(variationEditCount(100, 60) == 18); // the knee: unchanged below it
    CHECK(variationEditCount(100, 80) == 34);
    CHECK(variationEditCount(100, 99) == 49);
    CHECK(variationEditCount(10, 100) == 5);
    CHECK(variationEditCount(16, 25) < variationEditCount(16, 100));
}

TEST_CASE("strength 0, an unknown voice and a pattern without notes change nothing", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern pattern = source(style, 2, 7);
    const Pattern before = pattern;
    CHECK(applyVariation(pattern, style, {}, requestOf(1, 0)) == 0);
    CHECK(applyVariation(pattern, style, {}, requestOf(1, 50, 9)) == 0);
    CHECK(pattern == before);
    Pattern empty = makeEmptyPattern(1, "peak_time");
    const Pattern emptyBefore = empty;
    CHECK(applyVariation(empty, style, {}, requestOf(1, 80)) == 0);
    CHECK(empty == emptyBefore);
}

TEST_CASE("a variation is deterministic, differs between seeds and is marked as a variation", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern base = source(style, 4, 11);
    Pattern a = base;
    Pattern b = base;
    REQUIRE(applyVariation(a, style, {}, requestOf(5, 40)) > 0);
    REQUIRE(applyVariation(b, style, {}, requestOf(5, 40)) > 0);
    CHECK(a == b);
    CHECK(a.info.source == "variation");
    CHECK(a.context == base.context);
    CHECK(a.phrases == base.phrases);
    std::vector<std::vector<Note>> results;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        Pattern p = base;
        applyVariation(p, style, {}, requestOf(seed, 40));
        results.push_back(p.voices[1].notes);
    }
    size_t different = 0;
    for (size_t i = 1; i < results.size(); ++i) {
        different += results[i] != results[0] ? 1 : 0;
    }
    CHECK(different >= 9);
}

TEST_CASE("variation series: valid, constraint-clean, ids stable, strength 1-100 always changes something",
          "[variation][series]") {
    for (const char* name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : {1u, 2u, 4u, 8u}) {
            const Pattern base = source(style, bars, 100 + bars);
            for (uint64_t seed = 1; seed <= 25; ++seed) {
                for (const int strength : {1, 20, 50, 100}) {
                    INFO(name << " bars " << bars << " seed " << seed << " strength " << strength);
                    Pattern p = base;
                    const size_t changed = applyVariation(p, style, {}, requestOf(seed, strength));
                    REQUIRE(changed > 0);
                    CHECK(validatePattern(p).empty());
                    CHECK(p.qualityScore <= 100);
                    // The constraint layer ran: a second pass finds nothing to repair.
                    Pattern again = p;
                    applyConstraints(again, constraintSettingsFor(again, style));
                    CHECK(again.voices == p.voices);
                    for (size_t v = 0; v < p.voices.size(); ++v) {
                        std::set<uint32_t> ids;
                        for (const Note& note : p.voices[v].notes) {
                            CHECK(ids.insert(note.id).second);
                        }
                        // Below the structural threshold no note is added or removed; above it at most two per
                        // operator, and a voice keeps two notes.
                        const size_t baseSize = base.voices[v].notes.size();
                        const size_t room = 2 * structuralEditCount(strength);
                        CHECK(p.voices[v].notes.size() <= baseSize + room);
                        CHECK(p.voices[v].notes.size() + room >= baseSize);
                        CHECK(p.voices[v].notes.size() >= std::min<size_t>(baseSize, 2));
                    }
                }
            }
        }
    }
}

TEST_CASE("a subtle variation keeps most of the notes and changes more at a higher strength", "[variation][series]") {
    const StyleProfile style = loadShipped("peak_time");
    size_t low = 0;
    size_t high = 0;
    size_t total = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const Pattern base = source(style, 4, 200 + seed);
        Pattern weak = base;
        Pattern strong = base;
        applyVariation(weak, style, {}, requestOf(seed, 10));
        applyVariation(strong, style, {}, requestOf(seed, kLastSubtlePct));
        for (size_t v = 0; v < base.voices.size(); ++v) {
            low += differing(base.voices[v], weak.voices[v]);
            high += differing(base.voices[v], strong.voices[v]);
            total += base.voices[v].notes.size();
            // The cap, an overshoot of the velocity contour and what the constraint layer repairs.
            const size_t size = base.voices[v].notes.size();
            CHECK(differing(base.voices[v], strong.voices[v]) <=
                  variationEditCount(size, kLastSubtlePct) + 3 + size / 10);
        }
    }
    CHECK(low < high);
    CHECK(high * 100 < total * 45);
}

TEST_CASE("locked voices stay bit-identical, also when asked for explicitly", "[variation][voice-lock]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Pattern base = source(style, 4, seed);
        REQUIRE(setVoiceLocked(base, 0, true) == 1);
        Pattern p = base;
        REQUIRE(applyVariation(p, style, {}, requestOf(seed, 100)) > 0);
        CHECK(p.voices[0] == base.voices[0]);
        CHECK(p.voices[1].notes != base.voices[1].notes);
        Pattern forced = base;
        CHECK(applyVariation(forced, style, {}, requestOf(seed, 100, 0)) == 0);
        CHECK(forced == base);
        REQUIRE(setVoiceLocked(base, 1, true) == 1);
        Pattern all = base;
        CHECK(applyVariation(all, style, {}, requestOf(seed, 100)) == 0);
        CHECK(all == base);
    }
}

TEST_CASE("one voice only: the others stay as they are", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 15; ++seed) {
        const Pattern base = source(style, 4, seed);
        Pattern p = base;
        REQUIRE(applyVariation(p, style, {}, requestOf(seed, 80, 1)) > 0);
        CHECK(differing(base.voices[0], p.voices[0]) == 0);
        CHECK(differing(base.voices[1], p.voices[1]) > 0);
    }
}

TEST_CASE("a voice varies the same way whether or not the other one is varied", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern base = source(style, 4, 3);
    Pattern both = base;
    Pattern only = base;
    REQUIRE(applyVariation(both, style, {}, requestOf(9, 40)) > 0);
    REQUIRE(applyVariation(only, style, {}, requestOf(9, 40, 1)) > 0);
    // The melody is drawn from its own stream: the same edits before the constraint layer. The layer may repair the
    // bass of `both` differently, so only the melody notes are compared.
    CHECK(both.voices[1].notes == only.voices[1].notes);
}

TEST_CASE("locks on single notes are respected per dimension", "[variation][voice-lock]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 15; ++seed) {
        Pattern base = source(style, 4, seed);
        for (Track& track : base.voices) {
            for (Note& note : track.notes) {
                note.lock.rhythm = true; // length may not change
            }
        }
        Pattern p = base;
        applyVariation(p, style, {}, requestOf(seed, 100));
        for (size_t v = 0; v < p.voices.size(); ++v) {
            std::map<uint32_t, uint32_t> lengths;
            for (const Note& note : base.voices[v].notes) {
                lengths[note.id] = note.lengthTicks;
            }
            for (const Note& note : p.voices[v].notes) {
                if (lengths.count(note.id) != 0) {
                    // The constraint layer (kick clearance, overlap) may shorten a note; variation never lengthens.
                    CHECK(note.lengthTicks <= lengths[note.id]);
                }
            }
        }
        Pattern pitchLocked = base;
        for (Track& track : pitchLocked.voices) {
            for (Note& note : track.notes) {
                note.lock = {true, false, false};
            }
        }
        const Pattern pitchBefore = pitchLocked;
        Pattern q = pitchLocked;
        applyVariation(q, style, {}, requestOf(seed, 100));
        for (size_t v = 0; v < q.voices.size(); ++v) {
            std::map<uint32_t, uint8_t> pitches;
            for (const Note& note : pitchBefore.voices[v].notes) {
                pitches[note.id] = note.pitch;
            }
            for (const Note& note : q.voices[v].notes) {
                const auto it = pitches.find(note.id);
                if (it != pitches.end()) {
                    CHECK(note.pitch == it->second); // notes may move or go (rhythm is free), pitches do not change
                }
            }
        }
    }
}

TEST_CASE("the accent operator keeps the number of accents", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = makeEmptyPattern(1, "peak_time");
    // Only accents can move: a voice whose notes can neither change length nor pitch nor velocity level otherwise.
    for (uint32_t i = 0; i < 8; ++i) {
        REQUIRE(addNote(base, 1, 60, i * 480, 240, 90) != 0);
    }
    for (Note& note : base.voices[1].notes) {
        note.lock = {true, true, false};
        note.accent = note.id % 4 == 1;
    }
    size_t accents = 0;
    for (const Note& note : base.voices[1].notes) {
        accents += note.accent ? 1 : 0;
    }
    bool moved = false;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern p = base;
        applyVariation(p, style, {}, requestOf(seed, kLastSubtlePct, 1));
        size_t now = 0;
        for (const Note& note : p.voices[1].notes) {
            now += note.accent ? 1 : 0;
        }
        CHECK(now == accents);
        for (size_t i = 0; i < p.voices[1].notes.size(); ++i) {
            moved = moved || p.voices[1].notes[i].accent != base.voices[1].notes[i].accent;
        }
    }
    CHECK(moved);
}

TEST_CASE("replaced notes stay in the scale or the chord and inside the range", "[variation]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        const Pattern base = source(style, 4, seed);
        Pattern p = base;
        applyVariation(p, style, {}, requestOf(seed, kLastSubtlePct, 1));
        const Scale* scale = findScale(base.context.scaleId);
        REQUIRE(scale != nullptr);
        std::map<uint32_t, uint8_t> pitches;
        for (const Note& note : base.voices[1].notes) {
            pitches[note.id] = note.pitch;
        }
        for (const Note& after : p.voices[1].notes) {
            const auto it = pitches.find(after.id);
            if (it != pitches.end() && it->second != after.pitch) {
                CHECK(std::abs(static_cast<int>(it->second) - after.pitch) <= 2);
                std::optional<Chord> chord;
                for (const ChordEvent& event : base.context.progression) {
                    const uint32_t half = after.startTick / (kTicksPerBar / 2);
                    if (half >= event.startHalfBar && half < event.startHalfBar + event.lengthHalfBars) {
                        chord = event.chord;
                    }
                }
                CHECK(isAllowed(*scale, base.context.root, chord, after.pitch));
            }
        }
    }
}

TEST_CASE("notes never grow into the next note or past the pattern end", "[variation][series]") {
    for (const char* name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 25; ++seed) {
            const Pattern base = source(style, 2, 300 + seed);
            Pattern p = base;
            applyVariation(p, style, {}, requestOf(seed, 100));
            for (size_t v = 0; v < p.voices.size(); ++v) {
                std::map<uint32_t, uint32_t> before;
                for (const Note& note : base.voices[v].notes) {
                    before[note.id] = note.lengthTicks;
                }
                const auto& notes = p.voices[v].notes;
                for (size_t i = 0; i < notes.size(); ++i) {
                    const uint32_t limit = i + 1 < notes.size() ? notes[i + 1].startTick : p.lengthBars * kTicksPerBar;
                    const auto it = before.find(notes[i].id);
                    // A note that did not overlap its successor before does not afterwards.
                    if (it != before.end() && notes[i].startTick + it->second <= limit) {
                        CHECK(notes[i].startTick + notes[i].lengthTicks <= limit);
                    }
                }
            }
        }
    }
}

TEST_CASE("the quality score is recomputed after a variation", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    ArchetypeSettings settings;
    int differs = 0;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        const Pattern base = source(style, 4, seed);
        Pattern p = base;
        REQUIRE(applyVariation(p, style, settings, requestOf(seed, 100)) > 0);
        const auto context = qualityContextFor(p, style, settings);
        const int expected =
            std::clamp(overallScore(scoreCriteria(p, context), style.quality, settings.creativityPct), 0, 100);
        CHECK(p.qualityScore == expected);
        differs += p.qualityScore != base.qualityScore ? 1 : 0;
    }
    CHECK(differs > 0);
}

TEST_CASE("every voice draws from its own stream", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    // Two voices with the same rhythm, and nothing but length, velocity and accent free: from one shared stream they
    // would be varied identically.
    Pattern base = makeEmptyPattern(2, "peak_time");
    for (size_t v = 0; v < 2; ++v) {
        for (uint32_t i = 0; i < 16; ++i) {
            REQUIRE(addNote(base, v, v == 0 ? 40 : 64, i * 480, 240, 90) != 0);
        }
        size_t index = 0;
        for (Note& note : base.voices[v].notes) {
            note.lock.pitch = true;
            note.accent = index++ % 3 == 0;
        }
    }
    size_t different = 0;
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        Pattern p = base;
        REQUIRE(applyVariation(p, style, {}, requestOf(seed, 100)) > 0);
        std::map<uint32_t, const Note*> bassByStart;
        for (const Note& note : p.voices[0].notes) {
            bassByStart[note.startTick] = &note; // the constraint layer may drop bass notes on a kick
        }
        for (const Note& note : p.voices[1].notes) {
            const auto it = bassByStart.find(note.startTick);
            if (it != bassByStart.end()) {
                different += (it->second->velocity != note.velocity || it->second->accent != note.accent) ? 1 : 0;
            }
        }
    }
    CHECK(different > 0);
}

TEST_CASE("a replacement never leaves the range of the voice", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = makeEmptyPattern(2, "peak_time");
    const auto constraints = constraintSettingsFor(base, style);
    const Scale* scale = findScale(base.context.scaleId);
    REQUIRE(scale != nullptr);
    for (size_t v = 0; v < 2; ++v) {
        const int low = constraints.voices[v].rangeLow;
        const int high = constraints.voices[v].rangeHigh;
        for (const int edge : {low, high}) {
            Pattern p = base;
            int pitch = edge;
            while (!isAllowed(*scale, p.context.root, p.context.progression.front().chord, pitch)) {
                pitch += edge == low ? 1 : -1;
            }
            for (uint32_t i = 0; i < 8; ++i) {
                REQUIRE(addNote(p, v, static_cast<uint8_t>(pitch), i * 480 + 240, 120, 90) != 0);
            }
            for (Note& note : p.voices[v].notes) {
                note.lock = {false, true, true}; // only replacing is left
            }
            const Pattern source = p;
            for (uint64_t seed = 1; seed <= 25; ++seed) {
                Pattern q = source;
                applyVariation(q, style, {}, requestOf(seed, kLastSubtlePct, v));
                for (size_t i = 0; i < q.voices[v].notes.size(); ++i) {
                    const int now = q.voices[v].notes[i].pitch;
                    CHECK(now >= low);
                    CHECK(now <= high);
                    CHECK(std::abs(now - pitch) <= 2);
                }
            }
        }
    }
}

TEST_CASE("a locked velocity keeps velocity and accent of the note", "[variation][voice-lock]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 15; ++seed) {
        Pattern base = source(style, 4, seed);
        // Every other note has its velocity locked, so accents have locked and free neighbours.
        std::set<uint32_t> locked;
        for (Track& track : base.voices) {
            for (size_t i = 0; i < track.notes.size(); i += 2) {
                track.notes[i].lock = {false, false, true};
                locked.insert(track.notes[i].id);
            }
        }
        Pattern p = base;
        applyVariation(p, style, {}, requestOf(seed, 100));
        for (size_t v = 0; v < p.voices.size(); ++v) {
            std::map<uint32_t, const Note*> byId;
            for (const Note& note : base.voices[v].notes) {
                byId[note.id] = &note;
            }
            for (const Note& note : p.voices[v].notes) {
                const auto it = byId.find(note.id);
                if (it != byId.end() && locked.count(note.id) != 0) {
                    CHECK(note.velocity == it->second->velocity);
                    CHECK(note.accent == it->second->accent);
                }
            }
        }
    }
}

TEST_CASE("velocities stay between 1 and 127 at the edges", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = makeEmptyPattern(2, "peak_time");
    for (uint32_t i = 0; i < 16; ++i) {
        REQUIRE(addNote(base, 1, 64, i * 480, 240, i % 2 == 0 ? 127 : 1) != 0);
    }
    for (Note& note : base.voices[1].notes) {
        note.lock = {true, true, false};
    }
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern p = base;
        applyVariation(p, style, {}, requestOf(seed, 100, 1));
        for (const Note& note : p.voices[1].notes) {
            CHECK(note.velocity >= 1);
            CHECK(note.velocity <= 127);
        }
    }
}

TEST_CASE("replacing prefers weak steps", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = makeEmptyPattern(2, "peak_time");
    for (uint32_t i = 0; i < 16; ++i) {
        REQUIRE(addNote(base, 1, 64, i * 480, 120, 90) != 0); // half of them on the beat, half off
    }
    for (Note& note : base.voices[1].notes) {
        note.lock = {false, true, true};
    }
    int weak = 0;
    int total = 0;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        Pattern p = base;
        if (applyVariation(p, style, {}, requestOf(seed, 1, 1)) == 0) {
            continue;
        }
        for (size_t i = 0; i < p.voices[1].notes.size(); ++i) {
            if (p.voices[1].notes[i].pitch != base.voices[1].notes[i].pitch) {
                ++total;
                weak += p.voices[1].notes[i].startTick % 960 != 0 ? 1 : 0;
            }
        }
    }
    REQUIRE(total > 200);
    CHECK(weak * 100 > total * 65); // three to one by weight: 75 % expected, a uniform choice would give 50 %
}

TEST_CASE("structural operators join from the threshold on and never below it", "[variation][series]") {
    const StyleProfile style = loadShipped("peak_time");
    size_t structuralBelow = 0;
    size_t structuralAbove = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const Pattern base = source(style, 4, 400 + seed);
        const auto starts = [](const Track& track) {
            std::vector<uint32_t> result;
            for (const Note& note : track.notes) {
                result.push_back(note.startTick);
            }
            return result;
        };
        Pattern below = base;
        Pattern above = base;
        applyVariation(below, style, {}, requestOf(seed, kLastSubtlePct));
        applyVariation(above, style, {}, requestOf(seed, 100));
        for (size_t v = 0; v < base.voices.size(); ++v) {
            // Stabs in chord memory move as a whole when a changed bass clashes with them (the constraint layer, D-229):
            // that is not an operator, so such a voice does not count for the subtle strengths.
            const bool memoryStabs = base.voicing.chordMemory && (base.voices[v].archetypeId == "stabs" ||
                                                                  base.voices[v].archetypeId == "aggro_stabs");
            structuralBelow += !memoryStabs && starts(below.voices[v]) != starts(base.voices[v]) ? 1 : 0;
            structuralAbove += starts(above.voices[v]) != starts(base.voices[v]) ? 1 : 0;
        }
    }
    CHECK(structuralBelow == 0);
    CHECK(structuralAbove > 30); // of 80 voices, with up to three operators each
}

TEST_CASE("at full strength the bass changes about a quarter and the melody most notes in pitch or timing",
          "[variation][series]") {
    // Measured shares (3 styles x 20 seeds x 4 bars, D-211): bass 26 %, melody 69 %; before the change 13 % and 33 %.
    size_t bassChanged = 0;
    size_t bassTotal = 0;
    size_t melodyChanged = 0;
    size_t melodyTotal = 0;
    for (const char* name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            const Pattern base = source(style, 4, 300 + seed);
            Pattern strong = base;
            applyVariation(strong, style, {}, requestOf(seed, 100));
            for (size_t v = 0; v < base.voices.size(); ++v) {
                size_t& changed = v == 0 ? bassChanged : melodyChanged;
                size_t& total = v == 0 ? bassTotal : melodyTotal;
                for (const Note& note : base.voices[v].notes) {
                    ++total;
                    const auto& after = strong.voices[v].notes;
                    const auto it = std::find_if(after.begin(), after.end(), [&](const Note& n) { return n.id == note.id; });
                    changed += (it == after.end() || it->pitch != note.pitch || it->startTick != note.startTick ||
                                it->lengthTicks != note.lengthTicks)
                                   ? 1
                                   : 0;
                }
            }
        }
    }
    CHECK(bassChanged * 100 >= bassTotal * 20);
    CHECK(melodyChanged * 100 >= melodyTotal * 50);
}

TEST_CASE("a subtle variation never merges the tones of a chord", "[variation]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        Pattern base = makeEmptyPattern(2, "peak_time");
        base.voices[1].archetypeId = "stabs";
        for (const uint32_t start : {480u, 1440u, 2400u, 3360u, 4320u, 5280u}) {
            for (const int pitch : {64, 65, 67}) { // a cluster: neighbours one or two semitones apart
                Note note;
                note.id = allocateNoteId(base);
                note.pitch = static_cast<uint8_t>(pitch);
                note.startTick = start;
                note.lengthTicks = 240;
                base.voices[1].notes.push_back(note);
            }
        }
        Pattern varied = base;
        applyVariation(varied, style, {}, requestOf(seed, kLastSubtlePct, 1));
        CHECK(varied.voices[1].notes.size() == base.voices[1].notes.size());
    }
}

// -- ratchet and chance as subtle operators (SPEC 3.6, v1.1, D-234) --------------------------------------------------

namespace {

/// A 4-bar voice of loud 16ths on the beats and quiet ones in between, with the archetype `archetype`.
Pattern sixteenths(const std::string& archetype, size_t voice = 1) {
    Pattern p = makeEmptyPattern(4, "peak_time");
    p.voices[voice].archetypeId = archetype;
    for (uint32_t step = 0; step < 64; ++step) {
        Note note;
        note.id = allocateNoteId(p);
        note.pitch = static_cast<uint8_t>(voice == 0 ? 36 : 64);
        note.startTick = step * 240;
        note.lengthTicks = 180;
        note.velocity = step % 2 == 1 ? 70 : 105;
        p.voices[voice].notes.push_back(note);
    }
    return p;
}

int ratchetBar(const Track& track, uint32_t bar) {
    int count = 0;
    for (const Note& n : track.notes) {
        count += n.startTick / kTicksPerBar == bar && n.ratchet > 1 ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("a subtle variation adds and takes away ratchets only in the archetypes that take them", "[variation][ratchet]") {
    const StyleProfile style = loadShipped("hard_industrial");
    int added = 0;
    int removed = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern base = sixteenths("acid_siren");
        if (seed % 2 == 0) {
            base.voices[1].notes[8].ratchet = 3; // one to take away
        }
        Pattern varied = base;
        applyVariation(varied, style, {}, requestOf(seed, kLastSubtlePct, 1));
        for (uint32_t bar = 0; bar < 4; ++bar) {
            CHECK(ratchetBar(varied.voices[1], bar) <= 1); // one per bar
        }
        size_t before = 0;
        size_t after = 0;
        for (size_t i = 0; i < base.voices[1].notes.size(); ++i) {
            before += base.voices[1].notes[i].ratchet > 1 ? 1 : 0;
            after += varied.voices[1].notes[i].ratchet > 1 ? 1 : 0;
            CHECK(varied.voices[1].notes[i].startTick == base.voices[1].notes[i].startTick);
            CHECK_FALSE((varied.voices[1].notes[i].ratchet > 1 && varied.voices[1].notes[i].slide));
        }
        added += after > before ? 1 : 0;
        removed += after < before ? 1 : 0;
        CHECK(validatePattern(varied).empty());
        // another archetype never takes one
        Pattern other = sixteenths("hypnotic_motif");
        applyVariation(other, style, {}, requestOf(seed, kLastSubtlePct, 1));
        CHECK(ratchetBar(other.voices[1], 0) + ratchetBar(other.voices[1], 1) + ratchetBar(other.voices[1], 2) +
                  ratchetBar(other.voices[1], 3) ==
              0);
    }
    CHECK(added > 5);
    CHECK(removed > 2);
}

TEST_CASE("a locked rhythm keeps its ratchets and chances", "[variation][ratchet]") {
    const StyleProfile style = loadShipped("hard_industrial");
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Pattern base = sixteenths("acid_siren");
        base.voices[1].lock.rhythm = true;
        Pattern varied = base;
        applyVariation(varied, style, {}, requestOf(seed, kLastSubtlePct, 1));
        for (size_t i = 0; i < base.voices[1].notes.size(); ++i) {
            CHECK(varied.voices[1].notes[i].ratchet == 1);
            CHECK(varied.voices[1].notes[i].chance == 100);
        }
    }
}

TEST_CASE("a subtle variation sets and clears the chance of quiet off-beat notes, sparingly", "[variation][chance]") {
    const StyleProfile style = loadShipped("peak_time");
    int set = 0;
    int cleared = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern base = sixteenths("hypnotic_motif");
        if (seed % 2 == 0) {
            base.voices[1].notes[9].chance = 60; // one to clear
            base.voices[1].notes[0].condB = 2;   // and a condition that stays
        }
        base.voices[1].notes[3].accent = true; // an accent is no ornament
        Pattern varied = base;
        applyVariation(varied, style, {}, requestOf(seed, kLastSubtlePct, 1));
        size_t marked = 0;
        for (size_t i = 0; i < varied.voices[1].notes.size(); ++i) {
            const Note& n = varied.voices[1].notes[i];
            if (n.chance < 100) {
                ++marked;
                if (base.voices[1].notes[i].chance == 100) {
                    CHECK(n.startTick % 480 == 240); // a new one is on an off 16th
                    CHECK(n.velocity < 100);
                    CHECK_FALSE(n.accent);
                    CHECK(n.chance >= 50);
                    CHECK(n.chance <= 80);
                }
            }
            CHECK(n.condB == base.voices[1].notes[i].condB); // conditions are not touched
        }
        CHECK(marked * 100 <= varied.voices[1].notes.size() * 15 + 100); // sparing (the one that was there is allowed)
        for (size_t i = 0; i < base.voices[1].notes.size(); ++i) {
            set += base.voices[1].notes[i].chance == 100 && varied.voices[1].notes[i].chance < 100 ? 1 : 0;
            cleared += base.voices[1].notes[i].chance < 100 && varied.voices[1].notes[i].chance == 100 ? 1 : 0;
        }
    }
    CHECK(set > 10);
    CHECK(cleared > 3);
}
