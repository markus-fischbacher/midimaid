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

/// Retries a parked generation result on the message thread until the host renders in real time again.
class ProcessorBase::ParkTimer : private juce::Timer {
public:
    explicit ParkTimer(std::function<void()> retry) : retry_(std::move(retry)) { startTimer(kReturnCollectIntervalMs); }
    ~ParkTimer() override { stopTimer(); }

private:
    void timerCallback() override { retry_(); }
    std::function<void()> retry_;
};

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
      publisher_(handover_, styles_), player_(mm::core::PatternView{nullptr, 0, mm::core::kTicksPerBar}),
      returnCollector_(handover_),
      generator_(styles_, [this](const GenerationJob& job, std::optional<mm::core::Pattern> pattern) {
          onGenerated(job, std::move(pattern));
      }) {
    slotValue_ = parameters_.getRawParameterValue("slot");
    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        muteValues_[static_cast<size_t>(voice - 1)] =
            parameters_.getRawParameterValue(mm::core::muteParameterId(voice));
    }
    player_.attach(&handover_);
    groupLink_ = GroupLink::create(*this);
    groupLink_->setRole(settings_.role, settings_.outputVoice);
}

ProcessorBase::~ProcessorBase() {
    groupLink_->detach();                              // first: from here on no registry callback reaches this instance
    onGroupStatusChanged(mm::core::GroupStatus::Solo); // gives the channel slot back
}

void ProcessorBase::onGroupStatusChanged(mm::core::GroupStatus status) {
    using mm::core::GroupStatus;
    auto& channel = mm::engine::processGroupChannel();
    // Hub and voices report to the channel; a solo instance does not take part in the planning.
    const bool member = status == GroupStatus::Hub || status == GroupStatus::VoiceConnected ||
                        status == GroupStatus::HubMissing || status == GroupStatus::HubOffered;
    if (member && channelMember_ == mm::engine::GroupChannel::kNone) {
        channelMember_ = channel.acquire();
        groupSync_.setMember(channelMember_);
    } else if (!member && channelMember_ != mm::engine::GroupChannel::kNone) {
        groupSync_.setMember(mm::engine::GroupChannel::kNone); // before the slot goes back
        channel.release(channelMember_);                       // also clears the hub if it was this one
        channelMember_ = mm::engine::GroupChannel::kNone;
    }
    if (channelMember_ != mm::engine::GroupChannel::kNone) {
        if (status == GroupStatus::Hub) {
            channel.setHub(channelMember_);
        } else if (channel.hub() == channelMember_) {
            channel.setHub(mm::engine::GroupChannel::kNone);
        }
    }
    if (status == GroupStatus::Hub) {
        // A voice that took over the hub (acceptHubOffer) is a hub from now on: in its settings, its state and for the
        // distribution of its own changes.
        const juce::ScopedLock lock(slotsLock_);
        settings_.role = mm::core::InstanceRole::Hub;
    }
    updateSlotMode();
}

void ProcessorBase::updateSlotMode() {
    mm::core::InstanceRole role;
    mm::core::SlotFollow follow;
    {
        const juce::ScopedLock lock(slotsLock_);
        role = settings_.role;
        follow = settings_.slotFollow;
    }
    auto mode = mm::engine::SlotMode::Own;
    if (role == mm::core::InstanceRole::Hub && groupLink_ != nullptr &&
        groupLink_->status() == mm::core::GroupStatus::Hub) {
        mode = mm::engine::SlotMode::Hub;
    } else if (role == mm::core::InstanceRole::Voice && follow == mm::core::SlotFollow::Hub) {
        mode = mm::engine::SlotMode::FollowHub;
    }
    slotMode_.store(static_cast<int>(mode), std::memory_order_release);
}

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
    mm::core::SlotSnapshot forVoices;
    {
        const juce::ScopedLock lock(slotsLock_);
        change(slots_);
        publisher_.sync(slots_);
        if (settings_.role == mm::core::InstanceRole::Hub) {
            forVoices = std::make_shared<const mm::core::SlotBank>(slots_);
        }
    }
    if (forVoices) {
        groupLink_->publish(std::move(forVoices)); // outside the lock: the registry may call back
    }
}

void ProcessorBase::adoptHubSlots(const mm::core::SlotSnapshot& snapshot) {
    const juce::ScopedLock lock(slotsLock_);
    if (settings_.role != mm::core::InstanceRole::Voice || snapshot == nullptr) {
        return;
    }
    slots_ = *snapshot; // a copy: the hub's snapshot stays immutable
    publisher_.sync(slots_);
}

mm::core::GroupStatus ProcessorBase::groupStatus() const {
    return groupLink_->status();
}

size_t ProcessorBase::groupVoices() const {
    return groupLink_->voices();
}

