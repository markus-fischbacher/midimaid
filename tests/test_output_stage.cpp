#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/KickGrid.h"
#include "core/OutputStage.h"
#include "core/PatternValidation.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

constexpr int64_t kPatternEnd = 3840; // one bar

Pattern oneBar() {
    return makeEmptyPattern(1, "peak_time"); // kick grid 4otf: kicks at 0, 960, 1920, 2880
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

/// Settings that leave the kick out of the picture (the bass is treated like a long_tied bass).
OutputSettings noKick() {
    OutputSettings settings;
    settings.ignoresKick = {true, true, true};
    return settings;
}

void setSwing(Pattern& p, size_t voice, float swing, float amount = 1.0f) {
    p.voices[voice].groove.swing = swing;
    p.voices[voice].groove.amount = amount;
}

void useDrumReference(Pattern& p, size_t voice, const RhythmReference& ref) {
    p.rhythmRef = ref;
    p.voices[voice].groove.templateId = kGrooveDrumReference;
}

const std::vector<OutputNote>& bass(const OutputPattern& out) {
    return out.voices[0].notes;
}

const std::vector<OutputNote>& melody(const OutputPattern& out) {
    return out.voices[1].notes;
}

} // namespace

TEST_CASE("swing offsets (golden values, integer arithmetic)", "[core][output]") {
    CHECK(swingOffsetTicks(0.50f, 1.0f) == 0);
    CHECK(swingOffsetTicks(0.52f, 1.0f) == 10);
    CHECK(swingOffsetTicks(0.55f, 1.0f) == 24);
    CHECK(swingOffsetTicks(0.60f, 1.0f) == 48);
    CHECK(swingOffsetTicks(0.66f, 1.0f) == 77);
    CHECK(swingOffsetTicks(0.75f, 1.0f) == 120);
    CHECK(swingOffsetTicks(0.90f, 1.0f) == 120); // limited to 75 %
    CHECK(swingOffsetTicks(0.40f, 1.0f) == 0);   // below 50 % there is no swing
    CHECK(swingOffsetTicks(0.75f, 0.5f) == 60);
    CHECK(swingOffsetTicks(0.75f, 0.0f) == 0);
    CHECK(swingOffsetTicks(0.75f, 2.0f) == 120); // amount limited to 1
}

TEST_CASE("without groove, slide, kick and accent the output equals the pattern", "[core][output]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100, 90);
    add(p, 0, 36, 480, 200, 95);
    add(p, 1, 60, 0, 300, 70);
    add(p, 1, 64, 0, 300, 71);
    const auto out = renderOutput(p, noKick());
    REQUIRE(bass(out).size() == 2);
    CHECK(bass(out)[0] == OutputNote{1, 1, 33, 90, 240, 340, false});
    CHECK(bass(out)[1] == OutputNote{2, 1, 36, 95, 480, 680, false});
    REQUIRE(melody(out).size() == 2);
    CHECK(melody(out)[0] == OutputNote{3, 2, 60, 70, 0, 300, false});
    CHECK(melody(out)[1] == OutputNote{4, 2, 64, 71, 0, 300, false});
    CHECK(out.lengthTicks == 3840);
    CHECK(out.voices[0].role == VoiceRole::Bass);
    CHECK(out.voices[1].channel == 2);
}

TEST_CASE("the input pattern is not changed", "[core][output]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100).slide = true;
    add(p, 0, 36, 480, 100);
    setSwing(p, 0, 0.7f);
    const Pattern before = p;
    renderOutput(p, OutputSettings{});
    CHECK(p == before);
}

TEST_CASE("stage 2: swing delays every second 16th", "[core][output][groove]") {
    Pattern p = oneBar();
    for (uint32_t step = 0; step < 4; ++step) {
        add(p, 0, 33, step * 240, 100);
    }
    setSwing(p, 0, 0.75f);
    const auto out = renderOutput(p, noKick());
    REQUIRE(bass(out).size() == 4);
    CHECK(bass(out)[0].startTick == 0);
    CHECK(bass(out)[1].startTick == 240 + 120);
    CHECK(bass(out)[2].startTick == 480);
    CHECK(bass(out)[3].startTick == 720 + 120);
    CHECK(bass(out)[1].endTick == 360 + 100); // the length stays, the end moves with the start
}

