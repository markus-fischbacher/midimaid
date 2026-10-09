#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "core/TextKeys.h"
#include "core/Theory.h"
#include "plugin/EmbeddedTranslation.h"
#include "plugin/GroupText.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override {
        if (!available) {
            return {};
        }
        return info;
    }

    PositionInfo info;
    bool available = true;
};

struct NoteEvent {
    long long sample;
    bool on;
};

/// Runs `blocks` blocks of `blockSize` samples at 120 BPM from PPQ 0 through the processor.
std::vector<NoteEvent> run(juce::AudioProcessor& processor, FakePlayHead& head, int blocks, int blockSize,
                           int numChannels) {
    constexpr double kSampleRate = 48000.0;
    processor.setRateAndBufferSizeDetails(kSampleRate, blockSize);
    processor.setPlayHead(&head);
    processor.prepareToPlay(kSampleRate, blockSize);

    std::vector<NoteEvent> events;
    juce::AudioBuffer<float> audio(numChannels, blockSize);
    juce::MidiBuffer midi;
    long long clock = 0;
    for (int block = 0; block < blocks; ++block) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(clock) / kSampleRate * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            events.push_back({clock + metadata.samplePosition, message.isNoteOn()});
        }
        clock += blockSize;
    }
    return events;
}

/// The instance starts silent (D-140). These tests were written against the one-bar offbeat pattern of phase 0, so
/// they put exactly that pattern into the selected slot before the transport starts.
void useOffbeatPattern(mm::plugin::ProcessorBase& processor) {
    const auto view = mm::core::placeholderPattern();
    std::vector<mm::core::PatternNote> notes(view.notes, view.notes + view.count);
    auto pattern = std::make_unique<mm::engine::OwnedPattern>();
    pattern->notes = std::move(notes);
    pattern->lengthTicks = view.lengthTicks;
    processor.switchPattern(std::move(pattern));
}

} // namespace

TEST_CASE("instrument processor plays the pattern of its slot from the host playhead", "[plugin]") {
    mm::plugin::InstrumentProcessor processor("Test");
    useOffbeatPattern(processor);
    FakePlayHead head;
    const auto events = run(processor, head, 100, 512, 2); // about 1.07 s = 2.13 beats

    REQUIRE(events.size() >= 4);
    CHECK(events[0].on);
    CHECK(std::abs(events[0].sample - 12000) <= 1); // PPQ 0.5
    CHECK_FALSE(events[1].on);                      // note ends 0.375 PPQ later
    CHECK(std::abs(events[1].sample - 21000) <= 1); // PPQ 0.875
    CHECK(std::abs(events[2].sample - 36000) <= 1); // PPQ 1.5
}

TEST_CASE("MIDI-FX processor without audio buses plays too", "[plugin]") {
    mm::plugin::MidiFxProcessor processor("Test");
    useOffbeatPattern(processor);
    FakePlayHead head;
    const auto events = run(processor, head, 100, 512, 0);
    REQUIRE(events.size() >= 2);
    CHECK(events[0].on);
}

TEST_CASE("no playhead position: nothing is played", "[plugin]") {
    mm::plugin::InstrumentProcessor processor("Test");
    FakePlayHead head;
    head.available = false;
    CHECK(run(processor, head, 100, 512, 2).empty());
}

TEST_CASE("bypass releases a sounding note", "[plugin]") {
    mm::plugin::InstrumentProcessor processor("Test");
    useOffbeatPattern(processor);
    FakePlayHead head;
    // 30 blocks of 512 = 15360 samples: the first note (from sample 12000) is sounding.
    auto events = run(processor, head, 30, 512, 2);
    REQUIRE(!events.empty());
    REQUIRE(events.back().on);

    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlockBypassed(audio, midi);
    int noteOffs = 0;
    for (const auto metadata : midi) {
        noteOffs += metadata.getMessage().isNoteOff() ? 1 : 0;
    }
    CHECK(noteOffs == 1);
}

namespace {

bool waitForExport(mm::plugin::MidiExporter& exporter, double bpm) {
    for (int i = 0; i < 400; ++i) {
        if (exporter.readyFile() != juce::File() && std::abs(exporter.exportedBpm() - bpm) < 0.01) {
            return true;
        }
        juce::Thread::sleep(5);
    }
    return false;
}

/// A pattern with four bass notes on the offbeats (steps 2, 6, 10, 14) in slot `slot` (1-based).
mm::core::Pattern offbeatBass(uint8_t pitch) {
    mm::core::Pattern pattern = mm::core::makeEmptyPattern(1, "peak_time");
    for (const uint32_t step : {2u, 6u, 10u, 14u}) {
        mm::core::Note note;
        note.id = mm::core::allocateNoteId(pattern);
        note.pitch = pitch;
        note.startTick = step * 240;
        note.lengthTicks = 240;
        pattern.voices[0].notes.push_back(note);
    }
    return pattern;
}

void fillSlot(mm::plugin::ProcessorBase& processor, size_t index, const mm::core::Pattern& pattern) {
    processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(index, pattern)); });
}

