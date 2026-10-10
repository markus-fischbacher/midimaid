#include "core/DrumReference.h"
#include "core/Groove.h"
#include "core/KickGrid.h"
#include "core/OutputStage.h"
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

MidiClipNote note(uint8_t pitch, uint32_t tick, uint8_t velocity = 100) {
    MidiClipNote n;
    n.channel = 10;
    n.pitch = pitch;
    n.velocity = velocity;
    n.startTick = tick;
    n.lengthTicks = 120;
    return n;
}

/// `bars` bars: kick on every beat, closed hat on every 16th (velocity 70, the odd ones late by `late` ticks), open
/// hat on the offbeat 8ths (steps 2, 6, 10, 14).
std::vector<MidiClipNote> house(uint32_t bars, int late = 0) {
    std::vector<MidiClipNote> notes;
    for (uint32_t bar = 0; bar < bars; ++bar) {
        for (uint32_t step = 0; step < 16; ++step) {
            const uint32_t tick = bar * 3840 + step * 240;
            if (step % 4 == 0) {
                notes.push_back(note(36, tick, 120));
            }
            if (step % 4 == 2) {
                notes.push_back(note(46, tick + (step % 2 == 1 ? late : 0), 90));
            } else {
                notes.push_back(note(42, tick + (step % 2 == 1 ? late : 0), 70));
            }
        }
    }
    return notes;
}

} // namespace

TEST_CASE("notes are mapped to drums after General MIDI unless the musician says otherwise", "[drum-reference]") {
    DrumMapping gm;
    CHECK(gm.roleOf(36) == DrumRole::Kick);
    CHECK(gm.roleOf(38) == DrumRole::Snare);
    CHECK(gm.roleOf(39) == DrumRole::Clap);
    CHECK(gm.roleOf(42) == DrumRole::ClosedHat);
    CHECK(gm.roleOf(46) == DrumRole::OpenHat);
    CHECK(gm.roleOf(51) == DrumRole::Ride);
    CHECK(gm.roleOf(60) == DrumRole::Other);

    DrumMapping rack;
    rack.overrides = {{60, DrumRole::Kick}, {36, DrumRole::Other}, {61, DrumRole::ClosedHat}};
    CHECK(rack.roleOf(60) == DrumRole::Kick);
    CHECK(rack.roleOf(36) == DrumRole::Other); // an entry also takes a GM note away
    CHECK(rack.roleOf(61) == DrumRole::ClosedHat);
    CHECK(rack.roleOf(38) == DrumRole::Snare); // the rest stays GM
}

TEST_CASE("the mapping is written as a line and read back", "[drum-reference]") {
    DrumMapping mapping;
    mapping.overrides = {{60, DrumRole::Kick}, {49, DrumRole::OpenHat}};
    const auto text = drumMappingToText(mapping);
    CHECK(text == "60=kick, 49=open_hat");
    CHECK(drumMappingFromText(text) == mapping);
    CHECK(drumMappingToText({}).empty());

    // what cannot be read is left out, the rest stays
    const auto messy = drumMappingFromText(" 60 = kick ;x=kick, 200=kick, 128=kick, 61=tuba,61=ride,\\n62=ride,60=snare,=kick,70");
    REQUIRE(messy.overrides.size() == 2);
    CHECK(messy.overrides[0] == DrumMapping::Entry{60, DrumRole::Kick}); // the first entry for a note wins
    CHECK(messy.overrides[1] == DrumMapping::Entry{61, DrumRole::Ride});
    CHECK(drumMappingFromText("").overrides.empty());
    CHECK(drumMappingFromText("1234=kick").overrides.empty());
    CHECK(drumMappingFromText("-3=kick").overrides.empty());

    for (const auto role : {DrumRole::Kick, DrumRole::Snare, DrumRole::Clap, DrumRole::ClosedHat, DrumRole::OpenHat,
                            DrumRole::Ride, DrumRole::Other}) {
        CHECK(drumRoleFromString(toString(role)) == role);
    }
    CHECK_FALSE(drumRoleFromString("tuba").has_value());
}