TEST_CASE("stage 2: only notes on a 16th are swung", "[core][output][groove]") {
    Pattern p = oneBar();
    add(p, 0, 33, 160, 80);  // a 16th triplet
    add(p, 0, 33, 250, 80);  // freely placed
    add(p, 0, 33, 720, 100); // step 3: swung
    setSwing(p, 0, 0.75f);
    const auto out = renderOutput(p, noKick());
    REQUIRE(bass(out).size() == 3);
    CHECK(bass(out)[0].startTick == 160);
    CHECK(bass(out)[1].startTick == 250);
    CHECK(bass(out)[2].startTick == 840);
}

TEST_CASE("stage 2: swing is set per voice and scaled by amount", "[core][output][groove]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100);
    add(p, 1, 60, 240, 100);
    setSwing(p, 0, 0.5f);
    setSwing(p, 1, 0.75f, 0.5f);
    const auto out = renderOutput(p, noKick());
    CHECK(bass(out)[0].startTick == 240);
    CHECK(melody(out)[0].startTick == 240 + 60);
}

TEST_CASE("stage 2: the drum reference replaces the swing control", "[core][output][groove]") {
    Pattern p = oneBar();
    add(p, 1, 60, 0, 100);
    add(p, 1, 60, 240, 100);
    add(p, 1, 60, 480, 100);
    RhythmReference ref;
    ref.bars = 1;
    ref.timingOffsetTicks[0] = 20;
    ref.timingOffsetTicks[1] = -30;
    ref.timingOffsetTicks[2] = 0;
    useDrumReference(p, 1, ref);
    setSwing(p, 1, 0.75f); // ignored: the template brings its own swing
    const auto out = renderOutput(p, noKick());
    REQUIRE(melody(out).size() == 3);
    CHECK(melody(out)[0].startTick == 20);
    CHECK(melody(out)[1].startTick == 210);
    CHECK(melody(out)[2].startTick == 480);
}

TEST_CASE("stage 2: the drum reference repeats and scales with amount", "[core][output][groove]") {
    Pattern p = makeEmptyPattern(2, "x");
    add(p, 1, 60, 0, 100);
    add(p, 1, 60, 3840, 100); // bar 2, step 0: uses the same reference step again
    RhythmReference ref;
    ref.bars = 1;
    ref.timingOffsetTicks[0] = 40;
    useDrumReference(p, 1, ref);
    p.voices[1].groove.amount = 0.5f;
    const auto out = renderOutput(p, noKick());
    CHECK(melody(out)[0].startTick == 20);
    CHECK(melody(out)[1].startTick == 3840 + 20);
}

TEST_CASE("stage 2: velocity profile of the drum reference", "[core][output][groove]") {
    auto velocities = [](float amount) {
        Pattern p = oneBar();
        for (uint32_t step = 0; step < 4; ++step) {
            add(p, 1, 60, step * 240, 100, 80);
        }
        RhythmReference ref;
        ref.bars = 1;
        ref.velocity[0] = 100;
        ref.velocity[1] = 50;
        ref.velocity[2] = 0; // no hat on this step
        ref.velocity[3] = 150;
        useDrumReference(p, 1, ref);
        p.voices[1].groove.amount = amount;
        const auto out = renderOutput(p, noKick());
        std::vector<int> result;
        for (const auto& note : melody(out)) {
            result.push_back(note.velocity);
        }
        return result;
    };
    // mean of the non-zero entries is 100: factors 1.0, 0.5, (none), 1.5
    CHECK(velocities(1.0f) == std::vector<int>{80, 40, 80, 120});
    CHECK(velocities(0.5f) == std::vector<int>{80, 60, 80, 100});
    CHECK(velocities(0.0f) == std::vector<int>{80, 80, 80, 80});
}