/// The pitches of the note-ons of a .mid file in order.
std::vector<int> notePitches(const juce::File& file, double* bpm = nullptr) {
    juce::MidiFile midi;
    juce::FileInputStream stream(file);
    REQUIRE(stream.openedOk());
    REQUIRE(midi.readFrom(stream));
    REQUIRE(midi.getNumTracks() == 1);
    CHECK(midi.getTimeFormat() == 960);
    std::vector<int> pitches;
    for (const auto* event : *midi.getTrack(0)) {
        if (event->message.isNoteOn()) {
            pitches.push_back(event->message.getNoteNumber());
        }
        if (bpm != nullptr && event->message.isTempoMetaEvent()) {
            *bpm = 60.0 / event->message.getTempoSecondsPerQuarterNote();
        }
    }
    return pitches;
}

void setHostTempo(mm::plugin::ProcessorBase& processor, FakePlayHead& head, double bpm) {
    head.info.setBpm(bpm);
    head.info.setPpqPosition(0.0);
    head.info.setIsPlaying(false);
    processor.setPlayHead(&head);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
}

} // namespace

TEST_CASE("the export carries the notes of the playing slot and the DAW tempo", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, offbeatBass(45));
    FakePlayHead head;
    setHostTempo(processor, head, 128.0);
    processor.updateExport();
    REQUIRE(waitForExport(processor.midiExporter(), 128.0));

    const auto file = processor.midiExporter().readyFile();
    CHECK(file.getFileName() == "MidiMaid_Slot1_Bass.mid");
    double bpm = 0.0;
    const auto pitches = notePitches(file, &bpm);
    CHECK(pitches == std::vector<int>(pitches.size(), 45));
    CHECK(pitches.size() >= 3);
    CHECK(std::abs(bpm - 128.0) < 0.01);

    juce::MidiFile midi;
    juce::FileInputStream stream(file);
    REQUIRE(midi.readFrom(stream));
    int noteOffs = 0;
    for (const auto* event : *midi.getTrack(0)) {
        noteOffs += event->message.isNoteOff() ? 1 : 0;
    }
    CHECK(noteOffs == static_cast<int>(pitches.size()));
}

TEST_CASE("the export follows the slot, the octave and the tempo", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, offbeatBass(45));
    fillSlot(processor, 1, offbeatBass(50));
    FakePlayHead head;
    setHostTempo(processor, head, 120.0);
    processor.updateExport();
    REQUIRE(waitForExport(processor.midiExporter(), 120.0));
    const auto first = processor.midiExporter().readyFile();
    CHECK(notePitches(first).front() == 45);

    processor.selectSlot(2);
    setHostTempo(processor, head, 120.0); // the audio thread reports the slot that plays
    processor.updateExport();
    for (int i = 0; i < 400 && processor.midiExporter().readyFile().getFileName() != "MidiMaid_Slot2_Bass.mid"; ++i) {
        juce::Thread::sleep(5);
    }
    const auto second = processor.midiExporter().readyFile();
    REQUIRE(second.getFileName() == "MidiMaid_Slot2_Bass.mid");
    CHECK(notePitches(second).front() == 50);
    for (int i = 0; i < 400 && first.exists(); ++i) {
        juce::Thread::sleep(5);
    }
    CHECK_FALSE(first.exists()); // the file of the slot before is gone

    auto settings = processor.instanceSettings();
    settings.octave = 1;
    processor.setInstanceSettings(settings);
    processor.updateExport();
    for (int i = 0; i < 400 && notePitches(processor.midiExporter().readyFile()).front() != 62; ++i) {
        juce::Thread::sleep(5);
    }
    CHECK(notePitches(processor.midiExporter().readyFile()).front() == 62);

    setHostTempo(processor, head, 140.0);
    processor.updateExport();
    CHECK(waitForExport(processor.midiExporter(), 140.0));
}

/// Bass plays `bass` and the melody plays `melody`, four notes each on the offbeats.
mm::core::Pattern twoVoices(uint8_t bass, uint8_t melody) {
    mm::core::Pattern pattern = offbeatBass(bass);
    REQUIRE(pattern.voices.size() >= 2);
    for (const uint32_t step : {2u, 6u, 10u, 14u}) {
        mm::core::Note note;
        note.id = mm::core::allocateNoteId(pattern);
        note.pitch = melody;
        note.startTick = step * 240;
        note.lengthTicks = 240;
        pattern.voices[1].notes.push_back(note);
    }
    return pattern;
}

bool waitUntil(const std::function<bool()>& condition) {
    for (int i = 0; i < 400; ++i) {
        if (condition()) {
            return true;
        }
        juce::Thread::sleep(5);
    }
    return condition();
}

TEST_CASE("a solo instance exports every voice, the primary one is its own", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, twoVoices(45, 72));
    processor.updateExport();
    auto& exporter = processor.midiExporter();
    REQUIRE(waitForExport(exporter, 120.0));
    const auto bass = exporter.readyFile(1);
    const auto melody = exporter.readyFile(2);
    REQUIRE(bass != juce::File());
    REQUIRE(melody != juce::File());
    CHECK(bass.getFileName() == "MidiMaid_Slot1_Bass.mid");
    CHECK(melody.getFileName() == "MidiMaid_Slot1_Melody.mid");
    CHECK(exporter.readyFile() == bass); // the own voice is the first
    CHECK(notePitches(bass).front() == 45);
    CHECK(notePitches(melody).front() == 72);
    CHECK(exporter.readyFile(3) == juce::File()); // the pattern has two voices
    CHECK(exporter.readyFile(0) == juce::File());
}

