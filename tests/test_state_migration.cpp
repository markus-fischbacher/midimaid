#include "core/PatternGenerator.h"
#include "plugin/ProcessorBase.h"
#include "plugin/StateMigration.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

using namespace mm::plugin;

#ifndef MIDIMAID_GOLDEN_DIR
#error "MIDIMAID_GOLDEN_DIR must point to tests/golden"
#endif

namespace {

juce::XmlElement documentAt(int version) {
    juce::XmlElement root("MidiMaid");
    if (version != -1) {
        root.setAttribute("stateVersion", version);
    }
    root.setAttribute("trail", "");
    return root;
}

/// A step that appends its own number to the attribute `trail`.
StateMigration marker(int number) {
    return [number](juce::XmlElement& root, std::string&) {
        root.setAttribute("trail", root.getStringAttribute("trail") + juce::String(number));
        return true;
    };
}

void setParameter(ProcessorBase& processor, const char* id, float value) {
    auto* parameter = processor.parameters().getParameter(id);
    REQUIRE(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

juce::MemoryBlock saved(ProcessorBase& processor) {
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return block;
}

std::string xmlText(const juce::MemoryBlock& block) {
    const auto root = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(root != nullptr);
    auto text = root->toString().toStdString();
    // JUCE writes the line ends of the platform; the golden file has plain line feeds
    for (size_t at = text.find("\r\n"); at != std::string::npos; at = text.find("\r\n", at)) {
        text.erase(at, 1);
    }
    return text;
}

/// An instance with three slots from fixed seeds, names, colours and settings: the same on every platform.
void fill(InstrumentProcessor& processor) {
    const auto* style = processor.styles().find("peak_time");
    REQUIRE(style != nullptr);
    processor.editSlots([&](mm::core::SlotBank& bank) {
        for (size_t slot = 0; slot < 3; ++slot) {
            mm::core::GenerationRequest request;
            request.lengthBars = slot == 1 ? 2 : 4;
            request.seed = 100 + slot;
            const auto result = mm::core::generatePattern(*style, request);
            REQUIRE(result.success);
            REQUIRE(bank.setResult(slot, result.pattern));
            REQUIRE(bank.setName(slot, slot == 0 ? "Intro" : slot == 1 ? "Drop \xC3\xBC" : "Break"));
            REQUIRE(bank.setColor(slot, static_cast<uint8_t>(slot + 1)));
        }
        mm::core::GenerationRequest again;
        again.lengthBars = 4;
        again.seed = 200;
        REQUIRE(bank.setResult(0, mm::core::generatePattern(*style, again).pattern)); // a second result: history
    });
    auto settings = processor.instanceSettings();
    settings.generation.seed = 77;
    settings.generation.prompt = "dark & rolling <test>";
    settings.generation.energyPct = 63;
    settings.octave = -1;
    processor.setInstanceSettings(settings);
    setParameter(processor, "slot", 3.0f);
    setParameter(processor, "mute_2", 1.0f);
}

std::string goldenPath() {
    return std::string(MIDIMAID_GOLDEN_DIR) + "/state_v1.xml";
}

bool updateGolden() {
    const char* value = std::getenv("MIDIMAID_UPDATE_GOLDEN");
    return value != nullptr && value[0] == '1';
}

} // namespace

// ---- the migration chain ----------------------------------------------------------------------------------------

TEST_CASE("a state of the current version is left alone", "[state-migration]") {
    auto root = documentAt(3);
    const StateMigration steps[] = {marker(2), marker(3)};
    const auto result = migrateState(root, 3, steps);
    CHECK(result.ok);
    CHECK_FALSE(result.fromNewer);
    CHECK(root.getStringAttribute("trail").isEmpty());
    CHECK(root.getIntAttribute("stateVersion") == 3);
}

TEST_CASE("an older state runs through every step in order and gets the new version", "[state-migration]") {
    const StateMigration steps[] = {marker(2), marker(3), marker(4)};
    auto root = documentAt(1);
    CHECK(migrateState(root, 4, steps).ok);
    CHECK(root.getStringAttribute("trail") == "234");
    CHECK(root.getIntAttribute("stateVersion") == 4);
    auto middle = documentAt(2);
    CHECK(migrateState(middle, 4, steps).ok);
    CHECK(middle.getStringAttribute("trail") == "34"); // only the steps that are missing
    CHECK(middle.getIntAttribute("stateVersion") == 4);
    auto last = documentAt(3);
    CHECK(migrateState(last, 4, steps).ok);
    CHECK(last.getStringAttribute("trail") == "4");
}

TEST_CASE("a newer state is reported and not touched", "[state-migration]") {
    auto root = documentAt(9);
    const StateMigration steps[] = {marker(2)};
    const auto result = migrateState(root, 2, steps);
    CHECK(result.ok);
    CHECK(result.fromNewer);
    CHECK(root.getIntAttribute("stateVersion") == 9);
    CHECK(root.getStringAttribute("trail").isEmpty());
}

TEST_CASE("a missing, zero or negative version cannot be migrated", "[state-migration]") {
    const StateMigration steps[] = {marker(2)};
    for (const int version : {-1, 0, -5}) {
        auto root = documentAt(version);
        const auto result = migrateState(root, 2, steps);
        CHECK_FALSE(result.ok);
        CHECK(result.error.find("stateVersion") != std::string::npos);
    }
}

TEST_CASE("a missing step stops the migration before anything runs", "[state-migration]") {
    auto root = documentAt(1);
    const StateMigration steps[] = {marker(2)};
    const auto result = migrateState(root, 3, steps); // needs two steps
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("no migration") != std::string::npos);
    CHECK(root.getStringAttribute("trail").isEmpty());
    // an empty step is as good as a missing one
    const StateMigration empty[] = {StateMigration{}};
    auto other = documentAt(1);
    CHECK_FALSE(migrateState(other, 2, empty).ok);
}

TEST_CASE("a failing step gives its reason, or a default one", "[state-migration]") {
    const StateMigration steps[] = {marker(2),
                                    [](juce::XmlElement&, std::string& error) {
                                        error = "slots: cannot convert";
                                        return false;
                                    },
                                    marker(4)};
    auto root = documentAt(1);
    const auto result = migrateState(root, 4, steps);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "slots: cannot convert");
    CHECK(root.getStringAttribute("trail") == "2"); // step 4 never ran
    CHECK(root.getIntAttribute("stateVersion") == 2);