TEST_CASE("stage 2: limits keep every note inside the pattern", "[core][output][groove]") {
    Pattern p = oneBar();
    add(p, 1, 60, 0, 200);
    add(p, 1, 62, 3600, 200);
    add(p, 1, 64, 3360, 200);
    RhythmReference ref;
    ref.bars = 1;
    ref.timingOffsetTicks[0] = -50;
    ref.timingOffsetTicks[15] = 5000; // pushed to the very end: one tick would remain, the note is dropped
    ref.timingOffsetTicks[14] = 100;
    useDrumReference(p, 1, ref);
    const auto out = renderOutput(p, noKick());
    REQUIRE(melody(out).size() == 2);
    CHECK(melody(out)[0].startTick == 0); // not before the pattern
    CHECK(melody(out)[1].startTick == 3460);
    CHECK(melody(out)[1].endTick == 3660);
    for (const auto& note : melody(out)) {
        CHECK(note.endTick <= 3840);
    }
}

TEST_CASE("a drum reference template without a reference falls back to the swing control", "[core][output][groove]") {
    Pattern p = oneBar();
    add(p, 1, 60, 240, 100);
    p.voices[1].groove.templateId = kGrooveDrumReference;
    setSwing(p, 1, 0.75f);
    const auto out = renderOutput(p, noKick());
    CHECK(melody(out)[0].startTick == 360);
}

TEST_CASE("stage 3: a slide overlaps the next note", "[core][output][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 120).slide = true;
    add(p, 0, 36, 480, 120);
    const auto out = renderOutput(p, noKick());
    CHECK(bass(out)[0].endTick == 480 + 60);
    CHECK(bass(out)[0].slideIntoNext);
    CHECK(bass(out)[1].endTick == 600);
    CHECK_FALSE(bass(out)[1].slideIntoNext);
}

TEST_CASE("stage 3: the overlap is relative to the shifted next note", "[core][output][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100).slide = true; // step 1 -> 360
    add(p, 0, 36, 720, 100);              // step 3 -> 840
    setSwing(p, 0, 0.75f);
    const auto out = renderOutput(p, noKick());
    CHECK(bass(out)[0].startTick == 360);
    CHECK(bass(out)[0].endTick == 840 + 60);
    CHECK(bass(out)[1].startTick == 840);
}

TEST_CASE("stage 3: the slide overlap is configurable and bridges gaps", "[core][output][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 0, 100).slide = true;
    add(p, 0, 36, 1000, 100);
    auto settings = noKick();
    settings.slideOverlapTicks = 30;
    const auto out = renderOutput(p, settings);
    CHECK(bass(out)[0].endTick == 1030);
}

TEST_CASE("stage 3: a slide onto the same pitch is a tie without overlap", "[core][output][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 120).slide = true;
    add(p, 0, 33, 480, 120);
    const auto out = renderOutput(p, noKick());
    CHECK(bass(out)[0].endTick == 480);
    CHECK_FALSE(bass(out)[0].slideIntoNext);
}

TEST_CASE("stage 3: the last slide glides into the first note of the next pass", "[core][output][slide]") {
    Pattern p = oneBar();
    add(p, 0, 36, 480, 240);
    add(p, 0, 33, 3360, 240).slide = true;
    const auto out = renderOutput(p, noKick());
    CHECK(bass(out)[1].endTick == 3840 + 480 + 60);
    CHECK(bass(out)[1].slideIntoNext);

    Pattern same = oneBar(); // equal pitch: no overlap
    add(same, 0, 33, 480, 240);
    add(same, 0, 33, 3360, 240).slide = true;
    const auto tied = renderOutput(same, noKick());
    CHECK(bass(tied)[1].endTick == 3840 + 480);
    CHECK_FALSE(bass(tied)[1].slideIntoNext);

    Pattern single = oneBar(); // the only note slides into itself
    add(single, 0, 33, 480, 240).slide = true;
    CHECK(bass(renderOutput(single, noKick()))[0].endTick == 3840 + 480);
}

TEST_CASE("stage 3: a slide loses its overlap if the groove moved the target in front of it", "[core][output][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 720, 100).slide = true; // step 3
    add(p, 0, 36, 960, 100);              // step 4, moved 300 ticks earlier
    RhythmReference ref;
    ref.bars = 1;
    ref.timingOffsetTicks[4] = -300;
    useDrumReference(p, 0, ref);
    const auto out = renderOutput(p, noKick());
    REQUIRE(bass(out).size() == 2);
    const auto& a = bass(out)[1]; // sorted by start: the moved note comes first
    CHECK(a.noteId == 1);
    CHECK(a.startTick == 720);
    CHECK(a.endTick == 820);
    CHECK_FALSE(a.slideIntoNext);
}

