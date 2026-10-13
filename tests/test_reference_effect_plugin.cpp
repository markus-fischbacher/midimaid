#include "NoUserSettings.h"
#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"
#include "core/CopyProtection.h"
#include "core/MidiFile.h"
#include "core/ReferenceProfile.h"
#include "core/SetProfile.h"
#include "core/PatternGenerator.h"
#include "plugin/AiBackend.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/ProcessorBase.h"
#include "plugin/ReferenceStore.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <memory>

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

bool finished(ProcessorBase& processor) {
    return processor.generationStatus() != GenerationStatus::Generating;
}

/// A reference store in memory that `referenceStore()` returns for the test, and the old one back afterwards.
struct ScopedStore {
    ScopedStore() : previous(&referenceStore()) {
        overrideReferenceStore(&store);
        REQUIRE(pumpUntil([&] { return store.ready(); }));
    }
    ~ScopedStore() { overrideReferenceStore(previous); }
    ReferenceStore store{juce::File(), embeddedStyles()};
    ReferenceStore* previous;
};

struct ScopedSettings {
    ScopedSettings() : store(file.getFile()) {
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        overrideGlobalSettings(&store);
    }
    ~ScopedSettings() { overrideGlobalSettings(&mmtest::disabledSettings()); }
    juce::TemporaryFile file;
    GlobalSettingsStore store;
};

struct TempFolder {
    TempFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mm_eff_" + juce::Uuid().toString())) {
        folder.createDirectory();
    }
    ~TempFolder() { folder.deleteRecursively(); }
    juce::File write(const juce::String& name, const std::vector<uint8_t>& bytes) const {
        const auto file = folder.getChildFile(name);
        file.replaceWithData(bytes.data(), bytes.size());
        return file;
    }
    juce::File folder;
};

std::vector<uint8_t> bassFile(int shift, bool halfStepLoop = false) {
    const int roots[] = {45, 45, 48, 43};
    const int loop[] = {45, 46, 45, 46}; // A and B flat: a chord the profile of the style does not use
    std::vector<PatternNote> list;
    for (uint32_t bar = 0; bar < 4; ++bar) {
        for (uint32_t i = 0; i < 4; ++i) {
            list.push_back({bar * kTicksPerBar + i * 960, 200, 1, static_cast<uint8_t>((halfStepLoop ? loop[bar] : roots[bar]) + shift), 100});
        }
    }
    return writeMidiFile(PatternView{list.data(), list.size(), 4 * kTicksPerBar}, MidiFileOptions{});
}

/// Imports five bass files (a different key each), so that the progressions of the set take effect.
void fillSet(ReferenceStore& store, const std::string& style = "peak_time", bool halfStepLoop = false) {
    TempFolder folder;
    std::vector<juce::File> files;
    for (int i = 0; i < 5; ++i) {
        files.push_back(folder.write("bass" + juce::String(i) + ".mid", bassFile(i, halfStepLoop)));
    }
    store.importFiles(style, files);
    REQUIRE(pumpUntil([&] { return !store.progress().running; }));
    store.waitForIdle();
    REQUIRE(store.get(style).items.size() == 5);
}

std::shared_ptr<mm::ai::MockProvider> mock() {
    return std::make_shared<mm::ai::MockProvider>();
}

AiBackendSettings backendWith(std::shared_ptr<mm::ai::MockProvider> provider, bool cloud) {
    AiBackendSettings settings;
    settings.provider = std::move(provider);
    settings.model = "mock-model";
    settings.cloud = cloud;
    return settings;
}

std::string answerFor(ProcessorBase& processor, uint64_t seed = 5) {
    const auto* style = processor.styles().find("peak_time");
    REQUIRE(style != nullptr);
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    const auto result = generatePattern(*style, request);
    REQUIRE(result.success);
    return mm::ai::patternToSchemaJson(result.pattern, style, false)->json;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("a pattern made with an active set carries the set id, one without a set carries none",
          "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.slotsSnapshot().slot(0)->pattern.has_value());
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.referenceSetId.empty());

    fillSet(scoped.store);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    // the id of the set the files went into (v1.1: named sets, "Standard" when only the favorites were active)
    CHECK(scoped.store.get("peak_time").setName == "Standard");
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.referenceSetId == scoped.store.get("peak_time").setId);
    CHECK_FALSE(processor.slotsSnapshot().slot(0)->pattern->info.referenceSetId.empty());

    // every item switched off: no set
    for (const auto& item : scoped.store.get("peak_time").items) {
        scoped.store.setActive("peak_time", item.id, false);
    }
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.referenceSetId.empty());
}

TEST_CASE("the set of another style does not act on this one", "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    fillSet(scoped.store, "melodic_techno");
    InstrumentProcessor processor("Test"); // the default style is peak_time
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.referenceSetId.empty());
}

