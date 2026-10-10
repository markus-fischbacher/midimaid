#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/StyleProfile.h"
#include "core/VoiceImport.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

using namespace mm::core;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

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

/// A four-bar bass line A A C G in A minor, off-grid starts and odd velocities (it must come through exactly).
ImportPlan bassPlan(int shift = 0, uint32_t bars = 4) {
    ImportPlan plan;
    plan.status = ImportStatus::Ok;
    plan.lengthBars = bars;
    plan.sourceBars = bars;
    const int roots[] = {45, 45, 48, 43}; // A2 A2 C3 G2
    for (uint32_t bar = 0; bar < bars; ++bar) {
        for (uint32_t step = 0; step < 4; ++step) {
            MidiClipNote note;
            note.pitch = static_cast<uint8_t>(roots[bar % 4] + shift);
            note.velocity = static_cast<uint8_t>(61 + step * 7);
            note.startTick = bar * kTicksPerBar + step * 960 + (step == 3 ? 37 : 0);
            note.lengthTicks = 311;
            plan.notes.push_back(note);
        }
    }
    return plan;
}

} // namespace

TEST_CASE("an imported voice replaces the voice, exact and locked", "[voice-import]") {
    const auto style = shipped("peak_time");
    const auto plan = bassPlan();
    const auto pattern = importVoice(style, std::nullopt, 0, plan);
    REQUIRE(pattern.has_value());
    CHECK(validatePattern(*pattern).empty());
    CHECK(pattern->lengthBars == 4);
    CHECK(pattern->info.source == "import");

    const Track& bass = pattern->voices[0];
    REQUIRE(bass.notes.size() == plan.notes.size());
    for (size_t i = 0; i < plan.notes.size(); ++i) {
        CHECK(bass.notes[i].pitch == plan.notes[i].pitch);
        CHECK(bass.notes[i].velocity == plan.notes[i].velocity);
        CHECK(bass.notes[i].startTick == plan.notes[i].startTick);
        CHECK(bass.notes[i].lengthTicks == plan.notes[i].lengthTicks);
    }
    CHECK(isVoiceLocked(bass));
    CHECK(bass.archetypeId.empty());
    CHECK(bass.groove.amount == 0.0f); // the groove of the producer is in the ticks: no second swing
    CHECK(bass.octaveOffset == 0);
    CHECK(pattern->voices[1].notes.empty());
    CHECK_FALSE(isVoiceLocked(pattern->voices[1]));
}

TEST_CASE("the harmony of the pattern comes from the imported voice", "[voice-import]") {
    const auto style = shipped("peak_time");
    const auto pattern = importVoice(style, std::nullopt, 0, bassPlan());
    REQUIRE(pattern.has_value());
    CHECK(pattern->context.root == 9);
    CHECK(pattern->context.scaleId == "minor_pentatonic"); // A C G: three tones, the five-tone scale fits best
    uint32_t next = 0;
    for (const ChordEvent& event : pattern->context.progression) {
        CHECK(event.startHalfBar == next); // gapless
        next += event.lengthHalfBars;
    }
    CHECK(next == 8);
    // bar 3 is C (offset 3), bar 4 is G (offset 10)
    bool seenC = false;
    bool seenG = false;
    for (const ChordEvent& event : pattern->context.progression) {
        seenC = seenC || (event.startHalfBar == 4 && event.chord.rootOffset == 3);
        seenG = seenG || (event.startHalfBar == 6 && event.chord.rootOffset == 10);
    }
    CHECK(seenC);
    CHECK(seenG);
}

TEST_CASE("an imported voice moved to another key moves the harmony with it", "[voice-import]") {
    const auto style = shipped("peak_time");
    const auto a = importVoice(style, std::nullopt, 0, bassPlan(0));
    const auto b = importVoice(style, std::nullopt, 0, bassPlan(5));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(b->context.root == (a->context.root + 5) % 12);
    CHECK(b->context.scaleId == a->context.scaleId);
    CHECK(b->context.progression == a->context.progression);
}

