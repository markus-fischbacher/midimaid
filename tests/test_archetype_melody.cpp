#include "PatternFixtures.h"
#include "core/Archetype.h"
#include "core/Theory.h"
#include "core/Voicing.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;
using namespace mm::fixtures;

namespace {

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    INFO(name << ": " << result.error);
    REQUIRE(result.ok());
    return *result.profile;
}

const std::vector<std::string> kStyles{"peak_time", "melodic_techno", "hard_industrial"};

struct ChordSpan {
    uint8_t rootOffset;
    ChordQuality quality;
    uint32_t halfBars;
};

void setProgression(Pattern& p, const std::vector<ChordSpan>& spans) {
    p.context.progression.clear();
    uint32_t position = 0;
    for (const ChordSpan& span : spans) {
        ChordEvent event;
        event.chord = {span.rootOffset, span.quality};
        event.startHalfBar = position;
        event.lengthHalfBars = span.halfBars;
        p.context.progression.push_back(event);
        position += span.halfBars;
    }
    REQUIRE(position == p.lengthBars * 2);
}

/// A varied progression over the whole pattern (all chord qualities occur).
void variedProgression(Pattern& p, uint64_t seed) {
    static const std::vector<ChordSpan> cycle{{0, ChordQuality::Minor, 2},
                                              {10, ChordQuality::Major, 1},
                                              {5, ChordQuality::Diminished, 1},
                                              {8, ChordQuality::Sus2, 1},
                                              {7, ChordQuality::Sus4, 1}};
    std::vector<ChordSpan> spans;
    uint32_t covered = 0;
    for (size_t i = 0; covered < p.lengthBars * 2; ++i) {
        ChordSpan span = cycle[(i + seed) % cycle.size()];
        span.halfBars = std::min(span.halfBars, p.lengthBars * 2 - covered);
        covered += span.halfBars;
        spans.push_back(span);
    }
    setProgression(p, spans);
}

Pattern makePattern(uint32_t bars, const std::string& kickGrid = "4otf") {
    Pattern p = makeEmptyPattern(bars, "test");
    p.kickGridId = kickGrid;
    return p;
}

/// Generates a bass (rolling16) and then the melody archetype; returns the melody notes.
std::vector<Note> melodyOf(Pattern& p, const std::string& id, const StyleProfile& style, uint64_t seed, int energy = 50,
                           int creativity = 40, bool withBass = true) {
    Pcg32 rng = Pcg32::fromSeed(seed);
    if (withBass) {
        REQUIRE(generateVoice(p, 0, "rolling16", style, ArchetypeSettings{energy, creativity}, rng));
    }
    REQUIRE(generateVoice(p, 1, id, style, ArchetypeSettings{energy, creativity}, rng));
    return p.voices[1].notes;
}

std::vector<std::string> melodyIds() {
    std::vector<std::string> ids;
    for (const Archetype& archetype : allArchetypes()) {
        if (archetype.role == VoiceRole::Melody) {
            ids.emplace_back(archetype.id);
        }
    }
    return ids;
}

} // namespace

TEST_CASE("the table lists the eleven melody archetypes", "[archetype-melody]") {
    const std::vector<std::string> expected{"hypnotic_motif", "stabs",        "arp",           "acid_siren",
                                            "chord_arp",      "lead_phrase",  "call_response", "pluck_seq",
                                            "aggro_stabs",    "atonal_motif", "sparse_hits"};
    CHECK(melodyIds() == expected);
    CHECK(findArchetype("polymeter_seq") == nullptr); // [v1.1]
    for (const std::string& id : expected) {
        const Archetype* archetype = findArchetype(id);
        REQUIRE(archetype != nullptr);
        CHECK(archetype->role == VoiceRole::Melody);
        CHECK_FALSE(archetype->ignoresKick);
    }
}

TEST_CASE("generated melodies need no repair from the constraint layer", "[archetype-melody]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const std::string& id : melodyIds()) {
            for (const uint32_t bars : {1u, 2u, 4u, 8u}) {
                for (const int energy : {0, 40, 80}) {
                    for (uint64_t seed = 1; seed <= 4; ++seed) {
                        Pattern p = makePattern(bars);
                        p.context.scaleId = std::string(style.scales.front().id);
                        variedProgression(p, seed);
                        const auto notes = melodyOf(p, id, style, seed, energy, 60);
                        INFO(styleName << " " << id << " bars " << bars << " energy " << energy << " seed " << seed);
                        CHECK_FALSE(notes.empty());
                        Pattern repaired = p;
                        const ConstraintReport report =
                            applyConstraints(repaired, constraintSettingsFor(repaired, style));
                        CHECK(report.total() == 0);
                        CHECK(repaired == p);
                    }
                }
            }
        }
    }
}

namespace {

/// (step within the bar, pitch, length) of the notes of bars [from, to), relative to the first bar.
using Signature = std::vector<std::tuple<uint32_t, int, uint32_t>>;

Signature signature(const std::vector<Note>& notes, uint32_t from, uint32_t to) {
    Signature s;
    for (const Note& n : notes) {
        const uint32_t bar = n.startTick / kTicksPerBar;
        if (bar >= from && bar < to) {
            s.emplace_back(n.startTick - from * kTicksPerBar, n.pitch, n.lengthTicks);
        }
    }
    return s;
}

/// Like `signature`, but without the lengths (the last note of a pattern is cut at the pattern end).
std::vector<std::pair<uint32_t, int>> startsAndPitches(const std::vector<Note>& notes, uint32_t from, uint32_t to) {
    std::vector<std::pair<uint32_t, int>> result;
    for (const auto& [start, pitch, length] : signature(notes, from, to)) {
        (void)length;
        result.emplace_back(start, pitch);
    }
    return result;
}

std::vector<uint32_t> stepsInBar(const std::vector<Note>& notes, uint32_t bar) {
    std::vector<uint32_t> steps;
    for (const Note& n : notes) {
        if (n.startTick / kTicksPerBar == bar &&
            (steps.empty() || steps.back() != (n.startTick % kTicksPerBar) / 240)) {
            steps.push_back((n.startTick % kTicksPerBar) / 240);
        }
    }
    return steps;
}

int span(const std::vector<Note>& notes) {
    int low = 127;
    int high = 0;
    for (const Note& n : notes) {
        low = std::min<int>(low, n.pitch);
        high = std::max<int>(high, n.pitch);
    }
    return notes.empty() ? 0 : high - low;
}

} // namespace

TEST_CASE("hypnotic_motif repeats a short motif with spare variations", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    int oneBar = 0;
    int twoBars = 0;
    bool velocityVaried = false;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Pattern p = makePattern(16);
        const auto notes = melodyOf(p, "hypnotic_motif", style, seed, 50, 40, false);
        INFO("seed " << seed);
        REQUIRE_FALSE(notes.empty());
        CHECK(span(notes) <=
              7 + 12); // the motif spans 7 semitones; a variation may transpose, an octave fold is possible
        // blocks of one or two bars repeat at least three times before the first variation
        const bool one =
            signature(notes, 0, 1) == signature(notes, 1, 2) && signature(notes, 1, 2) == signature(notes, 2, 3);
        const bool two =
            signature(notes, 0, 2) == signature(notes, 2, 4) && signature(notes, 2, 4) == signature(notes, 4, 6);
        CHECK((one || two));
        oneBar += one ? 1 : 0;
        twoBars += two && !one ? 1 : 0;
        for (const Note& n : notes) {
            velocityVaried = velocityVaried || n.velocity != 100;
        }
    }
    CHECK(oneBar > 30);    // energy 50: four notes per bar, too dense for two bars
    CHECK(velocityVaried); // micro variations change the velocity and survive the contour
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Pattern p = makePattern(16);
        const auto notes = melodyOf(p, "hypnotic_motif", style, seed, 0, 40, false);
        const bool two =
            signature(notes, 0, 2) == signature(notes, 2, 4) && signature(notes, 2, 4) == signature(notes, 4, 6);
        twoBars += two && signature(notes, 0, 1) != signature(notes, 1, 2) ? 1 : 0;
    }
    CHECK(twoBars > 10); // energy 0: two-bar motifs of four notes
}

