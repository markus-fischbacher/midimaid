#include "plugin/ProcessorBase.h"

#include "core/Analysis.h"
#include "core/DrumReference.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "core/VoiceImport.h"
#include "core/SlotBankJson.h"
#include "core/TextKeys.h"
#include "core/Variation.h"
#include "plugin/AiBackend.h"
#include "plugin/AiConnection.h"
#include "plugin/EmbeddedPrompts.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/EmbeddedTranslation.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/PlaceholderEditor.h"

#include <algorithm>
#include <set>

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
      returnCollector_(handover_), importer_([this](const ImportRequest& request, ImportOutcome outcome) {
          onImported(request, std::move(outcome));
      }),
      generator_(styles_, [this](const GenerationJob& job, std::optional<mm::core::Pattern> pattern) {
          onGenerated(job, std::move(pattern));
      }) {
    AiConnection::instance().start(); // applies the provider of the settings once they are read
    generator_.setReportHandler([this](const GenerationJob&, const GenerationReport& report) { lastReport_ = report; });
    slotValue_ = parameters_.getRawParameterValue("slot");
    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        muteValues_[static_cast<size_t>(voice - 1)] =
            parameters_.getRawParameterValue(mm::core::muteParameterId(voice));
    }
    player_.attach(&handover_);
    groupLink_ = GroupLink::create(*this);
    groupLink_->setRole(settings_.role, settings_.outputVoice);
    applyStartKey();
}

void ProcessorBase::applyStartKey() {
    auto& store = globalSettings();
    if (!startKeyPending_ || !store.enabled()) {
        startKeyPending_ = false;
        return;
    }
    if (!store.ready()) {
        // The file is read in the background: ask again until it is there.
        if (startKeyTimer_ == nullptr) {
            startKeyTimer_ = std::make_unique<ParkTimer>([this] { applyStartKey(); });
        }
        return;
    }
    startKeyPending_ = false;
    // Only an instance that still has the built-in key takes the last one the user chose (D-162); a loaded state or a
    // choice made in the meantime stays.
    auto next = instanceSettings();
    const mm::core::GenerationSettings builtIn;
    if (next.generation.root == builtIn.root && next.generation.scaleId == builtIn.scaleId) {
        const auto global = store.get();
        next.generation.root = global.startRoot;
        next.generation.scaleId = global.startScaleId;
        if (!(next == instanceSettings())) {
            setInstanceSettings(next);
        }
    }
    startKeyTimer_.reset(); // may destroy the timer that is calling: nothing runs after this line
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
    mm::core::OutputMode output;
    {
        const juce::ScopedLock lock(slotsLock_);
        role = settings_.role;
        follow = settings_.slotFollow;
        output = settings_.outputMode;
    }
    silent_.store(role == mm::core::InstanceRole::Hub && output == mm::core::OutputMode::None,
                  std::memory_order_release);
    voiceRole_.store(role == mm::core::InstanceRole::Voice, std::memory_order_release);
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
    std::optional<double> stamp;
    {
        const juce::ScopedLock lock(slotsLock_);
        change(slots_);
        const bool distributing = settings_.role == mm::core::InstanceRole::Hub;
        // A hub with company plans the switch for the whole group: the same bar line for every instance (D-144).
        if (distributing && groupLink_->status() == mm::core::GroupStatus::Hub &&
            mm::engine::processGroupChannel().memberCount() > 1) {
            stamp = mm::engine::plannedStamp(mm::engine::processGroupChannel(), lastKnownBpm());
        }
        publisher_.sync(slots_, 4.0, stamp);
        if (distributing) {
            forVoices = std::make_shared<const mm::core::SlotBank>(slots_);
        }
    }
    if (forVoices) {
        groupLink_->publish(std::move(forVoices), stamp); // outside the lock: the registry may call back
    }
}

bool ProcessorBase::editNotes(size_t slotIndex, const std::function<size_t(mm::core::Pattern&)>& change,
                              bool mergeWithPrevious) {
    mm::core::SlotSnapshot forVoices;
    {
        const juce::ScopedLock lock(slotsLock_);
        const auto* slot = slots_.slot(slotIndex);
        if (settings_.role == mm::core::InstanceRole::Voice || slot == nullptr || !slot->pattern) {
            return false;
        }
        const mm::core::Pattern before = *slot->pattern;
        mm::core::Pattern after = before;
        if (change(after) == 0 || !slots_.edit(slotIndex, after)) {
            return false;
        }
        undo_.recordEdit(slotIndex, before, std::move(after), mergeWithPrevious);
        publisher_.publishEdit(slots_, slotIndex);
        if (settings_.role == mm::core::InstanceRole::Hub) {
            forVoices = std::make_shared<const mm::core::SlotBank>(slots_);
        }
    }
    if (forVoices) {
        groupLink_->publish(std::move(forVoices), std::nullopt); // outside the lock: the registry may call back
    }
    return true;
}

