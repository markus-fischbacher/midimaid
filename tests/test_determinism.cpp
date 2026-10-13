#include "NoUserSettings.h"
#include "core/MidiFile.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/ProcessorBase.h"
#include "plugin/ReferenceStore.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>

using namespace mm::plugin;
using namespace mm::core;

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 20000) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) {
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

struct ScopedStore {
    ScopedStore() : previous(&referenceStore()) {
        overrideReferenceStore(&store);
        REQUIRE(pumpUntil([&] { return store.ready(); }));
    }
    ~ScopedStore() { overrideReferenceStore(previous); }
    ReferenceStore store{juce::File(), embeddedStyles()};
    ReferenceStore* previous;
};

struct TempFolder {
    TempFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("mm_det_" + juce::Uuid().toString())) {
        folder.createDirectory();
    }
    ~TempFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

/// Five bass loops in different keys: enough for the personal progressions to act on the generator (D-182).
void fillSet(ReferenceStore& store, const TempFolder& folder) {
    const int roots[] = {45, 45, 48, 43};
    std::vector<juce::File> files;
    for (int i = 0; i < 5; ++i) {
        std::vector<PatternNote> list;
        for (uint32_t bar = 0; bar < 4; ++bar) {
            for (uint32_t step = 0; step < 4; ++step) {
                list.push_back({bar * kTicksPerBar + step * 960, 200, 1, static_cast<uint8_t>(roots[bar] + i), 100});
            }
        }
        const auto bytes = writeMidiFile(PatternView{list.data(), list.size(), 4 * kTicksPerBar}, MidiFileOptions{});
        const auto file = folder.folder.getChildFile("bass" + juce::String(i) + ".mid");
        file.replaceWithData(bytes.data(), bytes.size());
        files.push_back(file);
    }
    store.importFiles("peak_time", files);
    REQUIRE(pumpUntil([&] { return !store.progress().running; }));
    store.waitForIdle();
    REQUIRE(store.get("peak_time").items.size() == 5);
}

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

struct Event {
    long long sample;
    uint8_t bytes[3];
    bool operator==(const Event&) const = default;
};

/// Plays `bars` bars at 120 BPM from PPQ 0 with the given block size and returns every MIDI event with its absolute
/// sample position and raw bytes.
std::vector<Event> render(ProcessorBase& processor, int blockSize, int bars) {
    constexpr double kRate = 48000.0;
    FakePlayHead head;
    processor.setRateAndBufferSizeDetails(kRate, blockSize);
    processor.setPlayHead(&head);
    processor.prepareToPlay(kRate, blockSize);
    std::vector<Event> events;
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    const long long total = static_cast<long long>(bars * 4 * kRate / 2.0);
    for (long long clock = 0; clock < total; clock += blockSize) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(clock) / kRate * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            Event event{clock + metadata.samplePosition, {0, 0, 0}};
            for (int i = 0; i < message.getRawDataSize() && i < 3; ++i) {
                event.bytes[i] = message.getRawData()[i];
            }
            events.push_back(event);
        }
    }
    return events;
}

juce::MemoryBlock stateOf(ProcessorBase& processor) {
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return block;
}

} // namespace

TEST_CASE("a saved pattern sounds bit for bit the same after the sets changed and the project was reloaded",
          "[determinism]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    TempFolder folder;
    fillSet(scoped.store, folder);

    InstrumentProcessor original("Test");
    auto settings = original.instanceSettings();
    settings.generation.seed = 77;
    settings.generation.lengthBars = 8;
    original.setInstanceSettings(settings);
    original.generate();
    REQUIRE(pumpUntil([&] { return original.generationStatus() != GenerationStatus::Generating; }));
    const auto generated = *original.slotsSnapshot().slot(0)->pattern;
    REQUIRE(generated.info.referenceSetId == scoped.store.get("peak_time").setId); // the real id of the set (D-238)
    REQUIRE_FALSE(generated.info.referenceSetId.empty());

    const auto before = render(original, 512, 8);
    REQUIRE_FALSE(before.empty());
    const auto saved = stateOf(original);

    // the set changes in every way the musician can: weights, roles, switched off, removed
    const auto items = scoped.store.get("peak_time").items;
    scoped.store.setWeight("peak_time", items[0].id, 1);
    scoped.store.setRole("peak_time", items[1].id, TrackRole::Melody);
    scoped.store.setActive("peak_time", items[2].id, false);
    scoped.store.remove("peak_time", items[3].id);
    scoped.store.waitForIdle();

    InstrumentProcessor reloaded("Test");
    reloaded.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(reloaded.slotLoadProblems().empty());
    CHECK(*reloaded.slotsSnapshot().slot(0)->pattern == generated);

    CHECK(render(reloaded, 512, 8) == before);
    // the host's block size changes where the events fall into blocks, not where they are in time
    InstrumentProcessor again("Test");
    again.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    // other block sizes: the same notes in the same order; a position that falls exactly on a sample boundary may land
    // one sample apart (the host's PPQ is a double: the rounding differs with the start of the block)
    const auto odd = render(again, 77, 8);
    REQUIRE(odd.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        CHECK(std::equal(std::begin(before[i].bytes), std::end(before[i].bytes), std::begin(odd[i].bytes)));
        CHECK(std::abs(before[i].sample - odd[i].sample) <= 1);
    }
    // and saving the reloaded project gives the very same state
    CHECK(stateOf(reloaded) == saved);
}

TEST_CASE("the same seed and the same set make the same pattern, a changed set is a different pattern",
          "[determinism]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    TempFolder folder;
    fillSet(scoped.store, folder);

    const auto make = [&](uint64_t seed) {
        InstrumentProcessor processor("Test");
        auto settings = processor.instanceSettings();
        settings.generation.seed = seed;
        processor.setInstanceSettings(settings);
        processor.generate();
        REQUIRE(pumpUntil([&] { return processor.generationStatus() != GenerationStatus::Generating; }));
        return *processor.slotsSnapshot().slot(0)->pattern;
    };

    const auto first = make(5);
    CHECK(make(5) == first);

    // all items off: the set no longer acts, so the same seed gives a pattern without the set id
    for (const auto& item : scoped.store.get("peak_time").items) {
        scoped.store.setActive("peak_time", item.id, false);
    }
    const auto without = make(5);
    CHECK(without.info.referenceSetId.empty());
    CHECK_FALSE(without == first);
}