TEST_CASE("hypnotic_motif keeps strong steps on chord tones and passes on weak steps", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    size_t weak = 0;
    size_t weakNonChord = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Pattern p = makePattern(2);
        const auto notes = melodyOf(p, "hypnotic_motif", style, seed, 50, 40, false);
        const Chord chord = p.context.progression.front().chord;
        for (const Note& n : notes) {
            const uint32_t step = (n.startTick % kTicksPerBar) / 240;
            const bool chordTone = isChordTone(p.context.root, chord, n.pitch);
            if (step % 4 == 0) {
                CHECK(chordTone);
            } else {
                ++weak;
                weakNonChord += chordTone ? 0 : 1;
            }
        }
    }
    CHECK(weak > 0);
    CHECK(weakNonChord * 100 >= weak * 20);
}

TEST_CASE("lead_phrase ends on a chord tone and puts its climax into the middle of the phrase", "[archetype-melody]") {
    for (const char* styleName : {"melodic_techno", "peak_time"}) {
        const StyleProfile style = loadShipped(styleName);
        for (const uint32_t bars : {2u, 4u, 8u, 16u}) {
            for (uint64_t seed = 1; seed <= 40; ++seed) {
                Pattern p = makePattern(bars);
                p.context.scaleId = std::string(style.scales.front().id);
                const auto notes = melodyOf(p, "lead_phrase", style, seed, 50, 40, false);
                INFO(styleName << " bars " << bars << " seed " << seed);
                REQUIRE_FALSE(notes.empty());
                CHECK(span(notes) <= 19); // octave plus fifth
                const Note& last = *std::max_element(
                    notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
                CHECK(isChordTone(p.context.root, p.context.progression.front().chord, last.pitch));
                int top = 0;
                for (const Note& n : notes) {
                    top = std::max<int>(top, n.pitch);
                }
                const auto firstTop =
                    std::find_if(notes.begin(), notes.end(), [&](const Note& n) { return n.pitch == top; });
                const uint32_t length = bars * kTicksPerBar;
                CHECK(firstTop->startTick >= length / 4);
                CHECK(firstTop->startTick < length * 3 / 4);
            }
        }
    }
}

TEST_CASE("lead_phrase shapes every phrase of the pattern on its own", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern p = makePattern(8);
        p.phrases.clear();
        p.phrases.push_back(makePhrase(0, 4, PhraseRole::Main));
        p.phrases.push_back(makePhrase(4, 4, PhraseRole::Variation));
        const auto notes = melodyOf(p, "lead_phrase", style, seed, 50, 40, false);
        for (const uint32_t start : {0u, 4u}) {
            std::vector<Note> phrase;
            for (const Note& n : notes) {
                if (n.startTick / kTicksPerBar >= start && n.startTick / kTicksPerBar < start + 4) {
                    phrase.push_back(n);
                }
            }
            INFO("seed " << seed << " phrase " << start);
            REQUIRE_FALSE(phrase.empty());
            int top = 0;
            for (const Note& n : phrase) {
                top = std::max<int>(top, n.pitch);
            }
            const auto firstTop =
                std::find_if(phrase.begin(), phrase.end(), [&](const Note& n) { return n.pitch == top; });
            CHECK(firstTop->startTick >= (start + 1) * kTicksPerBar);
            CHECK(firstTop->startTick < (start + 3) * kTicksPerBar);
            const Note& last = phrase.back();
            CHECK(isChordTone(p.context.root, p.context.progression.front().chord, last.pitch));
        }
    }
}

TEST_CASE("call_response answers the call with a varied motif", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        {
            Pattern p = makePattern(4); // 2 + 2 bars
            const auto notes = melodyOf(p, "call_response", style, seed, 50, 40, false);
            INFO("seed " << seed);
            CHECK(signature(notes, 0, 2) != signature(notes, 2, 4));
            CHECK_FALSE(signature(notes, 0, 2).empty());
            CHECK_FALSE(signature(notes, 2, 4).empty());
        }
        {
            Pattern p = makePattern(8); // 4 + 4 bars, the call repeats its motif, so does the answer
            const auto notes = melodyOf(p, "call_response", style, seed, 50, 40, false);
            INFO("seed " << seed);
            const auto callA = signature(notes, 0, 4);
            CHECK(callA != signature(notes, 4, 8));
            // the call: one motif of one or two bars, repeated; the answer the same for its own motif
            const bool oneBarCall = signature(notes, 0, 1) == signature(notes, 1, 2) &&
                                    signature(notes, 1, 2) == signature(notes, 2, 3) &&
                                    signature(notes, 2, 3) == signature(notes, 3, 4);
            const bool twoBarCall = signature(notes, 0, 2) == signature(notes, 2, 4);
            CHECK((oneBarCall || twoBarCall));
            const bool oneBarAnswer = signature(notes, 4, 5) == signature(notes, 5, 6) &&
                                      signature(notes, 5, 6) == signature(notes, 6, 7) &&
                                      signature(notes, 6, 7) == signature(notes, 7, 8);
            const bool twoBarAnswer = signature(notes, 4, 6) == signature(notes, 6, 8);
            CHECK((oneBarAnswer || twoBarAnswer));
        }
        {
            Pattern p = makePattern(2); // 1 + 1 bar
            const auto notes = melodyOf(p, "call_response", style, seed, 50, 40, false);
            CHECK(signature(notes, 0, 1) != signature(notes, 1, 2));
        }
        {
            Pattern p = makePattern(1); // a call alone
            const auto notes = melodyOf(p, "call_response", style, seed, 50, 40, false);
            CHECK_FALSE(notes.empty());
        }
    }
}

TEST_CASE("pluck_seq repeats a pitch sequence on an 8th or 16th grid", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    int eighths = 0;
    int sixteenths = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Pattern p = makePattern(1);
        const auto notes = melodyOf(p, "pluck_seq", style, seed, 100, 0, false);
        INFO("seed " << seed);
        REQUIRE(notes.size() == 6); // middle of the density band at energy 100
        const bool sixteenth =
            std::any_of(notes.begin(), notes.end(), [](const Note& n) { return n.startTick % 480 != 0; });
        (sixteenth ? sixteenths : eighths) += 1;
        std::set<std::vector<int>> positionsByPitchClass;
        for (const Note& n : notes) {
            CHECK(n.lengthTicks == (sixteenth ? 180u : 240u) - 0u);
        }
        // one cycle of 3-6 degrees, so equal positions modulo the cycle have equal pitches
        const uint32_t gridTicks = sixteenth ? 240 : 480;
        bool cycleFound = false;
        for (uint32_t cycle = 3; cycle <= 6 && !cycleFound; ++cycle) {
            std::map<uint32_t, int> pitchOf;
            bool consistent = true;
            for (const Note& n : notes) {
                const uint32_t index = (n.startTick / gridTicks) % cycle;
                const auto it = pitchOf.find(index);
                if (it == pitchOf.end()) {
                    pitchOf[index] = n.pitch;
                } else {
                    consistent = consistent && it->second == n.pitch;
                }
            }
            cycleFound = consistent;
        }
        CHECK(cycleFound);
    }
    CHECK(eighths > 10);
    CHECK(sixteenths > 10);
}

TEST_CASE("pluck_seq density follows the energy", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const std::map<int, size_t> expected{{0, 2}, {50, 4}, {100, 6}};
    for (const auto& [energy, count] : expected) {
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Pattern p = makePattern(2);
            const auto notes = melodyOf(p, "pluck_seq", style, seed, energy, 0, false);
            CHECK(notes.size() == 2 * count);
        }
    }
}