TEST_CASE("the primary file is the one of the output voice", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, twoVoices(45, 72));
    auto settings = processor.instanceSettings();
    settings.outputVoice = 2;
    processor.setInstanceSettings(settings);
    processor.updateExport();
    auto& exporter = processor.midiExporter();
    REQUIRE(waitForExport(exporter, 120.0));
    CHECK(exporter.readyFile().getFileName() == "MidiMaid_Slot1_Melody.mid");
    CHECK(exporter.readyFile(1).getFileName() == "MidiMaid_Slot1_Bass.mid");
}

TEST_CASE("a voice exports only its own voice, and a change of role changes the files", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, twoVoices(45, 72));
    processor.updateExport();
    auto& exporter = processor.midiExporter();
    REQUIRE(waitForExport(exporter, 120.0));
    const auto bass = exporter.readyFile(1);
    const auto melody = exporter.readyFile(2);
    REQUIRE(melody != juce::File());

    auto settings = processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    settings.outputVoice = 2;
    processor.setInstanceSettings(settings);
    processor.updateExport();
    REQUIRE(waitUntil([&] { return exporter.readyFile(1) == juce::File(); }));
    CHECK(exporter.readyFile(2) == melody);
    CHECK(exporter.readyFile() == melody);
    CHECK(waitUntil([&] { return !bass.exists(); })); // the file of the other voice is gone
    CHECK(melody.existsAsFile());

    settings.role = mm::core::InstanceRole::Solo; // and back
    processor.setInstanceSettings(settings);
    processor.updateExport();
    REQUIRE(waitUntil([&] { return exporter.readyFile(1) != juce::File() && exporter.readyFile(1).existsAsFile(); }));
    CHECK(notePitches(exporter.readyFile(1)).front() == 45);
}

TEST_CASE("the octave of the instance moves its own voice in the export, not the others", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, twoVoices(45, 72));
    auto settings = processor.instanceSettings();
    settings.octave = 1;
    settings.outputVoice = 2;
    processor.setInstanceSettings(settings);
    processor.updateExport();
    auto& exporter = processor.midiExporter();
    REQUIRE(waitForExport(exporter, 120.0));
    CHECK(notePitches(exporter.readyFile(2)).front() == 84);
    CHECK(notePitches(exporter.readyFile(1)).front() == 45);
}

TEST_CASE("every voice of a pattern is exported, however many there are", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    mm::core::Pattern pattern = twoVoices(45, 72);
    pattern.voices.push_back(pattern.voices[1]); // a third voice
    pattern.voices[2].midiChannel = 3;
    for (auto& note : pattern.voices[2].notes) {
        note.id = mm::core::allocateNoteId(pattern);
        note.pitch = 60;
    }
    processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });
    processor.updateExport();
    auto& exporter = processor.midiExporter();
    REQUIRE(waitForExport(exporter, 120.0));
    REQUIRE(exporter.readyFile(3) != juce::File());
    CHECK(exporter.readyFile(3).getFileName() == "MidiMaid_Slot1_Voice3.mid");
    CHECK(notePitches(exporter.readyFile(3)).front() == 60);
    CHECK(processor.playingVoices().size() == 3);
}

TEST_CASE("leaving the slot removes the files of all its voices", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, twoVoices(45, 72));
    fillSlot(processor, 1, twoVoices(50, 77));
    processor.updateExport();
    auto& exporter = processor.midiExporter();
    REQUIRE(waitForExport(exporter, 120.0));
    const auto oldBass = exporter.readyFile(1);
    const auto oldMelody = exporter.readyFile(2);
    processor.selectSlot(2);
    FakePlayHead head;
    setHostTempo(processor, head, 120.0); // the audio thread reports the slot that plays
    processor.updateExport();
    REQUIRE(waitUntil([&] { return exporter.readyFile(2).getFileName() == "MidiMaid_Slot2_Melody.mid"; }));
    CHECK(waitUntil([&] { return !oldBass.exists() && !oldMelody.exists(); }));
    CHECK(notePitches(exporter.readyFile(2)).front() == 77);
}

TEST_CASE("the playing voices are those of the pattern, with the octave on the own voice only", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(processor.playingVoices().empty()); // an empty slot has no voices
    fillSlot(processor, 0, twoVoices(45, 72));
    auto settings = processor.instanceSettings();
    settings.octave = -1;
    settings.outputVoice = 1;
    processor.setInstanceSettings(settings);
    const auto views = processor.playingVoices();
    REQUIRE(views.size() == 2);
    CHECK(views[0].voice == 1);
    CHECK(views[1].voice == 2);
    CHECK(views[0].octave == -1);
    CHECK(views[1].octave == 0);
    CHECK(views[0].notes.front().pitch == 33);
    CHECK(views[1].notes.front().pitch == 72);
    CHECK(views[0].voiceName == "Bass");
    CHECK(views[1].voiceName == "Melody");
    CHECK(views[0].slot == 1);
    CHECK(views[0].sameSource(processor.playingVoice())); // the own voice is the same view
}

