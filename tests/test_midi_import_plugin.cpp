#include "core/MidiFile.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace mm::plugin;
using namespace mm::core;

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 10000) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) {
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

void pumpFor(int ms) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

juce::Component* findById(juce::Component& parent, const juce::String& id) {
    for (auto* component : parent.getChildren()) {
        if (component->getComponentID() == id) {
            return component;
        }
        if (auto* deeper = findById(*component, id)) {
            return deeper;
        }
    }
    return nullptr;
}

/// A MIDI file with `notes` notes (one per bar from the start, 1/16 long) and `bars` bars of length.
std::vector<uint8_t> midiBytes(size_t notes, uint32_t bars, MidiFileOptions options = {}) {
    std::vector<PatternNote> list;
    for (size_t i = 0; i < notes; ++i) {
        list.push_back({static_cast<uint32_t>(i) * kTicksPerBar, 240, 1, 45, 96});
    }
    return writeMidiFile(PatternView{list.data(), list.size(), bars * kTicksPerBar}, options);
}

struct TempMidi {
    explicit TempMidi(const std::vector<uint8_t>& bytes, const juce::String& extension = ".mid")
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("mm_import_" + juce::Uuid().toString() + extension)) { // unique: tests run in parallel
        file.replaceWithData(bytes.data(), bytes.size());
    }
    ~TempMidi() { file.deleteFile(); }
    juce::File file;
};

bool settled(ProcessorBase& processor) {
    return processor.generationStatus() != GenerationStatus::Idle;
}

} // namespace

TEST_CASE("a dropped MIDI file is read in the background and the plan is kept", "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(3, 3));
    REQUIRE(processor.importMidi(midi.file, 1));
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::ImportRead);
    const auto& pending = processor.pendingImport();
    REQUIRE(pending.has_value());
    CHECK(pending->voice == 1);
    CHECK(pending->slot == 0);
    CHECK(pending->fileName == midi.file.getFileName().toStdString());
    CHECK(pending->plan.notes.size() == 3);
    CHECK(pending->plan.sourceBars == 3);
    CHECK(pending->plan.lengthBars == 4);
    processor.clearPendingImport();
    CHECK_FALSE(processor.pendingImport().has_value());
}

TEST_CASE("the slot of the drop is kept, .midi files count, and a failed read clears an older plan",
          "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    processor.selectSlot(4);
    TempMidi midi(midiBytes(2, 2), ".midi");
    REQUIRE(processor.importMidi(midi.file, 0));
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    REQUIRE(processor.pendingImport().has_value());
    CHECK(processor.pendingImport()->slot == 3);

    TempMidi broken(std::vector<uint8_t>{'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'});
    REQUIRE(processor.importMidi(broken.file, 0));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::ImportUnreadable; }));
    CHECK_FALSE(processor.pendingImport().has_value()); // the earlier plan does not stay around
}

TEST_CASE("a clip over 16 bars is cut and says so", "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(20, 20));
    REQUIRE(processor.importMidi(midi.file, 0));
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::ImportReadCut);
    REQUIRE(processor.pendingImport().has_value());
    CHECK(processor.pendingImport()->plan.lengthBars == 16);
    CHECK(processor.pendingImport()->plan.notes.size() == 16);
    CHECK(processor.pendingImport()->plan.droppedNotes == 4);
}

TEST_CASE("files that cannot be imported leave no plan and name the reason", "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    {
        MidiFileOptions waltz;
        waltz.timeSigNumerator = 3;
        TempMidi midi(midiBytes(2, 2, waltz));
        REQUIRE(processor.importMidi(midi.file, 0));
        REQUIRE(pumpUntil([&] { return settled(processor); }));
        CHECK(processor.generationStatus() == GenerationStatus::ImportNotFourFour);
        CHECK_FALSE(processor.pendingImport().has_value());
    }
    {
        TempMidi midi(midiBytes(0, 2));
        REQUIRE(processor.importMidi(midi.file, 0));
        REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::ImportNoNotes; }));
        CHECK_FALSE(processor.pendingImport().has_value());
    }
    {
        TempMidi midi(std::vector<uint8_t>{'n', 'o', 't', ' ', 'm', 'i', 'd', 'i', '!', '!', '!', '!', '!', '!'});
        REQUIRE(processor.importMidi(midi.file, 0));
        REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::ImportUnreadable; }));
        CHECK_FALSE(processor.pendingImport().has_value());
    }
    {
        const juce::File missing =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mm_gone.mid");
        REQUIRE(processor.importMidi(missing, 0));
        REQUIRE(pumpUntil([&] {
            return processor.generationStatus() == GenerationStatus::ImportNoNotes ||
                   processor.generationStatus() == GenerationStatus::ImportUnreadable;
        }));
        CHECK(processor.generationStatus() == GenerationStatus::ImportUnreadable);
    }
    {
        TempMidi text(std::vector<uint8_t>{'h', 'i'}, ".txt");
        CHECK_FALSE(processor.importMidi(text.file, 0)); // not a MIDI file by its name: refused at once
        CHECK(processor.generationStatus() == GenerationStatus::ImportUnreadable);
    }
}

