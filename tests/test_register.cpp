#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/Random.h"
#include "core/Register.h"
#include "core/Theory.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

Note& add(Pattern& p, size_t voice, int pitch, uint32_t start, uint32_t length) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

std::vector<int> pitchesOf(const Track& track) {
    std::vector<int> pitches;
    for (const Note& note : track.notes) {
        pitches.push_back(note.pitch);
    }
    return pitches;
}

} // namespace

TEST_CASE("default ranges are MIDI numbers (STYLES.md 1.6)", "[core][register]") {
    CHECK(kDefaultBassRange == Range{28, 52});
    CHECK(kDefaultMelodyRange == Range{55, 88});
    CHECK(kDefaultStabRange == Range{55, 79});
    CHECK(kDefaultBassRange.width() == 25);
    CHECK(kDefaultMelodyRange.width() == 34);
    CHECK(kDefaultBassRange.contains(28));
    CHECK(kDefaultBassRange.contains(52));
    CHECK_FALSE(kDefaultBassRange.contains(27));
    CHECK_FALSE(kDefaultBassRange.contains(53));
    const RegisterProfile profile;
    CHECK(profile.bass == kDefaultBassRange);
    CHECK(profile.melody == kDefaultMelodyRange);
}

TEST_CASE("the octave offset moves a range by whole octaves", "[core][register]") {
    CHECK(applyOctaveOffset(kDefaultBassRange, 0) == Range{28, 52});
    CHECK(applyOctaveOffset(kDefaultBassRange, -2) == Range{4, 28});
    CHECK(applyOctaveOffset(kDefaultBassRange, -1) == Range{16, 40});
    CHECK(applyOctaveOffset(kDefaultBassRange, 1) == Range{40, 64});
    CHECK(applyOctaveOffset(kDefaultBassRange, 2) == Range{52, 76});
    CHECK(applyOctaveOffset(kDefaultMelodyRange, -2) == Range{31, 64});
    CHECK(applyOctaveOffset(kDefaultMelodyRange, 2) == Range{79, 112});
    CHECK(applyOctaveOffset(kDefaultStabRange, -2) == Range{31, 55});
    CHECK(applyOctaveOffset(kDefaultStabRange, 2) == Range{79, 103});
}

TEST_CASE("the octave offset is limited to -2...+2 and the range to MIDI 0-127", "[core][register]") {
    CHECK(applyOctaveOffset(kDefaultBassRange, 7) == applyOctaveOffset(kDefaultBassRange, 2));
    CHECK(applyOctaveOffset(kDefaultBassRange, -9) == applyOctaveOffset(kDefaultBassRange, -2));
    CHECK(applyOctaveOffset(Range{0, 20}, -1) == Range{0, 8});
    CHECK(applyOctaveOffset(Range{100, 127}, 1) == Range{112, 127});
    CHECK(applyOctaveOffset(Range{120, 127}, 2) == Range{127, 127});
}

TEST_CASE("stab archetypes are recognised", "[core][register]") {
    CHECK(isVoicingArchetype("stabs"));
    CHECK(isVoicingArchetype("aggro_stabs"));
    CHECK_FALSE(isVoicingArchetype("hypnotic_motif"));
    CHECK_FALSE(isVoicingArchetype("arp"));
    CHECK_FALSE(isVoicingArchetype(""));
}

TEST_CASE("the effective range depends on role and archetype", "[core][register]") {
    const RegisterProfile profile;
    const VoicingSettings voicing; // 55-79
    CHECK(effectiveRange(profile, VoiceRole::Bass, "rolling16", voicing, 0) == Range{28, 52});
    CHECK(effectiveRange(profile, VoiceRole::Bass, "stabs", voicing, 0) == Range{28, 52}); // only melody plays stabs
    CHECK(effectiveRange(profile, VoiceRole::Melody, "hypnotic_motif", voicing, 0) == Range{55, 88});
    CHECK(effectiveRange(profile, VoiceRole::Melody, "", voicing, 0) == Range{55, 88});
    CHECK(effectiveRange(profile, VoiceRole::Melody, "stabs", voicing, 0) == Range{55, 79});
    CHECK(effectiveRange(profile, VoiceRole::Melody, "aggro_stabs", voicing, 1) == Range{67, 91});

    VoicingSettings custom;
    custom.lowNote = 50;
    custom.highNote = 80;
    CHECK(effectiveRange(profile, VoiceRole::Melody, "stabs", custom, -1) == Range{38, 68});
}

TEST_CASE("a profile may narrow the ranges", "[core][register]") {
    RegisterProfile hard;
    hard.bass = {28, 50}; // Hard/Industrial bass (STYLES.md 4)
    CHECK(effectiveRange(hard, VoiceRole::Bass, "rumble", VoicingSettings{}, 0) == Range{28, 50});
    CHECK(effectiveRange(hard, VoiceRole::Bass, "rumble", VoicingSettings{}, -1) == Range{16, 38});
}