TEST_CASE("an empty slot leaves nothing to drag", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, offbeatBass(45));
    processor.updateExport();
    REQUIRE(waitForExport(processor.midiExporter(), 120.0));
    const auto file = processor.midiExporter().readyFile();

    processor.editSlots([](mm::core::SlotBank& bank) { REQUIRE(bank.clear(0)); });
    processor.updateExport();
    for (int i = 0; i < 400 && processor.midiExporter().readyFile() != juce::File(); ++i) {
        juce::Thread::sleep(5);
    }
    CHECK(processor.midiExporter().readyFile() == juce::File());
    for (int i = 0; i < 400 && file.exists(); ++i) {
        juce::Thread::sleep(5);
    }
    CHECK_FALSE(file.exists());
}

TEST_CASE("exported file is removed with the instance", "[plugin][export]") {
    juce::File file;
    {
        mm::plugin::InstrumentProcessor processor("Test");
        fillSlot(processor, 0, offbeatBass(45));
        processor.updateExport();
        REQUIRE(waitForExport(processor.midiExporter(), 120.0));
        file = processor.midiExporter().readyFile();
        REQUIRE(file.existsAsFile());
    }
    CHECK_FALSE(file.exists());
    CHECK_FALSE(file.getParentDirectory().exists());
}

TEST_CASE("editor starts the export and can be created and destroyed", "[plugin][export]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    fillSlot(processor, 0, offbeatBass(45));
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        CHECK(waitForExport(processor.midiExporter(), processor.lastKnownBpm()));
    }
}

TEST_CASE("last known tempo follows the host", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(processor.lastKnownBpm() == 120.0);
    FakePlayHead head;
    run(processor, head, 2, 512, 2); // runs at 120 BPM
    head.info.setBpm(140.0);
    head.info.setPpqPosition(0.0);
    head.info.setIsPlaying(true);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    CHECK(processor.lastKnownBpm() == 140.0);
}

namespace {

/// A processor driven like a host would: 512-sample blocks at 120 BPM, one persistent MIDI buffer.
class Rig {
public:
    explicit Rig(mm::plugin::ProcessorBase& processor) : processor_(processor), audio_(2, kBlock) {
        processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
        processor.setPlayHead(&head_);
        processor.prepareToPlay(kSampleRate, kBlock);
    }

    struct Note {
        long long sample;
        bool on;
        int pitch;
    };

    std::vector<Note> run(int blocks) {
        std::vector<Note> notes;
        for (int i = 0; i < blocks; ++i) {
            head_.info.setIsPlaying(true);
            head_.info.setBpm(120.0);
            head_.info.setPpqPosition(static_cast<double>(clock_) / kSampleRate * 2.0);
            audio_.clear();
            midi_.clear();
            processor_.processBlock(audio_, midi_);
            for (const auto metadata : midi_) {
                const auto message = metadata.getMessage();
                notes.push_back({clock_ + metadata.samplePosition, message.isNoteOn(), message.getNoteNumber()});
            }
            clock_ += kBlock;
        }
        return notes;
    }

    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlock = 512;

private:
    mm::plugin::ProcessorBase& processor_;
    FakePlayHead head_;
    juce::AudioBuffer<float> audio_;
    juce::MidiBuffer midi_;
    long long clock_ = 0;
};

std::unique_ptr<mm::engine::OwnedPattern> ownedPattern(std::vector<mm::core::PatternNote> notes, uint64_t version,
                                                       std::shared_ptr<const void> token = {}) {
    auto pattern = std::make_unique<mm::engine::OwnedPattern>();
    pattern->notes = std::move(notes);
    pattern->lengthTicks = 4 * mm::core::kTicksPerQuarter;
    pattern->version = version;
    pattern->lifetimeToken = std::move(token);
    return pattern;
}

} // namespace

TEST_CASE("a switch queued on the processor takes effect at the next bar line", "[plugin][handover]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    CHECK(processor.activePatternVersion() == 0);
    rig.run(10);
    processor.switchPattern(ownedPattern({{0, 240, 1, 50, 100}}, 5));
    const auto notes = rig.run(200); // to about 4.3 PPQ
    std::vector<Rig::Note> fifties;
    for (const auto& n : notes) {
        if (n.pitch == 50 && n.on) {
            fifties.push_back(n);
        }
    }
    REQUIRE(fifties.size() == 1);
    CHECK(std::abs(fifties.front().sample - 96000) <= 1); // PPQ 4.0
    CHECK(processor.activePatternVersion() == 5);
    for (const auto& n : notes) {
        CHECK((n.pitch == 50 || n.sample < 96000)); // the placeholder stops at the bar line
    }
}

TEST_CASE("an edit queued on the processor keeps the position", "[plugin][handover]") {
    mm::plugin::MidiFxProcessor processor("Test");
    Rig rig(processor);
    rig.run(10);
    processor.editPattern(ownedPattern({{1920, 240, 1, 50, 100}}, 6)); // position 2.0
    const auto notes = rig.run(120);                                   // to about 2.6 PPQ
    std::vector<Rig::Note> fifties;
    for (const auto& n : notes) {
        CHECK((n.pitch == 50 || n.sample < 5120 + 512)); // no placeholder note after the edit
        if (n.pitch == 50 && n.on) {
            fifties.push_back(n);
        }
    }
    REQUIRE(fifties.size() == 1);
    CHECK(std::abs(fifties.front().sample - 48000) <= 1); // PPQ 2.0: no restart
    CHECK(processor.activePatternVersion() == 6);
}

