#include "ai/AiSchema.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/PatternValidation.h"
#include "core/Random.h"
#include "core/StyleProfile.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using namespace mm::ai;
using namespace mm::core;
using nlohmann::json;

namespace {

StyleProfile shippedStyle() {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/peak_time.json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

/// The example answer of SPEC 7.3 for 4 bars (without the v1.1 fields).
json validAnswer() {
    return json::parse(R"({
      "schema_version": 1,
      "intent": {"energy": 0.8, "density": 0.6, "contour": "rising", "motif_idea": "short call", "groove": "rolling"},
      "context": {"root": "A", "scale": "natural_minor", "progression": ["i", "i", "bVI", "bVII"]},
      "phrases": [{"start_bar": 0, "bars": 4, "role": "main"}],
      "voices": [
        {"role": "bass", "archetype": "rolling16",
         "notes": [{"step": 1, "degree": 1, "alt": 0, "octave": 0, "len": 1, "vel": 100, "accent": true, "slide": false},
                   {"step": 3, "degree": 5, "octave": 0, "len": 2, "vel": 90},
                   {"step": 9, "degree": 3, "octave": 1, "len": 1, "vel": 80}]},
        {"role": "melody", "archetype": "hypnotic_motif",
         "notes": [{"step": 4, "degree": 5, "octave": 1, "len": 1, "vel": 96},
                   {"step": 6, "degree": 2, "alt": -1, "octave": 1, "len": 1, "vel": 90}]}
      ]
    })");
}

BuildContext contextFor(const StyleProfile& style, uint32_t bars = 4) {
    BuildContext context;
    context.lengthBars = bars;
    context.styleId = style.id;
    context.style = &style;
    context.prompt = "dark rolling bass";
    context.providerId = "mock";
    context.modelId = "mock-model";
    return context;
}

BuildResult buildFrom(const json& answer, const StyleProfile& style, uint32_t bars = 4) {
    const auto text = answer.dump();
    const auto parsed = parseAiResponse(text);
    REQUIRE(parsed.ok);
    auto context = contextFor(style, bars);
    context.rawResponse = text;
    return buildPattern(parsed.draft, context);
}

} // namespace

TEST_CASE("the schema text is JSON with the v1.0 fields and none of v1.1", "[ai][schema]") {
    const auto schema = json::parse(schemaV1Json());
    CHECK(schema["required"].size() == 3);
    const auto& note = schema["properties"]["voices"]["items"]["properties"]["notes"]["items"]["properties"];
    for (const char* key : {"id", "step", "degree", "alt", "octave", "len", "vel", "accent", "slide"}) {
        INFO(key);
        CHECK(note.contains(key));
    }
    for (const char* key : {"ratchet", "chance", "cond"}) {
        INFO(key);
        CHECK_FALSE(note.contains(key));
    }
}

TEST_CASE("the example answer parses and builds the pattern it describes", "[ai][schema]") {
    const auto style = shippedStyle();
    const auto result = buildFrom(validAnswer(), style);
    REQUIRE(result.ok);
    const Pattern& p = result.pattern;
    CHECK(p.lengthBars == 4);
    CHECK(p.context.root == 9);
    CHECK(p.context.scaleId == "natural_minor");
    REQUIRE(p.context.progression.size() == 4);
    CHECK(p.context.progression[2].chord == Chord{8, ChordQuality::Major}); // bVI
    CHECK(p.context.progression[3].startHalfBar == 6);
    CHECK(p.voices[0].archetypeId == "rolling16");
    CHECK_FALSE(p.voices[0].archetypeAuto);
    REQUIRE(p.voices[0].notes.size() == 3);
    const Note& first = p.voices[0].notes[0];
    CHECK(first.pitch == 33); // the voice base of the bass: A1 (SPEC 7.3)
    CHECK(first.startTick == 240);
    CHECK(first.lengthTicks == 240);
    CHECK(first.velocity == 100);
    CHECK(first.accent);
    CHECK(p.voices[0].notes[1].pitch == 33 + 7); // degree 5 = E
    CHECK(p.voices[0].notes[1].lengthTicks == 480);
    CHECK(p.voices[0].notes[2].pitch == 33 + 3 + 12); // degree 3 one octave up
    REQUIRE(p.voices[1].notes.size() == 2);
    CHECK(p.voices[1].notes[0].pitch % 12 == (9 + 7) % 12);     // degree 5 of A minor
    CHECK(p.voices[1].notes[1].pitch % 12 == (9 + 2 - 1) % 12); // degree 2 lowered by a semitone
    // the data of the request is stored with the pattern
    CHECK(p.info.source == "ai");
    CHECK(p.info.prompt == "dark rolling bass");
    CHECK(p.info.providerId == "mock");
    CHECK(p.info.modelId == "mock-model");
    CHECK(p.info.rawResponse == validAnswer().dump());
    CHECK(p.info.styleProfileVersion == style.version);
    CHECK(validatePattern(p).empty());
    // ids are new, unique and the counter is ahead of them
    std::set<uint32_t> ids;
    for (const auto& track : p.voices) {
        for (const auto& note : track.notes) {
            CHECK(note.id >= 1);
            CHECK(note.id < p.nextNoteId);
            CHECK(ids.insert(note.id).second);
        }
    }
    // v1.0 notes never carry the v1.1 fields
    CHECK(first.ratchet == 1);
    CHECK(first.chance == 100);
    CHECK(first.condA == 1);
}

