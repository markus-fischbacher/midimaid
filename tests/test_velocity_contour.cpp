#include "PatternFixtures.h"
#include "core/OutputStage.h"
#include "core/Random.h"
#include "core/VelocityContour.h"

#include <catch2/catch_test_macros.hpp>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

VelocityContour rampContour() {
    VelocityContour contour;
    for (size_t i = 0; i < kContourSteps; ++i) {
        contour.steps[i] = static_cast<uint8_t>(80 + i); // 80...95
    }
    return contour;
}

Note& add(Pattern& p, size_t voice, uint32_t start, uint8_t velocity = 100) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = 45;
    note.startTick = start;
    note.lengthTicks = 120;
    note.velocity = velocity;
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

} // namespace

TEST_CASE("contour: default is flat 100 and valid", "[velocity]") {
    const VelocityContour contour;
    for (size_t i = 0; i < kContourSteps; ++i) {
        CHECK(contour.steps[i] == 100);
    }
    CHECK(isValidContour(contour));
}

TEST_CASE("contour: validation rejects 0 and 128", "[velocity]") {
    VelocityContour contour = rampContour();
    CHECK(isValidContour(contour));
    contour.steps[15] = 0;
    CHECK_FALSE(isValidContour(contour));
    contour.steps[15] = 128;
    CHECK_FALSE(isValidContour(contour));
    contour.steps[15] = 127;
    contour.steps[0] = 1;
    CHECK(isValidContour(contour));
}

TEST_CASE("contour: velocityAt wraps bar by bar", "[velocity]") {
    const VelocityContour contour = rampContour();
    CHECK(contour.velocityAt(0) == 80);
    CHECK(contour.velocityAt(15) == 95);
    CHECK(contour.velocityAt(16) == 80);
    CHECK(contour.velocityAt(35) == 83); // 35 % 16 = 3
}

TEST_CASE("contour: velocityAtTick takes the nearest step, halfway goes to the later one", "[velocity]") {
    const VelocityContour contour = rampContour();
    CHECK(contour.velocityAtTick(0) == 80);
    CHECK(contour.velocityAtTick(119) == 80);
    CHECK(contour.velocityAtTick(120) == 81);
    CHECK(contour.velocityAtTick(240) == 81);
    CHECK(contour.velocityAtTick(359) == 81);
    CHECK(contour.velocityAtTick(360) == 82);
    // last step rounds up into the next bar and wraps to step 0
    CHECK(contour.velocityAtTick(kTicksPerBar - 120) == 80);
    CHECK(contour.velocityAtTick(kTicksPerBar - 121) == 95);
    CHECK(contour.velocityAtTick(kTicksPerBar + 240) == 81);
}

TEST_CASE("energy: 0.5 leaves the contour unchanged, 0 and 1 shift by 12", "[velocity]") {
    const VelocityContour contour = rampContour();
    CHECK(applyEnergy(contour, 500) == contour);
    const VelocityContour low = applyEnergy(contour, 0);
    const VelocityContour high = applyEnergy(contour, 1000);
    for (size_t i = 0; i < kContourSteps; ++i) {
        CHECK(low.steps[i] == contour.steps[i] - 12);
        CHECK(high.steps[i] == contour.steps[i] + 12);
    }
}

TEST_CASE("energy: hand-computed shifts with rounding half away from zero", "[velocity]") {
    struct Case {
        uint16_t energy;
        int shift;
    };
    const Case cases[] = {{0, -12}, {250, -6}, {417, -2}, {458, -1}, {479, -1}, {480, 0},
                          {500, 0}, {520, 0},  {521, 1},  {542, 1},  {750, 6},  {1000, 12}};
    for (const Case& c : cases) {
        const VelocityContour result = applyEnergy(VelocityContour{}, c.energy);
        INFO("energy " << c.energy);
        CHECK(result.steps[0] == 100 + c.shift);
    }
}

TEST_CASE("energy: monotone, symmetric and shape preserving", "[velocity]") {
    const VelocityContour contour = rampContour();
    int previous = -1000;
    for (uint16_t e = 0; e <= 1000; ++e) {
        const VelocityContour result = applyEnergy(contour, e);
        const int shift = int(result.steps[0]) - int(contour.steps[0]);
        CHECK(shift >= previous);
        previous = shift;
        // symmetric around 500
        const int mirror = int(applyEnergy(contour, static_cast<uint16_t>(1000 - e)).steps[0]) - int(contour.steps[0]);
        CHECK(shift == -mirror);
        // same shift for every step: differences between steps stay
        for (size_t i = 1; i < kContourSteps; ++i) {
            CHECK(int(result.steps[i]) - int(result.steps[i - 1]) == 1);
        }
    }
}

TEST_CASE("energy: results are limited to 1-127, values above 1000 count as 1000", "[velocity]") {
    VelocityContour extreme;
    extreme.steps.fill(125);
    extreme.steps[0] = 3;
    const VelocityContour high = applyEnergy(extreme, 1000);
    CHECK(high.steps[1] == 127);
    CHECK(high.steps[0] == 15);
    const VelocityContour low = applyEnergy(extreme, 0);
    CHECK(low.steps[0] == 1);
    CHECK(low.steps[1] == 113);
    CHECK(applyEnergy(extreme, 5000) == high);
    CHECK(applyEnergy(extreme, 65535) == high);
    CHECK(isValidContour(high));
    CHECK(isValidContour(low));
}

