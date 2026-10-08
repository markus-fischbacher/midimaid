#include "plugin/ProcessorBase.h"

#include "core/SlotBankJson.h"
#include "plugin/EmbeddedStyles.h"
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
      parameters_(*this, nullptr, "MidiMaid", createParameterLayout()), styles_(embeddedStyles()),
      publisher_(handover_, styles_), returnCollector_(handover_) {
    slotValue_ = parameters_.getRawParameterValue("slot");
    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        muteValues_[static_cast<size_t>(voice - 1)] = parameters_.getRawParameterValue(mm::core::muteParameterId(voice));
    }
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

void ProcessorBase::editSlots(const std::function<void(mm::core::SlotBank&)>& change) {
    const juce::ScopedLock lock(slotsLock_);
    change(slots_);
    publisher_.sync(slots_);
}

mm::core::SlotBank ProcessorBase::slotsSnapshot() const {
    const juce::ScopedLock lock(slotsLock_);
    return slots_;
}

mm::core::InstanceSettings ProcessorBase::instanceSettings() const {
    const juce::ScopedLock lock(slotsLock_);
    return settings_;
}

void ProcessorBase::setInstanceSettings(const mm::core::InstanceSettings& settings) {
    const juce::ScopedLock lock(slotsLock_);
    setInstanceSettingsLocked(settings);
}

void ProcessorBase::setInstanceSettingsLocked(const mm::core::InstanceSettings& settings) {
    settings_ = settings;
    settings_.outputVoice = mm::core::clampOutputVoice(settings.outputVoice);
    outputVoice_.store(settings_.outputVoice);
    publisher_.setOutputVoice(settings_.outputVoice);
    publisher_.sync(slots_);
}

std::vector<std::string> ProcessorBase::slotLoadProblems() const {
    const juce::ScopedLock lock(slotsLock_);
    return slotLoadProblems_;
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
    const int voice = outputVoice_.load(std::memory_order_relaxed);
    player_.setMuted(muteValues_[static_cast<size_t>(voice - 1)]->load() >= 0.5f);
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
    // SPEC 9.1: XML with stateVersion; the hub settings of later versions join the same way.
    juce::XmlElement root("MidiMaid");
    root.setAttribute("stateVersion", kStateVersion);
    if (auto parameters = parameters_.copyState().createXml()) {
        root.addChildElement(parameters.release());
    }
    {
        const juce::ScopedLock lock(slotsLock_);
        const auto role = mm::core::toString(settings_.role);
        const auto mode = mm::core::toString(settings_.outputMode);
        root.setAttribute("role", juce::String(role.data(), role.size()));
        root.setAttribute("outputMode", juce::String(mode.data(), mode.size()));
        root.setAttribute("outputVoice", settings_.outputVoice);
        root.createNewChildElement("Slots")->addTextElement(juce::String(mm::core::slotBankToString(slots_)));
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

    mm::core::InstanceSettings settings;
    settings.role = mm::core::parseRole(root->getStringAttribute("role").toStdString());
    settings.outputMode = mm::core::parseOutputMode(root->getStringAttribute("outputMode").toStdString());
    settings.outputVoice = root->getIntAttribute("outputVoice", 1);

    mm::core::SlotBank bank;
    std::vector<std::string> problems;
    if (const auto* slotsElement = root->getChildByName("Slots")) {
        auto loaded = mm::core::loadSlotBank(slotsElement->getAllSubText().toStdString());
        if (loaded.ok()) {
            bank = std::move(*loaded.bank);
        }
        problems = std::move(loaded.problems);
        if (!loaded.error.empty()) {
            problems.push_back(loaded.error);
        }
    }
    const juce::ScopedLock lock(slotsLock_);
    slots_ = std::move(bank);
    slotLoadProblems_ = std::move(problems);
    setInstanceSettingsLocked(settings);
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