TEST_CASE("fields of later versions and unknown fields are ignored", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["schema_version"] = 2;
    answer["future"] = json::object({{"a", json::array({1, 2, 3})}});
    answer["voices"][0]["notes"][0]["ratchet"] = 4;
    answer["voices"][0]["notes"][0]["chance"] = 10;
    answer["voices"][0]["notes"][0]["cond"] = "1:2";
    answer["voices"][0]["unknownField"] = true;
    const auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    const Note& note = result.pattern.voices[0].notes[0];
    CHECK(note.ratchet == 1);
    CHECK(note.chance == 100);
    CHECK((note.condA == 1 && note.condB == 1));
    CHECK(parseAiResponse(answer.dump()).draft.schemaVersion == 2);
}

TEST_CASE("an answer in a code fence or between sentences is read", "[ai][schema]") {
    const auto text = validAnswer().dump();
    CHECK(parseAiResponse("```json\n" + text + "\n```").ok);
    CHECK(parseAiResponse("Here is your pattern:\n" + text + "\nHope you like it!").ok);
    CHECK(parseAiResponse("  \n" + text + "  \n").ok);
    CHECK_FALSE(parseAiResponse("no braces at all").ok);
    CHECK_FALSE(parseAiResponse("} reversed {").ok);
}

TEST_CASE("the progression repeats, splits bars and must fit the length", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["context"]["progression"] = json::array({"i|bVII", "bVI"});
    auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    const auto& events = result.pattern.context.progression;
    REQUIRE(events.size() == 6); // bar 0 two halves, bar 1 one chord, repeated
    CHECK(events[0].lengthHalfBars == 1);
    CHECK(events[1].startHalfBar == 1);
    CHECK(events[1].chord == Chord{10, ChordQuality::Major}); // bVII
    CHECK(events[2].lengthHalfBars == 2);
    CHECK(events[2].startHalfBar == 2);
    CHECK(events[3].startHalfBar == 4);
    CHECK(validatePattern(result.pattern).empty());

    answer["context"]["progression"] = json::array({"i", "bVI", "bVII"}); // 3 does not divide 4
    result = buildFrom(answer, style);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("progression") != std::string::npos);
    answer["context"]["progression"] = json::array({"i", "nonsense", "bVI", "bVII"});
    result = buildFrom(answer, style);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("progression[1]") != std::string::npos);
    answer["context"]["progression"] = json::array({"i|bVI|bVII", "i", "i", "i"});
    CHECK_FALSE(buildFrom(answer, style).ok);
    answer["context"]["progression"] = json::array({"i"}); // one chord for the whole pattern
    CHECK(buildFrom(answer, style).ok);
}

TEST_CASE("the key is named like a note and the scale must exist", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    for (const auto& [name, pitchClass] : std::vector<std::pair<std::string, int>>{
             {"C", 0}, {"C#", 1}, {"Db", 1}, {"Bb", 10}, {"F#", 6}, {"B", 11}, {"Cb", 11}}) {
        INFO(name);
        answer["context"]["root"] = name;
        const auto result = buildFrom(answer, style);
        REQUIRE(result.ok);
        CHECK(result.pattern.context.root == pitchClass);
        CHECK(result.pattern.voices[0].notes[0].pitch % 12 == pitchClass); // degree 1 is the root
    }
    for (const char* bad : {"H", "a", "", "C##", "Cb4", "A minor", "x"}) {
        INFO(bad);
        answer["context"]["root"] = bad;
        const auto result = buildFrom(answer, style);
        CHECK_FALSE(result.ok);
        CHECK(result.error.find("context.root") != std::string::npos);
    }
    answer["context"]["root"] = "A";
    answer["context"]["scale"] = "klingon";
    const auto result = buildFrom(answer, style);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("context.scale") != std::string::npos);
    answer["context"]["scale"] = "minor_pentatonic";
    answer["voices"][0]["notes"][0]["degree"] = 6; // a pentatonic scale has 5 degrees: 6 is the root an octave up
    const auto pentatonic = buildFrom(answer, style);
    REQUIRE(pentatonic.ok);
    CHECK(pentatonic.pattern.voices[0].notes[0].pitch == 33 + 12);
}