    const StateMigration silent[] = {[](juce::XmlElement&, std::string&) { return false; }};
    auto other = documentAt(1);
    const auto reason = migrateState(other, 2, silent);
    CHECK_FALSE(reason.ok);
    CHECK(reason.error.find("version 1") != std::string::npos);
}

TEST_CASE("this plugin version needs no migration yet", "[state-migration]") {
    CHECK(stateMigrations().empty());
    CHECK(kStateVersion == 1);
}

// ---- the processor ----------------------------------------------------------------------------------------------

TEST_CASE("a state from a newer version loads and says so", "[state-migration][plugin]") {
    InstrumentProcessor source("Test");
    fill(source);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved(source).getData(),
                                                       static_cast<int>(saved(source).getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("stateVersion", kStateVersion + 1);
    juce::MemoryBlock newer;
    juce::AudioProcessor::copyXmlToBinary(*root, newer);

    InstrumentProcessor target("Test");
    target.setStateInformation(newer.getData(), static_cast<int>(newer.getSize()));
    CHECK(target.activeSlot() == 3);
    CHECK_FALSE(target.slotsSnapshot().isEmpty(0));
    const auto problems = target.slotLoadProblems();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("newer plugin version") != std::string::npos);

    // the same state at the current version reports nothing
    InstrumentProcessor current("Test");
    const auto block = saved(source);
    current.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(current.slotLoadProblems().empty());
}

// ---- the state exactly as it comes back ------------------------------------------------------------------------

TEST_CASE("save, load and save again give the same state, with everything in it", "[state-migration][plugin]") {
    InstrumentProcessor source("Test");
    fill(source);
    const auto first = saved(source);

    InstrumentProcessor target("Test");
    target.setStateInformation(first.getData(), static_cast<int>(first.getSize()));
    CHECK(saved(target) == first);

    const auto a = source.slotsSnapshot();
    const auto b = target.slotsSnapshot();
    for (size_t i = 0; i < mm::core::kSlotCount; ++i) {
        REQUIRE(a.slot(i) != nullptr);
        REQUIRE(b.slot(i) != nullptr);
        CHECK(a.slot(i)->pattern == b.slot(i)->pattern);
        CHECK(a.slot(i)->name == b.slot(i)->name);
        CHECK(a.slot(i)->color == b.slot(i)->color);
        CHECK(a.slot(i)->history == b.slot(i)->history);
        CHECK(a.slot(i)->cursor == b.slot(i)->cursor);
    }
    CHECK(a.nextVersion() == b.nextVersion());
    CHECK(target.instanceSettings().generation.seed == std::optional<uint64_t>(77));
    CHECK(target.instanceSettings().generation.prompt == "dark & rolling <test>");
    CHECK(target.instanceSettings().generation.energyPct == 63);
    CHECK(target.instanceSettings().octave == -1);
    CHECK(target.activeSlot() == 3);
    CHECK(target.parameters().getRawParameterValue("mute_2")->load() >= 0.5f);
}

