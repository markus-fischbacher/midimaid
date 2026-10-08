#include "core/InstanceSettings.h"
#include "core/OutputStage.h"
#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "core/RollLayout.h"
#include "core/StyleLibrary.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm::core;

namespace {

std::string readStyle(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    std::stringstream text;
    text << stream.rdbuf();
    return text.str();
}

StyleLibrary shippedStyles() {
    const std::vector<std::string> texts{readStyle("peak_time"), readStyle("melodic_techno"),
                                         readStyle("hard_industrial")};
    return StyleLibrary::fromTexts({texts[0], texts[1], texts[2]});
}

} // namespace

TEST_CASE("the style library loads the shipped profiles by id", "[instance-core]") {
    const auto library = shippedStyles();
    CHECK(library.size() == 3);
    CHECK(library.problems().empty());
    REQUIRE(library.find("peak_time") != nullptr);
    CHECK(library.find("peak_time")->id == "peak_time");
    CHECK(library.find("melodic_techno") != nullptr);
    CHECK(library.find("hard_industrial") != nullptr);
    CHECK(library.find("no_such_style") == nullptr);
}

TEST_CASE("an unknown style id falls back to peak time, an empty library gives none", "[instance-core]") {
    const auto library = shippedStyles();
    REQUIRE(library.findOrFallback("no_such_style") != nullptr);
    CHECK(library.findOrFallback("no_such_style")->id == "peak_time");
    CHECK(library.findOrFallback("melodic_techno")->id == "melodic_techno");
    CHECK(StyleLibrary::fromTexts({}).findOrFallback("peak_time") == nullptr);
}

TEST_CASE("bad and duplicate style texts are reported and left out", "[instance-core]") {
    const std::string good = readStyle("hard_industrial");
    const std::string broken = "{ not json";
    const auto library = StyleLibrary::fromTexts({broken, good, good});
    CHECK(library.size() == 1);
    CHECK(library.problems().size() == 2);
    // Without peak_time the first profile that loaded is the fallback.
    CHECK(library.findOrFallback("peak_time")->id == "hard_industrial");
}

TEST_CASE("playback notes are the output stage result of one voice", "[instance-core]") {
    const auto library = shippedStyles();
    const StyleProfile& style = *library.find("peak_time");
    GenerationRequest request;
    request.seed = 7;
    const Pattern pattern = generateCandidate(style, request, 7);
    REQUIRE(pattern.voices.size() >= 2);

    OutputSettings settings;
    settings.kickClearanceTicks = style.bass.kickClearanceTicks;
    for (const Track& track : pattern.voices) {
        settings.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
    }
    const OutputPattern out = renderOutput(pattern, settings);

    for (size_t voice = 0; voice < 2; ++voice) {
        const PlaybackNotes playback = renderVoiceForPlayback(pattern, style, voice);
        CHECK(playback.lengthTicks == out.lengthTicks);
        REQUIRE_FALSE(playback.notes.empty());
        size_t expected = 0;
        for (const OutputNote& note : out.voices[voice].notes) {
            if (note.endTick > std::max<int32_t>(note.startTick, 0)) {
                ++expected;
            }
        }
        CHECK(playback.notes.size() == expected);
        uint32_t previous = 0;
        for (const auto& note : playback.notes) {
            CHECK(note.channel == pattern.voices[voice].midiChannel);
            CHECK(note.startTick + note.lengthTicks <= playback.lengthTicks); // slides are cut at the pattern end
            CHECK(note.lengthTicks > 0);
            CHECK(note.startTick >= previous);
            previous = note.startTick;
        }
    }
    CHECK(renderVoiceForPlayback(pattern, style, 5).notes.empty());
    CHECK(renderVoiceForPlayback(pattern, style, 5).lengthTicks == out.lengthTicks);
}

TEST_CASE("the voices of one pattern give different notes", "[instance-core]") {
    const auto library = shippedStyles();
    const StyleProfile& style = *library.find("melodic_techno");
    GenerationRequest request;
    const Pattern pattern = generateCandidate(style, request, 3);
    const auto bass = renderVoiceForPlayback(pattern, style, 0);
    const auto melody = renderVoiceForPlayback(pattern, style, 1);
    REQUIRE_FALSE(bass.notes.empty());
    REQUIRE_FALSE(melody.notes.empty());
    CHECK(bass.notes.front().channel != melody.notes.front().channel);
}

