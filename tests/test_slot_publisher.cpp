#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "engine/PatternPlayer.h"
#include "engine/SlotPublisher.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;

namespace {

std::string readStyle(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    std::stringstream text;
    text << stream.rdbuf();
    return text.str();
}

/// The three shipped profiles, kept alive for the whole test run.
const core::StyleLibrary& styles() {
    static const std::vector<std::string> texts{readStyle("peak_time"), readStyle("melodic_techno"),
                                                readStyle("hard_industrial")};
    static const core::StyleLibrary library = core::StyleLibrary::fromTexts({texts[0], texts[1], texts[2]});
    return library;
}

core::Pattern generated(const std::string& style, uint64_t seed) {
    core::GenerationRequest request;
    request.seed = seed;
    return core::generateCandidate(*styles().find(style), request, seed);
}

struct Rig {
    engine::PatternHandover handover;
    engine::SlotPublisher publisher{handover, styles()};
    core::SlotBank bank;
};

std::unique_ptr<engine::OwnedPattern> takeResult(engine::PatternHandover& handover, size_t slot) {
    return std::unique_ptr<engine::OwnedPattern>(handover.takeResult(slot));
}

} // namespace

TEST_CASE("an empty bank publishes nothing", "[slot-publisher]") {
    Rig rig;
    CHECK(rig.publisher.sync(rig.bank) == 0);
    for (size_t slot = 0; slot <= engine::kSlotCount; ++slot) {
        CHECK_FALSE(rig.handover.hasResult(slot));
    }
}

TEST_CASE("a result is rendered for the output voice and published once", "[slot-publisher]") {
    Rig rig;
    const core::Pattern pattern = generated("peak_time", 5);
    REQUIRE(rig.bank.setResult(2, pattern));
    CHECK(rig.publisher.sync(rig.bank) == 1);
    CHECK(rig.publisher.sync(rig.bank) == 0); // nothing changed

    REQUIRE(rig.handover.hasResult(2));
    const auto owned = takeResult(rig.handover, 2);
    const auto expected = core::renderVoiceForPlayback(pattern, *styles().find("peak_time"), 0);
    REQUIRE_FALSE(expected.notes.empty());
    CHECK(owned->lengthTicks == expected.lengthTicks);
    CHECK(owned->version == rig.bank.slot(2)->pattern->version);
    CHECK(owned->gridPpq == 4.0);
    REQUIRE(owned->notes.size() == expected.notes.size());
    for (size_t i = 0; i < expected.notes.size(); ++i) {
        CHECK(owned->notes[i].startTick == expected.notes[i].startTick);
        CHECK(owned->notes[i].pitch == expected.notes[i].pitch);
        CHECK(owned->notes[i].lengthTicks == expected.notes[i].lengthTicks);
    }
}

TEST_CASE("changing the output voice republishes the filled slots with that voice", "[slot-publisher]") {
    Rig rig;
    const core::Pattern pattern = generated("melodic_techno", 9);
    REQUIRE(rig.bank.setResult(0, pattern));
    REQUIRE(rig.bank.setResult(4, pattern));
    CHECK(rig.publisher.sync(rig.bank) == 2);
    takeResult(rig.handover, 0);
    takeResult(rig.handover, 4);

    rig.publisher.setOutputVoice(2);
    CHECK(rig.publisher.outputVoice() == 2);
    CHECK(rig.publisher.sync(rig.bank) == 2);
    const auto owned = takeResult(rig.handover, 0);
    const auto melody = core::renderVoiceForPlayback(pattern, *styles().find("melodic_techno"), 1);
    REQUIRE(owned->notes.size() == melody.notes.size());
    CHECK(owned->notes.front().channel == pattern.voices[1].midiChannel);

    rig.publisher.setOutputVoice(2); // same voice: nothing to do
    CHECK(rig.publisher.sync(rig.bank) == 0);
    rig.publisher.setOutputVoice(99); // clamped to the maximum, a voice the pattern does not have
    CHECK(rig.publisher.outputVoice() == core::kMaxVoices);
    CHECK(rig.publisher.sync(rig.bank) == 2);
    CHECK(takeResult(rig.handover, 0)->notes.empty());
}

TEST_CASE("a cleared slot gets a silent pattern, a slot that was never filled stays untouched", "[slot-publisher]") {
    Rig rig;
    REQUIRE(rig.bank.setResult(1, generated("peak_time", 1)));
    rig.publisher.sync(rig.bank);
    takeResult(rig.handover, 1);

    REQUIRE(rig.bank.clear(1));
    CHECK(rig.publisher.sync(rig.bank) == 1);
    const auto silent = takeResult(rig.handover, 1);
    REQUIRE(silent != nullptr);
    CHECK(silent->notes.empty());
    CHECK(silent->lengthTicks == core::kTicksPerBar);
    for (size_t slot = 0; slot <= engine::kSlotCount; ++slot) {
        CHECK_FALSE(rig.handover.hasResult(slot)); // slot 7, say, was never touched
    }
    CHECK(rig.publisher.sync(rig.bank) == 0);
}

