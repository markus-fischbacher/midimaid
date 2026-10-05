#include "PatternFixtures.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/Groove.h"
#include "core/KickGrid.h"
#include "core/OutputStage.h"
#include "core/Register.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
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

long percent(float swing) {
    return std::lround(swing * 100.0f);
}

RhythmReference referenceWithOddOffsets(uint8_t bars, int16_t offset) {
    RhythmReference ref;
    ref.bars = bars;
    for (size_t i = 1; i < static_cast<size_t>(bars) * 16; i += 2) {
        ref.timingOffsetTicks[i] = offset;
    }
    return ref;
}

} // namespace

TEST_CASE("the swing defaults of the shipped styles stay within STYLES.md 1.15", "[groove]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        INFO(name);
        CHECK(style.bass.swingDefault >= 0.50f);
        CHECK(style.bass.swingDefault <= 0.54f);
        CHECK(style.melody.swingDefault >= 0.50f);
        CHECK(style.melody.swingDefault <= 0.58f);
        CHECK_FALSE(bassSwingMayFlam(VoiceRole::Bass, style.bass.swingDefault));
    }
}

TEST_CASE("applyStyleGroove sets swing, amount and template of every voice by role", "[groove]") {
    const StyleProfile peak = loadShipped("peak_time"); // bass 0.52, melody 0.55
    Pattern p = makeEmptyPattern(2, "peak_time");
    for (Track& track : p.voices) {
        track.groove.swing = 0.7f;
        track.groove.amount = 0.3f;
        track.groove.templateId = kGrooveDrumReference;
    }
    applyStyleGroove(p, peak);
    for (const Track& track : p.voices) {
        const float expected = track.role == VoiceRole::Bass ? 0.52f : 0.55f;
        CHECK(track.groove.swing == expected);
        CHECK(track.groove.amount == 1.0f);
        CHECK(track.groove.templateId == kGrooveStraight);
    }
    REQUIRE(p.voices.size() >= 2);
    CHECK(p.voices[0].role == VoiceRole::Bass);
    CHECK(p.voices[1].role == VoiceRole::Melody);
}

TEST_CASE("without a drum reference the swing control works and shows its own value", "[groove]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    p.voices[0].groove.swing = 0.62f;
    EffectiveGroove groove = effectiveGroove(p, p.voices[0]);
    CHECK(groove.controlActive);
    CHECK(percent(groove.swing) == 62);
    // the template asks for a reference that the pattern does not have: the control keeps working
    p.voices[0].groove.templateId = kGrooveDrumReference;
    groove = effectiveGroove(p, p.voices[0]);
    CHECK(groove.controlActive);
    CHECK(percent(groove.swing) == 62);
    // an empty reference (no bars) is no reference
    p.rhythmRef = referenceWithOddOffsets(0, 120);
    groove = effectiveGroove(p, p.voices[0]);
    CHECK(groove.controlActive);
    CHECK(percent(groove.swing) == 62);
    // a reference without the template is not used either
    p.voices[0].groove.templateId = kGrooveStraight;
    p.rhythmRef = referenceWithOddOffsets(1, 120);
    groove = effectiveGroove(p, p.voices[0]);
    CHECK(groove.controlActive);
    CHECK(percent(groove.swing) == 62);
}

TEST_CASE("a drum reference deactivates the control and shows the swing of the template", "[groove]") {
    const auto shown = [](const RhythmReference& ref, float amount = 1.0f) {
        Pattern p = makeEmptyPattern(2, "peak_time");
        p.rhythmRef = ref;
        p.voices[0].groove.templateId = kGrooveDrumReference;
        p.voices[0].groove.swing = 0.51f; // ignored
        p.voices[0].groove.amount = amount;
        const EffectiveGroove groove = effectiveGroove(p, p.voices[0]);
        CHECK_FALSE(groove.controlActive);
        return percent(groove.swing);
    };
    CHECK(shown(referenceWithOddOffsets(1, 120)) == 75); // 120 * 100 / 480 = 25 points
    CHECK(shown(referenceWithOddOffsets(1, 60)) == 63);  // 12.5 rounds away from zero
    CHECK(shown(referenceWithOddOffsets(1, 48)) == 60);
    CHECK(shown(referenceWithOddOffsets(2, 24)) == 55);
    CHECK(shown(referenceWithOddOffsets(1, 0)) == 50);
    CHECK(shown(referenceWithOddOffsets(1, -40)) == 50);       // laid back before the beat does not swing
    CHECK(shown(referenceWithOddOffsets(1, 240)) == 75);       // capped
    CHECK(shown(referenceWithOddOffsets(1, 120), 0.5f) == 63); // the amount scales the offsets: 60 ticks
    CHECK(shown(referenceWithOddOffsets(1, 120), 0.0f) == 50);

    RhythmReference mixed; // two of eight odd steps carry 48 ticks: mean 12 ticks = 2.5 points
    mixed.bars = 1;
    mixed.timingOffsetTicks[1] = 48;
    mixed.timingOffsetTicks[3] = 48;
    mixed.timingOffsetTicks[2] = 200; // even steps do not count
    CHECK(shown(mixed) == 53);
}