TEST_CASE("a range below 12 pitches is refused", "[core][register]") {
    RegisterProfile profile;
    profile.bass = {30, 41}; // exactly 12 pitches
    CHECK(effectiveRange(profile, VoiceRole::Bass, "", VoicingSettings{}, 0).has_value());
    profile.bass = {30, 40}; // 11 pitches
    CHECK_FALSE(effectiveRange(profile, VoiceRole::Bass, "", VoicingSettings{}, 0).has_value());

    VoicingSettings narrow;
    narrow.lowNote = 60;
    narrow.highNote = 65;
    CHECK_FALSE(effectiveRange(RegisterProfile{}, VoiceRole::Melody, "stabs", narrow, 0).has_value());

    // clamping at the top of MIDI can shrink a range below 12 pitches
    RegisterProfile high;
    high.melody = {100, 127};
    CHECK(effectiveRange(high, VoiceRole::Melody, "", VoicingSettings{}, 0).has_value());
    CHECK_FALSE(effectiveRange(high, VoiceRole::Melody, "", VoicingSettings{}, 2).has_value());
}

TEST_CASE("the voice base moves with the octave offset", "[core][register]") {
    for (PitchClass root = 0; root < 12; ++root) {
        const auto base = voiceBase(root, kDefaultBassRange.low, kDefaultBassRange.high);
        REQUIRE(base.has_value());
        for (int offset = -2; offset <= 2; ++offset) {
            const auto range = effectiveRange(RegisterProfile{}, VoiceRole::Bass, "", VoicingSettings{}, offset);
            REQUIRE(range.has_value());
            const auto shifted = voiceBase(root, range->low, range->high);
            REQUIRE(shifted.has_value());
            CHECK(*shifted == *base + 12 * offset);
        }
    }
}

TEST_CASE("constraint settings use the octave offset of every voice", "[core][register][constraints]") {
    Pattern p = makeEmptyPattern(1, "x");
    p.voices[0].octaveOffset = -1;
    p.voices[1].octaveOffset = 2;
    const auto settings = ConstraintSettings::defaultsFor(p);
    REQUIRE(settings.voices.size() == 2);
    CHECK(settings.voices[0].rangeLow == 16);
    CHECK(settings.voices[0].rangeHigh == 40);
    CHECK(settings.voices[1].rangeLow == 79);
    CHECK(settings.voices[1].rangeHigh == 112);
}

TEST_CASE("constraint settings: stabs use the voicing range, profiles narrow the range",
          "[core][register][constraints]") {
    Pattern p = makeEmptyPattern(1, "x");
    p.voices[1].archetypeId = "stabs";
    p.voicing.lowNote = 60;
    p.voicing.highNote = 79;
    RegisterProfile profile;
    profile.bass = {28, 50};
    const auto settings = ConstraintSettings::forPattern(p, profile);
    CHECK(settings.voices[0].rangeHigh == 50);
    CHECK(settings.voices[1].rangeLow == 60);
    CHECK(settings.voices[1].rangeHigh == 79);
}

TEST_CASE("constraint settings fall back to the unshifted range if the effective one is invalid",
          "[core][register][constraints]") {
    Pattern p = makeEmptyPattern(1, "x");
    p.voices[1].archetypeId = "stabs";
    p.voicing.lowNote = 60;
    p.voicing.highNote = 65; // too narrow
    const auto settings = ConstraintSettings::defaultsFor(p);
    CHECK(settings.voices[1].rangeLow == 55);
    CHECK(settings.voices[1].rangeHigh == 88);
}

TEST_CASE("the constraint layer keeps notes inside the shifted range", "[core][register][constraints]") {
    Pattern p = makeEmptyPattern(1, "x");
    p.voices[0].octaveOffset = 1; // bass 40-64
    p.voices[1].octaveOffset = 2; // melody 79-112
    add(p, 0, 33, 240, 120);      // A below 40 -> A in 40-64: 45
    add(p, 1, 60, 240, 120);      // C below 79 -> 84
    applyConstraints(p, ConstraintSettings::defaultsFor(p));
    REQUIRE(p.voices[0].notes.size() == 1);
    CHECK(p.voices[0].notes[0].pitch == 45);
    REQUIRE(p.voices[1].notes.size() == 1);
    CHECK(p.voices[1].notes[0].pitch == 84);
}