TEST_CASE("kick, hats and accents come out of a drum clip", "[drum-reference]") {
    const auto ref = deriveRhythmReference(house(1), 1);
    REQUIRE(ref.has_value());
    CHECK(ref->bars == 1);
    for (uint32_t step = 0; step < 16; ++step) {
        CHECK(ref->kickSteps.test(step) == (step % 4 == 0));
        CHECK(ref->hatSteps.test(step));
        CHECK(ref->accentSteps.test(step) == (step % 4 == 2)); // the open hats
        CHECK(ref->timingOffsetTicks[step] == 0);
        CHECK(ref->velocity[step] == (step % 4 == 2 ? 90 : 70));
    }
    CHECK(ref->kickSteps.count() == 4);
    CHECK((ref->kickSteps.to_ulong() & ~0xffffu) == 0);
}

TEST_CASE("the timing of the hats gives the groove, swing included once", "[drum-reference]") {
    const auto ref = deriveRhythmReference(house(2, 48), 2);
    REQUIRE(ref.has_value());
    CHECK(ref->bars == 2);
    for (uint32_t step = 0; step < 32; ++step) {
        CHECK(ref->timingOffsetTicks[step] == (step % 2 == 1 ? 48 : 0));
    }
    // the swing control shows the swing of the reference and is switched off (SPEC 3.9)
    Pattern pattern = makeEmptyPattern(2, "peak_time");
    pattern.rhythmRef = ref;
    pattern.voices[0].groove.templateId = kGrooveDrumReference;
    pattern.voices[0].groove.amount = 1.0f;
    const auto effective = effectiveGroove(pattern, pattern.voices[0]);
    CHECK_FALSE(effective.controlActive);
    CHECK(effective.swing == 0.60f); // 48 ticks of a pair of 480

    const auto early = deriveRhythmReference(house(1, -30), 1);
    REQUIRE(early.has_value());
    CHECK(early->timingOffsetTicks[1] == -30);
    CHECK(early->timingOffsetTicks[0] == 0);
}

TEST_CASE("starts go to the nearest 16th and far starts are bounded", "[drum-reference]") {
    std::vector<MidiClipNote> notes = {note(36, 0), note(36, 3840 - 100), note(42, 239), note(42, 480 + 119)};
    const auto ref = deriveRhythmReference(notes, 1);
    REQUIRE(ref.has_value());
    CHECK(ref->kickSteps.test(0));
    CHECK_FALSE(ref->kickSteps.test(15)); // 3740 is nearest to step 16 = the start of the next bar: outside, dropped
    CHECK(ref->hatSteps.test(1));
    CHECK(ref->timingOffsetTicks[1] == -1);
    CHECK(ref->hatSteps.test(2));
    CHECK(ref->timingOffsetTicks[2] == 119);
}

TEST_CASE("a longer clip is folded onto two bars by the majority of its repeats", "[drum-reference]") {
    auto notes = house(4); // two repeats of two bars
    // the kick on beat 2 of bar 1 is missing in both repeats, the one on beat 3 of bar 1 only in the second
    std::erase_if(notes, [](const MidiClipNote& n) {
        const uint32_t bar = n.startTick / 3840;
        const uint32_t step = (n.startTick % 3840) / 240;
        return n.pitch == 36 && ((step == 4 && (bar == 0 || bar == 2)) || (step == 8 && bar == 2));
    });
    const auto ref = deriveRhythmReference(notes, 4);
    REQUIRE(ref.has_value());
    CHECK(ref->bars == 2);
    CHECK_FALSE(ref->kickSteps.test(4)); // in none of the repeats
    CHECK(ref->kickSteps.test(8));       // in one of two: half is enough
    CHECK(ref->kickSteps.test(0));
    CHECK(ref->kickSteps.test(20)); // bar 2 of the reference, in both repeats
    CHECK(ref->kickSteps.count() == 7);
}

TEST_CASE("a step that is not there in most repeats is dropped", "[drum-reference]") {
    auto notes = house(8);
    std::erase_if(notes, [](const MidiClipNote& n) {
        const uint32_t bar = n.startTick / 3840;
        const uint32_t step = (n.startTick % 3840) / 240;
        return n.pitch == 36 && step == 4 && bar != 0; // one of four repeats has it
    });
    const auto ref = deriveRhythmReference(notes, 8);
    REQUIRE(ref.has_value());
    CHECK_FALSE(ref->kickSteps.test(4));
    CHECK(ref->kickSteps.test(0));
}

