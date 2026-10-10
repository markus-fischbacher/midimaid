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
std::vector<uint8_t> midiBytes(size_t notes, uint32_t bars, MidiFileOptions options = {}, uint8_t pitch = 45) {
    std::vector<PatternNote> list;
    for (size_t i = 0; i < notes; ++i) {
        list.push_back({static_cast<uint32_t>(i) * kTicksPerBar, 240, 1, pitch, 96});
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

/// The import was applied and the generation around it has ended.
bool importDone(ProcessorBase& processor) {
    const auto status = processor.generationStatus();
    return status != GenerationStatus::Idle && status != GenerationStatus::Generating &&
           status != GenerationStatus::ImportApplied;
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

    // since P4-C a drop on a row puts the clip into that voice (D-178); P4-A only read it
    row->filesDropped(juce::StringArray{midi.file.getFullPathName()}, 0, 0);
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    const auto pattern = processor.slotsSnapshot().slot(0)->pattern;
    REQUIRE(pattern.has_value());
    CHECK(isVoiceLocked(pattern->voices[1])); // row 2 is the second voice
    CHECK(pattern->voices[1].notes.size() == 3);
    CHECK_FALSE(isVoiceLocked(pattern->voices[0]));
    auto* label = dynamic_cast<juce::Label*>(findById(*editor, "status"));
    REQUIRE(label != nullptr);
    REQUIRE(pumpUntil([&] { return label->getText().contains("gesperrte Stimmen"); }));

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
    // a voice window only shows (SPEC 8.1): the hub applies imports, the voice says so
    CHECK(processor.generationStatus() == GenerationStatus::UseHub);
    CHECK_FALSE(processor.slotsSnapshot().slot(0)->pattern.has_value());
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

TEST_CASE("an imported voice is put into the slot, locked, and the others are generated around it",
          "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(3, 3));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::DoneLocked);
    CHECK_FALSE(processor.pendingImport().has_value()); // used up, not kept

    const auto pattern = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(pattern.lengthBars == 4); // 3 bars rounded up
    CHECK(isVoiceLocked(pattern.voices[0]));
    REQUIRE(pattern.voices[0].notes.size() == 3);
    CHECK(pattern.voices[0].notes[1].startTick == kTicksPerBar);
    CHECK(pattern.voices[0].notes[1].pitch == 45);
    CHECK(pattern.voices[0].notes[1].velocity == 96);
    CHECK(pattern.voices[0].notes[1].lengthTicks == 240);
    CHECK_FALSE(pattern.voices[1].notes.empty());
    CHECK_FALSE(pattern.voices[1].archetypeId.empty());
    CHECK(pattern.context.root == 9); // the bass is an A

    // key, scale and length of the settings are the ones that were found
    const auto settings = processor.instanceSettings().generation;
    CHECK(settings.root == pattern.context.root);
    CHECK(settings.scaleId == pattern.context.scaleId);
    CHECK(settings.lengthBars == 4);

    // two undo steps: the generation, then the import
    REQUIRE(processor.undo());
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[1].notes.empty());
    CHECK(isVoiceLocked(processor.slotsSnapshot().slot(0)->pattern->voices[0]));
    REQUIRE(processor.undo());
    CHECK_FALSE(processor.slotsSnapshot().slot(0)->pattern.has_value());
}

TEST_CASE("a clip over 16 bars is cut and the final status says so", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(20, 20));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::DoneImportCut);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->lengthBars == 16);
    CHECK(processor.instanceSettings().generation.lengthBars == 16);
    // the notice does not stay for the next generation
    processor.generate();
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::DoneLocked);
}

TEST_CASE("an import into a slot that is not selected any more is not generated around", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(2, 2));
    REQUIRE(processor.importVoice(midi.file, 0)); // the slot of the drop is slot 1
    processor.selectSlot(2);
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    pumpFor(300);
    CHECK(processor.generationStatus() == GenerationStatus::ImportApplied);
    const auto pattern = processor.slotsSnapshot().slot(0)->pattern;
    REQUIRE(pattern.has_value());
    CHECK(isVoiceLocked(pattern->voices[0]));
    CHECK(pattern->voices[1].notes.empty());
    CHECK_FALSE(processor.slotsSnapshot().slot(1)->pattern.has_value());
}