TEST_CASE("a voice instance imports for the voice it plays", "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Voice");
    auto settings = processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    settings.outputVoice = 2;
    processor.setInstanceSettings(settings);
    TempMidi midi(midiBytes(2, 2));
    REQUIRE(processor.importMidi(midi.file, 5)); // the voice of the argument does not count here
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    REQUIRE(processor.pendingImport().has_value());
    CHECK(processor.pendingImport()->voice == 1);
}

TEST_CASE("only the newest of two quick drops arrives", "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi first(midiBytes(5, 8));
    TempMidi second(midiBytes(2, 2));
    REQUIRE(processor.importMidi(first.file, 0));
    REQUIRE(processor.importMidi(second.file, 1));
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    pumpFor(200);
    REQUIRE(processor.pendingImport().has_value());
    CHECK(processor.pendingImport()->fileName == second.file.getFileName().toStdString());
    CHECK(processor.pendingImport()->plan.notes.size() == 2);
}

TEST_CASE("destroying a processor during an import is safe", "[midi-import-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempMidi midi(midiBytes(8, 8));
    {
        InstrumentProcessor processor("Test");
        REQUIRE(processor.importMidi(midi.file, 0));
    }
    pumpFor(100); // the queued delivery finds the processor gone and does nothing
    SUCCEED();
}

TEST_CASE("the hub rows take MIDI files and show the result", "[midi-import-plugin][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* row = dynamic_cast<juce::FileDragAndDropTarget*>(findById(*editor, "row_2"));
    REQUIRE(row != nullptr);
    TempMidi midi(midiBytes(3, 4));
    TempMidi text(std::vector<uint8_t>{'h', 'i'}, ".txt");
    CHECK(row->isInterestedInFileDrag(juce::StringArray{midi.file.getFullPathName()}));
    CHECK(row->isInterestedInFileDrag(juce::StringArray{text.file.getFullPathName(), midi.file.getFullPathName()}));
    CHECK_FALSE(row->isInterestedInFileDrag(juce::StringArray{text.file.getFullPathName()}));

    row->filesDropped(juce::StringArray{midi.file.getFullPathName()}, 0, 0);
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    REQUIRE(processor.pendingImport().has_value());
    CHECK(processor.pendingImport()->voice == 1); // row 2 is the second voice
    auto* label = dynamic_cast<juce::Label*>(findById(*editor, "status"));
    REQUIRE(label != nullptr);
    REQUIRE(pumpUntil([&] { return label->getText().contains("MIDI gelesen"); }));
    CHECK(label->getText().contains("3 Noten"));

    // a drop of something that is no MIDI file does nothing
    processor.clearPendingImport();
    row->filesDropped(juce::StringArray{text.file.getFullPathName()}, 0, 0);
    pumpFor(100);
    CHECK_FALSE(processor.pendingImport().has_value());
}

TEST_CASE("the voice window takes MIDI files for its own voice", "[midi-import-plugin][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Voice");
    auto settings = processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    settings.outputVoice = 1;
    processor.setInstanceSettings(settings);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(editor.get());
    REQUIRE(target != nullptr);
    TempMidi midi(midiBytes(2, 2));
    REQUIRE(pumpUntil([&] { return target->isInterestedInFileDrag(juce::StringArray{midi.file.getFullPathName()}); }));
    target->filesDropped(juce::StringArray{midi.file.getFullPathName()}, 0, 0);
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    REQUIRE(processor.pendingImport().has_value());
    CHECK(processor.pendingImport()->voice == 0);
}

TEST_CASE("the hub window itself does not take drops outside the rows", "[midi-import-plugin][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(editor.get());
    REQUIRE(target != nullptr);
    TempMidi midi(midiBytes(2, 2));
    pumpFor(100);
    CHECK_FALSE(target->isInterestedInFileDrag(juce::StringArray{midi.file.getFullPathName()}));
}