TEST_CASE("the import goes into the voice that was asked for", "[voice-import]") {
    const auto style = shipped("peak_time");
    const auto pattern = importVoice(style, std::nullopt, 1, bassPlan());
    REQUIRE(pattern.has_value());
    CHECK(isVoiceLocked(pattern->voices[1]));
    CHECK(pattern->voices[1].notes.size() == 16);
    CHECK_FALSE(isVoiceLocked(pattern->voices[0]));
    CHECK(pattern->voices[0].notes.empty());
}

TEST_CASE("an import that cannot be used gives nothing", "[voice-import]") {
    const auto style = shipped("peak_time");
    CHECK_FALSE(importVoice(style, std::nullopt, 2, bassPlan()).has_value()); // no third voice
    ImportPlan empty = bassPlan();
    empty.notes.clear();
    CHECK_FALSE(importVoice(style, std::nullopt, 0, empty).has_value());
    ImportPlan notFourFour = bassPlan();
    notFourFour.status = ImportStatus::NotFourFour;
    CHECK_FALSE(importVoice(style, std::nullopt, 0, notFourFour).has_value());
    ImportPlan odd = bassPlan();
    odd.lengthBars = 3;
    CHECK_FALSE(importVoice(style, std::nullopt, 0, odd).has_value());
}

TEST_CASE("an import of the same length keeps the other locked voice and clears the unlocked ones", "[voice-import]") {
    const auto style = shipped("peak_time");
    GenerationRequest request;
    request.seed = 11;
    request.lengthBars = 4;
    auto generated = generatePattern(style, request);
    REQUIRE(generated.success);
    Pattern current = generated.pattern;
    current.voices.push_back(current.voices[1]); // a third voice, locked, is kept
    current.voices[2].lock = {true, true, true};
    current.voices[2].midiChannel = 3;
    const auto lockedNotes = current.voices[2].notes;
    REQUIRE_FALSE(lockedNotes.empty());

    const auto pattern = importVoice(style, current, 0, bassPlan());
    REQUIRE(pattern.has_value());
    CHECK(validatePattern(*pattern).empty());
    CHECK(pattern->voices.size() == 3);
    CHECK(pattern->voices[1].notes.empty());   // generated again later
    CHECK(pattern->voices[2].notes == lockedNotes);
    CHECK(pattern->kickGridId == current.kickGridId);
    CHECK(pattern->nextNoteId > current.nextNoteId);
}

TEST_CASE("an import of another length builds the pattern anew with the voices of the slot", "[voice-import]") {
    const auto style = shipped("peak_time");
    GenerationRequest request;
    request.seed = 5;
    request.lengthBars = 4;
    request.voices = {VoiceRole::Melody, VoiceRole::Bass};
    auto generated = generatePattern(style, request);
    REQUIRE(generated.success);
    Pattern current = generated.pattern;
    current.voices[1].lock = {true, true, true};

    const auto pattern = importVoice(style, current, 0, bassPlan(0, 8));
    REQUIRE(pattern.has_value());
    CHECK(validatePattern(*pattern).empty());
    CHECK(pattern->lengthBars == 8);
    CHECK(pattern->voices.size() == 2);
    CHECK(pattern->voices[0].role == VoiceRole::Melody);
    CHECK(pattern->voices[1].role == VoiceRole::Bass);
    CHECK(pattern->voices[1].notes.empty()); // the old locked voice does not fit the new length
    CHECK_FALSE(isVoiceLocked(pattern->voices[1]));
    CHECK(isVoiceLocked(pattern->voices[0]));
    CHECK(pattern->voices[0].notes.size() == 32);
    CHECK(pattern->nextNoteId == current.nextNoteId + 32);
}