TEST_CASE("destroying a processor frees active and pending patterns", "[plugin][handover]") {
    auto active = std::make_shared<int>(0);
    auto pending = std::make_shared<int>(0);
    auto replaced = std::make_shared<int>(0);
    {
        mm::plugin::InstrumentProcessor processor("Test");
        Rig rig(processor);
        processor.editPattern(ownedPattern({{0, 240, 1, 50, 100}}, 1, replaced));
        rig.run(2);
        processor.editPattern(ownedPattern({{0, 240, 1, 51, 100}}, 2, active)); // retires the first one
        rig.run(2);
        processor.switchPattern(ownedPattern({{0, 240, 1, 52, 100}}, 3, pending)); // never taken
        CHECK(active.use_count() == 2);
        CHECK(pending.use_count() == 2);
    }
    CHECK(active.use_count() == 1);
    CHECK(pending.use_count() == 1);
    CHECK(replaced.use_count() == 1);
}

TEST_CASE("the host MIDI buffer keeps its storage across blocks", "[plugin][handover]") {
    // The buffer belongs to the host (O-23, D-134): the processor reserves the bound once. JUCE allocates the storage
    // with malloc, which cannot be counted portably, so this only guards that the storage does not move between
    // blocks (the address of the first event).
    mm::plugin::InstrumentProcessor processor("Test");
    useOffbeatPattern(processor);
    FakePlayHead head;
    processor.setRateAndBufferSizeDetails(48000.0, 512);
    processor.setPlayHead(&head);
    processor.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    const juce::uint8* storage = nullptr;
    int blocksWithEvents = 0;
    for (int block = 0; block < 400; ++block) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(block) * 512.0 / 48000.0 * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            if (storage == nullptr) {
                storage = metadata.data;
            }
            CHECK(metadata.data - storage < 12288); // inside the one reserved block
            ++blocksWithEvents;
            break;
        }
    }
    CHECK(blocksWithEvents >= 6);
}

TEST_CASE("selecting a slot on the processor plays its pattern from the next bar line", "[plugin][handover]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    processor.submitResult(2, ownedPattern({{0, 240, 1, 55, 100}}, 4)); // stored, slot 1 keeps playing
    rig.run(10);
    CHECK(processor.activePatternVersion() == 0);
    processor.selectSlot(2);
    CHECK(processor.activeSlot() == 2);
    const auto notes = rig.run(200); // to about 4.3 PPQ
    std::vector<Rig::Note> played;
    for (const auto& n : notes) {
        if (n.pitch == 55 && n.on) {
            played.push_back(n);
        }
    }
    REQUIRE(played.size() == 1);
    CHECK(std::abs(played.front().sample - 96000) <= 1); // PPQ 4.0
    CHECK(processor.activePatternVersion() == 4);
}

// ---- host parameters (SPEC 3.13, D-93, D-138) ----

namespace {

void setParameter(mm::plugin::ProcessorBase& processor, const char* id, float unnormalised) {
    auto* parameter = processor.parameters().getParameter(id);
    REQUIRE(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(unnormalised));
}

} // namespace

TEST_CASE("every register parameter exists with its ID, range, default and automation flag", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(static_cast<size_t>(processor.getParameters().size()) == mm::core::kParameterCount);
    for (const auto& spec : mm::core::parameterRegister()) {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(
            processor.parameters().getParameter(juce::String(spec.id.data(), spec.id.size())));
        REQUIRE(parameter != nullptr);
        INFO(std::string(spec.id));
        const auto range = parameter->getNormalisableRange();
        CHECK(static_cast<int>(range.start) == spec.min);
        CHECK(static_cast<int>(range.end) == spec.max);
        CHECK(static_cast<int>(std::lround(parameter->convertFrom0to1(parameter->getDefaultValue()))) ==
              spec.defaultValue);
        CHECK(parameter->getVersionHint() == mm::core::kParameterVersionHint);
        CHECK(parameter->isAutomatable() == spec.isActive()); // later releases cannot be automated
        CHECK(parameter->getName(64) == juce::String(spec.name.data(), spec.name.size()));
    }
}

TEST_CASE("the slot parameter drives the playback and a start plays it at once", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    processor.submitResult(3, ownedPattern({{0, 240, 1, 57, 100}}, 8));
    setParameter(processor, "slot", 3.0f);
    CHECK(processor.activeSlot() == 3);
    const auto notes = rig.run(10); // the first block is a start
    REQUIRE_FALSE(notes.empty());
    CHECK(notes.front().pitch == 57);
    CHECK(notes.front().sample == 0);
    CHECK(processor.activePatternVersion() == 8);
}

TEST_CASE("the mute parameter silences the voice and un-muting brings it back", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    useOffbeatPattern(processor);
    Rig rig(processor);
    auto notes = rig.run(30); // the first note (from sample 12000) sounds
    REQUIRE_FALSE(notes.empty());
    REQUIRE(notes.back().on);

    setParameter(processor, "mute_1", 1.0f);
    notes = rig.run(2);
    int offs = 0;
    for (const auto& n : notes) {
        offs += n.on ? 0 : 1;
    }
    CHECK(offs == 1);
    notes = rig.run(120); // more notes would start
    CHECK(notes.empty());

    setParameter(processor, "mute_1", 0.0f);
    notes = rig.run(200);
    CHECK_FALSE(notes.empty());
}

TEST_CASE("the mute of another voice does not silence this instance", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    useOffbeatPattern(processor);
    Rig rig(processor);
    setParameter(processor, "mute_2", 1.0f);
    CHECK_FALSE(rig.run(100).empty());
}