TEST_CASE("values outside their ranges are cut and counted", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][0]["notes"] = json::array({
        {{"step", 0}, {"degree", 1}, {"alt", 5}, {"octave", 0}, {"len", 1}, {"vel", 100}},  // alt cut to +1
        {{"step", 2}, {"degree", 1}, {"alt", -9}, {"octave", 0}, {"len", 1}, {"vel", 100}}, // alt cut to -1
        {{"step", 4}, {"degree", 1}, {"octave", 99}, {"len", 1}, {"vel", 100}},             // octave cut to +4
        {{"step", 6}, {"degree", 1}, {"octave", -99}, {"len", 1}, {"vel", 100}},            // octave cut to -4
        {{"step", 62}, {"degree", 1}, {"len", 500}, {"vel", 100}},                          // length cut to the end
        {{"step", 8}, {"degree", 1}, {"len", 0}, {"vel", 0}},                               // length 1, velocity 1
        {{"step", 10}, {"degree", 1}, {"len", -5}, {"vel", 999}},                           // length 1, velocity 127
    });
    const auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    CHECK(result.clampedValues == 2 + 2 + 1 + 2 + 2);
    const auto& notes = result.pattern.voices[0].notes;
    REQUIRE(notes.size() + result.droppedNotes == 7);
    for (const auto& note : notes) {
        CHECK(note.velocity >= 1);
        CHECK(note.velocity <= 127);
        CHECK(note.lengthTicks >= 1);
        CHECK(note.startTick + note.lengthTicks <= 4 * kTicksPerBar);
    }
    const auto last = std::find_if(notes.begin(), notes.end(), [](const Note& n) { return n.startTick == 62 * 240; });
    REQUIRE(last != notes.end());
    CHECK(last->lengthTicks == 2 * 240); // steps 62 and 63 are left
}

TEST_CASE("notes with an invalid position or degree are dropped and counted", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][1]["notes"] = json::array({
        {{"step", 0}, {"degree", 1}},  // good
        {{"step", -1}, {"degree", 1}}, // before the start
        {{"step", 64}, {"degree", 1}}, // after the end of 4 bars
        {{"step", 4}, {"degree", 0}},  // no such degree
        {{"step", 5}, {"degree", -2}},
        {{"step", 6}},                     // no degree
        {{"degree", 1}},                   // no step
        {{"step", "four"}, {"degree", 1}}, // wrong type
        42,                                // not an object
        "note",
        nullptr,
        {{"step", 7.0}, {"degree", 2.0}}, // whole numbers written as floats are fine
    });
    const auto parsed = parseAiResponse(answer.dump());
    REQUIRE(parsed.ok);
    CHECK(parsed.droppedNotes == 6); // missing fields, wrong types and non-objects are dropped while parsing
    auto context = contextFor(style);
    const auto result = buildPattern(parsed.draft, context);
    REQUIRE(result.ok);
    CHECK(result.droppedNotes == 4); // position and degree are checked when building
    CHECK(result.pattern.voices[1].notes.size() == 2);
}

TEST_CASE("accent and slide flags reach the notes", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][0]["notes"] = json::array({{{"step", 0}, {"degree", 1}, {"accent", true}, {"slide", false}},
                                                {{"step", 4}, {"degree", 2}, {"accent", false}, {"slide", true}},
                                                {{"step", 8}, {"degree", 3}, {"accent", "yes"}, {"slide", 1}}});
    const auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    const auto& notes = result.pattern.voices[0].notes;
    CHECK((notes[0].accent && !notes[0].slide));
    CHECK((!notes[1].accent && notes[1].slide));
    CHECK((!notes[2].accent && !notes[2].slide)); // not booleans: ignored
}