TEST_CASE("stage 3: without a slide a note never runs into the next one of its line", "[core][output][slide]") {
    Pattern bassLine = oneBar();
    add(bassLine, 0, 33, 0, 600);
    add(bassLine, 0, 36, 240, 100);
    CHECK(bass(renderOutput(bassLine, noKick()))[0].endTick == 240); // the bass is monophonic

    Pattern melodyLine = oneBar();
    add(melodyLine, 1, 60, 0, 600);
    add(melodyLine, 1, 64, 240, 100); // a chord tone may overlap
    add(melodyLine, 1, 60, 240, 100); // the same pitch may not
    const auto out = renderOutput(melodyLine, noKick());
    for (const auto& note : melody(out)) {
        if (note.noteId == 1) {
            CHECK(note.endTick == 240);
        }
        if (note.noteId == 2) {
            CHECK(note.endTick == 340);
        }
    }

    Pattern swung = oneBar(); // the swing delays a note into its successor
    add(swung, 0, 33, 240, 240);
    add(swung, 0, 36, 480, 100);
    setSwing(swung, 0, 0.75f);
    const auto swungOut = renderOutput(swung, noKick());
    CHECK(bass(swungOut)[0].startTick == 360);
    CHECK(bass(swungOut)[0].endTick == 480);
}

TEST_CASE("stage 4: the kick clearance is checked after the groove", "[core][output][kick]") {
    Pattern p = oneBar();
    add(p, 0, 33, 720, 120); // ends at 840 = 960 - 120: fits without groove
    const auto plain = renderOutput(p, OutputSettings{});
    CHECK(bass(plain)[0].endTick == 840);

    setSwing(p, 0, 0.75f); // step 3 moves to 840: nothing is left before the kick
    const auto swung = renderOutput(p, OutputSettings{});
    CHECK(bass(swung).empty());
}

TEST_CASE("stage 4: note ends are shortened before the next kick", "[core][output][kick]") {
    Pattern p = oneBar();
    add(p, 0, 33, 720, 240);  // would end on the kick at 960
    add(p, 0, 33, 1200, 500); // ends at 1700 < 1800: untouched
    add(p, 0, 33, 3360, 480); // ends at the pattern end, where the next pass starts with a kick
    const auto out = renderOutput(p, OutputSettings{});
    REQUIRE(bass(out).size() == 3);
    CHECK(bass(out)[0].endTick == 840);
    CHECK(bass(out)[1].endTick == 1700);
    CHECK(bass(out)[2].endTick == 3720);
}

TEST_CASE("stage 4: clearance settings", "[core][output][kick]") {
    auto endWith = [](uint32_t clearance) {
        Pattern p = oneBar();
        add(p, 0, 33, 480, 480);
        OutputSettings settings;
        settings.kickClearanceTicks = clearance;
        return bass(renderOutput(p, settings))[0].endTick;
    };
    CHECK(endWith(0) == 960);
    CHECK(endWith(60) == 900);
    CHECK(endWith(120) == 840);
    CHECK(endWith(240) == 720);
}

TEST_CASE("stage 4: only the bass, and not long_tied", "[core][output][kick]") {
    Pattern p = oneBar();
    add(p, 0, 33, 720, 240);
    add(p, 1, 60, 720, 240);
    const auto out = renderOutput(p, OutputSettings{});
    CHECK(bass(out)[0].endTick == 840);
    CHECK(melody(out)[0].endTick == 960); // the melody does not avoid kicks

    OutputSettings longTied;
    longTied.ignoresKick = {true};
    CHECK(bass(renderOutput(p, longTied))[0].endTick == 960);
}

TEST_CASE("stage 4: notes with less than the minimum left are dropped", "[core][output][kick]") {
    Pattern exact = oneBar();
    add(exact, 0, 33, 780, 100); // limit 840: exactly the minimum of 60 ticks are left
    const auto kept = renderOutput(exact, OutputSettings{});
    REQUIRE(bass(kept).size() == 1);
    CHECK(bass(kept)[0].endTick == 840);

    Pattern tooShort = oneBar();
    add(tooShort, 0, 36, 790, 100); // only 50 ticks are left
    CHECK(bass(renderOutput(tooShort, OutputSettings{})).empty());
}

