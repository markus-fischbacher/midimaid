#include "PatternFixtures.h"
#include "SeriesCheck.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/MidiFile.h"
#include "core/OutputStage.h"
#include "core/PatternGenerator.h"
#include "core/PatternJson.h"
#include "core/PatternValidation.h"
#include "core/Quality.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;
using namespace mm::fixtures;
using namespace mm::series;

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
const std::vector<uint32_t> kLengths{1, 2, 4, 8, 16};

/// Seeds per style and archetype (SPEC 12).
constexpr int kSeedsPerCase = 1000;

struct SeriesCase {
    std::string style;
    VoiceRole role;
    std::string archetype;
};

std::vector<SeriesCase> casesOf(const std::vector<std::string>& styles) {
    std::vector<SeriesCase> cases;
    for (const std::string& name : styles) {
        const StyleProfile style = loadShipped(name);
        for (const WeightedId& id : style.bass.archetypes) {
            if (const Archetype* a = findArchetype(id.id); a != nullptr && a->role == VoiceRole::Bass) {
                cases.push_back({name, VoiceRole::Bass, id.id});
            }
        }
        for (const WeightedId& id : style.melody.archetypes) {
            if (const Archetype* a = findArchetype(id.id); a != nullptr && a->role == VoiceRole::Melody) {
                cases.push_back({name, VoiceRole::Melody, id.id});
            }
        }
    }
    return cases;
}

GenerationRequest requestOf(const SeriesCase& c, uint64_t seed, int index) {
    GenerationRequest request;
    request.lengthBars = kLengths[static_cast<size_t>(index) % kLengths.size()];
    request.seed = seed;
    const int energies[] = {30, 60, 90};
    const int creativities[] = {40, 0, 80, 100};
    request.settings.energyPct = energies[index % 3];
    request.settings.creativityPct = creativities[index % 4];
    (c.role == VoiceRole::Bass ? request.bassArchetype : request.melodyArchetype) = c.archetype;
    return request;
}

struct Stats {
    double onsetsPerBar = 0;
    double bars = 0;
    double repeatPairs = 0;
    double equalPairs = 0;
    double weakNotes = 0;
    double weakNonChord = 0;
    int energy = 0;
};

void collect(Stats& stats, const Pattern& p, const Track& track) {
    std::set<uint32_t> onsets;
    for (const Note& n : track.notes) {
        onsets.insert(n.startTick);
        if (n.startTick % 960 != 0) {
            stats.weakNotes += 1;
            stats.weakNonChord += isChordTone(p, n.startTick, n.pitch) ? 0 : 1;
        }
    }
    stats.onsetsPerBar += static_cast<double>(onsets.size());
    stats.bars += p.lengthBars;
    for (uint32_t bar = 1; bar < p.lengthBars; ++bar) {
        std::set<uint32_t> before;
        std::set<uint32_t> now;
        for (const Note& n : track.notes) {
            if (n.startTick / kTicksPerBar == bar - 1) {
                before.insert(n.startTick % kTicksPerBar);
            } else if (n.startTick / kTicksPerBar == bar) {
                now.insert(n.startTick % kTicksPerBar);
            }
        }
        stats.repeatPairs += 1;
        stats.equalPairs += before == now ? 1 : 0;
    }
}

} // namespace

const std::set<std::string> kLineArchetypes{"hypnotic_motif", "lead_phrase", "call_response", "pluck_seq"};
// phrase-shaped lines have no bar-for-bar rhythm, all others repeat their cell
const std::set<std::string> kNoCellArchetypes{"lead_phrase", "call_response", "sparse_hits"};

TEST_CASE("series: every archetype of every style keeps the rules on all pattern lengths", "[series]") {
    for (const SeriesCase& c : casesOf(kStyles)) {
        const StyleProfile style = loadShipped(c.style);
        Stats stats;
        int failures = 0;
        for (int i = 0; i < kSeedsPerCase; ++i) {
            const uint64_t seed = 1 + static_cast<uint64_t>(i);
            const GenerationRequest request = requestOf(c, seed, i);
            const Pattern p = generateCandidate(style, request, seed * 7919);
            const auto issues = violations(p, style);
            const auto invalid = validatePattern(p);
            const Track& track = p.voices[c.role == VoiceRole::Bass ? 0 : 1];
            if ((!issues.empty() || !invalid.empty() || track.archetypeId != c.archetype) && ++failures <= 3) {
                INFO(c.style << " " << c.archetype << " seed " << seed << " bars " << request.lengthBars << ": "
                             << (issues.empty() ? (invalid.empty() ? track.archetypeId : invalid.front())
                                                : issues.front()));
                CHECK(issues.empty());
                CHECK(invalid.empty());
                CHECK(track.archetypeId == c.archetype);
            }
            collect(stats, p, track);
        }
        INFO(c.style << " " << c.archetype);
        CHECK(failures == 0);
        REQUIRE(stats.bars > 0);
        CHECK(stats.onsetsPerBar / stats.bars >= 0.5);
        if (kLineArchetypes.count(c.archetype) != 0) {
            // STYLES.md 1.11: at least 20 % non-chord tones on the weak steps
            REQUIRE(stats.weakNotes > 0);
            CHECK(stats.weakNonChord / stats.weakNotes >= 0.2);
        }
        if (kNoCellArchetypes.count(c.archetype) == 0) {
            REQUIRE(stats.repeatPairs > 0);
            CHECK(stats.equalPairs / stats.repeatPairs >= 0.6);
        }
        const Archetype* archetype = findArchetype(c.archetype);
        if (c.role == VoiceRole::Bass && archetype->density == ArchetypeDensity::Dense) {
            // the dense bass lines sit in the target band of the mean energy (60 %), with a margin of 20 %
            const DensityBand band = densityBand(VoiceRole::Bass, 60);
            const double perBar = stats.onsetsPerBar / stats.bars;
            CHECK(perBar >= band.low * 0.8 / 100.0);
            CHECK(perBar <= band.high * 1.2 / 100.0);
        }
    }
}