TEST_CASE("arp plays chord tones in 16ths in all four directions", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    int up = 0;
    int down = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Pattern p = makePattern(1);
        const auto notes = melodyOf(p, "arp", style, seed, 100, 0, false);
        INFO("seed " << seed);
        REQUIRE(notes.size() == 16);
        for (size_t i = 0; i < notes.size(); ++i) {
            CHECK(notes[i].startTick == i * 240);
            CHECK(isChordTone(p.context.root, p.context.progression.front().chord, notes[i].pitch));
            CHECK((notes[i].lengthTicks == 180 || notes[i].lengthTicks == 240));
        }
        CHECK(span(notes) < 24);
        const bool ascending = notes[0].pitch < notes[1].pitch && notes[1].pitch < notes[2].pitch;
        const bool descending = notes[0].pitch > notes[1].pitch && notes[1].pitch > notes[2].pitch;
        up += ascending ? 1 : 0;
        down += descending ? 1 : 0;
    }
    CHECK(up > 8);
    CHECK(down > 8);
}

TEST_CASE("arp density, harmony and range", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    const std::map<int, size_t> expected{{0, 7}, {50, 12}, {100, 16}}; // ceil(16 * (40 % + 0.6 * energy))
    for (const auto& [energy, count] : expected) {
        Pattern p = makePattern(2);
        const auto notes = melodyOf(p, "arp", style, 3, energy, 0, false);
        CHECK(notes.size() == 2 * count);
    }
    Pattern p = makePattern(2);
    setProgression(p, {{0, ChordQuality::Minor, 2}, {10, ChordQuality::Major, 2}});
    const auto notes = melodyOf(p, "arp", style, 5, 100, 0, false);
    for (const Note& n : notes) {
        const Chord chord = n.startTick < kTicksPerBar ? Chord{0, ChordQuality::Minor} : Chord{10, ChordQuality::Major};
        CHECK(isChordTone(p.context.root, chord, n.pitch));
        CHECK(n.pitch >= 55);
        CHECK(n.pitch <= 88);
    }
}

TEST_CASE("chord_arp draws a new pattern per phrase and plays seventh chords", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    int differs = 0;
    bool eighth = false;
    bool sixteenth = false;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Pattern p = makePattern(8);
        p.phrases.clear();
        p.phrases.push_back(makePhrase(0, 4, PhraseRole::Main));
        p.phrases.push_back(makePhrase(4, 4, PhraseRole::Variation));
        const auto notes = melodyOf(p, "chord_arp", style, seed, 100, 0, false);
        INFO("seed " << seed);
        REQUIRE_FALSE(notes.empty());
        differs += signature(notes, 0, 1) != signature(notes, 4, 5) ? 1 : 0;
        for (const Note& n : notes) {
            const auto pcs = colorTones(*findScale(p.context.scaleId), p.context.root,
                                        p.context.progression.front().chord, ChordColor::Min7);
            CHECK(std::find(pcs.begin(), pcs.end(), pitchClassOf(n.pitch)) != pcs.end());
            (n.startTick % 480 == 0 ? eighth : sixteenth) = true;
            sixteenth = sixteenth || n.startTick % 480 != 0;
        }
    }
    CHECK(differs > 30);
    CHECK(eighth);
    CHECK(sixteenth);
}

namespace {

/// The pitch classes of the notes that start at `tick` (a chord hit), sorted.
std::set<int> chordAtTick(const std::vector<Note>& notes, uint32_t tick) {
    std::set<int> pcs;
    for (const Note& n : notes) {
        if (n.startTick == tick) {
            pcs.insert(pitchClassOf(n.pitch));
        }
    }
    return pcs;
}

} // namespace

TEST_CASE("stabs hit the offbeats first, then syncopations, as chords", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    const std::map<int, size_t> hits{{0, 2}, {50, 4}, {100, 6}};
    for (const auto& [energy, count] : hits) {
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Pattern p = makePattern(2);
            const auto notes = melodyOf(p, "stabs", style, seed, energy, 0, false);
            INFO("energy " << energy << " seed " << seed);
            for (uint32_t bar = 0; bar < 2; ++bar) {
                const auto steps = stepsInBar(notes, bar);
                CHECK(steps.size() == count);
                const size_t offbeats = static_cast<size_t>(
                    std::count_if(steps.begin(), steps.end(), [](uint32_t s) { return s % 4 == 2; }));
                CHECK(offbeats == std::min<size_t>(count, 4));
            }
            for (const Note& n : notes) {
                CHECK((n.lengthTicks == 240 || n.lengthTicks == 480));
                CHECK(n.pitch >= 55);
                CHECK(n.pitch <= 79);
            }
        }
    }
    // at energy 50 the four offbeats of every bar
    Pattern p = makePattern(1);
    const auto notes = melodyOf(p, "stabs", style, 1, 50, 0, false);
    CHECK(stepsInBar(notes, 0) == std::vector<uint32_t>{2, 6, 10, 14});
}

TEST_CASE("stab chords follow the colour weights of the profile", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time"); // min 40, fifth 30, min7 20, sus 10
    std::map<std::string, size_t> counts;
    size_t total = 0;
    for (uint64_t seed = 1; seed <= 2000; ++seed) {
        Pattern p = makePattern(1);
        const auto notes = melodyOf(p, "stabs", style, seed, 50, 0, false);
        const auto pcs = chordAtTick(notes, 2 * 240);
        std::string kind = "other";
        if (pcs == std::set<int>{9, 0, 4}) {
            kind = "min";
        } else if (pcs == std::set<int>{9, 4}) {
            kind = "fifth";
        } else if (pcs == std::set<int>{9, 0, 4, 7}) {
            kind = "min7";
        } else if (pcs == std::set<int>{9, 11, 4} || pcs == std::set<int>{9, 2, 4}) {
            kind = "sus";
        }
        ++counts[kind];
        ++total;
    }
    CHECK(counts["other"] == 0);
    CHECK(counts["min"] * 100 > total * 35);
    CHECK(counts["min"] * 100 < total * 45);
    CHECK(counts["fifth"] * 100 > total * 25);
    CHECK(counts["fifth"] * 100 < total * 35);
    CHECK(counts["min7"] * 100 > total * 15);
    CHECK(counts["min7"] * 100 < total * 25);
    CHECK(counts["sus"] * 100 > total * 6);
    CHECK(counts["sus"] * 100 < total * 14);
}

TEST_CASE("stab voicings lead the voices from chord to chord", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        Pattern p = makePattern(4);
        setProgression(p, {{0, ChordQuality::Minor, 2},
                           {10, ChordQuality::Major, 2},
                           {8, ChordQuality::Major, 2},
                           {0, ChordQuality::Minor, 2}});
        const auto notes = melodyOf(p, "stabs", style, seed, 50, 0, false);
        std::vector<std::vector<int>> voicings;
        for (uint32_t bar = 0; bar < 4; ++bar) {
            std::vector<int> pitches;
            for (const Note& n : notes) {
                if (n.startTick == bar * kTicksPerBar + 2 * 240) {
                    pitches.push_back(n.pitch);
                }
            }
            REQUIRE_FALSE(pitches.empty());
            voicings.push_back(pitches);
        }
        INFO("seed " << seed);
        for (size_t i = 1; i < voicings.size(); ++i) {
            // the nearest-tone distance in both directions; a close voicing never jumps further than a fifth
            CHECK(maxVoiceMovement(voicings[i - 1], voicings[i]) <= 7);
        }
        // the same chord of the same colour in two bars would keep its voicing; every hit of a bar is the same chord
        for (uint32_t bar = 0; bar < 4; ++bar) {
            for (const uint32_t step : stepsInBar(notes, bar)) {
                std::vector<int> pitches;
                for (const Note& n : notes) {
                    if (n.startTick == bar * kTicksPerBar + step * 240) {
                        pitches.push_back(n.pitch);
                    }
                }
                CHECK(pitches == voicings[bar]);
            }
        }
    }
}

