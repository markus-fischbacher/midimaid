#include "plugin/ProcessorBase.h"

#include "plugin/PlaceholderEditor.h"

#include <algorithm>

namespace mm::plugin {

namespace {

constexpr int kReturnCollectIntervalMs = 250;
/// Upper bound of the bytes one block can write: the engine's event capacity, with room for the event header.
constexpr size_t kMidiBufferBytes = mm::engine::MidiEventList::kCapacity * 12;

} // namespace

ProcessorBase::ReturnCollector::ReturnCollector(mm::engine::PatternHandover& handover) : handover_(handover) {
    startTimer(kReturnCollectIntervalMs);
}

ProcessorBase::ReturnCollector::~ReturnCollector() {
    stopTimer();
}

void ProcessorBase::ReturnCollector::timerCallback() {
    handover_.collectReturned();
}

juce::AudioProcessorValueTreeState::ParameterLayout ProcessorBase::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : mm::core::parameterRegister()) {
        const juce::ParameterID id{juce::String(spec.id.data(), spec.id.size()), mm::core::kParameterVersionHint};
        const juce::String name(spec.name.data(), spec.name.size());
        // Parameters of later releases exist with their final ID but do nothing and cannot be automated (SPEC 3.13).
        if (spec.type == mm::core::ParamType::Bool) {
            layout.add(std::make_unique<juce::AudioParameterBool>(
                id, name, spec.defaultValue != 0,
                juce::AudioParameterBoolAttributes{}.withAutomatable(spec.isActive())));
        } else {
            layout.add(std::make_unique<juce::AudioParameterInt>(
                id, name, spec.min, spec.max, spec.defaultValue,
                juce::AudioParameterIntAttributes{}.withAutomatable(spec.isActive())));
        }
    }
    return layout;
}

ProcessorBase::ProcessorBase(const BusesProperties& buses, juce::String name)
    : juce::AudioProcessor(buses), name_(std::move(name)),
      parameters_(*this, nullptr, "MidiMaid", createParameterLayout()), returnCollector_(handover_) {
    slotValue_ = parameters_.getRawParameterValue("slot");
    const std::string muteId = mm::core::muteParameterId(outputVoice_);
    muteValue_ = parameters_.getRawParameterValue(muteId);
    player_.attach(&handover_);
}

ProcessorBase::~ProcessorBase() = default;

void ProcessorBase::switchPattern(std::unique_ptr<mm::engine::OwnedPattern> pattern, double gridPpq) {
    handover_.publishSwitch(std::move(pattern), gridPpq);
    handover_.collectReturned();
}

void ProcessorBase::editPattern(std::unique_ptr<mm::engine::OwnedPattern> pattern) {
    handover_.publishEdit(std::move(pattern));
    handover_.collectReturned();
}

void ProcessorBase::submitResult(int slot, std::unique_ptr<mm::engine::OwnedPattern> pattern, double gridPpq) {
    const auto index = static_cast<size_t>(std::clamp(slot, 1, static_cast<int>(mm::engine::kSlotCount)) - 1);
    handover_.publishResult(index, std::move(pattern), gridPpq);
    handover_.collectReturned();
}