bool ProcessorBase::setVoiceLocked(size_t slotIndex, size_t voice, bool locked) {
    return editNotes(slotIndex,
                     [&](mm::core::Pattern& pattern) { return mm::core::setVoiceLocked(pattern, voice, locked); });
}

bool ProcessorBase::undo() {
    return stepUndo(true);
}

bool ProcessorBase::redo() {
    return stepUndo(false);
}

bool ProcessorBase::stepUndo(bool back) {
    mm::core::SlotSnapshot forVoices;
    {
        const juce::ScopedLock lock(slotsLock_);
        if (settings_.role == mm::core::InstanceRole::Voice) {
            return false;
        }
        size_t slot = 0;
        if (!(back ? undo_.undo(slots_, &slot) : undo_.redo(slots_, &slot))) {
            return false;
        }
        if (!publisher_.publishEdit(slots_, slot)) {
            publisher_.sync(slots_); // the slot is empty again: the engine drops its pattern at the next bar line
        }
        if (settings_.role == mm::core::InstanceRole::Hub) {
            forVoices = std::make_shared<const mm::core::SlotBank>(slots_);
        }
    }
    if (forVoices) {
        groupLink_->publish(std::move(forVoices), std::nullopt);
    }
    return true;
}

bool ProcessorBase::canUndo() const {
    const juce::ScopedLock lock(slotsLock_);
    return settings_.role != mm::core::InstanceRole::Voice && undo_.canUndo();
}

bool ProcessorBase::canRedo() const {
    const juce::ScopedLock lock(slotsLock_);
    return settings_.role != mm::core::InstanceRole::Voice && undo_.canRedo();
}

void ProcessorBase::adoptHubSlots(const mm::core::SlotSnapshot& snapshot, std::optional<double> stampPpq) {
    const juce::ScopedLock lock(slotsLock_);
    if (settings_.role != mm::core::InstanceRole::Voice || snapshot == nullptr) {
        return;
    }
    slots_ = *snapshot; // a copy: the hub's snapshot stays immutable
    undo_.clear();
    publisher_.sync(slots_, 4.0, stampPpq);
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
    settings_.octave = mm::core::clampOctave(settings.octave);
    publisher_.setOctave(settings_.octave);
    settings_.generation = mm::core::sanitize(settings.generation);
    outputVoice_.store(settings_.outputVoice);
    publisher_.setOutputVoice(settings_.outputVoice);
    publisher_.sync(slots_);
}

void ProcessorBase::generate() {
    GenerationJob job;
    bool isVoice;
    {
        const juce::ScopedLock lock(slotsLock_);
        job.settings = settings_.generation;
        isVoice = settings_.role == mm::core::InstanceRole::Voice;
    }
    if (isVoice) {
        // A voice plays what the hub generates: the request goes to the hub (SPEC 6.5), with the hub's settings.
        // Outside the lock: the registry calls into another instance.
        if (groupLink_->status() == mm::core::GroupStatus::VoiceConnected) {
            generationStatus_ = GenerationStatus::Forwarded;
            groupLink_->forward(mm::core::GroupAction::Generate);
        } else {
            generationStatus_ = GenerationStatus::UseHub; // no hub to ask
        }
        return;
    }
    job.seed =
        job.settings.seed ? *job.settings.seed : static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64());
    job.slot = std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1;
    {
        const juce::ScopedLock lock(slotsLock_);
        const auto* slot = slots_.slot(static_cast<size_t>(job.slot));
        if (slot != nullptr && slot->pattern && mm::core::hasLockedVoice(*slot->pattern)) {
            job.lockedFrom = slot->pattern;
        }
    }
    if (job.lockedFrom && std::all_of(job.lockedFrom->voices.begin(), job.lockedFrom->voices.end(),
                                      [](const mm::core::Track& track) { return mm::core::isVoiceLocked(track); })) {
        generationStatus_ = GenerationStatus::AllLocked; // nothing to generate
        return;
    }
    const auto backend = AiBackend::instance().get();
    if (backend.provider != nullptr) {
        job.provider = backend.provider;
        job.templates = &embeddedPrompts();
        job.model = backend.model;
        job.timeoutSeconds = backend.timeoutSeconds;
        job.maxTokens = backend.maxTokens;
    }
    startJob(std::move(job));
}

void ProcessorBase::startJob(GenerationJob job) {
    parked_.reset(); // a newer request replaces a result that is still waiting
    parkTimer_.reset();
    generationStatus_ = GenerationStatus::Generating;
    lastReport_ = {};
    lastJobUsesAi_ = job.provider != nullptr;
    lastJob_ = job;
    generator_.request(job);
}