TEST_CASE("aggro_stabs are short, loud clusters and fifths", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial"); // cluster 40, fifth 40, min 20
    std::map<std::string, size_t> counts;
    size_t total = 0;
    for (uint64_t seed = 1; seed <= 1500; ++seed) {
        Pattern p = makePattern(1);
        p.context.scaleId = "phrygian"; // A phrygian has the flat second, so the cluster survives
        const auto notes = melodyOf(p, "aggro_stabs", style, seed, 50, 0, false);
        const auto pcs = chordAtTick(notes, 2 * 240);
        std::string kind = "other";
        if (pcs == std::set<int>{9, 10, 4}) {
            kind = "cluster";
        } else if (pcs == std::set<int>{9, 4}) {
            kind = "fifth";
        } else if (pcs == std::set<int>{9, 0, 4}) {
            kind = "min";
        }
        ++counts[kind];
        ++total;
        for (const Note& n : notes) {
            CHECK((n.lengthTicks == 120 || n.lengthTicks == 240));
            CHECK(n.velocity == 112);
        }
    }
    CHECK(counts["other"] == 0);
    CHECK(counts["cluster"] * 100 > total * 34);
    CHECK(counts["cluster"] * 100 < total * 46);
    CHECK(counts["fifth"] * 100 > total * 34);
    CHECK(counts["fifth"] * 100 < total * 46);
    CHECK(counts["min"] * 100 > total * 14);
    CHECK(counts["min"] * 100 < total * 26);
}

TEST_CASE("stabs accent more often at high energy, aggro_stabs mostly", "[archetype-melody]") {
    size_t soft = 0;
    size_t hard = 0;
    size_t aggro = 0;
    size_t total = 0;
    const StyleProfile peak = loadShipped("peak_time");
    const StyleProfile industrial = loadShipped("hard_industrial");
    for (uint64_t seed = 1; seed <= 400; ++seed) {
        for (const int energy : {0, 100}) {
            Pattern p = makePattern(1);
            for (const Note& n : melodyOf(p, "stabs", peak, seed, energy, 0, false)) {
                (energy == 0 ? soft : hard) += n.accent ? 1 : 0;
            }
        }
        Pattern q = makePattern(1);
        for (const Note& n : melodyOf(q, "aggro_stabs", industrial, seed, 50, 0, false)) {
            aggro += n.accent ? 1 : 0;
            ++total;
        }
    }
    CHECK(soft > 0);
    CHECK(hard > soft);
    CHECK(aggro * 100 > total * 50);
    CHECK(aggro * 100 < total * 70);
}

TEST_CASE("stab velocity follows the contour and the energy", "[archetype-melody]") {
    const StyleProfile peak = loadShipped("peak_time");
    const std::map<int, int> expected{{0, 93}, {50, 105}, {100, 117}};
    for (const auto& [energy, velocity] : expected) {
        Pattern p = makePattern(1);
        for (const Note& n : melodyOf(p, "stabs", peak, 2, energy, 0, false)) {
            CHECK(n.velocity == velocity);
        }
    }
}

TEST_CASE("acid_siren is a monophonic 16th line within an octave, with slides and accents", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    size_t slides = 0;
    size_t eligible = 0;
    size_t accents = 0;
    size_t total = 0;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        Pattern p = makePattern(2);
        p.context.scaleId = seed % 2 == 0 ? "natural_minor" : "minor_pentatonic";
        const auto notes = melodyOf(p, "acid_siren", style, seed, 100, 0, false);
        INFO("seed " << seed);
        REQUIRE(notes.size() == 32); // 100 % of the 16 steps in both bars
        CHECK(span(notes) <= 12);
        for (size_t i = 0; i < notes.size(); ++i) {
            CHECK(notes[i].startTick == i * 240);
            CHECK((notes[i].lengthTicks == 120 || notes[i].lengthTicks == 240));
            ++total;
            accents += notes[i].accent ? 1 : 0;
            CHECK(inScale(*findScale(p.context.scaleId), p.context.root, notes[i].pitch));
            if (i + 1 < notes.size()) {
                if (notes[i].pitch == notes[i + 1].pitch) {
                    CHECK_FALSE(notes[i].slide);
                } else {
                    ++eligible;
                    slides += notes[i].slide ? 1 : 0;
                }
            } else {
                CHECK_FALSE(notes[i].slide);
            }
        }
    }
    // energy 100: 70 % accents, 45 % slides
    CHECK(accents * 100 > total * 63);
    CHECK(accents * 100 < total * 77);
    CHECK(slides * 100 > eligible * 40);
    CHECK(slides * 100 < eligible * 50);
}

TEST_CASE("acid_siren density follows the energy and has gaps", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial");
    const std::map<int, size_t> expected{{0, 7}, {50, 12}, {100, 16}};
    for (const auto& [energy, count] : expected) {
        Pattern p = makePattern(1);
        const auto notes = melodyOf(p, "acid_siren", style, 4, energy, 0, false);
        CHECK(notes.size() == count);
    }
}

TEST_CASE("atonal_motif repeats two to four notes of tension, bar for bar", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial");
    const std::map<int, size_t> expected{{0, 2}, {50, 3}, {100, 4}};
    for (const auto& [energy, count] : expected) {
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            Pattern p = makePattern(4);
            p.context.scaleId = "phrygian";
            const auto notes = melodyOf(p, "atonal_motif", style, seed, energy, 100, false);
            INFO("energy " << energy << " seed " << seed);
            CHECK(notes.size() == 4 * count);
            for (uint32_t bar = 1; bar < 4; ++bar) {
                CHECK(startsAndPitches(notes, bar, bar + 1) == startsAndPitches(notes, 0, 1)); // doggedly repeated
            }
            for (const Note& n : notes) {
                const int offset = (n.pitch - 69 + 12) % 12; // semitones above the root A
                CHECK((offset == 0 || offset == 1 || offset == 5 ||
                       offset == 7)); // root, flat second, or snapped tritone
                CHECK((n.lengthTicks == 240 || n.lengthTicks == 480 || n.lengthTicks == 240 + 0));
            }
        }
    }
}

TEST_CASE("atonal_motif uses the flat second and, where the scale has it, the tritone", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial");
    std::set<int> phrygian;
    std::set<int> locrian;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        for (const char* scale : {"phrygian", "locrian"}) {
            Pattern p = makePattern(1);
            p.context.scaleId = scale;
            for (const Note& n : melodyOf(p, "atonal_motif", style, seed, 100, 0, false)) {
                (std::string(scale) == "phrygian" ? phrygian : locrian).insert((n.pitch - 69 + 12) % 12);
            }
        }
    }
    CHECK(phrygian.count(1) == 1);
    CHECK(locrian.count(1) == 1);
    CHECK(locrian.count(6) == 1); // A locrian has the diminished fifth
    CHECK(phrygian.count(6) == 0);
}

TEST_CASE("sparse_hits plays one to four hits per two bars and leaves room", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial");
    const std::map<int, size_t> expected{{0, 1}, {50, 2}, {100, 4}};
    std::set<int> offsets;
    for (const auto& [energy, count] : expected) {
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            Pattern p = makePattern(4);
            p.context.scaleId = "phrygian";
            const auto notes = melodyOf(p, "sparse_hits", style, seed, energy, 0, false);
            INFO("energy " << energy << " seed " << seed);
            CHECK(notes.size() == 2 * count); // 2 cells of 2 bars
            CHECK(startsAndPitches(notes, 0, 2) == startsAndPitches(notes, 2, 4));
            for (const Note& n : notes) {
                offsets.insert((n.pitch - 69 + 12) % 12);
                CHECK((n.lengthTicks == 240 || n.lengthTicks == 480));
            }
        }
    }
    CHECK(offsets.count(0) == 1);
    CHECK(offsets.count(1) == 1);
    CHECK(offsets.size() <= 4);
}