TEST_CASE("a cloud provider gets the examples only after the consent of that provider", "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    ScopedSettings settings;
    fillSet(scoped.store);
    InstrumentProcessor processor("Test");

    const auto ask = [&](bool cloud) {
        auto provider = mock();
        provider->enqueueText(answerFor(processor));
        ScopedAiBackend backend(backendWith(provider, cloud));
        processor.generate();
        REQUIRE(pumpUntil([&] { return finished(processor); }));
        REQUIRE(provider->requestCount() == 1);
        return provider->requests().front().userPrompt;
    };

    // a cloud provider without consent: the AI gets the request, no examples
    CHECK_FALSE(contains(ask(true), "own style"));
    // consent for another provider does not count
    settings.store.update([](mm::core::GlobalSettings& s) { s.ai.cloudConsent.insert("someone-else"); });
    CHECK_FALSE(contains(ask(true), "own style"));
    // consent for this one ("mock") does
    settings.store.update([](mm::core::GlobalSettings& s) { s.ai.cloudConsent.insert("mock"); });
    const auto with = ask(true);
    CHECK(contains(with, "The musician's own style"));
    CHECK(contains(with, "- bass, 4 bars,"));
    // revoked: gone again
    settings.store.update([](mm::core::GlobalSettings& s) { s.ai.cloudConsent.erase("mock"); });
    CHECK_FALSE(contains(ask(true), "own style"));
    // a provider on this computer needs none
    CHECK(contains(ask(false), "The musician's own style"));
}

TEST_CASE("a cloud provider without consent still gets the taste summary, which has no notes (D-248)",
          "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    ScopedSettings settings;
    fillSet(scoped.store);
    InstrumentProcessor processor("Test");

    const auto ask = [&] {
        auto provider = mock();
        provider->enqueueText(answerFor(processor));
        ScopedAiBackend backend(backendWith(provider, true));
        processor.generate();
        REQUIRE(pumpUntil([&] { return finished(processor); }));
        REQUIRE(provider->requestCount() == 1);
        return provider->requests().front().userPrompt;
    };

    const auto prompt = ask();
    CHECK(contains(prompt, "The musician's taste"));
    CHECK(contains(prompt, "- bass: about"));
    CHECK_FALSE(contains(prompt, "own style")); // the examples stay back without consent

    // "Personal" at 0 switches the summary off as well
    auto next = processor.instanceSettings();
    next.generation.personalPct = 0;
    processor.setInstanceSettings(next);
    CHECK_FALSE(contains(ask(), "The musician's taste"));
}

TEST_CASE("an empty set adds nothing to the prompt even with consent", "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    provider->enqueueText(answerFor(processor));
    ScopedAiBackend backend(backendWith(provider, false));
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK_FALSE(contains(provider->requests().front().userPrompt, "own style"));
    CHECK_FALSE(contains(provider->requests().front().userPrompt, "taste")); // no profile, no summary
}

TEST_CASE("an AI answer that copies the set is reported as flagged and still put in the slot",
          "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    const auto* style = processor.styles().find("peak_time");
    REQUIRE(style != nullptr);
    // the melody of the answer goes into the set as a file
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 5;
    const auto made = generatePattern(*style, request);
    REQUIRE(made.success);
    std::vector<PatternNote> list;
    for (const auto& note : made.pattern.voices[1].notes) {
        list.push_back({note.startTick, note.lengthTicks, 1, note.pitch, note.velocity});
    }
    ReferenceEntry entry;
    entry.role = VoiceRole::Melody;
    entry.bars = 4;
    entry.notes = made.pattern.voices[1].notes;
    REQUIRE(isDistinctive(entry));
    TempFolder folder;
    const auto file = folder.write("melody.mid", writeMidiFile(PatternView{list.data(), list.size(), 4 * kTicksPerBar},
                                                              MidiFileOptions{}));
    scoped.store.importFiles("peak_time", {file});
    REQUIRE(pumpUntil([&] { return !scoped.store.progress().running; }));
    scoped.store.waitForIdle();
    const auto set = scoped.store.get("peak_time");
    REQUIRE(set.items.size() == 1);
    scoped.store.setRole("peak_time", set.items[0].id, TrackRole::Melody);

    auto provider = mock();
    provider->enqueueText(answerFor(processor, 5));
    ScopedAiBackend backend(backendWith(provider, true));
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Done);
    CHECK(processor.lastReport().copyFlagged);
    CHECK(processor.lastReport().belowMinScore);
    CHECK(processor.slotsSnapshot().slot(0)->pattern.has_value());
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.referenceSetId == scoped.store.get("peak_time").setId);

    // switched off, the same answer is no copy
    scoped.store.setActive("peak_time", set.items[0].id, false);
    auto again = mock();
    again->enqueueText(answerFor(processor, 5));
    ScopedAiBackend second(backendWith(again, true));
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK_FALSE(processor.lastReport().copyFlagged);
}