TEST_CASE("applyContour: sets velocity from the start step and counts changes", "[velocity]") {
    Pattern p = makeEmptyPattern(2, "peak_time");
    add(p, 0, 0);
    add(p, 0, 2 * kTicksPerStep);
    add(p, 0, kTicksPerBar + 3 * kTicksPerStep, 82); // bar 2, step 3 -> 83
    add(p, 0, kTicksPerStep + 130);                  // between steps 1 and 2, nearer to 2
    const VelocityContour contour = rampContour();
    CHECK(applyContour(p.voices[0], contour) == 4);
    CHECK(p.voices[0].notes[0].velocity == 80);
    CHECK(p.voices[0].notes[1].velocity == 82);
    CHECK(p.voices[0].notes[2].velocity == 83);
    CHECK(p.voices[0].notes[3].velocity == 82);
    // second run changes nothing (idempotent)
    CHECK(applyContour(p.voices[0], contour) == 0);
}

TEST_CASE("applyContour: velocity locks stay", "[velocity]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 0, 0, 50).lock.velocity = true;
    add(p, 0, kTicksPerStep, 50);
    CHECK(applyContour(p.voices[0], rampContour()) == 1);
    CHECK(p.voices[0].notes[0].velocity == 50);
    CHECK(p.voices[0].notes[1].velocity == 81);

    p.voices[0].lock.velocity = true;
    p.voices[0].notes[1].velocity = 50;
    CHECK(applyContour(p.voices[0], rampContour()) == 0);
    CHECK(p.voices[0].notes[1].velocity == 50);
}

TEST_CASE("applyContour: other lock kinds do not protect the velocity", "[velocity]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    Note& note = add(p, 0, 0, 50);
    note.lock.pitch = true;
    note.lock.rhythm = true;
    p.voices[0].lock.pitch = true;
    p.voices[0].lock.rhythm = true;
    CHECK(applyContour(p.voices[0], rampContour()) == 1);
    CHECK(p.voices[0].notes[0].velocity == 80);
}

TEST_CASE("accent settings: validity", "[velocity]") {
    AccentSettings settings;
    CHECK(settings.accentVelocity == 124);
    CHECK(settings.importThreshold == 110);
    CHECK(settings.isValid());
    settings.importThreshold = 124;
    CHECK(settings.isValid());
    settings.importThreshold = 125;
    CHECK_FALSE(settings.isValid());
    settings.importThreshold = 0;
    CHECK_FALSE(settings.isValid());
    settings.importThreshold = 1;
    CHECK(settings.isValid());
    settings.accentVelocity = 127;
    CHECK(settings.isValid());
    settings.accentVelocity = 128;
    CHECK_FALSE(settings.isValid());
}

TEST_CASE("accent import: threshold is inclusive", "[velocity]") {
    CHECK_FALSE(isAccentVelocity(109, 110));
    CHECK(isAccentVelocity(110, 110));
    CHECK(isAccentVelocity(111, 110));
    CHECK(isAccentVelocity(127, 110));
    CHECK_FALSE(isAccentVelocity(1, 110));

    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 0, 0, 109);
    add(p, 0, kTicksPerStep, 110);
    add(p, 0, 2 * kTicksPerStep, 127);
    CHECK(detectAccents(p.voices[0], 110) == 2);
    CHECK_FALSE(p.voices[0].notes[0].accent);
    CHECK(p.voices[0].notes[0].velocity == 109);
    CHECK(p.voices[0].notes[1].accent);
    CHECK(p.voices[0].notes[1].velocity == 100);
    CHECK(p.voices[0].notes[2].accent);
    CHECK(p.voices[0].notes[2].velocity == 100);
}

TEST_CASE("accent import: custom base velocity and a custom threshold", "[velocity]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 0, 0, 100);
    add(p, 0, kTicksPerStep, 101);
    CHECK(detectAccents(p.voices[0], 101, 90) == 1);
    CHECK(p.voices[0].notes[0].velocity == 100);
    CHECK_FALSE(p.voices[0].notes[0].accent);
    CHECK(p.voices[0].notes[1].velocity == 90);
}

TEST_CASE("output stage: accents get the accent velocity, plain notes keep the contour", "[velocity]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 0, 0);
    add(p, 0, 4 * kTicksPerStep);
    add(p, 0, 8 * kTicksPerStep);
    applyContour(p.voices[0], rampContour());
    p.voices[0].notes[1].accent = true;
    OutputSettings settings;
    settings.kickClearanceTicks = 0;
    const OutputPattern out = renderOutput(p, settings);
    REQUIRE(out.voices[0].notes.size() == 3);
    CHECK(out.voices[0].notes[0].velocity == 80);
    CHECK(out.voices[0].notes[1].velocity == 124);
    CHECK(out.voices[0].notes[2].velocity == 88);
    settings.accentVelocity = 120;
    CHECK(renderOutput(p, settings).voices[0].notes[1].velocity == 120);
}

TEST_CASE("property: contours and energy keep velocities valid on random patterns", "[velocity][property]") {
    Pcg32 rng(2024, 7);
    for (int round = 0; round < 200; ++round) {
        Pattern p = randomPattern(rng);
        VelocityContour contour;
        for (auto& step : contour.steps) {
            step = static_cast<uint8_t>(1 + rng.bounded(127));
        }
        REQUIRE(isValidContour(contour));
        const VelocityContour shaped = applyEnergy(contour, static_cast<uint16_t>(rng.bounded(1100)));
        REQUIRE(isValidContour(shaped));
        Pattern copy = p;
        for (auto& voice : p.voices) {
            applyContour(voice, shaped);
        }
        for (auto& voice : copy.voices) {
            applyContour(voice, shaped);
        }
        CHECK(p == copy); // deterministic
        for (size_t v = 0; v < p.voices.size(); ++v) {
            for (const Note& note : p.voices[v].notes) {
                CHECK(note.velocity >= 1);
                CHECK(note.velocity <= 127);
            }
            // idempotent
            CHECK(applyContour(p.voices[v], shaped) == 0);
        }
    }
}