TEST_CASE("the line archetypes stay near the density band of the energy", "[archetype-melody]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const char* id : {"hypnotic_motif", "lead_phrase", "call_response", "pluck_seq"}) {
            for (const int energy : {0, 30, 60, 100}) {
                for (uint64_t seed = 1; seed <= 12; ++seed) {
                    Pattern p = makePattern(8);
                    const auto notes = melodyOf(p, id, style, seed, energy, 40, false);
                    const DensityBand band = densityBand(VoiceRole::Melody, energy);
                    const int perBar = static_cast<int>(notes.size()) * 100 / 8;
                    INFO(styleName << " " << id << " energy " << energy << " seed " << seed << " notes "
                                   << notes.size());
                    // the motif rules (pickups, a note count of at least 3) may take a note out of the band
                    CHECK(perBar >= band.low - 100);
                    CHECK(perBar <= band.high + 100);
                }
            }
        }
    }
}

TEST_CASE("the same seed gives the same melody, other seeds differ", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    for (const std::string& id : melodyIds()) {
        Pattern a = makePattern(4);
        Pattern b = makePattern(4);
        melodyOf(a, id, style, 77);
        melodyOf(b, id, style, 77);
        INFO(id);
        CHECK(a == b);
        std::set<Signature> shapes;
        for (uint64_t seed = 1; seed <= 30; ++seed) {
            Pattern p = makePattern(4);
            shapes.insert(signature(melodyOf(p, id, style, seed, 60, 40, false), 0, 4));
        }
        CHECK(shapes.size() > 5);
    }
}

TEST_CASE("generateVoice refuses melody requests that do not fit", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makePattern(2);
    Pcg32 rng = Pcg32::fromSeed(1);
    REQUIRE(generateVoice(p, 1, "arp", style, {}, rng));
    const Pattern before = p;
    CHECK_FALSE(generateVoice(p, 0, "arp", style, {}, rng));    // voice 0 is the bass
    CHECK_FALSE(generateVoice(p, 1, "rumble", style, {}, rng)); // a bass archetype for the melody
    CHECK_FALSE(generateVoice(p, 1, "polymeter_seq", style, {}, rng));
    p.context.scaleId = "no_such_scale";
    CHECK_FALSE(generateVoice(p, 1, "arp", style, {}, rng));
    p.context.scaleId = before.context.scaleId;
    CHECK(p == before);
    // stabs without any chord cannot be voiced
    Pattern empty = makePattern(1);
    empty.context.progression.clear();
    CHECK_FALSE(generateVoice(empty, 1, "stabs", style, {}, rng));
    CHECK(empty.voices[1].notes.empty());
}

TEST_CASE("the melody gives way to the bass: at least an octave above a simultaneous attack", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    size_t withoutBass = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern lone = makePattern(1);
        for (const Note& n : melodyOf(lone, "stabs", style, seed, 50, 0, false)) {
            withoutBass += n.pitch < 64 ? 1 : 0;
        }
        Pattern p = makePattern(1);
        for (const uint32_t step : {2u, 6u, 10u, 14u}) {
            Note bass;
            bass.id = allocateNoteId(p);
            bass.pitch = 52;
            bass.startTick = step * 240;
            bass.lengthTicks = 240;
            p.voices[0].notes.push_back(bass);
        }
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(generateVoice(p, 1, "stabs", style, {}, rng));
        REQUIRE_FALSE(p.voices[1].notes.empty());
        for (const Note& n : p.voices[1].notes) {
            INFO("seed " << seed << " tick " << n.startTick);
            CHECK_FALSE(violatesBassRules(p.voices[0], n.startTick, n.lengthTicks, n.pitch, false));
            if (n.startTick % 960 == 480 || n.startTick % 960 == 0) {
                CHECK(n.pitch >= 64); // bass 52 + 12 where both attack together
            }
        }
    }
    CHECK(withoutBass > 0); // the rule has something to do
}

TEST_CASE("the automatic choice of the melody follows weights and energy", "[archetype-melody]") {
    const StyleProfile style =
        loadShipped("melodic_techno"); // chord_arp 4, lead_phrase 3, call_response 3, pluck_seq 3
    const auto share = [&](const std::string& id, int energy) {
        Pcg32 rng = Pcg32::fromSeed(3);
        size_t hits = 0;
        for (int i = 0; i < 10000; ++i) {
            hits += chooseArchetype(style, VoiceRole::Melody, energy, rng) == id ? 1 : 0;
        }
        return hits;
    };
    // lead_phrase is calm: 3 * (150 - e) against 4 * (50 + e), 300 and 3 * (50 + e)
    CHECK(share("lead_phrase", 0) > 10000 * 38 / 100); // 450 of 1100
    CHECK(share("lead_phrase", 0) < 10000 * 44 / 100);
    CHECK(share("lead_phrase", 100) > 10000 * 7 / 100); // 150 of 1500
    CHECK(share("lead_phrase", 100) < 10000 * 13 / 100);
    CHECK(share("call_response", 50) > 10000 * 21 / 100); // 300 of 1300 at energy 50
    CHECK(share("call_response", 50) < 10000 * 25 / 100);
    // the three styles all have a melody now
    for (const std::string& name : kStyles) {
        Pcg32 rng = Pcg32::fromSeed(1);
        const StyleProfile profile = loadShipped(name);
        for (int i = 0; i < 50; ++i) {
            const std::string id = chooseArchetype(profile, VoiceRole::Melody, 50, rng);
            REQUIRE(findArchetype(id) != nullptr);
            CHECK(findArchetype(id)->role == VoiceRole::Melody);
        }
    }
}

TEST_CASE("a manual melody archetype wins, the track keeps its id", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    Track track;
    track.role = VoiceRole::Melody;
    track.archetypeAuto = false;
    track.archetypeId = "lead_phrase"; // not in the Peak Time profile, but implemented
    Pcg32 rng = Pcg32::fromSeed(8);
    Pcg32 untouched = Pcg32::fromSeed(8);
    CHECK(resolveArchetype(track, style, 50, rng) == "lead_phrase");
    CHECK(rng.next() == untouched.next());
    Pattern p = makePattern(4);
    Pcg32 gen = Pcg32::fromSeed(1);
    REQUIRE(generateVoice(p, 1, "lead_phrase", style, {}, gen));
    CHECK(p.voices[1].archetypeId == "lead_phrase");
}

TEST_CASE("candidate selection works with bass and melody together", "[archetype-melody]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const std::string& id : melodyIds()) {
            const auto generate = [&](uint64_t seed) {
                Pattern p = makePattern(4);
                p.context.scaleId = std::string(style.scales.front().id);
                Pcg32 rng = Pcg32::fromSeed(seed);
                REQUIRE(generateVoice(p, 0, "rolling16", style, {60, 40}, rng));
                REQUIRE(generateVoice(p, 1, id, style, {60, 40}, rng));
                return p;
            };
            const SelectionResult result =
                selectBest(generate, 4321, style.quality, qualityContextFor(generate(1), style, {60, 40}));
            INFO(styleName << " " << id);
            CHECK(result.success);
            if (result.success) {
                CHECK(generate(result.seed) == result.pattern);
            }
        }
    }
}

TEST_CASE("melodies rated by the quality criteria respect the bass", "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    for (const std::string& id : melodyIds()) {
        Pattern p = makePattern(4);
        melodyOf(p, id, style, 5, 60, 40);
        const SoftScores scores = scoreCriteria(p, qualityContextFor(p, style, {60, 40}));
        INFO(id);
        CHECK(scores.intervals == 100); // no tense interval against the bass on a strong step
        CHECK(scores.kick == 100);
    }
}

TEST_CASE("melody archetypes pull the automatic choice as planned", "[archetype-melody]") {
    const std::map<std::string, ArchetypeDensity> expected{{"hypnotic_motif", ArchetypeDensity::Neutral},
                                                           {"stabs", ArchetypeDensity::Neutral},
                                                           {"arp", ArchetypeDensity::Dense},
                                                           {"acid_siren", ArchetypeDensity::Dense},
                                                           {"chord_arp", ArchetypeDensity::Dense},
                                                           {"lead_phrase", ArchetypeDensity::Calm},
                                                           {"call_response", ArchetypeDensity::Neutral},
                                                           {"pluck_seq", ArchetypeDensity::Dense},
                                                           {"aggro_stabs", ArchetypeDensity::Neutral},
                                                           {"atonal_motif", ArchetypeDensity::Neutral},
                                                           {"sparse_hits", ArchetypeDensity::Calm}};
    for (const auto& [id, density] : expected) {
        INFO(id);
        REQUIRE(findArchetype(id) != nullptr);
        CHECK(findArchetype(id)->density == density);
    }
}