TEST_CASE("the flam hint applies to a swung bass above 56 %", "[groove]") {
    CHECK_FALSE(bassSwingMayFlam(VoiceRole::Bass, 0.50f));
    CHECK_FALSE(bassSwingMayFlam(VoiceRole::Bass, 0.56f));
    CHECK(bassSwingMayFlam(VoiceRole::Bass, 0.57f));
    CHECK(bassSwingMayFlam(VoiceRole::Bass, 0.75f));
    CHECK_FALSE(bassSwingMayFlam(VoiceRole::Melody, 0.75f));
    CHECK_FALSE(bassSwingMayFlam(VoiceRole::Melody, 0.57f));
}

TEST_CASE("without groove in the export the notes keep their grid", "[groove][output]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Pattern p = makeEmptyPattern(2, name);
            Pcg32 rng = Pcg32::fromSeed(seed);
            REQUIRE(generateVoice(p, 0, "rolling16", style, ArchetypeSettings{70, 40}, rng));
            REQUIRE(generateVoice(p, 1, "pluck_seq", style, ArchetypeSettings{70, 40}, rng));
            Pattern swung = p;
            for (Track& track : swung.voices) {
                track.groove.swing = 0.75f;
            }
            swung.rhythmRef = referenceWithOddOffsets(2, 100);
            swung.voices[1].groove.templateId = kGrooveDrumReference;
            OutputSettings off;
            off.includeGroove = false;
            OutputSettings on;
            Pattern straight = p; // default groove: swing 0.5, straight
            INFO(name << " seed " << seed);
            CHECK(renderOutput(swung, off) == renderOutput(straight, on));
            CHECK_FALSE(renderOutput(swung, on) == renderOutput(straight, on));
        }
    }
}

TEST_CASE("generated patterns render with the swing of the style and with the maximum swing", "[groove][output]") {
    std::vector<std::string> bassIds;
    std::vector<std::string> melodyIds;
    for (const Archetype& archetype : allArchetypes()) {
        (archetype.role == VoiceRole::Bass ? bassIds : melodyIds).emplace_back(archetype.id);
    }
    size_t index = 0;
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : {1u, 2u, 4u}) {
            for (const std::string& bassId : bassIds) {
                for (uint64_t seed = 1; seed <= 3; ++seed) {
                    const std::string& melodyId = melodyIds[index++ % melodyIds.size()];
                    Pattern p = makeEmptyPattern(bars, name);
                    p.context.scaleId = std::string(style.scales.front().id);
                    Pcg32 rng = Pcg32::fromSeed(seed);
                    REQUIRE(generateVoice(p, 0, bassId, style, ArchetypeSettings{60, 40}, rng));
                    REQUIRE(generateVoice(p, 1, melodyId, style, ArchetypeSettings{60, 40}, rng));
                    for (const float swing : {-1.0f, 0.75f}) { // -1: the defaults of the style
                        Pattern grooved = p;
                        applyStyleGroove(grooved, style);
                        if (swing > 0.0f) {
                            for (Track& track : grooved.voices) {
                                track.groove.swing = swing;
                            }
                        }
                        OutputSettings settings;
                        settings.kickClearanceTicks = style.bass.kickClearanceTicks;
                        for (const Track& track : grooved.voices) {
                            settings.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
                        }
                        const OutputPattern out = renderOutput(grooved, settings);
                        INFO(name << " " << bassId << "/" << melodyId << " bars " << bars << " seed " << seed
                                  << " swing " << swing);
                        CHECK(renderOutput(grooved, settings) == out);
                        const auto kicks = kickTicks(grooved);
                        for (size_t v = 0; v < out.voices.size(); ++v) {
                            const Track& track = grooved.voices[v];
                            std::map<uint32_t, const Note*> sources;
                            for (const Note& note : track.notes) {
                                sources[note.id] = &note;
                            }
                            const auto& notes = out.voices[v].notes;
                            const int32_t shift = swingOffsetTicks(track.groove.swing, track.groove.amount);
                            for (size_t i = 0; i < notes.size(); ++i) {
                                const OutputNote& note = notes[i];
                                REQUIRE(sources.count(note.noteId) == 1);
                                const Note& source = *sources[note.noteId];
                                const int32_t end = static_cast<int32_t>(bars * kTicksPerBar);
                                CHECK(note.startTick >= 0);
                                CHECK(note.startTick < end);
                                CHECK(note.endTick > note.startTick);
                                // only 16ths are moved: the odd ones by the swing, the others stay
                                const int32_t step = static_cast<int32_t>(source.startTick / 240);
                                const int32_t expected = static_cast<int32_t>(source.startTick) +
                                                         (source.startTick % 240 == 0 && step % 2 == 1 ? shift : 0);
                                CHECK(note.startTick == std::min(expected, end - 1));
                                if (note.slideIntoNext) {
                                    CHECK(source.slide);
                                }
                                if (v == 0 && i + 1 < notes.size() && !note.slideIntoNext) {
                                    CHECK(note.endTick <= notes[i + 1].startTick); // the bass stays monophonic
                                }
                                if (v == 0 && !ignoresKickArchetype(track.archetypeId) &&
                                    settings.kickClearanceTicks > 0) {
                                    for (const uint32_t kick : kicks) {
                                        const bool reaches =
                                            note.startTick < static_cast<int32_t>(kick) &&
                                            note.endTick > static_cast<int32_t>(kick) -
                                                               static_cast<int32_t>(settings.kickClearanceTicks);
                                        CHECK_FALSE(reaches); // the kick stays straight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