void ProcessorBase::selectSlot(int slot) {
    if (auto* parameter = parameters_.getParameter("slot")) {
        const auto value = static_cast<float>(std::clamp(slot, 1, static_cast<int>(mm::engine::kSlotCount)));
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
}

int ProcessorBase::activeSlot() const {
    return static_cast<int>(std::lround(slotValue_->load()));
}

juce::AudioProcessorValueTreeState& ProcessorBase::parameters() {
    return parameters_;
}

uint64_t ProcessorBase::activePatternVersion() const {
    return handover_.activeVersion();
}

const juce::String ProcessorBase::getName() const {
    return name_;
}

void ProcessorBase::prepareToPlay(double, int) {
    // Keeps the active-note table: sounding notes get their note-off in the first block afterwards.
    player_.invalidateTransport();
}

void ProcessorBase::releaseResources() {}

void ProcessorBase::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    midi.clear(); // incoming MIDI is not evaluated in v1.0 and not passed through
    // The host owns the buffer, so it cannot be reserved in prepareToPlay (O-23, D-134). Reserving the bound once
    // keeps addEvent from growing it; this allocates at most once per host buffer, in the first blocks.
    midi.ensureSize(kMidiBufferBytes);

    mm::engine::TransportInfo transport;
    if (auto* host = getPlayHead()) {
        if (auto position = host->getPosition()) {
            transport.isPlaying = position->getIsPlaying();
            transport.isLooping = position->getIsLooping();
            const auto ppq = position->getPpqPosition();
            const auto bpm = position->getBpm();
            if (ppq.hasValue() && bpm.hasValue()) {
                transport.hasPosition = true;
                transport.ppq = *ppq;
                transport.bpm = *bpm;
                lastBpm_.store(*bpm);
            }
            if (const auto loop = position->getLoopPoints()) {
                transport.loopStartPpq = loop->ppqStart;
                transport.loopEndPpq = loop->ppqEnd;
            }
        }
    }

    // Host parameters are read once per block and never written here (SPEC 3.13, 6.1a rule 1).
    player_.setSlot(static_cast<int>(std::lround(slotValue_->load())));
    player_.setMuted(muteValue_->load() >= 0.5f);
    player_.process(transport, audio.getNumSamples(), getSampleRate(), events_);
    writeEvents(midi);
}

void ProcessorBase::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    audio.clear();
    midi.clear();
    midi.ensureSize(kMidiBufferBytes);
    events_.clear();
    player_.releaseAll(events_, 0);
    player_.invalidateTransport();
    writeEvents(midi);
}

void ProcessorBase::writeEvents(juce::MidiBuffer& midi) const {
    for (const auto& event : events_) {
        midi.addEvent(event.noteOn ? juce::MidiMessage::noteOn(event.channel, event.pitch, event.velocity)
                                   : juce::MidiMessage::noteOff(event.channel, event.pitch, event.velocity),
                      event.sampleOffset);
    }
}

MidiExporter& ProcessorBase::midiExporter() {
    return exporter_;
}

double ProcessorBase::lastKnownBpm() const {
    return lastBpm_.load();
}

bool ProcessorBase::acceptsMidi() const {
    return true;
}

bool ProcessorBase::producesMidi() const {
    return true;
}

double ProcessorBase::getTailLengthSeconds() const {
    return 0.0;
}

bool ProcessorBase::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* ProcessorBase::createEditor() {
    // JUCE takes ownership of the returned editor.
    return new PlaceholderEditor(*this);
}

int ProcessorBase::getNumPrograms() {
    return 1;
}

int ProcessorBase::getCurrentProgram() {
    return 0;
}

void ProcessorBase::setCurrentProgram(int) {}

const juce::String ProcessorBase::getProgramName(int) {
    return {};
}

void ProcessorBase::changeProgramName(int, const juce::String&) {}

void ProcessorBase::getStateInformation(juce::MemoryBlock& destData) {
    // SPEC 9.1: XML with stateVersion; slots, hub and voice settings join the parameters in later versions.
    juce::XmlElement root("MidiMaid");
    root.setAttribute("stateVersion", kStateVersion);
    if (auto parameters = parameters_.copyState().createXml()) {
        root.addChildElement(parameters.release());
    }
    copyXmlToBinary(root, destData);
}

void ProcessorBase::setStateInformation(const void* data, int sizeInBytes) {
    // Never crashes on bad data: anything unreadable is ignored, a newer version loads as far as understood.
    const auto root = getXmlFromBinary(data, sizeInBytes);
    if (root == nullptr || !root->hasTagName("MidiMaid") || root->getIntAttribute("stateVersion", 0) < 1) {
        return;
    }
    if (const auto* parameters = root->getChildByName(parameters_.state.getType())) {
        parameters_.replaceState(juce::ValueTree::fromXml(*parameters));
    }
}

InstrumentProcessor::InstrumentProcessor(juce::String name)
    : ProcessorBase(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true), std::move(name)) {}

bool InstrumentProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.inputBuses.isEmpty();
}

MidiFxProcessor::MidiFxProcessor(juce::String name) : ProcessorBase(BusesProperties(), std::move(name)) {}

bool MidiFxProcessor::isMidiEffect() const {
    return true;
}

bool MidiFxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
}

} // namespace mm::plugin