TEST_CASE("melodies fit a range of only twelve pitches", "[archetype-melody]") {
    for (const std::string& styleName : kStyles) {
        StyleProfile style = loadShipped(styleName);
        style.melody.range = {60, 71};
        for (const std::string& id : melodyIds()) {
            for (uint64_t seed = 1; seed <= 6; ++seed) {
                Pattern p = makePattern(4);
                p.context.scaleId = std::string(style.scales.front().id);
                p.voicing.lowNote = 60;
                p.voicing.highNote = 71;
                variedProgression(p, seed);
                INFO(styleName << " " << id << " seed " << seed);
                const auto notes = melodyOf(p, id, style, seed, 70, 50);
                for (const Note& n : notes) {
                    CHECK(n.pitch >= 60);
                    CHECK(n.pitch <= 71);
                }
                Pattern repaired = p;
                CHECK(applyConstraints(repaired, constraintSettingsFor(repaired, style)).total() == 0);
            }
        }
    }
}

TEST_CASE("tense intervals against a held bass note are allowed from a chromatic share of 30", "[archetype-melody]") {
    const std::map<int, bool> allowsTension{{29, false}, {30, true}, {35, true}};
    for (const auto& [chromatic, allowed] : allowsTension) {
        StyleProfile style = loadShipped("hard_industrial");
        style.chromaticDefaultPercent = chromatic;
        size_t tense = 0;
        for (uint64_t seed = 1; seed <= 100; ++seed) {
            Pattern p = makePattern(1);
            p.context.scaleId = "phrygian";
            Note bass;
            bass.id = allocateNoteId(p);
            bass.pitch = 33; // A, held over the whole bar
            bass.startTick = 0;
            bass.lengthTicks = kTicksPerBar;
            p.voices[0].notes.push_back(bass);
            p.voices[0].archetypeId = "long_tied"; // may hold over the kick
            Pcg32 rng = Pcg32::fromSeed(seed);
            REQUIRE(generateVoice(p, 1, "atonal_motif", style, {100, 0}, rng));
            for (const Note& n : p.voices[1].notes) {
                const int interval = ((n.pitch - 33) % 12 + 12) % 12;
                if (n.startTick % 960 == 0 && (interval == 1 || interval == 6 || interval == 11)) {
                    ++tense;
                }
            }
            Pattern repaired = p;
            CHECK(applyConstraints(repaired, constraintSettingsFor(repaired, style)).total() == 0);
        }
        INFO("chromatic share " << chromatic);
        CHECK((tense > 0) == allowed);
    }
}

// -- closing the gaps found by the mutation checks --------------------------------------------------------------------

namespace {

/// Start steps (0..15) of the notes of one bar, as a set.
std::set<uint32_t> stepSet(const std::vector<Note>& notes, uint32_t bar) {
    std::set<uint32_t> steps;
    for (const Note& n : notes) {
        if (n.startTick / kTicksPerBar == bar) {
            steps.insert((n.startTick % kTicksPerBar) / 240);
        }
    }
    return steps;
}

} // namespace

TEST_CASE("raster archetypes vary only the last bar of four, and move one note", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (const char* id : {"pluck_seq", "arp", "chord_arp", "stabs", "aggro_stabs", "acid_siren"}) {
        int moved = 0;
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            Pattern p = makePattern(8);
            const auto notes = melodyOf(p, id, style, seed, 30, 100, false);
            INFO(id << " seed " << seed);
            for (const uint32_t bar : {1u, 2u, 5u, 6u}) {
                CHECK(stepSet(notes, bar) == stepSet(notes, 0));
            }
            for (const uint32_t bar : {3u, 7u}) {
                const auto base = stepSet(notes, 0);
                const auto varied = stepSet(notes, bar);
                CHECK(varied.size() == base.size()); // the density stays
                std::vector<uint32_t> removed;
                std::set_difference(base.begin(), base.end(), varied.begin(), varied.end(),
                                    std::back_inserter(removed));
                CHECK(removed.size() <= 1); // one note moves, no more
                moved += removed.size() == 1 ? 1 : 0;
            }
        }
        CHECK(moved > 10);
    }
}

TEST_CASE("pluck_seq follows the root of the chord by scale steps", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const Scale* scale = findScale("natural_minor");
    REQUIRE(scale != nullptr);
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern a = makePattern(1);
        a.context.scaleId = "natural_minor";
        setProgression(a, {{0, ChordQuality::Minor, 2}});
        Pattern b = a;
        setProgression(b, {{5, ChordQuality::Minor, 2}}); // iv, the fourth scale degree (index 3)
        const auto base = melodyOf(a, "pluck_seq", style, seed, 100, 0, false);
        const auto shifted = melodyOf(b, "pluck_seq", style, seed, 100, 0, false);
        REQUIRE(base.size() == shifted.size());
        for (size_t i = 0; i < base.size(); ++i) {
            REQUIRE(base[i].startTick == shifted[i].startTick);
            const int pcBase = (base[i].pitch + 120 - a.context.root) % 12;
            const int pcShifted = (shifted[i].pitch + 120 - a.context.root) % 12;
            const auto index =
                std::find(scale->intervals.begin(), scale->intervals.end(), pcBase) - scale->intervals.begin();
            REQUIRE(index < static_cast<std::ptrdiff_t>(scale->size()));
            INFO("seed " << seed << " note " << i);
            CHECK(pcShifted == scale->intervals[static_cast<size_t>(index + 3) % scale->size()]);
        }
    }
}

TEST_CASE("atonal_motif never repeats a pitch in direct succession", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial");
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        Pattern p = makePattern(1);
        p.context.scaleId = "locrian"; // root, flat second and tritone are three different pitches here
        const auto notes = melodyOf(p, "atonal_motif", style, seed, 100, 0, false);
        REQUIRE(notes.size() == 4);
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
            INFO("seed " << seed);
            CHECK(notes[i].pitch != notes[i + 1].pitch);
        }
    }
}

TEST_CASE("sparse_hits weigh root, flat second and tritone 50 : 30 : 20", "[archetype-melody]") {
    const StyleProfile style = loadShipped("hard_industrial");
    std::map<int, int> counts;
    int total = 0;
    for (uint64_t seed = 1; seed <= 400; ++seed) {
        Pattern p = makePattern(2);
        p.context.scaleId = "locrian";
        for (const Note& n : melodyOf(p, "sparse_hits", style, seed, 100, 0, false)) {
            ++counts[(n.pitch - 69 + 120) % 12];
            ++total;
        }
    }
    REQUIRE(total == 400 * 4);
    CHECK(counts.size() == 3);
    CHECK(counts[0] * 100 > total * 44);
    CHECK(counts[0] * 100 < total * 56);
    CHECK(counts[1] * 100 > total * 24);
    CHECK(counts[1] * 100 < total * 36);
    CHECK(counts[6] * 100 > total * 14);
    CHECK(counts[6] * 100 < total * 26);
}