bool ProcessorBase::refine(const std::string& instruction, std::optional<size_t> voice, std::optional<size_t> phrase,
                           std::optional<size_t> slotChoice) {
    bool isVoice;
    GenerationJob job;
    {
        const juce::ScopedLock lock(slotsLock_);
        isVoice = settings_.role == mm::core::InstanceRole::Voice;
        job.settings = settings_.generation;
    }
    if (isVoice) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return false;
    }
    const auto text = juce::String(instruction).trim().toStdString();
    const auto backend = AiBackend::instance().get();
    if (text.empty()) {
        generationStatus_ = GenerationStatus::NothingToRefine;
        return false;
    }
    if (backend.provider == nullptr) {
        generationStatus_ = GenerationStatus::RefineNeedsAi;
        return false;
    }
    job.slot = slotChoice ? static_cast<int>(*slotChoice)
                          : std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1;
    RefineRequest request;
    {
        const juce::ScopedLock lock(slotsLock_);
        const auto* slot = slots_.slot(static_cast<size_t>(job.slot));
        if (slot == nullptr || !slot->pattern) {
            generationStatus_ = GenerationStatus::NothingToRefine;
            return false;
        }
        request.pattern = *slot->pattern;
    }
    const auto& pattern = request.pattern;
    if (voice && *voice >= pattern.voices.size()) {
        generationStatus_ = GenerationStatus::NothingToRefine;
        return false;
    }
    const bool allLocked =
        voice ? mm::core::isVoiceLocked(pattern.voices[*voice])
              : std::all_of(pattern.voices.begin(), pattern.voices.end(),
                            [](const mm::core::Track& track) { return mm::core::isVoiceLocked(track); });
    if (allLocked) {
        generationStatus_ = GenerationStatus::RefineAllLocked;
        return false;
    }
    request.instruction = text;
    request.voice = voice;
    request.phrase = phrase;                // `refineWithAi` ignores it next to a voice or when it is out of range
    job.settings.styleId = pattern.styleId; // the pattern keeps the style it was made in
    job.provider = backend.provider;
    job.templates = &embeddedPrompts();
    job.model = backend.model;
    job.timeoutSeconds = backend.timeoutSeconds;
    job.maxTokens = backend.maxTokens;
    job.refine = std::move(request);
    startJob(std::move(job));
    return true;
}

bool ProcessorBase::importMidi(const juce::File& file, std::optional<size_t> voice) {
    return startImport(file, voice, false);
}

bool ProcessorBase::importVoice(const juce::File& file, std::optional<size_t> voice) {
    return startImport(file, voice, true);
}

bool ProcessorBase::startImport(const juce::File& file, std::optional<size_t> voice, bool apply) {
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension != ".mid" && extension != ".midi") {
        generationStatus_ = GenerationStatus::ImportUnreadable;
        return false;
    }
    ImportRequest request;
    request.file = file;
    request.apply = apply;
    request.slot = std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1;
    {
        const juce::ScopedLock lock(slotsLock_);
        // a voice instance imports for the voice it plays; the hub for the row the file was dropped on
        request.voice = settings_.role == mm::core::InstanceRole::Voice
                            ? static_cast<size_t>(std::max(settings_.outputVoice, 1) - 1)
                            : voice.value_or(0);
    }
    importer_.request(request);
    return true;
}

void ProcessorBase::onImported(const ImportRequest& request, ImportOutcome outcome) {
    pendingImport_.reset();
    if (outcome.unreadable ||
        (outcome.error != mm::core::MidiReadError::None && outcome.error != mm::core::MidiReadError::NoNotes)) {
        generationStatus_ = GenerationStatus::ImportUnreadable;
        return;
    }
    if (outcome.error == mm::core::MidiReadError::NoNotes || outcome.plan.status == mm::core::ImportStatus::NoNotes) {
        generationStatus_ = GenerationStatus::ImportNoNotes;
        return;
    }
    if (outcome.plan.status == mm::core::ImportStatus::NotFourFour) {
        generationStatus_ = GenerationStatus::ImportNotFourFour;
        return;
    }
    if (request.apply) {
        // A drum clip is no voice: it becomes the drum reference (SPEC 3.14).
        if (mm::core::suggestRole(outcome.plan.notes) == mm::core::TrackRole::Drums) {
            applyDrumReference(request, outcome.plan);
        } else {
            applyImport(request, outcome.plan);
        }
        return;
    }
    PendingImport pending;
    pending.voice = request.voice;
    pending.slot = request.slot;
    pending.fileName = request.file.getFileName().toStdString();
    pending.plan = std::move(outcome.plan);
    generationStatus_ = pending.plan.truncated ? GenerationStatus::ImportReadCut : GenerationStatus::ImportRead;
    pendingImport_ = std::move(pending);
}