TEST_CASE("a clip without kick and hat gives no reference and other notes are ignored", "[drum-reference]") {
    CHECK_FALSE(deriveRhythmReference({}, 1).has_value());
    CHECK_FALSE(deriveRhythmReference({note(38, 960), note(39, 1920)}, 1).has_value());
    CHECK_FALSE(deriveRhythmReference(house(1), 0).has_value());
    // only a snare besides the kick: the snare does not appear
    const auto ref = deriveRhythmReference({note(36, 0), note(38, 960)}, 1);
    REQUIRE(ref.has_value());
    CHECK(ref->kickSteps.count() == 1);
    CHECK(ref->hatSteps.none());
    CHECK(ref->accentSteps.none());
}

TEST_CASE("a drum rack mapping is used", "[drum-reference]") {
    std::vector<MidiClipNote> notes;
    for (uint32_t i = 0; i < 4; ++i) {
        notes.push_back(note(60, i * 960));       // the kick of the rack
        notes.push_back(note(61, i * 960 + 480)); // its hat
    }
    CHECK_FALSE(deriveRhythmReference(notes, 1).has_value()); // GM does not know the notes
    DrumMapping rack;
    rack.overrides = {{60, DrumRole::Kick}, {61, DrumRole::ClosedHat}};
    const auto ref = deriveRhythmReference(notes, 1, rack);
    REQUIRE(ref.has_value());
    CHECK(ref->kickSteps.count() == 4);
    CHECK(ref->hatSteps.count() == 4);
    CHECK(ref->hatSteps.test(2));
}

TEST_CASE("hats that are clearly louder are accents", "[drum-reference]") {
    std::vector<MidiClipNote> notes = {note(36, 0)};
    for (uint32_t step = 0; step < 16; ++step) {
        notes.push_back(note(42, step * 240, step == 6 ? 120 : 80));
    }
    const auto ref = deriveRhythmReference(notes, 1);
    REQUIRE(ref.has_value());
    CHECK(ref->accentSteps.count() == 1);
    CHECK(ref->accentSteps.test(6));
}

TEST_CASE("a reference sets the kick grid and the groove and the notes adapt", "[drum-reference]") {
    const auto style = shipped("peak_time");
    GenerationRequest request;
    request.seed = 9;
    auto generated = generatePattern(style, request);
    REQUIRE(generated.success);
    Pattern pattern = generated.pattern;
    // a kick that the generated bass does not know: only on the "and" of each beat
    std::vector<MidiClipNote> kick;
    for (uint32_t i = 0; i < 16; ++i) {
        kick.push_back(note(36, i * 960 + 480));
    }
    const auto ref = deriveRhythmReference(kick, 4);
    REQUIRE(ref.has_value());

    applyRhythmReference(pattern, *ref, style);
    CHECK(validatePattern(pattern).empty());
    CHECK(pattern.rhythmRef == ref);
    CHECK(pattern.kickGridId == "custom");
    for (const Track& track : pattern.voices) {
        CHECK(std::string(track.groove.templateId) == kGrooveDrumReference);
        CHECK(track.groove.amount == 1.0f);
    }
    const auto ticks = kickTicks(pattern);
    CHECK(ticks.size() == 16); // the grid of the pattern is the one of the clip
    CHECK(ticks[0] == 480);
    // the bass keeps clear of the new kick (constraint layer, SPEC 4.2)
    for (const Note& n : pattern.voices[0].notes) {
        for (const uint32_t t : ticks) {
            CHECK_FALSE(n.startTick == t);
        }
    }
}

TEST_CASE("a voice with an imported line keeps playing as written", "[drum-reference]") {
    const auto style = shipped("peak_time");
    ImportPlan plan;
    plan.status = ImportStatus::Ok;
    plan.lengthBars = 2;
    for (uint32_t i = 0; i < 8; ++i) {
        MidiClipNote n;
        n.pitch = 45;
        n.startTick = i * 960 + 37;
        n.lengthTicks = 200;
        plan.notes.push_back(n);
    }
    auto pattern = importVoice(style, std::nullopt, 0, plan);
    REQUIRE(pattern.has_value());
    const auto bass = pattern->voices[0];
    const auto ref = deriveRhythmReference(house(2, 40), 2);
    REQUIRE(ref.has_value());

    applyRhythmReference(*pattern, *ref, style);
    CHECK(pattern->voices[0] == bass); // not a note, not a setting
    CHECK(std::string(pattern->voices[1].groove.templateId) == kGrooveDrumReference);
}