TEST_CASE("the stored state of version 1 still loads and saves back unchanged", "[state-migration][golden]") {
    InstrumentProcessor source("Test");
    fill(source);
    const auto current = xmlText(saved(source));
    if (updateGolden()) {
        std::ofstream out(goldenPath(), std::ios::binary | std::ios::trunc);
        out << current;
    }
    std::ifstream stream(goldenPath(), std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto golden = buffer.str();
    // Today's plugin writes exactly what version 1 wrote. A change here is a change of the format: raise
    // `kStateVersion`, add the migration, keep this file as the proof for old projects, and write the reason into
    // DECISIONS.md before renewing the golden state (MIDIMAID_UPDATE_GOLDEN=1).
    CHECK(current == golden);

    const auto parsed = juce::XmlDocument::parse(juce::String::fromUTF8(golden.c_str()));
    REQUIRE(parsed != nullptr);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*parsed, block);
    InstrumentProcessor loaded("Test");
    loaded.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(loaded.slotLoadProblems().empty());
    CHECK(xmlText(saved(loaded)) == golden);
    const auto bank = loaded.slotsSnapshot();
    REQUIRE(bank.slot(0)->pattern.has_value());
    CHECK(bank.slot(0)->name == "Intro");
    CHECK(bank.slot(1)->name == "Drop \xC3\xBC");
    CHECK(bank.slot(1)->pattern->lengthBars == 2);
    CHECK(bank.slot(2)->color == 3);
    CHECK(bank.slot(0)->history.size() == 2);
    CHECK(bank.slot(3)->pattern == std::nullopt);
    CHECK(loaded.activeSlot() == 3);
}

// ---- bad data --------------------------------------------------------------------------------------------------

TEST_CASE("damaged copies of a real state never crash and never spoil the next load", "[state-migration][plugin]") {
    InstrumentProcessor source("Test");
    fill(source);
    const auto good = xmlText(saved(source));
    std::mt19937 random(2026);
    InstrumentProcessor target("Test");
    for (int round = 0; round < 150; ++round) {
        std::string damaged = good;
        const int edits = 1 + static_cast<int>(random() % 6);
        for (int i = 0; i < edits; ++i) {
            const size_t at = random() % damaged.size();
            switch (random() % 4) {
            case 0:
                damaged[at] = static_cast<char>(random() % 256);
                break;
            case 1:
                damaged.erase(at, 1 + random() % 40);
                break;
            case 2:
                damaged.insert(at, std::string(1 + random() % 20, static_cast<char>('0' + random() % 10)));
                break;
            default:
                damaged.resize(at);
                break;
            }
            if (damaged.empty()) {
                damaged = "x";
            }
        }
        // as the host would hand it over: whatever XML survives, or the damaged bytes themselves
        if (const auto parsed = juce::XmlDocument::parse(juce::String::fromUTF8(damaged.c_str(), static_cast<int>(damaged.size())))) {
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary(*parsed, block);
            target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        }
        target.setStateInformation(damaged.data(), static_cast<int>(damaged.size()));
    }
    // a good state afterwards still loads completely
    const auto block = saved(source);
    target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(saved(target) == block);
}

TEST_CASE("absurd values in the attributes are cut to what the plugin can use", "[state-migration][plugin]") {
    InstrumentProcessor source("Test");
    fill(source);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved(source).getData(),
                                                       static_cast<int>(saved(source).getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("outputVoice", 99999);
    root->setAttribute("octave", -50);
    root->setAttribute("genBars", 3);
    root->setAttribute("genEnergy", 5000);
    root->setAttribute("genCreativity", -4);
    root->setAttribute("genRoot", "99");
    root->setAttribute("genScale", "no_such_scale");
    root->setAttribute("genSeed", "-12x");
    root->setAttribute("role", "emperor");
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*root, block);
    InstrumentProcessor target("Test");
    target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    const auto settings = target.instanceSettings();
    CHECK(settings.outputVoice >= 1);
    CHECK(settings.outputVoice <= 8);
    CHECK(settings.octave >= -2);
    CHECK(settings.octave <= 2);
    CHECK(settings.generation.energyPct >= 0);
    CHECK(settings.generation.energyPct <= 100);
    CHECK(settings.generation.creativityPct >= 0);
    CHECK(settings.generation.creativityPct <= 100);
    CHECK(mm::core::isValidPatternLength(settings.generation.lengthBars));
    CHECK_FALSE(settings.generation.root.has_value());
    CHECK_FALSE(settings.generation.scaleId.has_value());
    CHECK(settings.role == mm::core::InstanceRole::Solo);
    CHECK_FALSE(target.slotsSnapshot().isEmpty(0)); // the slots came through
}
