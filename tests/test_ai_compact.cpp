#include "ai/AiSchema.h"
#include "ai/PatternCompact.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/StyleProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

using namespace mm::ai;
using namespace mm::core;
using nlohmann::json;

namespace {

StyleProfile shipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

/// Writes the pattern in the schema, reads the answer and builds the pattern again (with the ids known).
BuildResult roundTrip(const Pattern& pattern, const StyleProfile& style, CompactPattern* compactOut = nullptr) {
    const auto compact = patternToSchemaJson(pattern, &style, true);
    REQUIRE(compact.has_value());
    if (compactOut != nullptr) {
        *compactOut = *compact;
    }
    const auto parsed = parseAiResponse(compact->json);
    REQUIRE(parsed.ok);
    std::set<uint32_t> ids;
    for (const auto& track : pattern.voices) {
        for (const auto& note : track.notes) {
            ids.insert(note.id);
        }
    }
    BuildContext context;
    context.lengthBars = pattern.lengthBars;
    context.styleId = pattern.styleId;
    context.style = &style;
    context.knownIds = &ids;
    context.firstNewId = pattern.nextNoteId;
    return buildPattern(parsed.draft, context);
}

Chord chordAtHalfBar(const Pattern& pattern, uint32_t half) {
    for (const auto& event : pattern.context.progression) {
        if (half >= event.startHalfBar && half < event.startHalfBar + event.lengthHalfBars) {
            return event.chord;
        }
    }
    return Chord{99, ChordQuality::Minor};
}

uint32_t roundedSteps(uint32_t ticks) {
    return std::max<uint32_t>(1, (ticks + 120) / 240);
}

} // namespace

TEST_CASE("the key is written with sharps", "[ai][compact]") {
    CHECK(rootName(0) == "C");
    CHECK(rootName(1) == "C#");
    CHECK(rootName(9) == "A");
    CHECK(rootName(11) == "B");
    CHECK(rootName(21) == "A"); // modulo 12
}

TEST_CASE("generated patterns survive the schema: same notes, ids, flags, harmony and phrases",
          "[ai][compact][series]") {
    size_t patterns = 0;
    size_t rounded = 0;
    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const auto style = shipped(name);
        for (const uint32_t bars : {1u, 2u, 4u, 8u}) {
            for (uint64_t seed = 1; seed <= 12; ++seed) {
                GenerationRequest request;
                request.lengthBars = bars;
                request.seed = seed;
                const auto generated = generatePattern(style, request);
                REQUIRE(generated.success);
                const Pattern& original = generated.pattern;
                CompactPattern compact;
                const auto rebuilt = roundTrip(original, style, &compact);
                INFO(name << " bars " << bars << " seed " << seed);
                REQUIRE(rebuilt.ok);
                const Pattern& back = rebuilt.pattern;
                CHECK(back.context.root == original.context.root);
                CHECK(back.context.scaleId == original.context.scaleId);
                for (uint32_t half = 0; half < bars * 2; ++half) { // the events come back split per bar
                    CHECK(chordAtHalfBar(back, half) == chordAtHalfBar(original, half));
                }
                CHECK(back.phrases == original.phrases);
                REQUIRE(back.voices.size() == original.voices.size());
                size_t offGrid = 0;
                for (size_t v = 0; v < original.voices.size(); ++v) {
                    const auto& a = original.voices[v].notes;
                    const auto& b = back.voices[v].notes;
                    REQUIRE(a.size() == b.size());
                    for (size_t i = 0; i < a.size(); ++i) {
                        CHECK(b[i].id == a[i].id);
                        CHECK(b[i].pitch == a[i].pitch);
                        CHECK(b[i].velocity == a[i].velocity);
                        CHECK(b[i].accent == a[i].accent);
                        CHECK(b[i].slide == a[i].slide);
                        CHECK(b[i].startTick == (a[i].startTick + 120) / 240 * 240);
                        CHECK(b[i].lengthTicks == roundedSteps(a[i].lengthTicks) * 240);
                        if (a[i].startTick % 240 != 0 || a[i].lengthTicks % 240 != 0) {
                            ++offGrid;
                        }
                    }
                }
                CHECK(compact.roundedNotes == offGrid);
                CHECK(back.nextNoteId >= original.nextNoteId);
                rounded += offGrid;
                ++patterns;
            }
        }
    }
    CHECK(patterns == 144);
    CHECK(rounded > 0); // the short notes of the rolling bass are not whole 16ths: the counter is exercised
}