void ProcessorBase::applyDrumReference(const ImportRequest& request, const mm::core::ImportPlan& plan) {
    mm::core::GenerationSettings settings;
    bool isVoice;
    {
        const juce::ScopedLock lock(slotsLock_);
        isVoice = settings_.role == mm::core::InstanceRole::Voice;
        settings = settings_.generation;
    }
    if (isVoice) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return;
    }
    const auto mapping = mm::core::drumMappingFromText(globalSettings().get().drumMap);
    const auto reference = mm::core::deriveRhythmReference(plan.notes, plan.lengthBars, mapping);
    const auto* style = styles_.findOrFallback(settings.styleId);
    if (!reference || style == nullptr) {
        generationStatus_ = GenerationStatus::DrumRefNoKickHat;
        return;
    }
    drumReference_ = reference;
    const auto index = static_cast<size_t>(request.slot);
    const bool applied = editNotes(index, [&](mm::core::Pattern& pattern) {
        mm::core::applyRhythmReference(pattern, *reference, *style);
        pattern.info.source = "edit";
        return size_t{1};
    });
    generationStatus_ = applied ? GenerationStatus::DrumRefApplied : GenerationStatus::DrumRefStored;
}

bool ProcessorBase::removeDrumReference(std::optional<size_t> slotChoice) {
    mm::core::GenerationSettings settings;
    {
        const juce::ScopedLock lock(slotsLock_);
        if (settings_.role == mm::core::InstanceRole::Voice) {
            return false;
        }
        settings = settings_.generation;
    }
    const auto* style = styles_.findOrFallback(settings.styleId);
    const size_t index =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    drumReference_.reset();
    editNotes(index, [&](mm::core::Pattern& pattern) {
        if (!pattern.rhythmRef || style == nullptr) {
            return size_t{0};
        }
        mm::core::removeRhythmReference(pattern, *style);
        pattern.info.source = "edit";
        return size_t{1};
    });
    generationStatus_ = GenerationStatus::DrumRefRemoved;
    return true;
}

void ProcessorBase::applyImport(const ImportRequest& request, const mm::core::ImportPlan& plan) {
    mm::core::GenerationSettings settings;
    bool isVoice;
    {
        const juce::ScopedLock lock(slotsLock_);
        isVoice = settings_.role == mm::core::InstanceRole::Voice;
        settings = settings_.generation;
    }
    if (isVoice) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return;
    }
    const auto* style = styles_.findOrFallback(settings.styleId);
    if (style == nullptr) {
        generationStatus_ = GenerationStatus::NoResult;
        return;
    }
    const auto index = static_cast<size_t>(request.slot);
    std::optional<mm::core::Pattern> imported;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* slot = bank.slot(index);
        std::optional<mm::core::Pattern> before = slot != nullptr ? slot->pattern : std::nullopt;
        const size_t cursor = slot != nullptr ? slot->cursor : 0;
        auto next = mm::core::importVoice(*style, before, request.voice, plan);
        if (next && bank.setResult(index, *next)) {
            undo_.recordResult(index, std::move(before), cursor, *bank.slot(index)->pattern);
            imported = std::move(next);
        }
    });
    if (!imported) {
        generationStatus_ = GenerationStatus::NoResult;
        return;
    }
    // The key of the slot is the one found (SPEC 3.18); the musician can correct it.
    auto next = instanceSettings();
    next.generation.root = imported->context.root;
    next.generation.scaleId = imported->context.scaleId;
    next.generation.lengthBars = imported->lengthBars;
    setInstanceSettings(next);
    generationStatus_ = GenerationStatus::ImportApplied;
    if (index == static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1)) {
        generate(); // the other voices, around the imported one
        importCut_ = plan.truncated && generationStatus_ == GenerationStatus::Generating;
    }
}

bool ProcessorBase::correctKey(mm::core::PitchClass root, const std::string& scaleId, std::optional<size_t> slotChoice) {
    const size_t index =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    const bool corrected = editNotes(index, [&](mm::core::Pattern& pattern) {
        if (pattern.context.root == root && pattern.context.scaleId == scaleId) {
            return size_t{0}; // nothing to correct
        }
        return mm::core::correctImportedKey(pattern, root, scaleId) ? size_t{1} : size_t{0};
    });
    if (corrected) {
        auto next = instanceSettings();
        next.generation.root = root;
        next.generation.scaleId = scaleId;
        setInstanceSettings(next);
    }
    return corrected;
}

ProcessorBase::RefineTargets ProcessorBase::refineTargets(std::optional<size_t> slotChoice) const {
    RefineTargets targets;
    const size_t index =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    const juce::ScopedLock lock(slotsLock_);
    const auto* slot = slots_.slot(index);
    if (slot != nullptr && slot->pattern) {
        for (const auto& track : slot->pattern->voices) {
            targets.voices.push_back(track.role);
        }
        for (const auto& phrase : slot->pattern->phrases) {
            targets.phrases.emplace_back(phrase.startBar, phrase.lengthBars);
        }
    }
    return targets;
}

void ProcessorBase::generateOffline() {
    if (!lastJob_ || lastJob_->refine) { // a refinement has no offline form
        return;
    }
    auto job = *lastJob_;
    job.provider.reset();
    startJob(std::move(job));
}

