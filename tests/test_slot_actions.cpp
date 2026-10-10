#include "NoUserSettings.h"
#include "core/PatternGenerator.h"
#include "plugin/EditorParts.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

using namespace mm::plugin;
using namespace mm::core;

namespace {

Pattern make(ProcessorBase& processor, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = 2;
    request.seed = seed;
    const auto result = generatePattern(*processor.styles().find("peak_time"), request);
    REQUIRE(result.success);
    return result.pattern;
}

void fill(ProcessorBase& processor, size_t index, uint64_t seed, const std::string& name = "", uint8_t color = 0) {
    const auto pattern = make(processor, seed);
    processor.editSlots([&](SlotBank& bank) {
        REQUIRE(bank.setResult(index, pattern));
        bank.setName(index, name);
        bank.setColor(index, color);
    });
}

/// The voices are what a pattern sounds like; `version` is stamped by the bank on every store.
bool sameMusic(const Pattern& a, const Pattern& b) {
    return a.voices == b.voices && a.lengthBars == b.lengthBars && a.context == b.context && a.styleId == b.styleId;
}

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

/// Plays bars [fromBar, toBar) at 120 BPM and returns how many note-ons start in every bar.
std::vector<int> noteOnsPerBar(ProcessorBase& processor, int fromBar, int toBar) {
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;
    FakePlayHead head;
    processor.setRateAndBufferSizeDetails(kRate, kBlock);
    processor.setPlayHead(&head);
    processor.prepareToPlay(kRate, kBlock);
    std::vector<int> counts(static_cast<size_t>(toBar - fromBar), 0);
    juce::AudioBuffer<float> audio(2, kBlock);
    juce::MidiBuffer midi;
    const long long first = static_cast<long long>(fromBar * 2.0 * kRate);
    const long long last = static_cast<long long>(toBar * 2.0 * kRate);
    for (long long clock = first; clock < last; clock += kBlock) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(clock) / kRate * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            if (metadata.getMessage().isNoteOn()) {
                const auto bar = static_cast<size_t>((clock + metadata.samplePosition) / static_cast<long long>(2 * kRate)) -
                                 static_cast<size_t>(fromBar);
                if (bar < counts.size()) {
                    ++counts[bar];
                }
            }
        }
    }
    return counts;
}