TEST_CASE("the kick grid of the style is the pattern's", "[ai][schema]") {
    auto style = shippedStyle();
    style.kickDefault = "broken_a";
    const auto result = buildFrom(validAnswer(), style);
    REQUIRE(result.ok);
    CHECK(result.pattern.kickGridId == "broken_a");
}

TEST_CASE("an answer without any usable note is refused", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][0]["notes"] = json::array();
    answer["voices"][1]["notes"] = json::array({{{"step", 999}, {"degree", 1}}});
    const auto result = buildFrom(answer, style);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
    answer["voices"][0]["notes"] = json::array({{{"step", 0}, {"degree", 1}}}); // one voice is enough to build
    CHECK(buildFrom(answer, style).ok);
}

TEST_CASE("bass and melody are both required, other roles are ignored", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"].push_back({{"role", "pad"}, {"notes", json::array()}});
    auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    CHECK(result.ignoredVoices == 1);
    CHECK(result.pattern.voices.size() == 2);
    answer["voices"] = json::array({answer["voices"][0]}); // only the bass
    result = buildFrom(answer, style);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("melody") != std::string::npos);
}

TEST_CASE("a voice of the answer that repeats a role: the first one counts", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    auto second = answer["voices"][0];
    second["notes"] = json::array({{{"step", 5}, {"degree", 2}}});
    answer["voices"].push_back(second);
    const auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes.size() == 3);
}

TEST_CASE("note ids: only known ids used once keep their note", "[ai][schema][refine]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][0]["notes"] = json::array({
        {{"id", 12}, {"step", 0}, {"degree", 1}},  // known: kept
        {{"id", 12}, {"step", 2}, {"degree", 2}},  // duplicate of a kept id: new note
        {{"id", 77}, {"step", 4}, {"degree", 3}},  // unknown: new note
        {{"step", 6}, {"degree", 4}},              // no id: new note
        {{"id", 40}, {"step", 8}, {"degree", 5}},  // known: kept
        {{"id", -3}, {"step", 10}, {"degree", 6}}, // invalid id: new note
    });
    const auto parsed = parseAiResponse(answer.dump());
    REQUIRE(parsed.ok);
    const std::set<uint32_t> known = {12, 40, 41};
    auto context = contextFor(style);
    context.knownIds = &known;
    context.firstNewId = 100;
    const auto result = buildPattern(parsed.draft, context);
    REQUIRE(result.ok);
    const auto& notes = result.pattern.voices[0].notes;
    REQUIRE(notes.size() == 6);
    CHECK(notes[0].id == 12);
    CHECK(notes[4].id == 40);
    std::set<uint32_t> ids;
    for (const auto& track : result.pattern.voices) {
        for (const auto& note : track.notes) {
            CHECK(ids.insert(note.id).second); // unique
        }
    }
    for (const size_t index : {size_t{1}, size_t{2}, size_t{3}, size_t{5}}) {
        CHECK(notes[index].id >= 100); // new notes start at the counter of the pattern being refined
    }
    CHECK(result.pattern.nextNoteId > *ids.rbegin());
    // without the set every note is new
    context.knownIds = nullptr;
    context.firstNewId = 1;
    const auto fresh = buildPattern(parsed.draft, context);
    REQUIRE(fresh.ok);
    CHECK(fresh.pattern.voices[0].notes[0].id != 12);
}

TEST_CASE("a kept id above the counter of the pattern raises the counter", "[ai][schema][refine]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][0]["notes"] =
        json::array({{{"id", 500}, {"step", 0}, {"degree", 1}}, {{"step", 2}, {"degree", 2}}});
    const auto parsed = parseAiResponse(answer.dump());
    const std::set<uint32_t> known = {500};
    auto context = contextFor(style);
    context.knownIds = &known;
    context.firstNewId = 3;
    const auto result = buildPattern(parsed.draft, context);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes[0].id == 500);
    CHECK(result.pattern.voices[0].notes[1].id > 500);
}