TEST_CASE("stage 4: the kick clearance beats a slide that the groove pushes over a kick",
          "[core][output][kick][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100).slide = true; // step 1
    add(p, 0, 36, 720, 100);              // step 3, the drum reference moves it 180 ticks later: 900 + 60 reaches 960
    RhythmReference ref;
    ref.bars = 1;
    ref.timingOffsetTicks[3] = 180;
    useDrumReference(p, 0, ref);
    const auto out = renderOutput(p, OutputSettings{});
    REQUIRE(bass(out).size() == 1); // the target now starts inside the clearance and is dropped
    CHECK(bass(out)[0].noteId == 1);
    CHECK(bass(out)[0].endTick == 340); // the slide is gone, the note ends like a plain note
    CHECK_FALSE(bass(out)[0].slideIntoNext);
}

TEST_CASE("stage 4: a slide that does not reach a kick stays", "[core][output][kick][slide]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100).slide = true; // step 1 -> 288 with 60 % swing
    add(p, 0, 36, 720, 100);              // step 3 -> 768, ends at 868 and is shortened to 840 by the clearance
    setSwing(p, 0, 0.6f);
    const auto out = renderOutput(p, OutputSettings{});
    REQUIRE(bass(out).size() == 2);
    CHECK(bass(out)[0].startTick == 288);
    CHECK(bass(out)[0].slideIntoNext);
    CHECK(bass(out)[0].endTick == 768 + 60);
    CHECK(bass(out)[1].startTick == 768);
    CHECK(bass(out)[1].endTick == 840);
}

TEST_CASE("stage 4: a slide reaching exactly onto a kick loses its overlap", "[core][output][kick][slide]") {
    auto slideKept = [](uint32_t targetStart) {
        Pattern p = oneBar();
        add(p, 0, 33, 300, 100).slide = true;
        add(p, 0, 36, targetStart, 100);
        OutputSettings settings;
        settings.kickClearanceTicks = 0; // otherwise the target would be dropped in the clearance zone
        return bass(renderOutput(p, settings))[0].slideIntoNext;
    };
    CHECK_FALSE(slideKept(900)); // 900 + 60 = 960 = the kick
    CHECK(slideKept(899));       // 899 + 60 = 959
}

TEST_CASE("stage 4: the last slide is checked against the kick of the next pass", "[core][output][kick][slide]") {
    Pattern p = oneBar();
    add(p, 0, 36, 480, 100);
    add(p, 0, 33, 3360, 100).slide = true; // the way into the next pass crosses the kick at the pattern end
    const auto out = renderOutput(p, OutputSettings{});
    CHECK_FALSE(bass(out)[1].slideIntoNext);
    CHECK(bass(out)[1].endTick == 3460);

    OutputSettings longTied;
    longTied.ignoresKick = {true};
    const auto kept = renderOutput(p, longTied);
    CHECK(bass(kept)[1].slideIntoNext);
}

TEST_CASE("stage 4: a phrase can switch the kick grid", "[core][output][kick]") {
    Pattern p = makeEmptyPattern(8, "x");
    p.phrases = {makePhrase(0, 4, PhraseRole::Main), makePhrase(4, 4, PhraseRole::Breakdown)};
    p.phrases[1].kickGridId = "halftime"; // kicks only on step 0 and 8
    add(p, 0, 33, 4 * 3840 + 720, 240);   // bar 5: no kick at 960 any more
    add(p, 0, 33, 720, 240);              // bar 1: the kick at 960 applies
    const auto out = renderOutput(p, OutputSettings{});
    REQUIRE(bass(out).size() == 2);
    CHECK(bass(out)[0].endTick == 840);
    CHECK(bass(out)[1].endTick == 4 * 3840 + 960);
}