template <typename T> T* child(juce::Component& parent, const juce::String& id) {
    for (auto* component : parent.getChildren()) {
        if (component->getComponentID() == id) {
            return dynamic_cast<T*>(component);
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("copy and paste carry pattern, name and colour and make a result with an undo step", "[slot-actions]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    CHECK_FALSE(processor.hasSlotClipboard());
    CHECK_FALSE(processor.copySlot(0)); // empty
    CHECK(processor.generationStatus() == GenerationStatus::SlotNothingToCopy);
    CHECK_FALSE(processor.pasteSlot(1)); // nothing on the clipboard
    CHECK(processor.generationStatus() == GenerationStatus::SlotNothingToCopy);

    fill(processor, 0, 11, "Drop", 3);
    REQUIRE(processor.copySlot(0));
    CHECK(processor.generationStatus() == GenerationStatus::SlotCopied);
    CHECK(processor.hasSlotClipboard());
    REQUIRE(processor.pasteSlot(4));
    CHECK(processor.generationStatus() == GenerationStatus::SlotPasted);

    auto bank = processor.slotsSnapshot();
    REQUIRE(bank.slot(4)->pattern.has_value());
    CHECK(sameMusic(*bank.slot(4)->pattern, *bank.slot(0)->pattern));
    CHECK(bank.slot(4)->name == "Drop");
    CHECK(bank.slot(4)->color == 3);
    CHECK(bank.slot(4)->history.size() == 1);
    CHECK(bank.slot(0)->pattern.has_value()); // the source stays

    // undo takes the paste back, redo brings it again
    REQUIRE(processor.canUndo());
    REQUIRE(processor.undo());
    CHECK(processor.slotsSnapshot().isEmpty(4));
    REQUIRE(processor.redo());
    CHECK_FALSE(processor.slotsSnapshot().isEmpty(4));

    // the clipboard survives: a second paste, over a filled slot, keeps the old pattern in the history
    fill(processor, 6, 12);
    const auto old = *processor.slotsSnapshot().slot(6)->pattern;
    REQUIRE(processor.pasteSlot(6));
    bank = processor.slotsSnapshot();
    CHECK(sameMusic(*bank.slot(6)->pattern, *bank.slot(0)->pattern));
    REQUIRE(processor.historyBack(6));
    CHECK(sameMusic(*processor.slotsSnapshot().slot(6)->pattern, old));
}

TEST_CASE("copying a slot to another is one action and leaves the clipboard of the instance", "[slot-actions]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    fill(processor, 2, 5);
    CHECK_FALSE(processor.copySlotTo(2, 2)); // onto itself: nothing
    REQUIRE(processor.copySlotTo(2, 9));
    const auto bank = processor.slotsSnapshot();
    CHECK(sameMusic(*bank.slot(9)->pattern, *bank.slot(2)->pattern));
    CHECK_FALSE(processor.copySlotTo(3, 10)); // slot 4 is empty
    CHECK(bank.isEmpty(10));
    CHECK_FALSE(processor.copySlot(99)); // out of range
}

TEST_CASE("clearing resets a slot completely and ends its undo steps", "[slot-actions]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    fill(processor, 0, 1, "Intro", 2);
    REQUIRE(processor.copySlot(0));
    REQUIRE(processor.pasteSlot(1)); // an undo step of slot 2
    REQUIRE(processor.canUndo());
    REQUIRE(processor.clearSlot(1));
    CHECK(processor.generationStatus() == GenerationStatus::SlotCleared);
    const auto bank = processor.slotsSnapshot();
    CHECK(bank.isEmpty(1));
    CHECK(bank.slot(1)->name.empty());
    CHECK(bank.slot(1)->color == 0);
    CHECK(bank.slot(1)->history.empty());
    CHECK_FALSE(processor.canUndo()); // the step no longer fits
    CHECK(bank.slot(0)->pattern.has_value());
    CHECK_FALSE(processor.clearSlot(99));
}

TEST_CASE("swapping exchanges two slots with name, colour and history, also with an empty one", "[slot-actions]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    fill(processor, 0, 1, "A", 1);
    fill(processor, 0, 2, "A", 1); // a second result: history of two
    fill(processor, 3, 3, "B", 5);
    const auto before = processor.slotsSnapshot();
    REQUIRE(processor.swapSlots(0, 3));
    CHECK(processor.generationStatus() == GenerationStatus::SlotsSwapped);
    auto after = processor.slotsSnapshot();
    CHECK(after.slot(3)->name == "A");
    CHECK(after.slot(3)->color == 1);
    CHECK(after.slot(3)->history.size() == before.slot(0)->history.size());
    CHECK(sameMusic(*after.slot(3)->pattern, *before.slot(0)->pattern));
    CHECK(after.slot(0)->name == "B");
    CHECK(sameMusic(*after.slot(0)->pattern, *before.slot(3)->pattern));

    // the undo steps of both slots end with a swap (they no longer fit)
    REQUIRE(processor.copySlot(0));
    REQUIRE(processor.pasteSlot(5));
    REQUIRE(processor.pasteSlot(6));
    REQUIRE(processor.canUndo());
    REQUIRE(processor.swapSlots(5, 6));
    CHECK_FALSE(processor.canUndo());

    REQUIRE(processor.swapSlots(0, 7)); // with an empty slot: a move
    after = processor.slotsSnapshot();
    CHECK(after.isEmpty(0));
    CHECK(after.slot(7)->name == "B");
    CHECK_FALSE(processor.swapSlots(2, 2));
    CHECK_FALSE(processor.swapSlots(2, 99));
}

TEST_CASE("a voice does not change the slots: it says use the hub", "[slot-actions]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    fill(processor, 0, 1);
    auto settings = processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    const auto before = processor.slotsSnapshot();
    CHECK_FALSE(processor.copySlot(0));
    CHECK(processor.generationStatus() == GenerationStatus::UseHub);
    CHECK_FALSE(processor.pasteSlot(1));
    CHECK_FALSE(processor.copySlotTo(0, 1));
    CHECK_FALSE(processor.clearSlot(0));
    CHECK_FALSE(processor.swapSlots(0, 1));
    const auto after = processor.slotsSnapshot();
    CHECK(after.slot(0)->pattern == before.slot(0)->pattern);
    CHECK(after.isEmpty(1));
}

TEST_CASE("clearing the playing slot silences it at the next bar line, a swap changes what plays",
          "[slot-actions][playback]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    fill(processor, 0, 21);
    processor.selectSlot(1);
    const auto before = noteOnsPerBar(processor, 0, 2);
    REQUIRE(before[0] + before[1] > 0);

    REQUIRE(processor.clearSlot(0));
    const auto after = noteOnsPerBar(processor, 2, 6);
    // the engine takes the change at a bar line: from the second bar on nothing sounds
    CHECK(after[1] == 0);
    CHECK(after[2] == 0);
    CHECK(after[3] == 0);

    // a pattern moved by a swap into the selected slot plays there
    fill(processor, 4, 22);
    REQUIRE(processor.swapSlots(4, 0));
    const auto swapped = noteOnsPerBar(processor, 6, 10);
    CHECK(swapped[1] + swapped[2] + swapped[3] > 0);
}

TEST_CASE("a slot field reads its drag description, and only accepts other fields", "[slot-actions][editor]") {
    CHECK(SlotButton::slotOf(SlotButton::describe(1)) == 1);
    CHECK(SlotButton::slotOf(SlotButton::describe(16)) == 16);
    CHECK(SlotButton::slotOf(juce::var("mm-slot:0")) == 0);
    CHECK(SlotButton::slotOf(juce::var("mm-slot:17")) == 0);
    CHECK(SlotButton::slotOf(juce::var("mm-slot:-1")) == 0);
    CHECK(SlotButton::slotOf(juce::var("mm-slot:")) == 0);
    CHECK(SlotButton::slotOf(juce::var("mm-slot:99999999999")) == 0);
    CHECK(SlotButton::slotOf(juce::var("slot:3")) == 0);
    CHECK(SlotButton::slotOf(juce::var()) == 0);

    SlotButton three(3);
    SlotButton five(5);
    const auto details = [&](int from) {
        return juce::DragAndDropTarget::SourceDetails(SlotButton::describe(from), &five, {});
    };
    CHECK(three.isInterestedInDragSource(details(5)));
    CHECK_FALSE(three.isInterestedInDragSource(details(3))); // itself
    CHECK_FALSE(three.isInterestedInDragSource(juce::DragAndDropTarget::SourceDetails(juce::var("file"), &five, {})));
}

TEST_CASE("dropping a slot field on another exchanges the slots, the editor's copy path copies",
          "[slot-actions][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    fill(processor, 0, 31, "One", 1);
    fill(processor, 1, 32, "Two", 2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* first = child<SlotButton>(*editor, "slot1");
    auto* second = child<SlotButton>(*editor, "slot2");
    auto* third = child<SlotButton>(*editor, "slot3");
    REQUIRE((first != nullptr && second != nullptr && third != nullptr));
    const auto original = processor.slotsSnapshot();

    // a plain drop (no Alt held in the test) exchanges
    second->itemDropped(juce::DragAndDropTarget::SourceDetails(SlotButton::describe(1), first, {}));
    auto bank = processor.slotsSnapshot();
    CHECK(bank.slot(0)->name == "Two");
    CHECK(bank.slot(1)->name == "One");
    CHECK(sameMusic(*bank.slot(1)->pattern, *original.slot(0)->pattern));

    // the copy path of the same callback
    REQUIRE(static_cast<bool>(third->onSlotDropped));
    third->onSlotDropped(2, 3, true);
    bank = processor.slotsSnapshot();
    CHECK(bank.slot(2)->name == "One");
    CHECK(bank.slot(1)->name == "One"); // the source stays
    // a menu is asked for with the slot and a position
    int asked = 0;
    third->onMenu = [&](int slot, juce::Point<int>) { asked = slot; };
    third->onMenu(3, {});
    CHECK(asked == 3);
}
