#include "core/InstanceSettings.h"
#include "core/OutputStage.h"
#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "core/StyleLibrary.h"

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
    CHECK(InstanceSettings{} == InstanceSettings{InstanceRole::Solo, OutputMode::OneVoice, 1});
}

TEST_CASE("the output voice is limited to 1 to the voice maximum", "[instance-core]") {
    CHECK(clampOutputVoice(0) == 1);
    CHECK(clampOutputVoice(-5) == 1);
    CHECK(clampOutputVoice(3) == 3);
    CHECK(clampOutputVoice(kMaxVoices) == kMaxVoices);
    CHECK(clampOutputVoice(kMaxVoices + 1) == kMaxVoices);
}