TEST_CASE("a second import replaces the voice again", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi first(midiBytes(4, 4));
    REQUIRE(processor.importVoice(first.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    const auto firstResult = *processor.slotsSnapshot().slot(0)->pattern;
    TempMidi second(midiBytes(2, 4));
    REQUIRE(processor.importVoice(second.file, 0));
    // the status of the first run still stands until the second file arrives: wait for its effect
    REQUIRE(pumpUntil([&] {
        const auto pattern = processor.slotsSnapshot().slot(0)->pattern;
        return pattern && pattern->voices[0].notes.size() == 2 && importDone(processor);
    }));
    const auto pattern = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(isVoiceLocked(pattern.voices[0]));
    CHECK_FALSE(pattern.voices[1].notes.empty());
    CHECK_FALSE(pattern.voices[1].notes == firstResult.voices[1].notes); // generated again for the new harmony
}

TEST_CASE("a voice instance does not apply an import", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Voice");
    auto settings = processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    TempMidi midi(midiBytes(2, 2));
    REQUIRE(processor.importVoice(midi.file));
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::UseHub);
    CHECK_FALSE(processor.slotsSnapshot().slot(0)->pattern.has_value());
    CHECK_FALSE(processor.correctKey(4, "dorian"));
}

TEST_CASE("an unusable file is not applied", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi bad(std::vector<uint8_t>{'n', 'o', 'p', 'e'});
    REQUIRE(processor.importVoice(bad.file, 0));
    REQUIRE(pumpUntil([&] { return settled(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::ImportUnreadable);
    CHECK_FALSE(processor.slotsSnapshot().slot(0)->pattern.has_value());
    TempMidi text(std::vector<uint8_t>{'h', 'i'}, ".txt");
    CHECK_FALSE(processor.importVoice(text.file, 0));
}

TEST_CASE("the key of an imported slot can be corrected and the voice stays", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    CHECK_FALSE(processor.correctKey(4, "dorian")); // an empty slot
    TempMidi midi(midiBytes(4, 4));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    const auto before = *processor.slotsSnapshot().slot(0)->pattern;

    REQUIRE(processor.correctKey(4, "dorian"));
    const auto after = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(after.context.root == 4);
    CHECK(after.context.scaleId == "dorian");
    CHECK(after.voices[0] == before.voices[0]);
    CHECK(after.voices[1] == before.voices[1]);
    CHECK(processor.instanceSettings().generation.root == 4);
    CHECK(processor.instanceSettings().generation.scaleId == "dorian");
    CHECK_FALSE(processor.correctKey(4, "no_such_scale"));
    REQUIRE(processor.undo()); // the correction is an undo step of its own
    CHECK(processor.slotsSnapshot().slot(0)->pattern->context.root == before.context.root);
}

TEST_CASE("the key boxes of the editor correct the key of a slot with an imported voice",
          "[midi-import-plugin][voice-import][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    TempMidi midi(midiBytes(4, 4));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    auto* key = dynamic_cast<juce::ComboBox*>(findById(*editor, "key"));
    auto* scale = dynamic_cast<juce::ComboBox*>(findById(*editor, "scale"));
    REQUIRE(key != nullptr);
    REQUIRE(scale != nullptr);
    const auto before = *processor.slotsSnapshot().slot(0)->pattern;

    key->setSelectedId(2 + 4, juce::sendNotificationSync); // E
    auto after = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(after.context.root == 4);
    CHECK(after.context.scaleId == before.context.scaleId);
    CHECK(after.voices[0] == before.voices[0]);

    int dorian = 0;
    for (int i = 0; i < scale->getNumItems(); ++i) {
        if (scale->getItemText(i).containsIgnoreCase("dorisch")) {
            dorian = scale->getItemId(i);
        }
    }
    REQUIRE(dorian != 0);
    scale->setSelectedId(dorian, juce::sendNotificationSync);
    after = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(after.context.scaleId == "dorian");
    CHECK(after.voices[0] == before.voices[0]);
}

TEST_CASE("the settings take the key that was found in the clip", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(4, 4, {}, 50)); // a D bass
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    const auto pattern = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(pattern.context.root == 2);
    CHECK(processor.instanceSettings().generation.root == 2);
    CHECK(processor.instanceSettings().generation.scaleId == pattern.context.scaleId);
}

TEST_CASE("a correction that changes nothing is no undo step", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(4, 4));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    const auto context = processor.slotsSnapshot().slot(0)->pattern->context;
    CHECK_FALSE(processor.correctKey(context.root, context.scaleId));
    REQUIRE(processor.undo()); // the step that comes first is the generation, not a correction
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[1].notes.empty());
}

TEST_CASE("the cut notice does not wait for a later generation when nothing was generated",
          "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.lengthBars = 16;
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    REQUIRE(processor.setVoiceLocked(0, 1, true)); // the other voice is locked: nothing is left to generate
    TempMidi midi(midiBytes(20, 20));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::AllLocked; }));
    REQUIRE(processor.setVoiceLocked(0, 1, false));
    processor.generate();
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::DoneLocked);
}

TEST_CASE("after the lock is released an imported voice can be varied", "[midi-import-plugin][voice-import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    TempMidi midi(midiBytes(4, 4));
    REQUIRE(processor.importVoice(midi.file, 0));
    REQUIRE(pumpUntil([&] { return importDone(processor); }));
    const auto imported = processor.slotsSnapshot().slot(0)->pattern->voices[0];

    CHECK_FALSE(processor.vary(100, 0)); // locked: nothing happens to the voice
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[0] == imported);

    REQUIRE(processor.setVoiceLocked(0, 0, false));
    CHECK_FALSE(isVoiceLocked(processor.slotsSnapshot().slot(0)->pattern->voices[0]));
    REQUIRE(processor.vary(100, 0, 7));
    CHECK(processor.lastVariationChanges() > 0);
    CHECK_FALSE(processor.slotsSnapshot().slot(0)->pattern->voices[0].notes == imported.notes);
}