TEST_CASE("instance settings convert to text and back, unknown text gives the defaults", "[instance-core]") {
    for (const auto role : {InstanceRole::Solo, InstanceRole::Hub, InstanceRole::Voice}) {
        CHECK(parseRole(toString(role)) == role);
    }
    for (const auto mode : {OutputMode::OneVoice, OutputMode::None}) {
        CHECK(parseOutputMode(toString(mode)) == mode);
    }
    CHECK(parseRole("conductor") == InstanceRole::Solo);
    CHECK(parseRole("") == InstanceRole::Solo);
    CHECK(parseOutputMode("everything") == OutputMode::OneVoice);
    for (const auto follow : {SlotFollow::Hub, SlotFollow::Own}) {
        CHECK(parseSlotFollow(toString(follow)) == follow);
    }
    CHECK(parseSlotFollow("") == SlotFollow::Hub);
    CHECK(parseSlotFollow("sideways") == SlotFollow::Hub);
    CHECK(InstanceSettings{} ==
          InstanceSettings{InstanceRole::Solo, OutputMode::OneVoice, 1, GenerationSettings{}, SlotFollow::Hub});
}

TEST_CASE("the output voice is limited to 1 to the voice maximum", "[instance-core]") {
    CHECK(clampOutputVoice(0) == 1);
    CHECK(clampOutputVoice(-5) == 1);
    CHECK(clampOutputVoice(3) == 3);
    CHECK(clampOutputVoice(kMaxVoices) == kMaxVoices);
    CHECK(clampOutputVoice(kMaxVoices + 1) == kMaxVoices);
}

TEST_CASE("generation settings are brought into their valid range", "[instance-core]") {
    GenerationSettings settings;
    CHECK(sanitize(settings) == settings); // the defaults are valid
    settings.lengthBars = 3;
    settings.energyPct = 140;
    settings.creativityPct = -1;
    settings.styleId = "anything";
    const GenerationSettings clean = sanitize(settings);
    CHECK(clean.lengthBars == 4);
    CHECK(clean.energyPct == 100);
    CHECK(clean.creativityPct == 0);
    CHECK(clean.styleId == "anything"); // an unknown style falls back at generation time
    for (const uint32_t bars : {1u, 2u, 4u, 8u, 16u}) {
        settings.lengthBars = bars;
        CHECK(sanitize(settings).lengthBars == bars);
    }
}

TEST_CASE("the octave shifts every pitch of the output stage and folds back at the ends", "[instance-core]") {
    Pattern pattern = makeEmptyPattern(1, "peak_time");
    for (const uint8_t pitch : {uint8_t{5}, uint8_t{45}, uint8_t{120}}) {
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = pitch;
        note.startTick = static_cast<uint32_t>(pattern.voices[0].notes.size()) * 960;
        note.lengthTicks = 480;
        pattern.voices[0].notes.push_back(note);
    }
    const auto pitches = [&](int shift) {
        OutputSettings settings;
        settings.octaveShift = shift;
        settings.kickClearanceTicks = 0;
        std::vector<int> result;
        const OutputPattern out = renderOutput(pattern, settings);
        for (const OutputNote& note : out.voices[0].notes) {
            result.push_back(note.pitch);
        }
        return result;
    };
    CHECK(pitches(0) == std::vector<int>{5, 45, 120});
    CHECK(pitches(1) == std::vector<int>{17, 57, 120}); // 132 folds back to 120
    CHECK(pitches(-1) == std::vector<int>{5, 33, 108}); // -7 folds back to 5
    CHECK(pitches(2) == std::vector<int>{29, 69, 120}); // 144 folds back to 120
    CHECK(pitches(-2) == std::vector<int>{5, 21, 96});
}

TEST_CASE("the octave of playback notes is limited to two up and down", "[instance-core]") {
    const auto library = shippedStyles();
    const StyleProfile& style = *library.find("peak_time");
    GenerationRequest request;
    const Pattern pattern = generateCandidate(style, request, 3);
    const auto plain = renderVoiceForPlayback(pattern, style, 0);
    REQUIRE_FALSE(plain.notes.empty());
    const auto up = renderVoiceForPlayback(pattern, style, 0, 1);
    const auto beyond = renderVoiceForPlayback(pattern, style, 0, 9);
    const auto two = renderVoiceForPlayback(pattern, style, 0, 2);
    REQUIRE(up.notes.size() == plain.notes.size());
    for (size_t i = 0; i < plain.notes.size(); ++i) {
        CHECK(up.notes[i].pitch == plain.notes[i].pitch + 12);
        CHECK(up.notes[i].startTick == plain.notes[i].startTick); // only the pitch changes
    }
    CHECK(clampOctave(9) == 2);
    CHECK(clampOctave(-9) == -2);
    for (size_t i = 0; i < two.notes.size(); ++i) {
        CHECK(beyond.notes[i].pitch == two.notes[i].pitch);
    }
}