TEST_CASE("the state keeps the parameter values and is stable", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor source("Test");
    setParameter(source, "slot", 5.0f);
    setParameter(source, "mute_1", 1.0f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.activeSlot() == 5);
    CHECK(target.parameters().getRawParameterValue("mute_1")->load() >= 0.5f);

    juce::MemoryBlock again;
    target.getStateInformation(again);
    CHECK(again == saved); // saving what was loaded gives the same bytes
}

TEST_CASE("damaged or foreign state data is ignored", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    setParameter(processor, "slot", 4.0f);

    const char garbage[] = "this is not a state";
    processor.setStateInformation(garbage, static_cast<int>(sizeof(garbage)));
    processor.setStateInformation(nullptr, 0);
    CHECK(processor.activeSlot() == 4);

    juce::XmlElement foreign("SomethingElse");
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(foreign, block);
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(processor.activeSlot() == 4);

    // A valid state without a version number is not trusted.
    mm::plugin::InstrumentProcessor other("Test");
    setParameter(other, "slot", 12.0f);
    juce::MemoryBlock valid;
    other.getStateInformation(valid);
    auto unversioned = juce::AudioProcessor::getXmlFromBinary(valid.getData(), static_cast<int>(valid.getSize()));
    REQUIRE(unversioned != nullptr);
    unversioned->removeAttribute("stateVersion");
    juce::MemoryBlock block2;
    juce::AudioProcessor::copyXmlToBinary(*unversioned, block2);
    processor.setStateInformation(block2.getData(), static_cast<int>(block2.getSize()));
    CHECK(processor.activeSlot() == 4);
}

TEST_CASE("a newer state version loads as far as understood, unknown parameters are skipped", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor source("Test");
    setParameter(source, "slot", 9.0f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("stateVersion", 99);
    root->setAttribute("somethingNew", "x");
    if (auto* parameters = root->getChildElement(0)) {
        auto* unknown = parameters->createNewChildElement("PARAM");
        unknown->setAttribute("id", "no_such_parameter");
        unknown->setAttribute("value", 3.0);
    }
    root->createNewChildElement("FutureSection");
    juce::MemoryBlock newer;
    juce::AudioProcessor::copyXmlToBinary(*root, newer);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(newer.getData(), static_cast<int>(newer.getSize()));
    CHECK(target.activeSlot() == 9);
}

namespace {

mm::core::Pattern generatedPattern(const std::string& style, uint64_t seed) {
    mm::plugin::InstrumentProcessor processor("Styles");
    mm::core::GenerationRequest request;
    request.seed = seed;
    return mm::core::generateCandidate(*processor.styles().find(style), request, seed);
}

int firstPitch(const mm::core::Pattern& pattern, const mm::core::StyleLibrary& styles, size_t voice) {
    const auto rendered = mm::core::renderVoiceForPlayback(pattern, *styles.find(pattern.styleId), voice);
    return rendered.notes.empty() ? -1 : rendered.notes.front().pitch;
}

} // namespace

TEST_CASE("the embedded style library holds the three shipped profiles", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(processor.styles().size() == 3);
    CHECK(processor.styles().problems().empty());
    CHECK(processor.styles().find("peak_time") != nullptr);
}

TEST_CASE("a pattern put in a slot plays when the slot is selected", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    const auto pattern = generatedPattern("peak_time", 5);
    processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(2, pattern)); });
    processor.selectSlot(3);
    const auto rendered = mm::core::renderVoiceForPlayback(pattern, *processor.styles().find("peak_time"), 0);
    REQUIRE_FALSE(rendered.notes.empty());
    const auto notes = rig.run(400); // to about 8.5 PPQ
    std::vector<Rig::Note> played;
    for (const auto& n : notes) {
        if (n.sample >= 96000 && n.on) {
            played.push_back(n);
        }
    }
    // The first note of the pattern sounds at the bar line plus its own start.
    const long long expected = 96000 + std::llround(rendered.notes.front().startTick / 960.0 * 24000.0);
    REQUIRE_FALSE(played.empty());
    CHECK(std::abs(played.front().sample - expected) <= 1);
    CHECK(played.front().pitch == rendered.notes.front().pitch);
    CHECK(processor.activePatternVersion() == processor.slotsSnapshot().slot(2)->pattern->version);
}

TEST_CASE("the mute parameter that applies follows the output voice", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor processor("Test");
    useOffbeatPattern(processor);
    Rig rig(processor);
    mm::core::InstanceSettings settings;
    settings.outputVoice = 2;
    processor.setInstanceSettings(settings);
    CHECK(processor.instanceSettings().outputVoice == 2);

    setParameter(processor, "mute_1", 1.0f); // another voice
    CHECK_FALSE(rig.run(100).empty());
    setParameter(processor, "mute_2", 1.0f);
    rig.run(2);
    CHECK(rig.run(120).empty());
    setParameter(processor, "mute_2", 0.0f);
    CHECK_FALSE(rig.run(200).empty());
}

