#include "NoUserSettings.h"
#include "core/MidiFile.h"
#include "core/PatternGenerator.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/ProcessorBase.h"
#include "plugin/ReferenceStore.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

// Measurements against SPEC 11. Hidden (`[.perf]`): timings of a debug build or of a busy CI machine mean nothing, so
// run them by hand on a release build: `mm_plugin_glue_tests "[.perf]"`.

using namespace mm::plugin;
using namespace mm::core;
using Clock = std::chrono::steady_clock;

namespace {

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

struct Blocks {
    double worstMs = 0.0;
    double totalMs = 0.0;
    double audioMs = 0.0;
};

/// Plays `seconds` seconds at 120 BPM block by block and times every call of `processBlock`.
Blocks timeBlocks(ProcessorBase& processor, double seconds, int blockSize, const std::atomic<bool>* stop = nullptr) {
    constexpr double kRate = 48000.0;
    FakePlayHead head;
    processor.setRateAndBufferSizeDetails(kRate, blockSize);
    processor.setPlayHead(&head);
    processor.prepareToPlay(kRate, blockSize);
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    Blocks result;
    const long long total = static_cast<long long>(seconds * kRate);
    for (long long clock = 0; clock < total && !(stop != nullptr && stop->load()); clock += blockSize) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(clock) / kRate * 2.0);
        audio.clear();
        midi.clear();
        const auto start = Clock::now();
        processor.processBlock(audio, midi);
        const double ms = millisecondsSince(start);
        result.worstMs = std::max(result.worstMs, ms);
        result.totalMs += ms;
        result.audioMs += blockSize / kRate * 1000.0;
    }
    return result;
}

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 60000) {
    const auto start = Clock::now();
    while (!done()) {
        if (millisecondsSince(start) > timeoutMs) {
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

} // namespace

TEST_CASE("offline generation of 16 bars with the whole selection takes under 100 ms", "[.perf]") {
    for (const auto& style : embeddedStyles().profiles()) {
        std::vector<double> times;
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            GenerationRequest request;
            request.lengthBars = 16;
            request.seed = seed;
            const auto start = Clock::now();
            const auto result = generatePattern(style, request);
            times.push_back(millisecondsSince(start));
            REQUIRE(result.success);
        }
        const double worst = *std::max_element(times.begin(), times.end());
        std::printf("[perf] generate 16 bars %-18s median %.1f ms, worst %.1f ms\n", style.id.c_str(), median(times),
                    worst);
        CHECK(median(times) < 100.0);
    }
}

TEST_CASE("a plug-in instance is there in under 500 ms", "[.perf]") {
    juce::ScopedJuceInitialiser_GUI gui;
    std::vector<double> times;
    for (int i = 0; i < 9; ++i) {
        const auto start = Clock::now();
        InstrumentProcessor processor("Test");
        times.push_back(millisecondsSince(start));
    }
    std::printf("[perf] construct instance median %.1f ms, worst %.1f ms\n", median(times),
                *std::max_element(times.begin(), times.end()));
    CHECK(median(times) < 500.0);
}

TEST_CASE("playing a pattern costs under 1 percent of a core, no block comes near its own length", "[.perf]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.seed = 3;
    settings.generation.lengthBars = 16;
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() != GenerationStatus::Generating; }));
    for (const int blockSize : {64, 512, 2048}) {
        const auto blocks = timeBlocks(processor, 120.0, blockSize);
        const double load = blocks.totalMs / blocks.audioMs * 100.0;
        std::printf("[perf] play, block %4d: %.4f %% of a core, worst block %.3f ms (block is %.1f ms)\n", blockSize,
                    load, blocks.worstMs, blockSize / 48.0);
        CHECK(load < 1.0);
        CHECK(blocks.worstMs < blockSize / 48.0 * 0.5);
    }
}

TEST_CASE("200 reference files are analysed in under 30 s and playback keeps its blocks short meanwhile", "[.perf]") {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("mm_perf_" + juce::Uuid().toString());
    folder.createDirectory();
    std::vector<juce::File> files;
    for (int i = 0; i < 200; ++i) {
        std::vector<PatternNote> list;
        for (uint32_t bar = 0; bar < 8; ++bar) {
            for (uint32_t step = 0; step < 16; ++step) {
                if ((step + static_cast<uint32_t>(i)) % 3 != 0) {
                    list.push_back({bar * kTicksPerBar + step * 240, 200, 1,
                                    static_cast<uint8_t>(36 + (i % 12) + ((bar + step) % 4) * 3), static_cast<uint8_t>(40 + i / 2)});
                }
            }
        }
        const auto bytes = writeMidiFile(PatternView{list.data(), list.size(), 8 * kTicksPerBar}, MidiFileOptions{});
        const auto file = folder.getChildFile("ref" + juce::String(i) + ".mid");
        file.replaceWithData(bytes.data(), bytes.size());
        files.push_back(file);
    }

    ReferenceStore store(juce::File(), embeddedStyles());
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    InstrumentProcessor processor("Test");
    std::atomic<bool> importDone{false};
    Blocks blocks;
    std::thread audio([&] { blocks = timeBlocks(processor, 60.0, 512, &importDone); });

    const auto start = Clock::now();
    store.importFiles("peak_time", files);
    REQUIRE(pumpUntil([&] { return !store.progress().running; }, 120000));
    store.waitForIdle();
    const double seconds = millisecondsSince(start) / 1000.0;
    importDone = true;
    audio.join();
    folder.deleteRecursively();

    std::printf("[perf] analyse 200 files: %.2f s (%zu items); during it worst block %.3f ms\n", seconds,
                store.get("peak_time").items.size(), blocks.worstMs);
    CHECK(store.get("peak_time").items.size() == 200); // every file is its own (the hash differs), none skipped
    CHECK(seconds < 30.0);
    CHECK(blocks.worstMs < 512 / 48.0 * 0.5);
}