TEST_CASE("series: the checker finds every kind of violation it is written for", "[series]") {
    const StyleProfile peak = loadShipped("peak_time"); // chromatic 0, kick clearance 120
    auto base = []() {
        Pattern p = makeEmptyPattern(2, "peak_time"); // A natural minor, chord i = Am
        p.voices[0].archetypeId = "offbeat";
        p.voices[1].archetypeId = "hypnotic_motif";
        return p;
    };
    auto note = [](uint32_t start, uint32_t length, uint8_t pitch) {
        Note n;
        n.startTick = start;
        n.lengthTicks = length;
        n.pitch = pitch;
        n.velocity = 100;
        return n;
    };
    auto has = [&](const Pattern& p, const std::string& part) {
        for (const std::string& issue : violations(p, peak)) {
            if (issue.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    Pattern clean = base();
    clean.voices[0].notes = {note(480, 240, 33)};
    clean.voices[1].notes = {note(960, 240, 69)};
    CHECK(violations(clean, peak).empty());

    Pattern p = clean;
    p.voices[0].notes = {note(481, 240, 33)};
    CHECK(has(p, "16th grid"));
    p.voices[0].notes = {note(480, 240000, 33)};
    CHECK(has(p, "outside the pattern"));
    p.voices[0].notes = {note(480, 240, 60)};
    CHECK(has(p, "outside the range"));
    p.voices[0].notes = {note(0, 240, 33)};
    CHECK(has(p, "bass on a kick"));
    p.voices[0].notes = {note(480, 480, 33)}; // ends at the kick of step 4 without clearance
    CHECK(has(p, "kick clearance"));
    p.voices[0].notes = {note(480, 240, 34)}; // A#: outside the scale, budget 0
    CHECK(has(p, "chromatic share"));
    p = clean;
    p.voices[1].notes = {note(960, 480, 69), note(1200, 240, 69)};
    CHECK(has(p, "equal pitch"));
    p = clean;
    p.voices[0].notes = {note(480, 480, 33), note(720, 240, 36)};
    CHECK(has(p, "not monophonic"));
    p = clean;
    p.voices[0].notes = {note(960, 240, 33)};
    p.voices[1].notes = {note(960, 240, 40)};
    CHECK(has(p, "octave above"));
    p.voices[0].notes = {note(1920, 240, 34)};
    p.voices[1].notes = {note(1920, 240, 47)}; // B over A#: minor second class, on a strong step
    CHECK(has(p, "tense interval"));
    // tense intervals are allowed in Hard/Industrial
    const StyleProfile hard = loadShipped("hard_industrial");
    CHECK_FALSE([&] {
        for (const std::string& issue : violations(p, hard)) {
            if (issue.find("tense interval") != std::string::npos) {
                return true;
            }
        }
        return false;
    }());
    // a note that holds into a strong step and meets a new bass attack counts as well
    p = clean;
    p.voices[1].notes = {note(480, 1440, 57)}; // sounds at tick 960 (step 4)
    p.voices[0].notes = {note(960, 240, 34)}; // A# attacks while the A sounds: major seventh class
    CHECK(has(p, "tense interval"));
    p.voices[0].notes = {note(960, 240, 36)}; // C against A: a minor third, clean
    CHECK_FALSE(has(p, "tense interval"));
}

TEST_CASE("series: automatic candidates survive the whole chain to the MIDI file", "[series]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (int i = 0; i < 150; ++i) {
            GenerationRequest request;
            request.lengthBars = kLengths[static_cast<size_t>(i) % kLengths.size()];
            request.seed = 1000 + static_cast<uint64_t>(i);
            request.settings.energyPct = 10 * (i % 11);
            request.settings.creativityPct = 20 * (i % 6);
            const Pattern p = generateCandidate(style, request, request.seed);
            INFO(name << " seed " << request.seed << " bars " << request.lengthBars);
            CHECK(validatePattern(p).empty());
            CHECK(violations(p, style).empty());
            CHECK(generateCandidate(style, request, request.seed) == p);

            const std::string text = patternToString(p);
            const auto loaded = loadPattern(text);
            REQUIRE(loaded.ok());
            CHECK(*loaded.pattern == p);

            OutputSettings settings;
            settings.kickClearanceTicks = style.bass.kickClearanceTicks;
            for (const Track& track : p.voices) {
                settings.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
            }
            const OutputPattern out = renderOutput(p, settings);
            CHECK(out.lengthTicks == p.lengthBars * kTicksPerBar);
            REQUIRE(out.voices.size() == p.voices.size());
            std::vector<PatternNote> notes;
            for (const OutputVoice& voice : out.voices) {
                int32_t lastStart = -1;
                for (const OutputNote& note : voice.notes) {
                    CHECK(note.startTick >= 0);
                    CHECK(note.endTick > note.startTick);
                    CHECK(note.startTick >= lastStart);
                    lastStart = note.startTick;
                    notes.push_back({static_cast<uint32_t>(note.startTick),
                                     static_cast<uint32_t>(note.endTick - note.startTick), note.channel, note.pitch,
                                     note.velocity});
                }
            }
            std::sort(notes.begin(), notes.end(), [](const PatternNote& a, const PatternNote& b) {
                return a.startTick < b.startTick;
            });
            const PatternView view{notes.data(), notes.size(), out.lengthTicks};
            const std::vector<uint8_t> midi = writeMidiFile(view, MidiFileOptions{});
            REQUIRE(midi.size() > 22);
            CHECK(std::string(midi.begin(), midi.begin() + 4) == "MThd");
        }
    }
}

TEST_CASE("series: the generator handles any number of voices", "[series][voices]") {
    const std::vector<std::vector<VoiceRole>> layouts{
        {VoiceRole::Bass},
        {VoiceRole::Melody},
        {VoiceRole::Bass, VoiceRole::Melody, VoiceRole::Melody},
        {VoiceRole::Melody, VoiceRole::Bass, VoiceRole::Melody},
        {VoiceRole::Bass, VoiceRole::Bass, VoiceRole::Melody},
        {VoiceRole::Bass, VoiceRole::Melody, VoiceRole::Melody, VoiceRole::Melody, VoiceRole::Melody,
         VoiceRole::Melody, VoiceRole::Melody, VoiceRole::Melody}};
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const auto& roles : layouts) {
            for (uint64_t seed = 1; seed <= 25; ++seed) {
                GenerationRequest request;
                request.lengthBars = kLengths[seed % kLengths.size()];
                request.seed = seed;
                request.voices = roles;
                const Pattern p = generateCandidate(style, request, seed);
                INFO(name << " voices " << roles.size() << " seed " << seed << " bars " << request.lengthBars);
                REQUIRE(p.voices.size() == roles.size());
                std::set<int> channels;
                for (size_t i = 0; i < roles.size(); ++i) {
                    CHECK(p.voices[i].role == roles[i]);
                    CHECK(channels.insert(p.voices[i].midiChannel).second);
                    CHECK_FALSE(p.voices[i].archetypeId.empty());
                    CHECK_FALSE(p.voices[i].notes.empty());
                }
                CHECK(validatePattern(p).empty());
                const auto issues = violations(p, style);
                INFO((issues.empty() ? std::string("clean") : issues.front()));
                CHECK(issues.empty());
                CHECK(generateCandidate(style, request, seed) == p);
                Pattern copy = p;
                CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
            }
        }
    }
}