TEST_CASE("the output voice is limited to the valid range", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor processor("Test");
    mm::core::InstanceSettings settings;
    settings.outputVoice = 0;
    processor.setInstanceSettings(settings);
    CHECK(processor.instanceSettings().outputVoice == 1);
    settings.outputVoice = 500;
    processor.setInstanceSettings(settings);
    CHECK(processor.instanceSettings().outputVoice == mm::core::kMaxVoices);
    Rig rig(processor); // must not read a mute parameter beyond the table
    rig.run(10);
}

TEST_CASE("changing the output voice plays the other voice of the slot", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor processor("Test");
    const auto pattern = generatedPattern("melodic_techno", 9);
    processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });
    mm::core::InstanceSettings settings;
    settings.outputVoice = 2;
    processor.setInstanceSettings(settings);
    Rig rig(processor);
    const auto notes = rig.run(400); // to about 8.5 PPQ
    REQUIRE_FALSE(notes.empty());
    const int expected = firstPitch(pattern, processor.styles(), 1);
    int first = -1;
    for (const auto& n : notes) {
        if (n.on) {
            first = n.pitch;
            break;
        }
    }
    CHECK(first == expected);
}

TEST_CASE("the state keeps slots, role, output mode and output voice", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor source("Test");
    source.editSlots([&](mm::core::SlotBank& bank) {
        REQUIRE(bank.setResult(0, generatedPattern("peak_time", 1)));
        REQUIRE(bank.setResult(7, generatedPattern("hard_industrial", 2)));
        bank.setName(7, "Drop");
        bank.setColor(7, 3);
    });
    mm::core::InstanceSettings settings;
    settings.role = mm::core::InstanceRole::Hub;
    settings.outputMode = mm::core::OutputMode::None;
    settings.outputVoice = 2;
    source.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.instanceSettings() == settings);
    const auto bank = target.slotsSnapshot();
    const auto original = source.slotsSnapshot();
    REQUIRE(bank.slot(0)->pattern.has_value());
    CHECK(*bank.slot(0)->pattern == *original.slot(0)->pattern);
    CHECK(*bank.slot(7)->pattern == *original.slot(7)->pattern);
    CHECK(bank.slot(7)->name == "Drop");
    CHECK(bank.slot(7)->color == 3);
    CHECK(bank.isEmpty(1));
    CHECK(target.slotLoadProblems().empty());

    juce::MemoryBlock again;
    target.getStateInformation(again);
    CHECK(again == saved);
}

TEST_CASE("a loaded slot plays without any further call", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor source("Test");
    const auto pattern = generatedPattern("peak_time", 4);
    source.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    Rig rig(target);
    const auto notes = rig.run(400);
    REQUIRE_FALSE(notes.empty());
    CHECK(target.activePatternVersion() == target.slotsSnapshot().slot(0)->pattern->version);
}

TEST_CASE("a state without slots or settings gives an empty bank and the defaults", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor processor("Test");
    processor.editSlots(
        [&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, generatedPattern("peak_time", 1))); });
    mm::core::InstanceSettings changed;
    changed.role = mm::core::InstanceRole::Voice;
    changed.outputVoice = 3;
    processor.setInstanceSettings(changed);

    // The format of the parameter step: only the version and the parameters.
    mm::plugin::InstrumentProcessor fresh("Test");
    juce::MemoryBlock saved;
    fresh.getStateInformation(saved);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->removeAttribute("role");
    root->removeAttribute("outputMode");
    root->removeAttribute("outputVoice");
    if (auto* slots = root->getChildByName("Slots")) {
        root->removeChildElement(slots, true);
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*root, old);

    processor.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(processor.instanceSettings() == mm::core::InstanceSettings{});
    const auto bank = processor.slotsSnapshot();
    for (size_t i = 0; i < mm::core::kSlotCount; ++i) {
        CHECK(bank.isEmpty(i));
    }
}

TEST_CASE("unreadable slot data leaves the bank empty and is reported, the rest of the state loads",
          "[plugin][instance]") {
    mm::plugin::InstrumentProcessor source("Test");
    setParameter(source, "slot", 6.0f);
    mm::core::InstanceSettings settings;
    settings.role = mm::core::InstanceRole::Hub;
    source.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    auto* slots = root->getChildByName("Slots");
    REQUIRE(slots != nullptr);
    slots->deleteAllTextElements();
    slots->addTextElement("{ this is not a slot bank");
    juce::MemoryBlock damaged;
    juce::AudioProcessor::copyXmlToBinary(*root, damaged);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(damaged.getData(), static_cast<int>(damaged.getSize()));
    CHECK(target.activeSlot() == 6);
    CHECK(target.instanceSettings().role == mm::core::InstanceRole::Hub);
    CHECK_FALSE(target.slotLoadProblems().empty());
    CHECK(target.slotsSnapshot().isEmpty(0));
}

TEST_CASE("unknown role text and out-of-range voice in a state fall back safely", "[plugin][instance]") {
    mm::plugin::InstrumentProcessor source("Test");
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("role", "conductor");
    root->setAttribute("outputMode", "everything");
    root->setAttribute("outputVoice", 99);
    juce::MemoryBlock odd;
    juce::AudioProcessor::copyXmlToBinary(*root, odd);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(odd.getData(), static_cast<int>(odd.getSize()));
    const auto settings = target.instanceSettings();
    CHECK(settings.role == mm::core::InstanceRole::Solo);
    CHECK(settings.outputMode == mm::core::OutputMode::OneVoice);
    CHECK(settings.outputVoice == mm::core::kMaxVoices);
}