TEST_CASE("phrases of the answer are used when they form a valid plan", "[ai][schema]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 8}, {"role", "main"}},
                                     {{"start_bar", 8}, {"bars", 8}, {"role", "variation"}, {"turnaround", true}}});
    answer["voices"][0]["notes"] = json::array({{{"step", 100}, {"degree", 1}}});
    auto result = buildFrom(answer, style, 16);
    REQUIRE(result.ok);
    REQUIRE(result.pattern.phrases.size() == 2);
    CHECK(result.pattern.phrases[1].role == PhraseRole::Variation);
    CHECK(result.pattern.phrases[1].turnaround);
    CHECK_FALSE(result.pattern.phrases[0].turnaround);
    CHECK(validatePattern(result.pattern).empty());

    // a turnaround needs a phrase of 8 or 16 bars: on 4 bars it is dropped
    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 4}, {"role", "main"}, {"turnaround", true}},
                                     {{"start_bar", 4}, {"bars", 4}, {"role", "answer"}}});
    result = buildFrom(answer, style, 8);
    REQUIRE(result.ok);
    REQUIRE(result.pattern.phrases.size() == 2);
    CHECK_FALSE(result.pattern.phrases[0].turnaround);
    CHECK(result.pattern.phrases[1].role == PhraseRole::Answer);

    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 4}, {"role", "main"}}}); // does not cover 8 bars
    result = buildFrom(answer, style, 8);
    REQUIRE(result.ok);
    REQUIRE(result.pattern.phrases.size() == 1);
    CHECK(result.pattern.phrases[0].lengthBars == 8);
    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 8}, {"role", "solo"}}}); // unknown role
    result = buildFrom(answer, style, 8);
    REQUIRE(result.ok);
    CHECK(result.pattern.phrases[0].role == PhraseRole::Main);
    answer["phrases"] = "nonsense";
    CHECK(buildFrom(answer, style, 8).ok);
}

TEST_CASE("a motif is repeated over a pattern of 16 bars", "[ai][schema][motif]") {
    const auto style = shippedStyle();
    auto answer = validAnswer();
    answer["voices"][0]["motif_bars"] = 2;
    answer["voices"][0]["notes"] = json::array({{{"step", 1}, {"degree", 1}, {"len", 2}},
                                                {{"step", 20}, {"degree", 5}, {"len", 40}}, // reaches past the motif: cut
                                                {{"step", 40}, {"degree", 2}}});            // outside the motif: dropped
    auto result = buildFrom(answer, style, 16);
    REQUIRE(result.ok);
    const auto& notes = result.pattern.voices[0].notes;
    CHECK(result.droppedNotes == 1);
    REQUIRE(notes.size() == 2 * 8); // two notes, eight statements of a two-bar motif
    CHECK(notes[0].startTick == 240);
    CHECK(notes[1].startTick == 20 * 240);
    CHECK(notes[1].lengthTicks == 12 * 240); // the motif ends at step 32
    CHECK(notes[2].startTick == 32 * 240 + 240); // the second statement
    CHECK(notes[15].startTick == 7 * 32 * 240 + 20 * 240);
    CHECK(notes[14].pitch == notes[0].pitch);
    CHECK(notes[15].pitch == notes[1].pitch);
    std::set<uint32_t> ids;
    for (const auto& note : notes) {
        CHECK(ids.insert(note.id).second);
    }
    CHECK(validatePattern(result.pattern).empty());
    // the melody has no motif length: its notes are the whole pattern
    CHECK(result.pattern.voices[1].notes.size() == 2);

    // a motif that does not divide the pattern is cut at the end of the pattern
    answer["voices"][0]["motif_bars"] = 5;
    answer["voices"][0]["notes"] = json::array({{{"step", 0}, {"degree", 1}, {"len", 4}},
                                                {{"step", 64}, {"degree", 3}, {"len", 16}}, // bar 5 of the motif
                                                {{"step", 76}, {"degree", 5}, {"len", 8}}});
    result = buildFrom(answer, style, 16);
    REQUIRE(result.ok);
    CHECK(validatePattern(result.pattern).empty());
    {
        const auto& cut = result.pattern.voices[0].notes;
        // statements start at bars 0, 5, 10 and 15: the last one has room for bar 1 only
        CHECK(cut.size() == 3 + 3 + 3 + 1);
        CHECK(cut.back().startTick == 15 * kTicksPerBar);
        for (const auto& note : cut) {
            CHECK(note.startTick + note.lengthTicks <= 16 * kTicksPerBar);
        }
    }
    // the ids of known notes belong to the first statement only
    {
        const auto parsed = parseAiResponse(answer.dump());
        REQUIRE(parsed.ok);
        auto draft = parsed.draft;
        draft.voices[0].notes[0].id = 7;
        const std::set<uint32_t> known = {7};
        auto context = contextFor(style, 16);
        context.knownIds = &known;
        const auto withIds = buildPattern(draft, context);
        REQUIRE(withIds.ok);
        std::set<uint32_t> ids;
        for (const auto& note : withIds.pattern.voices[0].notes) {
            CHECK(ids.insert(note.id).second);
        }
        CHECK(withIds.pattern.voices[0].notes[0].id == 7);
    }
    answer["voices"][0]["motif_bars"] = 12; // at most 8 bars
    answer["voices"][0]["notes"] = json::array({{{"step", 100}, {"degree", 1}}, {{"step", 127}, {"degree", 1}}});
    result = buildFrom(answer, style, 16);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes.size() == 4); // two statements of an eight-bar motif
    answer["voices"][0]["motif_bars"] = 0; // no motif
    result = buildFrom(answer, style, 16);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes.size() == 2);
    answer["voices"][0]["motif_bars"] = 2; // up to 8 bars the notes are the pattern, whatever the answer says
    answer["voices"][0]["notes"] = json::array({{{"step", 1}, {"degree", 1}}, {{"step", 40}, {"degree", 1}}});
    result = buildFrom(answer, style, 4);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes.size() == 2);
    result = buildFrom(answer, style, 8);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes.size() == 2);
}