TEST_CASE("a cleared slot stays silent after the voice changed", "[slot-publisher]") {
    Rig rig;
    REQUIRE(rig.bank.setResult(1, generated("peak_time", 1)));
    rig.publisher.sync(rig.bank);
    rig.bank.clear(1);
    rig.publisher.sync(rig.bank);
    takeResult(rig.handover, 1);

    rig.publisher.setOutputVoice(2);
    CHECK(rig.publisher.sync(rig.bank) == 1); // the engine must hear that the slot is empty
    CHECK(takeResult(rig.handover, 1)->notes.empty());
}

TEST_CASE("swapping two slots republishes both", "[slot-publisher]") {
    Rig rig;
    REQUIRE(rig.bank.setResult(0, generated("peak_time", 1)));
    REQUIRE(rig.bank.setResult(1, generated("hard_industrial", 2)));
    rig.publisher.sync(rig.bank);
    const auto first = takeResult(rig.handover, 0);
    const auto second = takeResult(rig.handover, 1);

    REQUIRE(rig.bank.swap(0, 1));
    CHECK(rig.publisher.sync(rig.bank) == 2);
    const auto swappedFirst = takeResult(rig.handover, 0);
    const auto swappedSecond = takeResult(rig.handover, 1);
    CHECK(swappedFirst->notes.size() == second->notes.size());
    CHECK(swappedSecond->notes.size() == first->notes.size());
}

TEST_CASE("an unknown style id renders with the fallback style and is counted", "[slot-publisher]") {
    Rig rig;
    core::Pattern pattern = generated("peak_time", 3);
    pattern.styleId = "no_such_style";
    REQUIRE(rig.bank.setResult(0, pattern));
    CHECK(rig.publisher.styleFallbacks() == 0);
    CHECK(rig.publisher.sync(rig.bank) == 1);
    CHECK(rig.publisher.styleFallbacks() == 1);
    CHECK_FALSE(takeResult(rig.handover, 0)->notes.empty());
}

TEST_CASE("an edit goes to the edit mailbox of its slot and is refused for an empty slot", "[slot-publisher]") {
    Rig rig;
    CHECK_FALSE(rig.publisher.publishEdit(rig.bank, 3));
    REQUIRE(rig.bank.setResult(3, generated("peak_time", 4)));
    REQUIRE(rig.bank.edit(3, generated("peak_time", 5)));
    CHECK(rig.publisher.publishEdit(rig.bank, 3));
    CHECK(rig.handover.hasEdit(3));
    CHECK_FALSE(rig.handover.hasResult(3));
    CHECK(rig.publisher.sync(rig.bank) == 0); // the edit counts as published
    delete rig.handover.takeEdit(3);
}

TEST_CASE("the published notes play from the bar line once the slot is selected", "[slot-publisher]") {
    Rig rig;
    const core::Pattern pattern = generated("peak_time", 5);
    REQUIRE(rig.bank.setResult(2, pattern));
    rig.publisher.sync(rig.bank);
    const auto expected = core::renderVoiceForPlayback(pattern, *styles().find("peak_time"), 0);
    REQUIRE_FALSE(expected.notes.empty());

    engine::PatternPlayer player;
    player.attach(&rig.handover);
    player.setSlot(3);
    engine::MidiEventList out;
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 512;
    std::vector<long long> noteOns;
    long long clock = 0;
    for (int block = 0; block < 400; ++block) { // from PPQ 0 to about 8.5 at 120 BPM
        engine::TransportInfo info;
        info.hasPosition = true;
        info.isPlaying = true;
        info.bpm = 120.0;
        info.ppq = static_cast<double>(clock) / kSampleRate * 2.0;
        player.process(info, kBlock, kSampleRate, out);
        for (const auto& event : out) {
            if (event.noteOn) {
                noteOns.push_back(clock + event.sampleOffset);
            }
        }
        clock += kBlock;
    }
    REQUIRE_FALSE(noteOns.empty());
    // The first bar of the pattern: the first note starts at its tick (PPQ = tick / 960 at 24000 samples per PPQ).
    const long long first = std::llround(static_cast<double>(expected.notes.front().startTick) / 960.0 * 24000.0);
    CHECK(std::abs(noteOns.front() - first) <= 1);
    CHECK(rig.handover.activeVersion() == rig.bank.slot(2)->pattern->version);
    CHECK(rig.handover.activeSlot() == 3);
}