TEST_CASE("series: more voices than the architecture allows are cut to kMaxVoices", "[series][voices]") {
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request;
    request.voices.assign(static_cast<size_t>(kMaxVoices) + 3, VoiceRole::Melody);
    const Pattern p = generateCandidate(style, request, 5);
    CHECK(p.voices.size() == static_cast<size_t>(kMaxVoices));
}

TEST_CASE("series: selection, regeneration and the quality rating work with three voices", "[series][voices]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        GenerationRequest request;
        request.lengthBars = 8;
        request.seed = 11;
        request.voices = {VoiceRole::Bass, VoiceRole::Melody, VoiceRole::Melody};
        const SelectionResult result = generatePattern(style, request);
        INFO(name);
        REQUIRE(result.success);
        CHECK(result.pattern.voices.size() == 3);
        CHECK(result.pattern.qualityScore == result.score);
        CHECK(replayWinner(style, request, result.seed) == result.pattern);
        for (size_t voice = 0; voice < 3; ++voice) {
            Pattern p = result.pattern;
            REQUIRE(regenerateVoice(p, voice, style, request, 70 + voice));
            CHECK(validatePattern(p).empty());
            CHECK(violations(p, style).empty());
        }
        Pattern p = result.pattern;
        REQUIRE(regeneratePhrase(p, 0, style, request, 5));
        CHECK(validatePattern(p).empty());
        CHECK(violations(p, style).empty());
        // the other voices keep their notes when one voice is locked
        p = result.pattern;
        p.voices[2].lock = LockFlags{true, true, true};
        const std::vector<Note> locked = p.voices[2].notes;
        REQUIRE(regeneratePhrase(p, 0, style, request, 6));
        CHECK(p.voices[2].notes == locked);
    }
}