TEST_CASE("an archetype is taken only when it exists for that role", "[ai][schema]") {
    const auto style = shippedStyle();
    const auto bass = std::find_if(allArchetypes().begin(), allArchetypes().end(),
                                   [](const Archetype& a) { return a.role == VoiceRole::Bass; });
    REQUIRE(bass != allArchetypes().end());
    auto answer = validAnswer();
    answer["voices"][0]["archetype"] = std::string(bass->id);
    auto result = buildFrom(answer, style);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].archetypeId == bass->id);
    CHECK_FALSE(result.pattern.voices[0].archetypeAuto);
    answer["voices"][0]["archetype"] = "made_up";
    result = buildFrom(answer, style);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].archetypeId.empty());
    CHECK(result.pattern.voices[0].archetypeAuto);
    answer["voices"][1]["archetype"] = std::string(bass->id); // a bass archetype on the melody voice
    result = buildFrom(answer, style);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[1].archetypeId.empty());
}

TEST_CASE("structural problems are refused with the path of the problem", "[ai][schema]") {
    struct Case {
        const char* what;
        std::function<void(json&)> damage;
        const char* path;
    };
    const std::vector<Case> cases = {
        {"no version", [](json& a) { a.erase("schema_version"); }, "schema_version"},
        {"version text", [](json& a) { a["schema_version"] = "1"; }, "schema_version"},
        {"version zero", [](json& a) { a["schema_version"] = 0; }, "schema_version"},
        {"version float", [](json& a) { a["schema_version"] = 1.5; }, "schema_version"},
        {"no context", [](json& a) { a.erase("context"); }, "context"},
        {"context array", [](json& a) { a["context"] = json::array(); }, "context"},
        {"no root", [](json& a) { a["context"].erase("root"); }, "context.root"},
        {"root number", [](json& a) { a["context"]["root"] = 9; }, "context.root"},
        {"no scale", [](json& a) { a["context"].erase("scale"); }, "context.scale"},
        {"no progression", [](json& a) { a["context"].erase("progression"); }, "context.progression"},
        {"empty progression", [](json& a) { a["context"]["progression"] = json::array(); }, "context.progression"},
        {"progression text", [](json& a) { a["context"]["progression"] = "i"; }, "context.progression"},
        {"progression entry", [](json& a) { a["context"]["progression"][1] = 5; }, "context.progression[1]"},
        {"no voices", [](json& a) { a.erase("voices"); }, "voices"},
        {"voices object", [](json& a) { a["voices"] = json::object(); }, "voices"},
        {"empty voices", [](json& a) { a["voices"] = json::array(); }, "voices"},
        {"voices without role", [](json& a) { a["voices"] = json::array({{{"notes", json::array()}}}); }, "voices"},
        {"no notes", [](json& a) { a["voices"][1].erase("notes"); }, "voices[1].notes"},
        {"notes object", [](json& a) { a["voices"][0]["notes"] = json::object(); }, "voices[0].notes"},
    };
    for (const auto& c : cases) {
        INFO(c.what);
        auto answer = validAnswer();
        c.damage(answer);
        const auto result = parseAiResponse(answer.dump());
        CHECK_FALSE(result.ok);
        CHECK(result.error.find(c.path) != std::string::npos);
    }
    for (const char* text : {"", " ", "{}", "[]", "null", "42", "\"text\"", "true", "{\"schema_version\":1}", "{", "}{",
                             "{\"schema_version\":1,\"context\":{}}"}) {
        INFO(text);
        const auto result = parseAiResponse(text);
        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.error.empty());
    }
}