TEST_CASE("arp directions: up, down, up-and-down and random all occur, from the lowest chord tone on",
          "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    int up = 0;
    int down = 0;
    int upDown = 0;
    int random = 0;
    int twoOctaves = 0;
    int oneOctave = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(1);
        const auto notes = melodyOf(p, "arp", style, seed, 100, 0, false);
        REQUIRE(notes.size() == 16);
        std::set<int> tones;
        for (const Note& n : notes) {
            tones.insert(n.pitch);
            CHECK(n.pitch >= 60); // the arp starts five semitones above the bottom of the range (55)
        }
        const std::vector<int> sorted(tones.begin(), tones.end());
        const size_t m = sorted.size();
        const auto rank = [&](int pitch) {
            return static_cast<size_t>(std::find(sorted.begin(), sorted.end(), pitch) - sorted.begin());
        };
        bool isUp = m > 2; // random tones may skip some chord tones, a pattern covers them all
        bool isDown = m > 2;
        bool isUpDown = m > 2;
        for (size_t i = 0; i < notes.size(); ++i) {
            const size_t r = rank(notes[i].pitch);
            isUp = isUp && r == i % m;
            isDown = isDown && r == m - 1 - i % m;
            const size_t j = i % (2 * m - 2);
            isUpDown = isUpDown && r == (j < m ? j : 2 * m - 2 - j);
        }
        up += isUp ? 1 : 0;
        down += isDown ? 1 : 0;
        upDown += isUpDown ? 1 : 0;
        random += !isUp && !isDown && !isUpDown ? 1 : 0;
        (sorted.back() - sorted.front() >= 12 ? twoOctaves : oneOctave) += 1;
    }
    CHECK(up > 20);
    CHECK(down > 20);
    CHECK(upDown > 20);
    CHECK(random > 20);
    CHECK(twoOctaves > 40);
    CHECK(oneOctave > 40);
}

TEST_CASE("arp plays triads, chord_arp plays sevenths; chord_arp mixes 8th and 16th grids", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    int sevenths = 0;
    int eighthGrid = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern a = makePattern(1);
        const auto triads = melodyOf(a, "arp", style, seed, 100, 0, false);
        std::set<int> pcs;
        for (const Note& n : triads) {
            pcs.insert(pitchClassOf(n.pitch));
        }
        CHECK(pcs.size() <= 3);
        Pattern b = makePattern(1);
        const auto chords = melodyOf(b, "chord_arp", style, seed, 100, 0, false);
        std::set<int> sevenPcs;
        bool onlyEighths = true;
        for (const Note& n : chords) {
            sevenPcs.insert(pitchClassOf(n.pitch));
            onlyEighths = onlyEighths && n.startTick % 480 == 0;
        }
        sevenths += sevenPcs.size() == 4 ? 1 : 0;
        eighthGrid += onlyEighths ? 1 : 0;
    }
    CHECK(sevenths > 60);
    CHECK(eighthGrid > 70); // half of the phrases play on an 8th grid
    CHECK(eighthGrid < 130);
}

TEST_CASE("hypnotic_motif: two-bar motifs in half of the cases at energy 0, span within seven semitones",
          "[archetype-melody]") {
    const StyleProfile style = loadShipped("peak_time");
    int twoBar = 0;
    int atLeastThree = 0;
    int oneBars = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(4);
        const auto notes = melodyOf(p, "hypnotic_motif", style, seed, 0, 0, false);
        CHECK(span(std::vector<Note>(notes.begin(), notes.begin() + 3)) <= 7); // a motif of a single chord
        twoBar += signature(notes, 0, 1) != signature(notes, 1, 2) ? 1 : 0;
        if (signature(notes, 0, 1) == signature(notes, 1, 2) && signature(notes, 1, 2) == signature(notes, 2, 3)) {
            atLeastThree += signature(notes, 0, 1).size() >= 3 ? 1 : 0;
            ++oneBars;
        }
    }
    CHECK(atLeastThree == oneBars);
    CHECK(oneBars > 70);
    CHECK(twoBar > 70);
    CHECK(twoBar < 130);
}

TEST_CASE("lead_phrase: the climax is in the middle half and the phrase never leaves an octave", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 400; ++seed) {
        Pattern p = makePattern(4);
        p.context.scaleId = std::string(style.scales.front().id);
        const auto notes = melodyOf(p, "lead_phrase", style, seed, 50, 40, false);
        int top = 0;
        for (const Note& n : notes) {
            top = std::max<int>(top, n.pitch);
        }
        const auto firstTop = std::find_if(notes.begin(), notes.end(), [&](const Note& n) { return n.pitch == top; });
        INFO("seed " << seed);
        CHECK(firstTop->startTick >= kTicksPerBar);
        CHECK(firstTop->startTick < 3 * kTicksPerBar);
        const Note& last = *std::max_element(notes.begin(), notes.end(),
                                             [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        CHECK(isChordTone(p.context.root, p.context.progression.front().chord, last.pitch));
        CHECK(&last != &*firstTop);
    }
}

TEST_CASE("call_response answers differ in kind: transposed, inverted or with a new ending", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    int transposed = 0;
    int inverted = 0;
    int newEnding = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(2);
        const auto notes = melodyOf(p, "call_response", style, seed, 50, 0, false);
        const auto call = startsAndPitches(notes, 0, 1);
        const auto answer = startsAndPitches(notes, 1, 2);
        if (call.size() != answer.size() || call.size() < 3) {
            continue;
        }
        bool sameRhythm = true;
        bool sameBody = true; // all notes but the last keep their pitch
        bool mirrored = true; // every interval turns around
        for (size_t i = 0; i < call.size(); ++i) {
            sameRhythm = sameRhythm && call[i].first == answer[i].first;
            if (i + 1 < call.size()) {
                sameBody = sameBody && call[i].second == answer[i].second;
            }
            if (i > 0) {
                const int a = call[i].second - call[i - 1].second;
                const int b = answer[i].second - answer[i - 1].second;
                mirrored = mirrored && ((a > 0 && b < 0) || (a < 0 && b > 0) || (a == 0 && b == 0));
            }
        }
        if (!sameRhythm) {
            continue;
        }
        if (sameBody && call.back().second != answer.back().second) {
            ++newEnding;
        } else if (mirrored) {
            ++inverted;
        } else {
            ++transposed;
        }
    }
    CHECK(transposed > 10);
    CHECK(inverted > 10);
    CHECK(newEnding > 10);
}

TEST_CASE("a note that gives way to the bass moves to the nearest allowed pitch", "[archetype-melody]") {
    StyleProfile style = loadShipped("peak_time");
    style.melody.range = {36, 84}; // reaches into the register of the bass, so that notes have to give way
    int moves = 0;
    int up = 0;
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        Pattern withBass = makePattern(2);
        withBass.context.scaleId = "natural_minor";
        Pcg32 bassRng = Pcg32::fromSeed(seed);
        REQUIRE(generateVoice(withBass, 0, "rolling16", style, ArchetypeSettings{70, 40}, bassRng));
        Pattern alone = withBass;
        alone.voices[0].notes.clear();
        for (Pattern* p : {&withBass, &alone}) {
            Pcg32 rng = Pcg32::fromSeed(seed + 1000);
            REQUIRE(generateVoice(*p, 1, "arp", style, ArchetypeSettings{70, 40}, rng));
        }
        const auto& free = alone.voices[1].notes;
        const auto& yielded = withBass.voices[1].notes;
        if (free.size() != yielded.size()) {
            continue; // a note without any place went, the pairs no longer line up
        }
        const auto range = effectiveRange(style.registerProfile(), VoiceRole::Melody, "arp", withBass.voicing, 0);
        REQUIRE(range.has_value());
        const bool tension = style.chromaticDefaultPercent >= 30;
        const Scale* scale = findScale(withBass.context.scaleId);
        REQUIRE(scale != nullptr);
        for (size_t i = 0; i < free.size(); ++i) {
            REQUIRE(free[i].startTick == yielded[i].startTick);
            if (free[i].pitch == yielded[i].pitch) {
                continue;
            }
            ++moves;
            const int from = free[i].pitch;
            const int to = yielded[i].pitch;
            const int distance = std::abs(to - from);
            up += to > from ? 1 : 0;
            const auto chord = std::optional<Chord>(Chord{0, ChordQuality::Minor});
            const auto allowed = [&](int candidate) {
                return range->contains(candidate) && isAllowed(*scale, withBass.context.root, chord, candidate) &&
                       !violatesBassRules(withBass.voices[0], free[i].startTick, free[i].lengthTicks, candidate,
                                          tension);
            };
            INFO("seed " << seed << " note " << i << " from " << from << " to " << to);
            for (int d = 1; d < distance; ++d) {
                CHECK_FALSE(allowed(from - d));
                CHECK_FALSE(allowed(from + d));
            }
            CHECK(allowed(to));
            if (to > from) {
                CHECK_FALSE(allowed(from - distance)); // at an equal distance the way down wins
            }
        }
    }
    CHECK(moves > 20);
    CHECK(up > 0);
}