TEST_CASE("the plug-in embeds the German table with every key the code asks for", "[plugin][i18n]") {
    const auto& table = mm::plugin::embeddedTranslation();
    CHECK(table.problems().empty());
    for (const auto key : mm::core::text::kAllKeys) {
        INFO(key);
        CHECK(table.has(key));
    }
    for (const auto& scale : mm::core::allScales()) {
        CHECK(table.has(mm::core::text::scaleKey(scale.id)));
    }
}

namespace {

/// A one-bar pattern (4 PPQ) with a bass note of `pitch` from tick 0 for `length` ticks.
mm::core::Pattern longNotePattern(uint8_t pitch, uint32_t length) {
    auto pattern = mm::core::makeEmptyPattern(1, "peak_time");
    REQUIRE(mm::core::addNote(pattern, 0, pitch, 0, length, 100) != 0);
    return pattern;
}

void putInSlot(mm::plugin::ProcessorBase& processor, mm::core::Pattern pattern) {
    processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, std::move(pattern))); });
}

int countOf(const std::vector<Rig::Note>& notes, int pitch, bool on) {
    return static_cast<int>(
        std::count_if(notes.begin(), notes.end(), [&](const Rig::Note& n) { return n.pitch == pitch && n.on == on; }));
}

} // namespace

TEST_CASE("a note edit plays at once and a deleted sounding note gets its note-off", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    putInSlot(processor, longNotePattern(45, 3000));
    auto notes = rig.run(10);
    REQUIRE(countOf(notes, 45, true) == 1);
    REQUIRE(countOf(notes, 45, false) == 0); // still sounding

    const auto id = processor.slotsSnapshot().slot(0)->pattern->voices[0].notes[0].id;
    REQUIRE(processor.editNotes(0, [&](mm::core::Pattern& pattern) {
        const std::vector<uint32_t> ids = {id};
        return mm::core::removeNotes(pattern, 0, ids);
    }));
    notes = rig.run(3);
    CHECK(countOf(notes, 45, false) == 1); // note-off within a few blocks, not at the bar line
    notes = rig.run(600);
    CHECK(countOf(notes, 45, true) == 0); // the note is gone, also in the next loop
}

TEST_CASE("a note added during playback sounds in the same bar", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    putInSlot(processor, longNotePattern(45, 240));
    rig.run(10); // PPQ 0.2
    REQUIRE(processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 50, 2 * 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    const auto notes = rig.run(100); // up to PPQ 2.3
    const auto it = std::find_if(notes.begin(), notes.end(), [](const auto& n) { return n.on && n.pitch == 50; });
    REQUIRE(it != notes.end());
    CHECK(std::abs(it->sample - 2 * 24000) <= 1); // PPQ 2.0: not postponed to the next bar line
}

TEST_CASE("undo and redo of a note edit reach the engine", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    putInSlot(processor, longNotePattern(45, 240));
    rig.run(10);
    CHECK_FALSE(processor.canUndo());
    REQUIRE(processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 50, 2 * 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    CHECK(processor.canUndo());
    CHECK_FALSE(processor.canRedo());
    REQUIRE(processor.undo());
    CHECK(processor.canRedo());
    auto notes = rig.run(450); // PPQ 0.2 to 9.8: the note would sound at 2, 6 and 10
    CHECK(countOf(notes, 50, true) == 0);
    REQUIRE(processor.redo());
    notes = rig.run(100);
    CHECK(countOf(notes, 50, true) == 1);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[0].notes.size() == 2);
}

TEST_CASE("a voice neither edits nor undoes", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Voice");
    auto settings = processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    putInSlot(processor, longNotePattern(45, 240));
    const auto before = processor.slotsSnapshot();
    CHECK_FALSE(processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 50, 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    CHECK_FALSE(processor.canUndo());
    CHECK_FALSE(processor.undo());
    CHECK(processor.slotsSnapshot().slot(0)->pattern == before.slot(0)->pattern);
}

TEST_CASE("an edit that changes nothing, in an empty slot or in an unknown one is no step", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK_FALSE(processor.editNotes(0, [](mm::core::Pattern&) { return size_t{1}; })); // empty slot
    putInSlot(processor, longNotePattern(45, 240));
    CHECK_FALSE(processor.editNotes(0, [](mm::core::Pattern&) { return size_t{0}; }));
    CHECK_FALSE(processor.editNotes(99, [](mm::core::Pattern&) { return size_t{1}; }));
    CHECK_FALSE(processor.canUndo());
}

TEST_CASE("loading a state clears the undo steps", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    putInSlot(processor, longNotePattern(45, 240));
    REQUIRE(processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 50, 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    REQUIRE(processor.canUndo());
    processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK_FALSE(processor.canUndo());
    CHECK_FALSE(processor.canRedo());
}

TEST_CASE("slots taken over from a hub clear the undo steps", "[plugin][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    putInSlot(processor, longNotePattern(45, 240));
    REQUIRE(processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 50, 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    auto settings = processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    mm::core::SlotBank hubBank;
    hubBank.setResult(0, longNotePattern(60, 240));
    processor.adoptHubSlots(std::make_shared<const mm::core::SlotBank>(hubBank));
    settings.role = mm::core::InstanceRole::Hub; // the voice takes the hub over
    processor.setInstanceSettings(settings);
    CHECK_FALSE(processor.canUndo());
}