void ProcessorBase::retryGeneration() {
    if (lastJob_) {
        startJob(*lastJob_);
    }
}

void ProcessorBase::cancelGeneration() {
    if (generationStatus_ != GenerationStatus::Generating) {
        return;
    }
    generator_.cancel();
    generationStatus_ = GenerationStatus::GenerationCancelled;
}

bool ProcessorBase::vary(int strengthPct, std::optional<size_t> voice, std::optional<uint64_t> seed,
                         std::optional<size_t> slotChoice) {
    mm::core::ArchetypeSettings settings;
    size_t slotIndex = 0;
    bool isVoice;
    {
        const juce::ScopedLock lock(slotsLock_);
        isVoice = settings_.role == mm::core::InstanceRole::Voice;
        settings.energyPct = settings_.generation.energyPct;
        settings.creativityPct = settings_.generation.creativityPct;
    }
    if (isVoice) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return false;
    }
    slotIndex = slotChoice
                    ? *slotChoice
                    : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    const uint64_t useSeed = seed ? *seed : static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64());
    bool done = false;
    bool allLocked = false;
    size_t changes = 0;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* slot = bank.slot(slotIndex);
        if (slot == nullptr || !slot->pattern) {
            return;
        }
        const mm::core::Pattern& current = *slot->pattern;
        const auto candidates = voice ? std::vector<size_t>{*voice} : [&] {
            std::vector<size_t> all;
            for (size_t i = 0; i < current.voices.size(); ++i) {
                all.push_back(i);
            }
            return all;
        }();
        allLocked = std::all_of(candidates.begin(), candidates.end(), [&](size_t i) {
            return i < current.voices.size() && mm::core::isVoiceLocked(current.voices[i]);
        });
        if (allLocked) {
            return;
        }
        const auto* style = styles_.findOrFallback(current.styleId);
        if (style == nullptr) {
            return;
        }
        mm::core::Pattern varied = current;
        mm::core::VariationRequest request;
        request.seed = useSeed;
        request.strengthPct = strengthPct;
        request.voice = voice;
        changes = mm::core::applyVariation(varied, *style, settings, request);
        if (changes == 0) {
            return;
        }
        std::optional<mm::core::Pattern> before = current;
        const size_t cursor = slot->cursor;
        if (bank.setResult(slotIndex, std::move(varied))) {
            undo_.recordResult(slotIndex, std::move(before), cursor, *bank.slot(slotIndex)->pattern);
            done = true;
        }
    });
    lastVariationChanges_ = done ? changes : 0;
    generationStatus_ = done        ? GenerationStatus::Varied
                        : allLocked ? GenerationStatus::VaryAllLocked
                                    : GenerationStatus::NothingToVary;
    return done;
}

bool ProcessorBase::renewVoice(size_t voice, std::optional<uint64_t> seed, std::optional<size_t> slotChoice) {
    mm::core::GenerationRequest request;
    bool isVoice;
    {
        const juce::ScopedLock lock(slotsLock_);
        isVoice = settings_.role == mm::core::InstanceRole::Voice;
        request.settings.energyPct = settings_.generation.energyPct;
        request.settings.creativityPct = settings_.generation.creativityPct;
    }
    if (isVoice) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return false;
    }
    const size_t slotIndex =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    const uint64_t useSeed = seed ? *seed : static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64());
    bool done = false;
    bool locked = false;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* slot = bank.slot(slotIndex);
        if (slot == nullptr || !slot->pattern || voice >= slot->pattern->voices.size()) {
            return;
        }
        const mm::core::Pattern& current = *slot->pattern;
        if (mm::core::isVoiceLocked(current.voices[voice])) {
            locked = true;
            return;
        }
        const auto* style = styles_.findOrFallback(current.styleId);
        if (style == nullptr) {
            return;
        }
        mm::core::Pattern renewed = current;
        if (!mm::core::regenerateVoice(renewed, voice, *style, request, useSeed)) {
            return;
        }
        renewed.info.source = "algorithm";
        std::optional<mm::core::Pattern> before = current;
        const size_t cursor = slot->cursor;
        if (bank.setResult(slotIndex, std::move(renewed))) {
            undo_.recordResult(slotIndex, std::move(before), cursor, *bank.slot(slotIndex)->pattern);
            done = true;
        }
    });
    generationStatus_ = done     ? GenerationStatus::Renewed
                        : locked ? GenerationStatus::RenewLocked
                                 : GenerationStatus::NothingToRenew;
    return done;
}

bool ProcessorBase::historyBack(std::optional<size_t> slot) {
    return browseHistory(true, slot);
}

bool ProcessorBase::historyForward(std::optional<size_t> slot) {
    return browseHistory(false, slot);
}