TEST_CASE("pluck_seq repeats its pitches in a cycle of three to six positions", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    std::map<size_t, int> found;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        Pattern p = makePattern(1);
        const auto notes = melodyOf(p, "pluck_seq", style, seed, 100, 0, false);
        const bool sixteenth =
            std::any_of(notes.begin(), notes.end(), [](const Note& n) { return n.startTick % 480 != 0; });
        if (!sixteenth) {
            continue; // the position of a note on the 8th grid cannot be told from a 16th position
        }
        size_t period = 0;
        for (size_t c = 3; c <= 6 && period == 0; ++c) {
            std::map<size_t, int> pitchAt;
            bool consistent = true;
            for (const Note& n : notes) {
                const auto [it, inserted] = pitchAt.emplace((n.startTick / 240) % c, n.pitch);
                consistent = consistent && (inserted || it->second == n.pitch);
            }
            period = consistent ? c : 0;
        }
        INFO("seed " << seed);
        REQUIRE(period != 0);
        ++found[period];
    }
    CHECK(found[6] > 3);
    CHECK(found[3] > 3);
}

TEST_CASE("lead_phrase ends on a chord tone of the chord that sounds there", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(4);
        p.context.scaleId = std::string(style.scales.front().id);
        variedProgression(p, seed);
        const auto notes = melodyOf(p, "lead_phrase", style, seed, 50, 40, false);
        const Note& last = *std::max_element(notes.begin(), notes.end(),
                                             [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        Chord chord{};
        for (const ChordEvent& event : p.context.progression) {
            if (last.startTick / (kTicksPerBar / 2) >= event.startHalfBar &&
                last.startTick / (kTicksPerBar / 2) < event.startHalfBar + event.lengthHalfBars) {
                chord = event.chord;
            }
        }
        INFO("seed " << seed);
        CHECK(isChordTone(p.context.root, chord, last.pitch));
    }
}

TEST_CASE("lead_phrase of two bars repeats a one-bar motif", "[archetype-melody]") {
    const StyleProfile style = loadShipped("melodic_techno");
    int repeated = 0;
    int maxSpan = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(2);
        const auto notes = melodyOf(p, "lead_phrase", style, seed, 50, 40, false);
        repeated += stepSet(notes, 0) == stepSet(notes, 1) ? 1 : 0;
        maxSpan = std::max(maxSpan, span(notes));
    }
    CHECK(repeated > 150); // the rhythm of the motif returns in the second bar
    CHECK(maxSpan <= 12 + 4);
}

// -- polyphonic melody: chord colours per style (STYLES.md 1.4, 1.10)
// ---------------------------------------------------

TEST_CASE("the shipped profiles carry the chord colours of STYLES.md", "[archetype-melody][polyphony]") {
    const std::map<std::string, std::map<std::string, int>> expected{
        {"peak_time", {{"min", 40}, {"fifth", 30}, {"min7", 20}, {"sus", 10}}},
        {"melodic_techno", {{"min7", 25}, {"min9", 25}, {"min", 30}, {"sus", 20}}},
        {"hard_industrial", {{"cluster", 40}, {"fifth", 40}, {"min", 20}}}};
    for (const auto& [name, colors] : expected) {
        const StyleProfile style = loadShipped(name);
        std::map<std::string, int> actual;
        for (const WeightedId& entry : style.melody.chordColors) {
            actual[entry.id] = entry.weight;
        }
        INFO(name);
        CHECK(actual == colors);
        // [v1.1] default of the field (on for peak time and hard, off for melodic), not used in v1.0
        CHECK(style.melody.chordMemory == (name != "melodic_techno"));
        StyleProfile flipped = style;
        flipped.melody.chordMemory = !style.melody.chordMemory;
        Pattern a = makePattern(2);
        Pattern b = makePattern(2);
        CHECK(melodyOf(a, "stabs", style, 3, 50, 40) ==
              melodyOf(b, "stabs", flipped, 3, 50, 40)); // voice leading everywhere
    }
}

TEST_CASE("stab chords are polyphonic hits that survive the whole chain", "[archetype-melody][polyphony]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const char* id : {"stabs", "aggro_stabs"}) {
            for (uint64_t seed = 1; seed <= 25; ++seed) {
                Pattern p = makePattern(4);
                p.context.scaleId = std::string(style.scales.front().id);
                variedProgression(p, seed);
                const auto notes = melodyOf(p, id, style, seed, 60, 50);
                INFO(styleName << " " << id << " seed " << seed);
                REQUIRE_FALSE(notes.empty());
                std::map<uint32_t, std::set<int>> hits;
                for (const Note& n : notes) {
                    CHECK(n.startTick % 240 == 0);
                    CHECK(n.pitch >= p.voicing.lowNote);
                    CHECK(n.pitch <= p.voicing.highNote);
                    CHECK_FALSE(n.slide);
                    CHECK(hits[n.startTick].insert(n.pitch).second); // no pitch twice in a chord
                }
                size_t polyphonic = 0;
                for (const auto& [tick, pitches] : hits) {
                    CHECK(pitches.size() >= 2);
                    CHECK(pitches.size() <= 5);
                    polyphonic += pitches.size() >= 3 ? 1 : 0;
                }
                CHECK(polyphonic > 0);
                // the bass stays monophonic next to it
                const auto& bass = p.voices[0].notes;
                for (size_t i = 0; i + 1 < bass.size(); ++i) {
                    CHECK(bass[i].startTick + bass[i].lengthTicks <= bass[i + 1].startTick);
                }
            }
        }
    }
}

TEST_CASE("slides on chord notes are removed by the constraint layer, single notes keep them",
          "[archetype-melody][polyphony]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makePattern(2);
    const auto notes = melodyOf(p, "stabs", style, 7, 60, 0);
    REQUIRE_FALSE(notes.empty());
    for (Note& n : p.voices[1].notes) {
        n.slide = true;
    }
    const ConstraintReport report = applyConstraints(p, constraintSettingsFor(p, style));
    CHECK(report.total() > 0);
    for (const Note& n : p.voices[1].notes) {
        CHECK_FALSE(n.slide);
    }
}

TEST_CASE("the voices of the stab chords move little in every style", "[archetype-melody][polyphony]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        int worst = 0;
        for (uint64_t seed = 1; seed <= 100; ++seed) {
            Pattern p = makePattern(4);
            p.context.scaleId = std::string(style.scales.front().id);
            setProgression(p, {{0, ChordQuality::Minor, 2},
                               {10, ChordQuality::Major, 2},
                               {8, ChordQuality::Major, 2},
                               {7, ChordQuality::Minor, 2}});
            const auto notes = melodyOf(p, "stabs", style, seed, 50, 0, false);
            std::vector<std::vector<int>> voicings;
            for (uint32_t bar = 0; bar < 4; ++bar) {
                std::vector<int> pitches;
                for (const Note& n : notes) {
                    if (n.startTick / kTicksPerBar == bar) {
                        pitches.clear();
                        const uint32_t first = n.startTick;
                        for (const Note& m : notes) {
                            if (m.startTick == first) {
                                pitches.push_back(m.pitch);
                            }
                        }
                        break;
                    }
                }
                REQUIRE_FALSE(pitches.empty());
                voicings.push_back(pitches);
            }
            for (size_t i = 1; i < voicings.size(); ++i) {
                worst = std::max(worst, maxVoiceMovement(voicings[i - 1], voicings[i]));
            }
        }
        INFO(styleName);
        CHECK(worst <= 5); // a close voicing leads every voice by a few semitones
    }
}