TEST_CASE("stage 5: accents get the accent velocity", "[core][output][accent]") {
    Pattern p = oneBar();
    add(p, 0, 33, 240, 100, 90).accent = true;
    add(p, 0, 36, 480, 100, 90);
    OutputSettings settings = noKick();
    CHECK(bass(renderOutput(p, settings))[0].velocity == 124);
    CHECK(bass(renderOutput(p, settings))[1].velocity == 90);
    settings.accentVelocity = 110;
    CHECK(bass(renderOutput(p, settings))[0].velocity == 110);
}

TEST_CASE("stage 5: the accent comes after the velocity profile", "[core][output][accent]") {
    Pattern p = oneBar();
    add(p, 1, 60, 0, 100, 80).accent = true;
    add(p, 1, 62, 240, 100, 80);
    RhythmReference ref;
    ref.bars = 1;
    ref.velocity[0] = 150;
    ref.velocity[1] = 50;
    useDrumReference(p, 1, ref);
    const auto out = renderOutput(p, noKick());
    CHECK(melody(out)[0].velocity == 124); // not scaled by the profile
    CHECK(melody(out)[1].velocity == 40);  // 80 * (50 / 100)
}

TEST_CASE("all stage combinations", "[core][output][combinations]") {
    enum class Groove { None, Swing, DrumReference };
    enum class SlideCase { None, ToOtherPitch, ToSamePitch, IntoNextPass };
    for (const Groove groove : {Groove::None, Groove::Swing, Groove::DrumReference}) {
        for (const SlideCase slide :
             {SlideCase::None, SlideCase::ToOtherPitch, SlideCase::ToSamePitch, SlideCase::IntoNextPass}) {
            for (const bool longTied : {false, true}) {
                for (const bool accent : {false, true}) {
                    Pattern p = oneBar();
                    // a bass line of 16ths on the off-beats and a sliding note
                    add(p, 0, 33, 240, 100, 90).accent = accent;
                    add(p, 0, 33, 720, 100, 90);
                    add(p, 0, slide == SlideCase::ToSamePitch ? 33 : 36, 1200, 100, 90);
                    Note& sliding = add(p, 0, 33, slide == SlideCase::IntoNextPass ? 3360 : 1440, 100, 90);
                    sliding.slide = slide != SlideCase::None;
                    add(p, 1, 60, 0, 100, 80);
                    add(p, 1, 62, 240, 100, 80);
                    if (groove == Groove::Swing) {
                        setSwing(p, 0, 0.6f);
                        setSwing(p, 1, 0.6f);
                    } else if (groove == Groove::DrumReference) {
                        RhythmReference ref;
                        ref.bars = 1;
                        for (size_t i = 0; i < 16; ++i) {
                            ref.timingOffsetTicks[i] = static_cast<int16_t>(static_cast<int>(i % 5) * 10 - 20);
                            ref.velocity[i] = static_cast<uint8_t>(60 + i * 4);
                        }
                        useDrumReference(p, 0, ref);
                        p.voices[1].groove.templateId = kGrooveDrumReference;
                    }
                    OutputSettings settings;
                    settings.ignoresKick = {longTied};

                    INFO("groove " << static_cast<int>(groove) << " slide " << static_cast<int>(slide) << " longTied "
                                   << longTied << " accent " << accent);
                    const Pattern before = p;
                    const auto out = renderOutput(p, settings);
                    REQUIRE(p == before);
                    REQUIRE(renderOutput(p, settings) == out);

                    const auto kicks = kickTicks(p);
                    for (const OutputVoice& voice : out.voices) {
                        for (size_t i = 0; i < voice.notes.size(); ++i) {
                            const OutputNote& note = voice.notes[i];
                            REQUIRE(note.startTick >= 0);
                            REQUIRE(note.startTick < kPatternEnd);
                            REQUIRE(note.endTick - note.startTick >= 60);
                            REQUIRE(note.velocity >= 1);
                            REQUIRE(note.velocity <= 127);
                            if (i > 0) {
                                REQUIRE(voice.notes[i - 1].startTick <= note.startTick);
                            }
                            if (voice.role == VoiceRole::Bass && !longTied && !note.slideIntoNext) {
                                const auto next =
                                    std::upper_bound(kicks.begin(), kicks.end(), static_cast<uint32_t>(note.startTick));
                                const int64_t nextKick = next != kicks.end() ? *next : kicks.front() + kPatternEnd;
                                REQUIRE(note.endTick <= nextKick - 120);
                            }
                            if (note.slideIntoNext) {
                                REQUIRE((slide == SlideCase::ToOtherPitch || slide == SlideCase::IntoNextPass));
                                REQUIRE(note.noteId == 4);
                            }
                            if (note.noteId == 1 && accent) {
                                REQUIRE(note.velocity == 124);
                            }
                        }
                    }
                    if (slide == SlideCase::ToSamePitch || slide == SlideCase::None) {
                        for (const OutputNote& note : out.voices[0].notes) {
                            REQUIRE_FALSE(note.slideIntoNext);
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Properties over random patterns that went through the constraint layer

namespace {

Pattern randomConstrainedPattern(Pcg32& rng, ConstraintSettings& constraints) {
    Pattern pattern = randomPattern(rng);
    for (Track& track : pattern.voices) {
        track.lock = {};
        for (Note& note : track.notes) {
            note.lock = {};
        }
        static const char* templates[] = {"", kGrooveStraight, kGrooveDrumReference};
        track.groove.templateId = templates[rng.bounded(3)];
    }
    if (pattern.rhythmRef.has_value()) { // moderate offsets keep the order of the notes
        for (auto& offset : pattern.rhythmRef->timingOffsetTicks) {
            offset = static_cast<int16_t>(rng.range(-100, 100));
        }
    } else if (rng.chance(50)) {
        RhythmReference ref;
        ref.bars = 1;
        for (size_t i = 0; i < 16; ++i) {
            ref.timingOffsetTicks[i] = static_cast<int16_t>(rng.range(-100, 100));
            ref.velocity[i] = static_cast<uint8_t>(rng.bounded(128));
        }
        pattern.rhythmRef = ref;
    }
    static const char* grids[] = {"4otf", "4otf_pickup", "halftime", "broken_a", "broken_b"};
    pattern.kickGridId = grids[rng.bounded(5)];
    for (Phrase& phrase : pattern.phrases) {
        phrase.kickGridId = std::nullopt;
    }
    constraints = ConstraintSettings::defaultsFor(pattern);
    for (auto& voice : constraints.voices) {
        voice.ignoresKick = rng.chance(25);
    }
    applyConstraints(pattern, constraints);
    return pattern;
}

} // namespace

TEST_CASE("output invariants over random patterns", "[core][output][property]") {
    for (uint64_t seed = 5000; seed < 5600; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        ConstraintSettings constraints;
        const Pattern pattern = randomConstrainedPattern(rng, constraints);
        OutputSettings settings;
        static const uint32_t overlaps[] = {0, 60, 120};
        static const uint32_t clearances[] = {0, 60, 120, 240};
        settings.slideOverlapTicks = overlaps[rng.bounded(3)];
        settings.kickClearanceTicks = clearances[rng.bounded(4)];
        settings.accentVelocity = static_cast<uint8_t>(100 + rng.bounded(28));
        for (const auto& voice : constraints.voices) {
            settings.ignoresKick.push_back(voice.ignoresKick);
        }
        INFO("seed " << seed);

        const Pattern before = pattern;
        const auto out = renderOutput(pattern, settings);
        REQUIRE(pattern == before);
        REQUIRE(renderOutput(pattern, settings) == out);
        REQUIRE(out.voices.size() == pattern.voices.size());

        const int64_t end = static_cast<int64_t>(pattern.lengthBars) * 3840;
        const auto kicks = kickTicks(pattern);
        std::map<uint32_t, const Note*> sources;
        for (const Track& track : pattern.voices) {
            for (const Note& note : track.notes) {
                sources[note.id] = &note;
            }
        }
        for (size_t v = 0; v < out.voices.size(); ++v) {
            const OutputVoice& voice = out.voices[v];
            const bool kickRules = voice.role == VoiceRole::Bass && !settings.ignoresKick[v];
            REQUIRE(voice.channel == pattern.voices[v].midiChannel);
            for (size_t i = 0; i < voice.notes.size(); ++i) {
                const OutputNote& note = voice.notes[i];
                const Note& source = *sources.at(note.noteId);
                REQUIRE(note.startTick >= 0);
                REQUIRE(note.startTick < end);
                REQUIRE(note.endTick - note.startTick >= 60);
                REQUIRE(note.velocity >= 1);
                REQUIRE(note.velocity <= 127);
                REQUIRE(note.pitch == source.pitch);
                REQUIRE(note.channel == voice.channel);
                if (source.accent) {
                    REQUIRE(note.velocity == settings.accentVelocity);
                }
                if (i > 0) {
                    const OutputNote& previous = voice.notes[i - 1];
                    REQUIRE(previous.startTick <= note.startTick);
                    if (previous.startTick == note.startTick) {
                        REQUIRE(previous.pitch <= note.pitch);
                    }
                }
                // equal pitches never overlap, also not across the loop
                for (size_t j = 0; j < voice.notes.size(); ++j) {
                    const OutputNote& other = voice.notes[j];
                    if (j != i && other.pitch == note.pitch && other.startTick > note.startTick) {
                        REQUIRE(note.endTick <= other.startTick);
                    }
                    if (j != i && other.pitch == note.pitch && other.startTick < note.startTick) {
                        REQUIRE(other.endTick <= note.startTick);
                    }
                }
                const OutputNote& next = voice.notes[(i + 1) % voice.notes.size()];
                const bool wraps = i + 1 == voice.notes.size();
                const int64_t nextStart = static_cast<int64_t>(next.startTick) + (wraps ? end : 0);
                if (note.slideIntoNext) {
                    REQUIRE(note.pitch != next.pitch);
                    REQUIRE(note.endTick == nextStart + settings.slideOverlapTicks);
                    if (kickRules) {
                        for (int pass = 0; pass < 2; ++pass) {
                            for (const uint32_t kick : kicks) {
                                const int64_t tick = static_cast<int64_t>(kick) + pass * end;
                                REQUIRE_FALSE((tick > note.startTick && tick <= note.endTick));
                            }
                        }
                    }
                } else if (note.endTick > end) {
                    // only a tie of the last note onto the same pitch of the first note reaches into the next pass
                    REQUIRE(wraps);
                    REQUIRE(next.pitch == note.pitch);
                    REQUIRE(note.endTick == nextStart);
                } else {
                    if (voice.role == VoiceRole::Bass && !wraps) {
                        REQUIRE(note.endTick <= next.startTick);
                    }
                    if (kickRules && settings.kickClearanceTicks > 0 && !kicks.empty()) {
                        const auto upper =
                            std::upper_bound(kicks.begin(), kicks.end(), static_cast<uint32_t>(note.startTick));
                        const int64_t nextKick = upper != kicks.end() ? *upper : kicks.front() + end;
                        const int64_t tie = (note.endTick > end) ? 0 : 1;
                        REQUIRE(tie == 1);
                        REQUIRE(note.endTick <= nextKick - static_cast<int64_t>(settings.kickClearanceTicks));
                    }
                }
            }
        }
    }
}

TEST_CASE("a straight pattern without slides or accents passes through unchanged", "[core][output][property]") {
    for (uint64_t seed = 6000; seed < 6200; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        ConstraintSettings constraints;
        Pattern pattern = randomConstrainedPattern(rng, constraints);
        for (Track& track : pattern.voices) {
            track.groove = {};
            for (Note& note : track.notes) {
                note.slide = false;
                note.accent = false;
            }
        }
        OutputSettings settings = noKick();
        settings.ignoresKick.assign(pattern.voices.size(), true);
        settings.kickClearanceTicks = 0;
        const auto out = renderOutput(pattern, settings);
        INFO("seed " << seed);
        for (size_t v = 0; v < pattern.voices.size(); ++v) {
            REQUIRE(out.voices[v].notes.size() == pattern.voices[v].notes.size());
            for (const OutputNote& note : out.voices[v].notes) {
                const auto it = std::find_if(pattern.voices[v].notes.begin(), pattern.voices[v].notes.end(),
                                             [&](const Note& n) { return n.id == note.noteId; });
                REQUIRE(it != pattern.voices[v].notes.end());
                REQUIRE(note.startTick == static_cast<int32_t>(it->startTick));
                REQUIRE(note.endTick == static_cast<int32_t>(it->startTick + it->lengthTicks));
                REQUIRE(note.velocity == it->velocity);
            }
        }
    }
}