bool ProcessorBase::browseHistory(bool back, std::optional<size_t> slotChoice) {
    {
        const juce::ScopedLock lock(slotsLock_);
        if (settings_.role == mm::core::InstanceRole::Voice) {
            return false;
        }
    }
    const size_t slotIndex =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    bool moved = false;
    editSlots([&](mm::core::SlotBank& bank) {
        moved = back ? bank.historyBack(slotIndex) : bank.historyForward(slotIndex);
        if (moved) {
            undo_.discardSlot(slotIndex);
        }
    });
    return moved;
}

void ProcessorBase::onGenerated(const GenerationJob& job, std::optional<mm::core::Pattern> pattern) {
    if (!pattern) {
        const bool aiFailed = lastReport_.usedAi && lastReport_.outcome != mm::ai::AiOutcome::Cancelled;
        if (aiFailed && autoOffline_ && !job.refine) {
            generateOffline();
            return;
        }
        // the slot stays as it was
        generationStatus_ = aiFailed ? GenerationStatus::AiFailed : GenerationStatus::NoResult;
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
    bool discarded = false;
    bool kept = false;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto index = static_cast<size_t>(job.slot);
        const auto* slot = bank.slot(index);
        if (job.refine && (slot == nullptr || !slot->pattern || slot->pattern->lengthBars != pattern.lengthBars ||
                           slot->pattern->voices.size() != pattern.voices.size())) {
            discarded = true; // the slot was emptied or replaced while the AI worked: the answer no longer fits
            return;
        }
        // Locked voices survive whatever happened while the job ran (SPEC 3.5): they are put back from the slot as it
        // is now. A result that no longer fits them (another length or number of voices) is dropped.
        if (slot != nullptr && slot->pattern && mm::core::hasLockedVoice(*slot->pattern)) {
            const auto& current = *slot->pattern;
            if (pattern.lengthBars != current.lengthBars || pattern.voices.size() != current.voices.size()) {
                discarded = true;
                return;
            }
            for (size_t i = 0; i < current.voices.size(); ++i) {
                if (mm::core::isVoiceLocked(current.voices[i])) {
                    pattern.voices[i] = current.voices[i];
                }
            }
            pattern.nextNoteId = std::max(pattern.nextNoteId, current.nextNoteId);
            // A result that was not made around the locks may reuse the ids of the locked notes: number it again.
            std::set<uint32_t> lockedIds;
            for (const auto& track : pattern.voices) {
                if (mm::core::isVoiceLocked(track)) {
                    for (const auto& note : track.notes) {
                        lockedIds.insert(note.id);
                    }
                }
            }
            for (auto& track : pattern.voices) {
                if (!mm::core::isVoiceLocked(track) &&
                    std::any_of(track.notes.begin(), track.notes.end(),
                                [&](const mm::core::Note& n) { return lockedIds.count(n.id) != 0; })) {
                    for (auto& note : track.notes) {
                        note.id = pattern.nextNoteId++;
                    }
                }
            }
            kept = true;
        }
        // New and generated patterns take the drum reference that was dropped last (SPEC 3.14).
        if (drumReference_ && !pattern.rhythmRef) {
            if (const auto* style = styles_.findOrFallback(pattern.styleId)) {
                mm::core::applyRhythmReference(pattern, *drumReference_, *style);
            }
        }
        std::optional<mm::core::Pattern> before = slot != nullptr ? slot->pattern : std::nullopt;
        const size_t cursor = slot != nullptr ? slot->cursor : 0;
        if (bank.setResult(index, std::move(pattern))) {
            undo_.recordResult(index, std::move(before), cursor, *bank.slot(index)->pattern);
        } else {
            discarded = true;
        }
    });
    generationStatus_ = discarded               ? GenerationStatus::NoResult
                        : job.refine            ? GenerationStatus::Refined
                        : kept && importCut_    ? GenerationStatus::DoneImportCut
                        : kept                  ? GenerationStatus::DoneLocked
                                                : GenerationStatus::Done;
    importCut_ = false;
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
    if (static_cast<mm::engine::SlotMode>(slotMode_.load(std::memory_order_acquire)) == mm::engine::SlotMode::Hub) {
        uint32_t mask = 0;
        for (size_t i = 0; i < muteValues_.size(); ++i) {
            if (muteValues_[i]->load() >= 0.5f) {
                mask |= 1u << i;
            }
        }
        groupSync_.reportMutes(mask); // mute works twice: the voices obey the hub's switches too
    }
    const bool muted = muteValues_[static_cast<size_t>(voice - 1)]->load() >= 0.5f ||
                       silent_.load(std::memory_order_acquire) ||
                       (voiceRole_.load(std::memory_order_acquire) && groupSync_.mutedByHub(voice));
    player_.setMuted(muted);
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