TEST_CASE("taking the reference out gives the grid and the groove of the style back", "[drum-reference]") {
    const auto style = shipped("peak_time");
    GenerationRequest request;
    request.seed = 4;
    Pattern pattern = generatePattern(style, request).pattern;
    const auto before = pattern;
    const auto ref = deriveRhythmReference(house(2, 40), 2);
    applyRhythmReference(pattern, *ref, style);
    REQUIRE(pattern.rhythmRef.has_value());
    removeRhythmReference(pattern, style);
    CHECK_FALSE(pattern.rhythmRef.has_value());
    CHECK(pattern.kickGridId == style.kickDefault);
    for (const Track& track : pattern.voices) {
        CHECK(std::string(track.groove.templateId) == kGrooveStraight);
    }
    CHECK(validatePattern(pattern).empty());
    (void)before;
}

TEST_CASE("a start half a 16th away goes to the later step", "[drum-reference]") {
    const auto ref = deriveRhythmReference({note(42, 360), note(42, 119), note(42, 120 + 960)}, 1);
    REQUIRE(ref.has_value());
    CHECK(ref->hatSteps.test(2));
    CHECK(ref->timingOffsetTicks[2] == -120); // 360 is 120 before step 2
    CHECK(ref->hatSteps.test(0));
    CHECK(ref->timingOffsetTicks[0] == 119);
    CHECK(ref->hatSteps.test(5));
    CHECK(ref->timingOffsetTicks[5] == -120);
}

TEST_CASE("the offset and the velocity of a step are the rounded mean of its repeats", "[drum-reference]") {
    // 4 bars: two repeats of two bars. Slot 1 has hats +1 and +2 late (velocity 70 and 71), slot 3 -1 and -2.
    std::vector<MidiClipNote> notes = {note(36, 0),
                                       note(42, 240 + 1, 70),
                                       note(42, 3840 * 2 + 240 + 2, 71),
                                       note(42, 720 - 1, 70),
                                       note(42, 3840 * 2 + 720 - 2, 71)};
    const auto ref = deriveRhythmReference(notes, 4);
    REQUIRE(ref.has_value());
    CHECK(ref->timingOffsetTicks[1] == 2);  // 1.5 rounds away from zero
    CHECK(ref->timingOffsetTicks[3] == -2); // -1.5 as well
    CHECK(ref->velocity[1] == 71);          // 70.5
    CHECK(ref->velocity[3] == 71);
}

TEST_CASE("an open hat is an accent even when it is not louder", "[drum-reference]") {
    std::vector<MidiClipNote> notes = {note(36, 0)};
    for (uint32_t step = 0; step < 16; ++step) {
        notes.push_back(note(step == 6 ? 46 : 42, step * 240, 80));
    }
    const auto ref = deriveRhythmReference(notes, 1);
    REQUIRE(ref.has_value());
    CHECK(ref->accentSteps.count() == 1);
    CHECK(ref->accentSteps.test(6));
}

TEST_CASE("a hat only a little louder than the average is no accent", "[drum-reference]") {
    std::vector<MidiClipNote> notes = {note(36, 0)};
    for (uint32_t step = 0; step < 16; ++step) {
        notes.push_back(note(42, step * 240, step == 6 ? 120 : step == 10 ? 88 : 80)); // mean 83: 88 is 106 %
    }
    const auto ref = deriveRhythmReference(notes, 1);
    REQUIRE(ref.has_value());
    CHECK(ref->accentSteps.test(6));
    CHECK_FALSE(ref->accentSteps.test(10));
    CHECK(ref->accentSteps.count() == 1);
}

TEST_CASE("applying a reference sets the groove of every generated voice to full amount", "[drum-reference]") {
    const auto style = shipped("peak_time");
    GenerationRequest request;
    request.seed = 6;
    Pattern pattern = generatePattern(style, request).pattern;
    for (Track& track : pattern.voices) {
        track.groove.amount = 0.4f;
        track.groove.templateId = kGrooveStraight;
    }
    applyRhythmReference(pattern, *deriveRhythmReference(house(1), 1), style);
    for (const Track& track : pattern.voices) {
        CHECK(track.groove.amount == 1.0f);
        CHECK(std::string(track.groove.templateId) == kGrooveDrumReference);
    }
}