TEST_CASE("the progressions of the set reach the offline generator", "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    const auto count = [&] {
        int withNewChord = 0;
        for (uint64_t seed = 1; seed <= 50; ++seed) {
            auto settings = processor.instanceSettings();
            settings.generation.seed = seed;
            processor.setInstanceSettings(settings);
            processor.generate();
            REQUIRE(pumpUntil([&] { return finished(processor); }));
            const auto bank = processor.slotsSnapshot();
            const auto& progression = bank.slot(0)->pattern->context.progression;
            const bool has = std::any_of(progression.begin(), progression.end(),
                                         [](const ChordEvent& e) { return e.chord.rootOffset == 1; });
            withNewChord += has ? 1 : 0;
        }
        return withNewChord;
    };
    CHECK(count() == 0); // without a set the profile has no chord a half step up
    fillSet(scoped.store, "peak_time", true);
    const auto cycles = personalProgressions(scoped.store.get("peak_time"));
    REQUIRE(cycles.size() == 1);
    REQUIRE(cycles[0].chords.size() == 2);
    REQUIRE(cycles[0].chords[1].rootOffset == 1);
    CHECK(count() > 0);
}

namespace {

/// Imports the melody of `pattern` as the only (active, melody) item of the set of peak_time.
void putMelodyInSet(ReferenceStore& store, const Pattern& pattern) {
    std::vector<PatternNote> list;
    for (const auto& note : pattern.voices[1].notes) {
        list.push_back({note.startTick, note.lengthTicks, 1, note.pitch, note.velocity});
    }
    ReferenceEntry entry;
    entry.role = VoiceRole::Melody;
    entry.bars = pattern.lengthBars;
    entry.notes = pattern.voices[1].notes;
    REQUIRE(isDistinctive(entry));
    TempFolder folder;
    const auto file = folder.write("melody.mid", writeMidiFile(PatternView{list.data(), list.size(),
                                                                          pattern.lengthBars * kTicksPerBar},
                                                              MidiFileOptions{}));
    store.importFiles("peak_time", {file});
    REQUIRE(pumpUntil([&] { return !store.progress().running; }));
    store.waitForIdle();
    const auto set = store.get("peak_time");
    REQUIRE(set.items.size() == 1);
    store.setRole("peak_time", set.items[0].id, TrackRole::Melody);
}

} // namespace

TEST_CASE("the offline generator does not make a melody that is in the set", "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.seed = 5;
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    const auto plain = *processor.slotsSnapshot().slot(0)->pattern;

    putMelodyInSet(scoped.store, plain);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    if (processor.generationStatus() == GenerationStatus::Done) {
        CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[1].notes != plain.voices[1].notes);
    }
}

TEST_CASE("a refinement that copies the set is reported as flagged", "[reference-effect-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    auto instance = processor.instanceSettings();
    instance.generation.seed = 5;
    processor.setInstanceSettings(instance);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    const auto pattern = *processor.slotsSnapshot().slot(0)->pattern;
    const auto* style = processor.styles().find("peak_time");
    const auto answer = mm::ai::patternToSchemaJson(pattern, style, true)->json;

    auto provider = mock();
    provider->enqueueText(answer);
    {
        ScopedAiBackend backend(backendWith(provider, true));
        REQUIRE(processor.refine("keep it"));
        REQUIRE(pumpUntil([&] { return finished(processor); }));
        CHECK_FALSE(processor.lastReport().copyFlagged);
    }

    putMelodyInSet(scoped.store, pattern);
    auto again = mock();
    again->enqueueText(answer);
    ScopedAiBackend backend(backendWith(again, true));
    REQUIRE(processor.refine("keep it"));
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.lastReport().copyFlagged);
}

TEST_CASE("a pattern made with a set of enough entries keeps the profile and the setting", "[reference-effect-plugin][personal]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.profile.empty()); // no set: no profile

    fillSet(scoped.store);
    CHECK(mm::core::referenceEffectOf(scoped.store.get("peak_time")).profile.valid);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    const auto first = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK_FALSE(first.info.profile.empty());
    CHECK(first.info.personalPct == 50);
    CHECK(mm::core::setProfileFromJson(first.info.profile).has_value());
    CHECK(mm::core::setProfileFromJson(first.info.profile)->activeItems == 5);

    // the setting of the instance is the one that is kept
    auto settings = processor.instanceSettings();
    settings.generation.personalPct = 85;
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.personalPct == 85);

    // fewer than five active items: no profile, the pattern has none
    const auto items = scoped.store.get("peak_time").items;
    scoped.store.setActive("peak_time", items[0].id, false);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.profile.empty());
}