void ProcessorBase::acceptHubOffer() {
    groupLink_->acceptOffer();
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
    mm::core::InstanceRole role;
    int voice;
    {
        const juce::ScopedLock lock(slotsLock_);
        setInstanceSettingsLocked(settings);
        role = settings_.role;
        voice = settings_.outputVoice;
    }
    updateSlotMode();
    groupLink_->setRole(role, voice); // outside the lock: the registry may call back
}

void ProcessorBase::setInstanceSettingsLocked(const mm::core::InstanceSettings& settings) {
    settings_ = settings;
    settings_.outputVoice = mm::core::clampOutputVoice(settings.outputVoice);
    settings_.generation = mm::core::sanitize(settings.generation);
    outputVoice_.store(settings_.outputVoice);
    publisher_.setOutputVoice(settings_.outputVoice);
    publisher_.sync(slots_);
}

void ProcessorBase::generate() {
    GenerationJob job;
    {
        const juce::ScopedLock lock(slotsLock_);
        job.settings = settings_.generation;
        if (settings_.role == mm::core::InstanceRole::Voice) {
            generationStatus_ = GenerationStatus::UseHub; // a voice plays what the hub generates (forwarding: H5)
            return;
        }
    }
    job.seed = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64());
    job.slot = std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1;
    parked_.reset(); // a newer request replaces a result that is still waiting
    parkTimer_.reset();
    generationStatus_ = GenerationStatus::Generating;
    generator_.request(job);
}

void ProcessorBase::onGenerated(const GenerationJob& job, std::optional<mm::core::Pattern> pattern) {
    if (!pattern) {
        generationStatus_ = GenerationStatus::NoResult; // the slot stays as it was
        return;
    }
    if (isNonRealtime()) {
        // An offline bounce must not hear a pattern appear in the middle of the render.
        parked_ = ParkedResult{job, std::move(*pattern)};
        parkTimer_ = std::make_unique<ParkTimer>([this] {
            if (!isNonRealtime() && parked_) {
                auto parked = std::move(*parked_);
                parked_.reset();
                parkTimer_.reset(); // destroys the timer that is calling: nothing runs after this line
                applyGenerated(parked.job, std::move(parked.pattern));
            }
        });
        return;
    }
    applyGenerated(job, std::move(*pattern));
}

void ProcessorBase::applyGenerated(const GenerationJob& job, mm::core::Pattern pattern) {
    editSlots([&](mm::core::SlotBank& bank) { bank.setResult(static_cast<size_t>(job.slot), std::move(pattern)); });
    generationStatus_ = GenerationStatus::Done;
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
    const auto decision = groupSync_.beginBlock(
        static_cast<mm::engine::SlotMode>(slotMode_.load(std::memory_order_acquire)),
        static_cast<int>(std::lround(slotValue_->load())), transport, audio.getNumSamples(), getSampleRate(),
        player_.startsOrJumps(transport, audio.getNumSamples(), getSampleRate()));
    if (decision.stamped) {
        player_.setSlotAt(decision.slot, decision.stampPpq);
    } else {
        player_.setSlot(decision.slot);
    }
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
        root.setAttribute("slotFollow", juce::String(mm::core::toString(settings_.slotFollow).data(),
                                                     mm::core::toString(settings_.slotFollow).size()));
        root.setAttribute("genStyle", juce::String(settings_.generation.styleId));
        root.setAttribute("genBars", static_cast<int>(settings_.generation.lengthBars));
        root.setAttribute("genEnergy", settings_.generation.energyPct);
        root.setAttribute("genCreativity", settings_.generation.creativityPct);
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
    settings.slotFollow = mm::core::parseSlotFollow(root->getStringAttribute("slotFollow").toStdString());
    const mm::core::GenerationSettings defaults;
    if (root->hasAttribute("genStyle")) {
        settings.generation.styleId = root->getStringAttribute("genStyle").toStdString();
    }
    settings.generation.lengthBars =
        static_cast<uint32_t>(std::max(0, root->getIntAttribute("genBars", static_cast<int>(defaults.lengthBars))));
    settings.generation.energyPct = root->getIntAttribute("genEnergy", defaults.energyPct);
    settings.generation.creativityPct = root->getIntAttribute("genCreativity", defaults.creativityPct);

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
    mm::core::InstanceRole role;
    int voice;
    {
        const juce::ScopedLock lock(slotsLock_);
        slots_ = std::move(bank);
        slotLoadProblems_ = std::move(problems);
        setInstanceSettingsLocked(settings);
        role = settings_.role;
        voice = settings_.outputVoice;
    }
    updateSlotMode();
    groupLink_->setRole(role, voice); // a hub then hands its loaded slots to the voices
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