TEST_CASE("hostile answers are refused without crashing", "[ai][schema][hostile]") {
    // too big: even an answer that is valid JSON is refused over the limit
    CHECK_FALSE(parseAiResponse(std::string(kMaxResponseBytes + 1, ' ')).ok);
    const auto padded = parseAiResponse(validAnswer().dump() + std::string(kMaxResponseBytes, ' '));
    CHECK_FALSE(padded.ok);
    CHECK(padded.error.find("too large") != std::string::npos);
    // nested a hundred thousand levels deep: refused before the JSON parser sees it
    const auto deep = parseAiResponse(std::string(100000, '[') + std::string(100000, ']'));
    CHECK_FALSE(deep.ok);
    CHECK(deep.error.find("deeply") != std::string::npos);
    const auto deepObject = [] {
        std::string text;
        for (int i = 0; i < 50000; ++i) {
            text += "{\"a\":";
        }
        return text;
    }();
    CHECK_FALSE(parseAiResponse(deepObject).ok);
    // brackets inside strings do not count as nesting
    auto answer = validAnswer();
    answer["intent"]["motif_idea"] = std::string(500, '[') + std::string(500, '{');
    CHECK(parseAiResponse(answer.dump()).ok);
    // too many notes or voices
    answer = validAnswer();
    json notes = json::array();
    for (size_t i = 0; i < kMaxNotesPerVoice + 1; ++i) {
        notes.push_back({{"step", 0}, {"degree", 1}});
    }
    answer["voices"][0]["notes"] = notes;
    const auto many = parseAiResponse(answer.dump());
    CHECK_FALSE(many.ok);
    CHECK(many.error.find("voices[0].notes") != std::string::npos);
    answer = validAnswer();
    answer["voices"] = json::array();
    for (int i = 0; i < kMaxVoices + 1; ++i) {
        answer["voices"].push_back({{"role", "bass"}, {"notes", json::array()}});
    }
    CHECK_FALSE(parseAiResponse(answer.dump()).ok);
    // a progression with more entries than a pattern can have bars
    answer = validAnswer();
    answer["context"]["progression"] = json::array();
    for (size_t i = 0; i < kMaxProgressionEntries + 1; ++i) {
        answer["context"]["progression"].push_back("i");
    }
    const auto longProgression = parseAiResponse(answer.dump());
    CHECK_FALSE(longProgression.ok);
    CHECK(longProgression.error.find("progression") != std::string::npos);
    // huge numbers and odd strings
    const auto text = R"({"schema_version": 1, "context": {"root": "A", "scale": "natural_minor", "progression": ["i"]},
        "voices": [{"role": "bass", "notes": [{"step": 1e300, "degree": 1}, {"step": 18446744073709551615, "degree": 1},
        {"step": 0, "degree": 9223372036854775807, "octave": -9223372036854775808, "len": 1e999, "vel": -1e999}]},
        {"role": "melody", "notes": [{"step": 0, "degree": 1}]}]})";
    const auto parsed = parseAiResponse(text);
    if (parsed.ok) {
        const auto style = shippedStyle();
        const auto built = buildPattern(parsed.draft, contextFor(style));
        if (built.ok) {
            CHECK(validatePattern(built.pattern).empty());
        }
    }
}

TEST_CASE("building refuses an invalid pattern length", "[ai][schema]") {
    const auto style = shippedStyle();
    const auto parsed = parseAiResponse(validAnswer().dump());
    REQUIRE(parsed.ok);
    for (const uint32_t bars : {0u, 3u, 5u, 32u}) {
        auto context = contextFor(style, bars);
        CHECK_FALSE(buildPattern(parsed.draft, context).ok);
    }
    auto context = contextFor(style, 16);
    CHECK(buildPattern(parsed.draft, context).ok); // the notes are the motif; the first bars hold them
}

TEST_CASE("without a style profile the default ranges apply", "[ai][schema]") {
    const auto parsed = parseAiResponse(validAnswer().dump());
    REQUIRE(parsed.ok);
    BuildContext context;
    context.lengthBars = 4;
    context.styleId = "peak_time";
    const auto result = buildPattern(parsed.draft, context);
    REQUIRE(result.ok);
    CHECK(result.pattern.voices[0].notes[0].pitch == 33);
}