namespace {

/// Bass or Melody for the first voice of that role, "Voice n" for every other: the names are the file names of the
/// export, so two voices never share one.
juce::String voiceDisplayName(const mm::core::Pattern& pattern, size_t voiceIndex) {
    if (voiceIndex < pattern.voices.size()) {
        const auto role = pattern.voices[voiceIndex].role;
        const bool firstOfRole =
            std::none_of(pattern.voices.begin(), pattern.voices.begin() + static_cast<std::ptrdiff_t>(voiceIndex),
                         [role](const mm::core::Track& track) { return track.role == role; });
        if (firstOfRole) {
            return role == mm::core::VoiceRole::Melody ? "Melody" : "Bass";
        }
    }
    return "Voice " + juce::String(static_cast<int>(voiceIndex) + 1);
}

} // namespace

ProcessorBase::VoiceView ProcessorBase::playingVoice() const {
    VoiceView view;
    const juce::ScopedLock lock(slotsLock_);
    const mm::core::Pattern* pattern = nullptr;
    const auto* slot = playingSlotLocked(view);
    view.voice = settings_.outputVoice;
    view.octave = settings_.octave;
    if (slot != nullptr && slot->pattern) {
        pattern = &*slot->pattern;
    }
    if (pattern == nullptr) {
        return view;
    }
    fillVoiceLocked(view, *pattern, static_cast<size_t>(settings_.outputVoice - 1));
    return view;
}

std::vector<ProcessorBase::VoiceView> ProcessorBase::playingVoices() const {
    std::vector<VoiceView> views;
    const juce::ScopedLock lock(slotsLock_);
    VoiceView base;
    const auto* slot = playingSlotLocked(base);
    if (slot == nullptr || !slot->pattern) {
        return views;
    }
    const mm::core::Pattern& pattern = *slot->pattern;
    const size_t count = std::min<size_t>(pattern.voices.size(), mm::core::kMaxVoices);
    for (size_t index = 0; index < count; ++index) {
        VoiceView view = base;
        view.voice = static_cast<int>(index) + 1;
        view.octave = view.voice == settings_.outputVoice ? settings_.octave : 0;
        fillVoiceLocked(view, pattern, index);
        views.push_back(std::move(view));
    }
    return views;
}

const mm::core::Slot* ProcessorBase::playingSlotLocked(VoiceView& view) const {
    const int playing = handover_.activeVersion() != 0 ? handover_.activeSlot() : activeSlot();
    const int slotIndex = std::clamp(playing, 1, static_cast<int>(mm::core::kSlotCount)) - 1;
    view.slot = slotIndex + 1;
    view.origin = slots_.origin();
    const mm::core::Slot* slot = slots_.slot(static_cast<size_t>(slotIndex));
    if (slot != nullptr) {
        view.revision = slot->revision;
    }
    return slot;
}

void ProcessorBase::fillVoiceLocked(VoiceView& view, const mm::core::Pattern& pattern, size_t voiceIndex) const {
    const auto* style = styles_.findOrFallback(pattern.styleId);
    if (style == nullptr) {
        return;
    }
    auto rendered = mm::core::renderVoiceForPlayback(pattern, *style, voiceIndex, view.octave);
    view.hasPattern = true;
    view.notes = std::move(rendered.notes);
    view.lengthTicks = rendered.lengthTicks;
    view.voiceName = voiceDisplayName(pattern, voiceIndex);
    view.melody = view.voiceName == "Melody";
    view.locked = voiceIndex < pattern.voices.size() && mm::core::isVoiceLocked(pattern.voices[voiceIndex]);
    view.root = pattern.context.root;
    view.scaleId = pattern.context.scaleId;
    view.source.clear();
    if (voiceIndex < pattern.voices.size()) {
        for (const auto& note : pattern.voices[voiceIndex].notes) {
            view.source.push_back(
                {note.id, note.pitch, note.startTick, note.lengthTicks, note.velocity, note.accent, note.slide});
        }
    }
    view.displayName = view.melody ? tr(mm::core::text::kVoiceMelody)
                       : view.voiceName == "Bass"
                           ? tr(mm::core::text::kVoiceBass)
                           : tr(mm::core::text::kVoiceN, {{"n", std::to_string(voiceIndex + 1)}});
}

std::array<bool, mm::core::kSlotCount> ProcessorBase::slotUsage() const {
    std::array<bool, mm::core::kSlotCount> used{};
    const juce::ScopedLock lock(slotsLock_);
    for (size_t i = 0; i < used.size(); ++i) {
        used[i] = !slots_.isEmpty(i);
    }
    return used;
}

ProcessorBase::SlotInfo ProcessorBase::playingSlotInfo() const {
    SlotInfo info;
    const juce::ScopedLock lock(slotsLock_);
    VoiceView view;
    const auto* slot = playingSlotLocked(view);
    info.slot = view.slot;
    if (slot == nullptr || !slot->pattern) {
        return info;
    }
    const auto& pattern = *slot->pattern;
    info.filled = true;
    info.styleId = pattern.styleId;
    info.root = pattern.context.root;
    info.scaleId = pattern.context.scaleId;
    info.lengthBars = pattern.lengthBars;
    info.seed = pattern.info.seed;
    info.winnerSeed = pattern.info.winnerSeed;
    info.drumReference = pattern.rhythmRef.has_value();
    info.historySize = slot->history.size();
    info.historyIndex = slot->cursor;
    return info;
}