TEST_CASE("after the constraints all notes lie in the effective range (random patterns)",
          "[core][register][property]") {
    for (uint64_t seed = 7000; seed < 7300; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        Pattern pattern = randomPattern(rng);
        for (Track& track : pattern.voices) {
            track.lock = {};
            for (Note& note : track.notes) {
                note.lock = {};
            }
            if (rng.chance(30)) {
                track.archetypeId = rng.chance(50) ? "stabs" : "aggro_stabs";
            }
        }
        const auto settings = ConstraintSettings::defaultsFor(pattern);
        applyConstraints(pattern, settings);
        INFO("seed " << seed);
        for (size_t v = 0; v < pattern.voices.size(); ++v) {
            const Track& track = pattern.voices[v];
            const auto range =
                effectiveRange(RegisterProfile{}, track.role, track.archetypeId, pattern.voicing, track.octaveOffset);
            if (range.has_value()) {
                REQUIRE(settings.voices[v].rangeLow == range->low);
                REQUIRE(settings.voices[v].rangeHigh == range->high);
            }
            for (const Note& note : track.notes) {
                REQUIRE(note.pitch >= settings.voices[v].rangeLow);
                REQUIRE(note.pitch <= settings.voices[v].rangeHigh);
            }
        }
    }
}

TEST_CASE("shifting a voice by octaves keeps the pitch classes", "[core][register]") {
    Track track;
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 33, 0, 100);
    add(p, 0, 45, 100, 100);
    track = p.voices[0];
    CHECK(shiftVoiceByOctaves(track, 1) == 2);
    CHECK(pitchesOf(track) == std::vector<int>{45, 57});
    CHECK(shiftVoiceByOctaves(track, -2) == 2);
    CHECK(pitchesOf(track) == std::vector<int>{21, 33});
    CHECK(shiftVoiceByOctaves(track, 0) == 0);
}

TEST_CASE("shifting never leaves MIDI 0-127 and never changes a pitch class", "[core][register]") {
    for (int pitch = 0; pitch <= 127; ++pitch) {
        for (int octaves = -2; octaves <= 2; ++octaves) {
            Track track;
            Note note;
            note.pitch = static_cast<uint8_t>(pitch);
            track.notes.push_back(note);
            shiftVoiceByOctaves(track, octaves);
            const int shifted = track.notes[0].pitch;
            REQUIRE(shifted >= 0);
            REQUIRE(shifted <= 127);
            REQUIRE(pitchClassOf(shifted) == pitchClassOf(pitch));
        }
    }
}

TEST_CASE("shifting respects pitch locks", "[core][register]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 33, 0, 100);
    add(p, 0, 36, 100, 100).lock.pitch = true;
    Track noteLocked = p.voices[0];
    CHECK(shiftVoiceByOctaves(noteLocked, 1) == 1);
    CHECK(pitchesOf(noteLocked) == std::vector<int>{45, 36});

    Track voiceLocked = p.voices[0];
    voiceLocked.lock.pitch = true;
    CHECK(shiftVoiceByOctaves(voiceLocked, 1) == 0);
    CHECK(pitchesOf(voiceLocked) == std::vector<int>{33, 36});

    Track rhythmLocked = p.voices[0]; // other lock dimensions do not matter
    rhythmLocked.lock.rhythm = true;
    CHECK(shiftVoiceByOctaves(rhythmLocked, 1) == 1);
}

TEST_CASE("changing the octave offset moves the notes with it", "[core][register]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 33, 0, 100);
    add(p, 0, 40, 100, 100);
    Track& bass = p.voices[0];
    changeOctaveOffset(bass, 1);
    CHECK(bass.octaveOffset == 1);
    CHECK(pitchesOf(bass) == std::vector<int>{45, 52});
    changeOctaveOffset(bass, -1);
    CHECK(bass.octaveOffset == -1);
    CHECK(pitchesOf(bass) == std::vector<int>{21, 28});
    changeOctaveOffset(bass, 0);
    CHECK(pitchesOf(bass) == std::vector<int>{33, 40}); // back and forth restores the original
    changeOctaveOffset(bass, 0);
    CHECK(pitchesOf(bass) == std::vector<int>{33, 40});
}

TEST_CASE("the octave offset is limited to -2...+2 when changed", "[core][register]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 60, 0, 100);
    changeOctaveOffset(p.voices[0], 9);
    CHECK(p.voices[0].octaveOffset == 2);
    CHECK(p.voices[0].notes[0].pitch == 84);
    changeOctaveOffset(p.voices[0], -9);
    CHECK(p.voices[0].octaveOffset == -2);
    CHECK(p.voices[0].notes[0].pitch == 36);
}

TEST_CASE("changing the offset of a pitch-locked voice only changes the range", "[core][register]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 36, 0, 100);
    p.voices[0].lock.pitch = true;
    changeOctaveOffset(p.voices[0], 1);
    CHECK(p.voices[0].octaveOffset == 1);
    CHECK(p.voices[0].notes[0].pitch == 36);
}