TEST_CASE("the other voices are generated around the imported one", "[voice-import]") {
    const auto style = shipped("peak_time");
    for (const uint32_t bars : {4u, 8u}) {
        const auto imported = importVoice(style, std::nullopt, 0, bassPlan(0, bars));
        REQUIRE(imported.has_value());
        GenerationRequest request;
        request.seed = 3;
        const auto result = generatePatternAroundLocks(style, request, *imported);
        REQUIRE(result.success);
        CHECK(validatePattern(result.pattern).empty());
        CHECK(result.pattern.voices[0].notes == imported->voices[0].notes); // exactly as written
        CHECK(result.pattern.context == imported->context);
        CHECK(result.pattern.lengthBars == bars);
        CHECK_FALSE(result.pattern.voices[1].notes.empty());
        CHECK_FALSE(result.pattern.voices[1].archetypeId.empty());
    }
}

TEST_CASE("the musician corrects the key of an imported pattern", "[voice-import]") {
    const auto style = shipped("peak_time");
    auto pattern = importVoice(style, std::nullopt, 0, bassPlan());
    REQUIRE(pattern.has_value());
    const auto before = pattern->context.progression;
    const auto voiceBefore = pattern->voices[0];

    REQUIRE(correctImportedKey(*pattern, 4, "dorian")); // E dorian: the bass A is the fourth degree
    CHECK(pattern->context.root == 4);
    CHECK(pattern->context.scaleId == "dorian");
    CHECK(pattern->voices[0] == voiceBefore);
    CHECK(validatePattern(*pattern).empty());
    CHECK(pattern->context.progression != before);
    uint32_t next = 0;
    for (const ChordEvent& event : pattern->context.progression) {
        CHECK(event.startHalfBar == next);
        next += event.lengthHalfBars;
    }
    CHECK(next == 8);
    CHECK(pattern->context.progression.front().chord.rootOffset == 5); // A is five semitones above E

    const auto unchanged = *pattern;
    CHECK_FALSE(correctImportedKey(*pattern, 4, "no_such_scale"));
    CHECK_FALSE(correctImportedKey(*pattern, 12, "dorian"));
    CHECK(*pattern == unchanged);

    Pattern unlocked = unchanged;
    unlocked.voices[0].lock = {};
    CHECK_FALSE(correctImportedKey(unlocked, 4, "dorian")); // nothing to derive from
}

TEST_CASE("an import over a generated voice leaves nothing of it behind", "[voice-import]") {
    const auto style = shipped("peak_time");
    GenerationRequest request;
    request.seed = 21;
    auto generated = generatePattern(style, request);
    REQUIRE(generated.success);
    Pattern current = generated.pattern;
    REQUIRE_FALSE(current.voices[0].archetypeId.empty());
    current.voices[0].groove = GrooveSettings{0.6f, "template", 0.8f};
    current.voices[0].octaveOffset = 2;
    current.info.prompt = "something dark";
    current.info.rawResponse = "{...}";

    const auto pattern = importVoice(style, current, 0, bassPlan());
    REQUIRE(pattern.has_value());
    CHECK(pattern->voices[0].archetypeId.empty());
    CHECK(pattern->voices[0].groove == GrooveSettings{0.5f, "", 0.0f});
    CHECK(pattern->voices[0].octaveOffset == 0);
    CHECK(pattern->voices[0].notes.size() == 16); // not the notes of the generated voice, not both
    CHECK(pattern->info.prompt.empty()); // the text of an earlier AI request does not belong to the import
    CHECK(pattern->info.rawResponse.empty());
}

TEST_CASE("notes outside the ranges of a pattern are brought into them", "[voice-import]") {
    const auto style = shipped("peak_time");
    ImportPlan plan = bassPlan();
    plan.notes.resize(3);
    plan.notes[0].velocity = 0;
    plan.notes[1].velocity = 200;
    plan.notes[2].pitch = 200;
    plan.notes[2].lengthTicks = 0;
    const auto pattern = importVoice(style, std::nullopt, 0, plan);
    REQUIRE(pattern.has_value());
    CHECK(validatePattern(*pattern).empty());
    CHECK(pattern->voices[0].notes[0].velocity == 1);
    CHECK(pattern->voices[0].notes[1].velocity == 127);
    CHECK(pattern->voices[0].notes[2].pitch == 127);
    CHECK(pattern->voices[0].notes[2].lengthTicks == 1);
}