bool ProcessorBase::mutedByOwnSwitch() const {
    const int voice = outputVoice_.load(std::memory_order_relaxed);
    return muteValues_[static_cast<size_t>(voice - 1)]->load() >= 0.5f;
}

bool ProcessorBase::mutedByHub() const {
    return voiceRole_.load(std::memory_order_acquire) &&
           groupSync_.mutedByHub(outputVoice_.load(std::memory_order_relaxed));
}

void ProcessorBase::showHub() {
    groupLink_->forward(mm::core::GroupAction::ShowHub);
}

void ProcessorBase::bringEditorToFront() {
    frontRequests_.fetch_add(1);
    if (auto* editor = getActiveEditor()) {
        if (auto* top = editor->getTopLevelComponent()) {
            top->toFront(true);
        }
    }
}

void ProcessorBase::updateExport() {
    const double bpm = lastKnownBpm();
    const auto role = instanceSettings().role;
    const bool all = role != mm::core::InstanceRole::Voice; // a voice drags its own voice, Solo and Hub every voice
    std::vector<VoiceView> views;
    if (all) {
        views = playingVoices();
    }
    const VoiceView own = playingVoice();
    if (!all && own.hasPattern) {
        views.push_back(own);
    }
    ExportKey key;
    key.bpmCentis = std::llround(bpm * 100.0);
    key.origin = own.origin;
    key.revision = own.revision;
    key.slot = own.slot - 1;
    key.voice = own.voice;
    key.octave = own.octave;
    key.all = all;
    key.empty = views.empty();
    if (exportKey_ && *exportKey_ == key) {
        return;
    }
    exportKey_ = key;
    if (key.empty) {
        exporter_.clear();
        return;
    }
    MidiExporter::Batch batch;
    batch.bpm = bpm;
    batch.primaryVoice = own.voice;
    for (auto& view : views) {
        MidiExporter::Request request;
        request.notes = std::move(view.notes);
        request.lengthTicks = view.lengthTicks;
        request.voice = view.voice;
        request.fileName = "MidiMaid_Slot" + juce::String(view.slot) + "_" + view.voiceName.replace(" ", "") + ".mid";
        request.trackName = "MidiMaid " + view.voiceName;
        batch.files.push_back(std::move(request));
    }
    exporter_.requestExport(std::move(batch));
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
        root.setAttribute("octave", settings_.octave);
        root.setAttribute("slotFollow", juce::String(mm::core::toString(settings_.slotFollow).data(),
                                                     mm::core::toString(settings_.slotFollow).size()));
        root.setAttribute("genStyle", juce::String(settings_.generation.styleId));
        root.setAttribute("genBars", static_cast<int>(settings_.generation.lengthBars));
        root.setAttribute("genEnergy", settings_.generation.energyPct);
        root.setAttribute("genCreativity", settings_.generation.creativityPct);
        root.setAttribute("genRoot", settings_.generation.root
                                         ? juce::String(static_cast<int>(*settings_.generation.root))
                                         : juce::String("auto"));
        root.setAttribute("genScale", settings_.generation.scaleId ? juce::String(*settings_.generation.scaleId)
                                                                   : juce::String("auto"));
        root.setAttribute("genPrompt", juce::String::fromUTF8(settings_.generation.prompt.c_str()));
        root.setAttribute("genSeed", settings_.generation.seed
                                         ? juce::String(std::to_string(*settings_.generation.seed))
                                         : juce::String("random"));
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
    startKeyPending_ = false; // the project has its own key
    startKeyTimer_.reset();
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
    settings.octave = mm::core::clampOctave(root->getIntAttribute("octave", 0));
    settings.generation.energyPct = root->getIntAttribute("genEnergy", defaults.energyPct);
    settings.generation.creativityPct = root->getIntAttribute("genCreativity", defaults.creativityPct);
    // A state without key and scale comes from before they were settings: it keeps drawing them (D-149).
    settings.generation.root.reset();
    settings.generation.scaleId.reset();
    if (const auto text = root->getStringAttribute("genRoot"); text.containsOnly("0123456789") && text.isNotEmpty()) {
        const int value = text.getIntValue();
        if (value >= 0 && value <= 11) {
            settings.generation.root = static_cast<mm::core::PitchClass>(value);
        }
    }
    if (const auto text = root->getStringAttribute("genScale").toStdString();
        !text.empty() && mm::core::findScale(text) != nullptr) {
        settings.generation.scaleId = text;
    }
    settings.generation.seed = mm::core::parseSeed(root->getStringAttribute("genSeed").toStdString());
    settings.generation.prompt = root->getStringAttribute("genPrompt").toStdString();

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
        undo_.clear();
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