TEST_CASE("the roll layout maps ticks and pitches to unit rectangles", "[instance-core]") {
    const std::vector<PatternNote> notes{{0, 960, 1, 40, 100}, {1920, 480, 1, 52, 90}};
    const RollLayout layout = layoutRoll(notes, 4 * 960 * 2, 12); // two bars
    CHECK(layout.bars == 2);
    CHECK(layout.lowPitch == 39); // one row below the lowest note
    CHECK(layout.highPitch == 53);
    REQUIRE(layout.notes.size() == 2);
    const float rows = 15.0f;
    CHECK(layout.notes[0].x == 0.0f);
    CHECK(layout.notes[0].width == Catch::Approx(960.0 / 7680.0));
    CHECK(layout.notes[1].x == Catch::Approx(1920.0 / 7680.0));
    CHECK(layout.notes[0].y == Catch::Approx((53.0f - 40.0f) / rows));
    CHECK(layout.notes[1].y == Catch::Approx(1.0f / rows)); // the highest note is the second row from the top
    CHECK(layout.notes[0].height == Catch::Approx(1.0f / rows));
    CHECK(layout.notes[1].velocity == 90);
}

TEST_CASE("the roll shows at least the minimum number of rows around the notes", "[instance-core]") {
    const RollLayout one = layoutRoll({{0, 480, 1, 45, 100}}, 3840, 12);
    CHECK(one.highPitch - one.lowPitch + 1 == 12);
    CHECK(one.lowPitch <= 44);
    CHECK(one.highPitch >= 46);
    REQUIRE(one.notes.size() == 1);
    CHECK(one.notes[0].y >= 0.0f);
    CHECK(one.notes[0].y + one.notes[0].height <= 1.0f);

    const RollLayout low = layoutRoll({{0, 480, 1, 0, 100}}, 3840, 12); // at the bottom of the range
    CHECK(low.lowPitch == 0);
    CHECK(low.highPitch - low.lowPitch + 1 == 12);
    const RollLayout high = layoutRoll({{0, 480, 1, 127, 100}}, 3840, 12);
    CHECK(high.highPitch == 127);
    CHECK(high.highPitch - high.lowPitch + 1 == 12);
}

TEST_CASE("an empty or zero length pattern gives a layout without notes", "[instance-core]") {
    const RollLayout empty = layoutRoll({}, 3840);
    CHECK(empty.notes.empty());
    CHECK(empty.highPitch - empty.lowPitch + 1 >= 12);
    const RollLayout zero = layoutRoll({{0, 100, 1, 50, 100}}, 0);
    CHECK(zero.bars == 1);
    REQUIRE(zero.notes.size() == 1);
    const RollLayout partial = layoutRoll({}, 3840 + 1);
    CHECK(partial.bars == 2); // a started bar counts
}

TEST_CASE("the seed text is decimal digits that fit 64 bits", "[instance-core]") {
    CHECK(parseSeed("0") == 0);
    CHECK(parseSeed("42") == 42);
    CHECK(parseSeed("18446744073709551615") == UINT64_MAX);
    CHECK_FALSE(parseSeed("18446744073709551616").has_value());
    CHECK_FALSE(parseSeed("99999999999999999999").has_value());
    CHECK_FALSE(parseSeed("123456789012345678901").has_value()); // 21 digits
    CHECK_FALSE(parseSeed("").has_value());
    CHECK_FALSE(parseSeed("-1").has_value());
    CHECK_FALSE(parseSeed("+1").has_value());
    CHECK_FALSE(parseSeed("12a").has_value());
    CHECK_FALSE(parseSeed(" 5").has_value());
    CHECK_FALSE(parseSeed("1e3").has_value());
}

TEST_CASE("an unknown key or scale in the settings becomes auto", "[instance-core]") {
    GenerationSettings settings;
    CHECK(settings.root == 9);
    CHECK(settings.scaleId == "natural_minor");
    CHECK(sanitize(settings) == settings);
    settings.root = 12;
    settings.scaleId = "whole_tone";
    const auto clean = sanitize(settings);
    CHECK_FALSE(clean.root.has_value());
    CHECK_FALSE(clean.scaleId.has_value());
    settings.root = 11;
    settings.scaleId = "locrian";
    CHECK(sanitize(settings).root == 11);
    CHECK(sanitize(settings).scaleId == "locrian");
    settings.seed = 7;
    CHECK(sanitize(settings).seed == 7);
}