TEST_CASE("every pitch of a voice is spelled so that the schema gives it back, in every scale", "[ai][compact]") {
    const auto style = shipped("peak_time");
    for (const auto& scale : allScales()) {
        for (const PitchClass root : {0, 4, 9, 11}) {
            Pattern pattern = makeEmptyPattern(2, "peak_time");
            pattern.context.root = root;
            pattern.context.scaleId = std::string(scale.id);
            uint32_t step = 0;
            for (int pitch = 28; pitch <= 52; ++pitch) {
                Note note;
                note.id = allocateNoteId(pattern);
                note.pitch = static_cast<uint8_t>(pitch);
                note.startTick = (step++ % 32) * 240;
                note.lengthTicks = 240;
                pattern.voices[0].notes.push_back(note);
            }
            std::stable_sort(pattern.voices[0].notes.begin(), pattern.voices[0].notes.end(),
                             [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
            Note melody;
            melody.id = allocateNoteId(pattern);
            melody.pitch = 70;
            melody.lengthTicks = 240;
            pattern.voices[1].notes.push_back(melody);
            INFO(scale.id << " root " << int(root));
            const auto rebuilt = roundTrip(pattern, style);
            REQUIRE(rebuilt.ok);
            const auto& a = pattern.voices[0].notes;
            const auto& b = rebuilt.pattern.voices[0].notes;
            REQUIRE(a.size() == b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                CHECK(b[i].pitch == a[i].pitch);
            }
        }
    }
}

TEST_CASE("a bar with two chords is written as x|y and one chord over many bars repeats per bar", "[ai][compact]") {
    const auto style = shipped("peak_time");
    Pattern pattern = makeEmptyPattern(4, "peak_time");
    pattern.context.progression.clear();
    const auto add = [&](uint32_t start, uint32_t length, uint8_t offset, ChordQuality quality) {
        ChordEvent event;
        event.chord = Chord{offset, quality};
        event.startHalfBar = start;
        event.lengthHalfBars = length;
        pattern.context.progression.push_back(event);
    };
    add(0, 3, 0, ChordQuality::Minor);  // i for a bar and a half
    add(3, 1, 10, ChordQuality::Major); // bVII for the second half of bar 2
    add(4, 4, 8, ChordQuality::Major);  // bVI for bars 3 and 4
    Note note;
    note.id = allocateNoteId(pattern);
    note.pitch = 33;
    note.lengthTicks = 240;
    pattern.voices[0].notes.push_back(note);
    REQUIRE(validatePattern(pattern).empty());
    const auto compact = patternToSchemaJson(pattern, &style);
    REQUIRE(compact.has_value());
    const auto parsed = json::parse(compact->json);
    CHECK(parsed["context"]["progression"] == json::array({"i", "i|bVII", "bVI", "bVI"}));
    const auto rebuilt = roundTrip(pattern, style);
    REQUIRE(rebuilt.ok);
    // the events come back split per bar, but the chord at every half bar is the same
    for (uint32_t half = 0; half < 8; ++half) {
        CHECK(chordAtHalfBar(rebuilt.pattern, half) == chordAtHalfBar(pattern, half));
    }
}

TEST_CASE("the note ids are left out on request and unknown scales or ranges give nothing", "[ai][compact]") {
    const auto style = shipped("peak_time");
    Pattern pattern = makeEmptyPattern(1, "peak_time");
    Note note;
    note.id = allocateNoteId(pattern);
    note.pitch = 40;
    pattern.voices[0].notes.push_back(note);
    const auto with = json::parse(patternToSchemaJson(pattern, &style, true)->json);
    const auto without = json::parse(patternToSchemaJson(pattern, &style, false)->json);
    CHECK(with["voices"][0]["notes"][0].contains("id"));
    CHECK_FALSE(without["voices"][0]["notes"][0].contains("id"));
    pattern.context.scaleId = "klingon";
    CHECK_FALSE(patternToSchemaJson(pattern, &style).has_value());
    pattern.context.scaleId = "natural_minor";
    pattern.context.progression.clear(); // no chord at all
    CHECK_FALSE(patternToSchemaJson(pattern, &style).has_value());
}

TEST_CASE("phrases and their turnaround are written", "[ai][compact]") {
    const auto style = shipped("peak_time");
    Pattern pattern = makeEmptyPattern(16, "peak_time");
    pattern.phrases.clear();
    Phrase a;
    a.startBar = 0;
    a.lengthBars = 8;
    a.role = PhraseRole::Main;
    Phrase b;
    b.startBar = 8;
    b.lengthBars = 8;
    b.role = PhraseRole::Breakdown;
    b.turnaround = true;
    pattern.phrases = {a, b};
    pattern.context.progression.clear();
    ChordEvent event;
    event.startHalfBar = 0;
    event.lengthHalfBars = 32;
    pattern.context.progression.push_back(event);
    const auto compact = patternToSchemaJson(pattern, &style);
    REQUIRE(compact.has_value());
    const auto phrases = json::parse(compact->json)["phrases"];
    REQUIRE(phrases.size() == 2);
    CHECK(phrases[0]["role"] == "main");
    CHECK_FALSE(phrases[0].contains("turnaround"));
    CHECK(phrases[1]["role"] == "breakdown");
    CHECK(phrases[1]["start_bar"] == 8);
    CHECK(phrases[1]["turnaround"] == true);
}

TEST_CASE("a note at the very end of the pattern keeps its step inside the pattern", "[ai][compact]") {
    const auto style = shipped("peak_time");
    Pattern pattern = makeEmptyPattern(1, "peak_time");
    Note note;
    note.id = allocateNoteId(pattern);
    note.pitch = 40;
    note.startTick = 3840 - 100; // rounds to step 16, which does not exist
    note.lengthTicks = 100;
    pattern.voices[0].notes.push_back(note);
    const auto compact = patternToSchemaJson(pattern, &style);
    REQUIRE(compact.has_value());
    CHECK(json::parse(compact->json)["voices"][0]["notes"][0]["step"] == 15);
    CHECK(compact->roundedNotes == 1);
}