TEST_CASE("random damage to a valid answer never gives an invalid pattern", "[ai][schema][series]") {
    const auto style = shippedStyle();
    const auto original = validAnswer().dump();
    Pcg32 rng = Pcg32::fromSeed(2024);
    size_t built = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string text = original;
        const int damage = 1 + static_cast<int>(rng.bounded(4));
        for (int d = 0; d < damage && !text.empty(); ++d) {
            const size_t at = rng.bounded(static_cast<uint32_t>(text.size()));
            switch (rng.bounded(5)) {
            case 0:
                text[at] = static_cast<char>(rng.bounded(256));
                break; // another byte
            case 1:
                text.erase(at, 1 + rng.bounded(8));
                break; // cut a piece
            case 2:
                text.insert(at, 1, "{}[],:\"0123456789-eE.tfn"[rng.bounded(24)]);
                break; // add a char
            case 3:
                text.insert(at, text.substr(at, 1 + rng.bounded(40)));
                break; // repeat a piece
            default:
                text.resize(at);
                break; // cut the end
            }
        }
        const auto parsed = parseAiResponse(text);
        if (!parsed.ok) {
            CHECK_FALSE(parsed.error.empty());
            continue;
        }
        auto context = contextFor(style, rng.bounded(2) == 0 ? 4u : 8u);
        context.rawResponse = text;
        const auto result = buildPattern(parsed.draft, context);
        if (!result.ok) {
            CHECK_FALSE(result.error.empty());
            continue;
        }
        ++built;
        Pattern pattern = result.pattern;
        REQUIRE(validatePattern(pattern).empty());
        applyConstraints(pattern, ConstraintSettings::forPattern(pattern, style.registerProfile()));
        const auto issues = validatePattern(pattern);
        INFO(text);
        REQUIRE(issues.empty());
    }
    CHECK(built > 100); // many of the damaged answers are still usable
}

TEST_CASE("random numbers in every field keep the invariants of the pattern", "[ai][schema][series]") {
    const auto style = shippedStyle();
    Pcg32 rng = Pcg32::fromSeed(77);
    const auto next64 = [&] { return (static_cast<uint64_t>(rng.next()) << 32) | rng.next(); };
    const auto number = [&]() -> json {
        switch (rng.bounded(8)) {
        case 0:
            return static_cast<int>(rng.bounded(200)) - 100;
        case 1:
            return static_cast<double>(rng.bounded(1000)) / 7.0;
        case 2:
            return static_cast<long long>(next64() >> 3) * (rng.bounded(2) == 0 ? 1 : -1);
        case 3:
            return static_cast<unsigned long long>(next64());
        case 4:
            return rng.bounded(2) == 0;
        case 5:
            return "7";
        case 6:
            return nullptr;
        default:
            return static_cast<int>(rng.bounded(70));
        }
    };
    size_t built = 0;
    for (int round = 0; round < 2000; ++round) {
        json answer = validAnswer();
        json notes = json::array();
        const int count = static_cast<int>(rng.bounded(30));
        for (int i = 0; i < count; ++i) {
            json note;
            for (const char* key : {"id", "step", "degree", "alt", "octave", "len", "vel", "accent", "slide"}) {
                if (rng.bounded(5) != 0) {
                    note[key] = number();
                }
            }
            notes.push_back(note);
        }
        answer["voices"][round % 2]["notes"] = notes;
        const auto parsed = parseAiResponse(answer.dump());
        REQUIRE(parsed.ok);
        const std::set<uint32_t> known = {1, 2, 3, 5, 8, 13, 21, 34};
        auto context = contextFor(style, rng.bounded(2) == 0 ? 2u : 4u);
        context.knownIds = rng.bounded(2) == 0 ? &known : nullptr;
        const auto result = buildPattern(parsed.draft, context);
        if (!result.ok) {
            continue;
        }
        ++built;
        const Pattern& p = result.pattern;
        INFO(answer.dump());
        REQUIRE(validatePattern(p).empty());
        std::set<uint32_t> ids;
        for (const auto& track : p.voices) {
            for (const auto& note : track.notes) {
                REQUIRE(ids.insert(note.id).second);
                REQUIRE(note.id < p.nextNoteId);
                REQUIRE(note.pitch <= 127);
            }
        }
    }
    CHECK(built > 500);
}
