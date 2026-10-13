#include "plugin/ProcessorBase.h"

#include "core/Analysis.h"
#include "core/DrumReference.h"
#include "core/KickGrid.h"
#include "core/LockRestore.h"
#include "core/Probability.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "core/VoiceImport.h"
#include "core/SlotBankJson.h"
#include "core/TextKeys.h"
#include "core/Variation.h"
#include "plugin/AiBackend.h"
#include "plugin/AiConnection.h"
#include "plugin/AppLog.h"
#include "plugin/EmbeddedPrompts.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/EmbeddedTranslation.h"
#include "core/Groove.h"
#include "core/OutputStage.h"
#include "plugin/EmbeddedGrooves.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/HostDetection.h"
#include "plugin/LibraryStore.h"
#include "plugin/PlaceholderEditor.h"
#include "plugin/ReferenceStore.h"
#include "plugin/StateMigration.h"

#include <algorithm>
#include <set>

namespace mm::plugin {

namespace {

constexpr int kReturnCollectIntervalMs = 250;
constexpr unsigned kTransposeThruBit = 20; ///< `midiThru` in the packed transposition settings (see `packTranspose`)
constexpr int kEvolveTickMs = 40;
/// The host parameters the message thread acts on: Evolve (D-214) and the triggers of SPEC 3.13 (D-218).
constexpr const char* kTriggerParameterIds[] = {"evolve",      "evolve_keep", "generate",    "variation_1",
                                                "variation_2", "variation_3", "variation_4", "variation_5",
                                                "variation_6", "variation_7", "variation_8", "variation_all"}; ///< how often the Evolve layer looks at the playhead
/// Upper bound of the bytes one block can write: the engine's event capacity, with room for the event header.
constexpr size_t kMidiBufferBytes = mm::engine::MidiEventList::kCapacity * 12;

/// Puts the reference set of the job's style into the job (SPEC 3.20, message thread): the entries for the copy
/// protection, the progressions and the set id always, the examples only for an AI job whose provider may get them
/// (a provider on this computer, or one the musician agreed to, D-86).
void addReferences(GenerationJob& job, bool withExamples, bool providerIsLocal) {
    const auto set = referenceStore().get(job.settings.styleId);
    job.references = mm::core::referenceEffectOf(set);
    job.examples.clear();
    if (!withExamples || job.provider == nullptr) {
        return;
    }
    const auto& consent = globalSettings().get().ai.cloudConsent;
    if (providerIsLocal || consent.count(job.provider->info().id) != 0) {
        job.examples = mm::core::personalExamples(set, job.settings.energyPct);
    }
}

} // namespace

/// Retries a parked generation result on the message thread until the host renders in real time again.
class ProcessorBase::ParkTimer : private juce::Timer {
public:
    explicit ParkTimer(std::function<void()> retry, int intervalMs = kReturnCollectIntervalMs)
        : retry_(std::move(retry)) {
        startTimer(intervalMs);
    }
    ~ParkTimer() override { stopTimer(); }

private:
    void timerCallback() override { retry_(); }
    std::function<void()> retry_;
};

// The listener of the two Evolve parameters (SPEC 3.13): the host may set them from any thread, the layer lives on the
// message thread.
class ProcessorBase::EvolveParameterListener : public juce::AudioProcessorValueTreeState::Listener {
public:
    EvolveParameterListener(ProcessorBase& owner, std::shared_ptr<bool> alive)
        : owner_(owner), alive_(std::move(alive)) {}

    void parameterChanged(const juce::String& id, float value) override {
        if (owner_.loadingState_.load()) {
            return; // a saved state sets the parameters: the layer itself is not saved
        }
        const bool on = value >= 0.5f;
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            owner_.onEvolveParameter(id, on);
        } else if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
            juce::MessageManager::callAsync([alive = alive_, owner = &owner_, id, on] {
                if (*alive) {
                    owner->onEvolveParameter(id, on);
                }
            });
        }
    }

private:
    ProcessorBase& owner_;
    std::shared_ptr<bool> alive_;
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
      publisher_(handover_, styles_, &embeddedGrooves()), player_(mm::core::PatternView{nullptr, 0, mm::core::kTicksPerBar}),
      returnCollector_(handover_), importer_([this](const ImportRequest& request, ImportOutcome outcome) {
          onImported(request, std::move(outcome));
      }),
      generator_(styles_, [this](const GenerationJob& job, std::optional<mm::core::Pattern> pattern) {
          onGenerated(job, std::move(pattern));
      }) {
    switch (detectHostCapabilities(*this).format) {
    case PluginFormat::Vst3:
        formatTag_ = "vst3";
        break;
    case PluginFormat::Au:
        formatTag_ = "au";
        break;
    default:
        break; // other formats are not counted: the warning is about the two that cannot share a registry
    }
    setFormatTag(formatTag_);
    AiConnection::instance().start(); // applies the provider of the settings once they are read
    generator_.setReportHandler([this](const GenerationJob&, const GenerationReport& report) { lastReport_ = report; });
    slotValue_ = parameters_.getRawParameterValue("slot");
    transposeValue_ = parameters_.getRawParameterValue("transpose");
    setTransposeSettings(mm::core::TransposeSettings{});
    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        muteValues_[static_cast<size_t>(voice - 1)] =
            parameters_.getRawParameterValue(mm::core::muteParameterId(voice));
    }
    player_.attach(&handover_);
    evolveListener_ = std::make_unique<EvolveParameterListener>(*this, libraryAlive_);
    for (const auto id : kTriggerParameterIds) {
        parameters_.addParameterListener(id, evolveListener_.get());
    }
    groupLink_ = GroupLink::create(*this);
    groupLink_->setRole(settings_.role, settings_.outputVoice);
    applyStartKey();
    syncTransposeSettings();
    transposeSyncTimer_ = std::make_unique<ParkTimer>([this] { syncTransposeSettings(); });
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
    transposeSyncTimer_.reset();
    for (const auto id : kTriggerParameterIds) {
        parameters_.removeParameterListener(id, evolveListener_.get());
    }
    evolveTimer_.reset();
    *libraryAlive_ = false;
    groupLink_->detach();                              // first: from here on no registry callback reaches this instance
    onGroupStatusChanged(mm::core::GroupStatus::Solo); // gives the channel slot back
}

void ProcessorBase::setFormatTag(std::string tag) {
    formatRegistration_.reset(); // first: gives the old tag back
    formatTag_ = std::move(tag);
    formatRegistration_ = std::make_unique<mm::platform::InstanceRegistration>(formatTag_);
}

bool ProcessorBase::otherFormatInProcess() const {
    return !formatTag_.empty() && mm::platform::otherFormatsPresent(formatTag_);
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

void ProcessorBase::onEvolveParameter(const juce::String& id, bool on) {
    if (id == "evolve") {
        if (!isNonRealtime()) { // an offline render ignores it (SPEC 6.6)
            setEvolve(on, true);
        }
    } else if (!on) {
        return; // the fall of a trigger back to 0 is no edge
    } else if (id == "evolve_keep") {
        if (!isNonRealtime()) {
            evolveKeep();
        }
        resetTrigger(id);
    } else if (id == "generate") {
        if (!isNonRealtime()) { // an AI request is neither fast nor reproducible: not in a bounce (SPEC 3.13)
            generate();
        }
        resetTrigger(id);
    } else if (id == "variation_all") {
        if (!isNonRealtime()) {
            varyFromTrigger(0);
        }
        resetTrigger(id);
    } else if (id.startsWith("variation_")) {
        if (!isNonRealtime()) {
            varyFromTrigger(id.fromLastOccurrenceOf("_", false, false).getIntValue());
        }
        resetTrigger(id);
    }
}

void ProcessorBase::resetTrigger(const juce::String& id) {
    if (auto* parameter = parameters_.getParameter(id)) { // a trigger: back to 0 for the next edge
        parameter->setValueNotifyingHost(0.0f);
    }
}

void ProcessorBase::varyFromTrigger(int voice) {
    mm::core::InstanceRole role;
    {
        const juce::ScopedLock lock(slotsLock_);
        role = settings_.role;
    }
    if (role == mm::core::InstanceRole::Voice) {
        // A voice's triggers go to the hub (SPEC 3.13); without a hub there is nothing to vary.
        if (groupLink_->status() == mm::core::GroupStatus::VoiceConnected) {
            generationStatus_ = GenerationStatus::Forwarded;
            groupLink_->forwardVary(voice);
        } else {
            generationStatus_ = GenerationStatus::UseHub;
        }
        return;
    }
    auto& store = globalSettings();
    const int strength = store.enabled() ? store.get().variationStrength : mm::core::GlobalSettings{}.variationStrength;
    vary(strength, voice > 0 ? std::optional<size_t>(static_cast<size_t>(voice - 1)) : std::nullopt);
}

void ProcessorBase::setEvolveParameter(bool on) {
    if (auto* parameter = parameters_.getParameter("evolve")) {
        const float value = on ? 1.0f : 0.0f;
        if (parameter->getValue() != value) {
            parameter->setValueNotifyingHost(value);
        }
    }
}

void ProcessorBase::sendOverlay(const std::optional<OverlayMessage>& message) {
    if (message) {
        groupLink_->publishOverlay(message->overlay, message->stamp); // outside the lock: the registry may call back
    }
}

bool ProcessorBase::evolveCompany() const {
    return settings_.role == mm::core::InstanceRole::Hub && groupLink_->status() == mm::core::GroupStatus::Hub &&
           mm::engine::processGroupChannel().memberCount() > 1;
}

std::optional<double> ProcessorBase::evolveBarStamp() const {
    return evolveCompany() ? mm::engine::plannedStamp(mm::engine::processGroupChannel(), lastKnownBpm())
                           : std::nullopt;
}

ProcessorBase::OverlayMessage ProcessorBase::evolveShow(size_t slot, const mm::core::Pattern* stage, double gridPpq,
                                                        std::optional<double> stamp) {
    if (stage != nullptr) {
        publisher_.publishPattern(slot, *stage, gridPpq, stamp);
    } else {
        publisher_.restoreSlot(slots_, slot, gridPpq, stamp);
    }
    OverlayMessage message;
    message.stamp = stamp;
    auto overlay = std::make_shared<mm::core::SlotOverlay>();
    overlay->slot = slot;
    if (stage != nullptr) {
        overlay->pattern = std::make_shared<const mm::core::Pattern>(*stage);
    }
    message.overlay = std::move(overlay);
    return message;
}

void ProcessorBase::setEvolve(bool on, bool fromHost) {
    if (on == evolveOn_) {
        if (!on) {
            evolveTimer_.reset(); // an idle timer from an offline render
        }
        return;
    }
    if (on) {
        const juce::ScopedLock lock(slotsLock_);
        if (settings_.role == mm::core::InstanceRole::Voice) {
            generationStatus_ = GenerationStatus::UseHub; // the layer belongs to the hub (SPEC 6.7)
            return;
        }
    }
    evolveOn_ = on;
    if (on) {
        if (!evolveTimer_) {
            evolveTimer_ = std::make_unique<ParkTimer>([this] { evolveTick(); }, kEvolveTickMs);
        }
    } else {
        evolveTimer_.reset();
        sendOverlay(evolveEnd(true));
    }
    if (!fromHost) { // the host moved the parameter itself: writing it back would race with its own writes (pluginval)
        setEvolveParameter(on);
    }
}

ProcessorBase::EvolveInfo ProcessorBase::evolveInfo() const {
    const juce::ScopedLock lock(slotsLock_);
    EvolveInfo info;
    info.on = evolveOn_;
    info.layerActive = evolveLayer_.active();
    if (info.layerActive) {
        info.stage = evolveLayer_.stage();
        info.canBack = evolveLayer_.canBack();
        info.changed = evolveLayer_.changed();
    }
    return info;
}

void ProcessorBase::setEvolveSettings(const mm::core::EvolveSettings& evolve) {
    const juce::ScopedLock lock(slotsLock_);
    settings_.evolve = mm::core::clampEvolveSettings(evolve);
}

uint64_t ProcessorBase::newEvolveSeed() const {
    return evolveSeed_ ? *evolveSeed_ : static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64());
}

std::optional<ProcessorBase::OverlayMessage> ProcessorBase::evolveEnd(bool restoreSlot) {
    std::optional<OverlayMessage> message;
    {
        const juce::ScopedLock lock(slotsLock_);
        if (evolveLayer_.active() && restoreSlot && evolveStagePublished_) {
            message = evolveShow(evolveLayer_.slot(), nullptr, 4.0, evolveBarStamp());
        }
        evolveLayer_.stop();
        evolveStagePublished_ = false;
        evolveLastPass_.reset();
        evolvePasses_ = 0;
        evolveAdvanceDue_ = false;
    }
    return message;
}

void ProcessorBase::evolveCommitBefore(size_t slotIndex) {
    if (!evolveOn_) {
        return;
    }
    bool due;
    {
        const juce::ScopedLock lock(slotsLock_);
        due = evolveLayer_.active() && evolveLayer_.slot() == slotIndex && evolveLayer_.changed();
    }
    if (due) {
        evolveKeep(); // a manual action works on what sounds: nothing the musician heard gets lost (SPEC 6.8)
    }
}

bool ProcessorBase::evolveKeep() {
    std::optional<mm::core::Pattern> stage;
    size_t slotIndex = 0;
    {
        const juce::ScopedLock lock(slotsLock_);
        if (!evolveOn_ || !evolveLayer_.active() || !evolveLayer_.changed()) {
            generationStatus_ = GenerationStatus::EvolveNothingToKeep;
            return false;
        }
        stage = evolveLayer_.current();
        slotIndex = evolveLayer_.slot();
    }
    bool done = false;
    uint64_t revision = 0;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* slot = bank.slot(slotIndex);
        if (slot == nullptr || !slot->pattern) {
            return;
        }
        std::optional<mm::core::Pattern> before = slot->pattern;
        const size_t cursor = slot->cursor;
        if (bank.setResult(slotIndex, std::move(*stage))) {
            undo_.recordResult(slotIndex, std::move(before), cursor, *bank.slot(slotIndex)->pattern);
            revision = bank.slot(slotIndex)->revision;
            done = true;
        }
    });
    if (!done) {
        generationStatus_ = GenerationStatus::EvolveNothingToKeep;
        return false;
    }
    std::optional<OverlayMessage> message;
    {
        // The stage is the slot now: the layer goes on from there; the engine already plays this content. The voices
        // get the slot set with it (above) and drop their copy of the stage.
        const juce::ScopedLock lock(slotsLock_);
        if (const auto* slot = slots_.slot(slotIndex); slot != nullptr && slot->pattern) {
            evolveLayer_.start(slotIndex, *slot->pattern, newEvolveSeed());
            evolveRevision_ = revision;
            evolveStagePublished_ = false;
            if (evolveCompany()) {
                message = OverlayMessage{nullptr, std::nullopt};
            }
        }
    }
    sendOverlay(message);
    generationStatus_ = GenerationStatus::EvolveKept;
    return true;
}

bool ProcessorBase::evolveBack() {
    std::optional<OverlayMessage> message;
    {
        const juce::ScopedLock lock(slotsLock_);
        if (!evolveOn_ || !evolveLayer_.back()) {
            return false;
        }
        const mm::core::Pattern stage = evolveLayer_.current();
        message = evolveShow(evolveLayer_.slot(), &stage, 4.0, evolveBarStamp());
        evolveStagePublished_ = true;
    }
    sendOverlay(message);
    return true;
}

void ProcessorBase::evolveTick() {
    if (!evolveOn_) {
        return;
    }
    if (isNonRealtime()) { // an offline render plays the saved slots only (SPEC 6.6)
        // Not `setEvolve`: it would destroy the timer whose callback is running. The idle timer goes with the next toggle.
        evolveOn_ = false;
        sendOverlay(evolveEnd(true));
        return;
    }
    std::optional<OverlayMessage> message;
    {
        const juce::ScopedLock lock(slotsLock_);
        evolveTickLocked(message);
    }
    sendOverlay(message);
}

void ProcessorBase::evolveTickLocked(std::optional<OverlayMessage>& message) {
    const auto slotIndex =
        static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    const std::optional<double> ppq = playheadPpq();
    if (settings_.role == mm::core::InstanceRole::Voice) {
        return;
    }
    const auto* slot = slots_.slot(slotIndex);
    if (slot == nullptr || !slot->pattern) {
        if (evolveLayer_.active()) {
            // the slot of the layer lost its content: nothing to restore
            evolveLayer_.stop();
            evolveStagePublished_ = false;
        }
        return;
    }
    if (evolveLayer_.active() && evolveLayer_.slot() != slotIndex) {
        // A slot change ends the layer of the old slot without taking it over (SPEC 6.7).
        if (evolveStagePublished_) {
            message = evolveShow(evolveLayer_.slot(), nullptr, 4.0, evolveBarStamp());
        }
        evolveLayer_.stop();
        evolveStagePublished_ = false;
    }
    if (!evolveLayer_.active() || slot->revision != evolveRevision_) {
        // New layer, or the slot got a manual result: the layer starts again from what the slot holds now (SPEC 6.8).
        evolveLayer_.start(slotIndex, *slot->pattern, newEvolveSeed());
        evolveRevision_ = slot->revision;
        evolveStagePublished_ = false;
        evolveLastPass_.reset();
        evolvePasses_ = 0;
        evolveAdvanceDue_ = false;
    }
    if (!ppq) {
        evolveLastPass_.reset(); // the transport stands: counting starts again at the next start
        evolveAdvanceDue_ = false;
        return;
    }
    const double lengthPpq = static_cast<double>(slot->pattern->lengthBars) * 4.0;
    if (lengthPpq <= 0.0) {
        return;
    }
    const auto pass = static_cast<int64_t>(std::floor(*ppq / lengthPpq));
    if (!evolveLastPass_ || *evolveLastPass_ != pass) {
        evolveLastPass_ = pass;
        ++evolvePasses_;
        evolveAdvanceDue_ = evolvePasses_ % settings_.evolve.everyPasses == 0;
    }
    // The stage is made in the middle of the pass, not at its start: the switch of the stage before sits at the start
    // of this pass, and a voice that runs a little behind the hub must have taken it before the next stage reaches it
    // (one pending result per slot would replace it, D-214).
    if (!evolveAdvanceDue_ || *ppq - static_cast<double>(pass) * lengthPpq < lengthPpq / 2.0) {
        return;
    }
    evolveAdvanceDue_ = false;
    const auto* style = styles_.findOrFallback(slot->pattern->styleId);
    if (style == nullptr) {
        return;
    }
    mm::core::ArchetypeSettings archetype;
    archetype.energyPct = settings_.generation.energyPct;
    archetype.creativityPct = settings_.generation.creativityPct;
    if (evolveLayer_.advance(*slot->pattern, *style, archetype, settings_.evolve)) {
        // The stage plays from the next pass: the pass that has just begun is the last one of the stage before. In a
        // group the start is stamped, so that hub and voices switch at the same position.
        const mm::core::Pattern stage = evolveLayer_.current();
        std::optional<double> stamp;
        if (evolveCompany()) {
            stamp = static_cast<double>(pass + 1) * lengthPpq;
        }
        message = evolveShow(slotIndex, &stage, lengthPpq, stamp);
        evolveStagePublished_ = true;
    }
}

void ProcessorBase::adoptHubOverlay(const mm::core::OverlayPtr& overlay, std::optional<double> stampPpq) {
    const juce::ScopedLock lock(slotsLock_);
    if (settings_.role != mm::core::InstanceRole::Voice) {
        return;
    }
    if (overlay != nullptr && overlay->pattern != nullptr) {
        voiceOverlay_ = overlay;
        publisher_.publishPattern(overlay->slot, *overlay->pattern, 4.0, stampPpq);
    } else if (voiceOverlay_ != nullptr) {
        publisher_.restoreSlot(slots_, voiceOverlay_->slot, 4.0, stampPpq);
        voiceOverlay_.reset();
    }
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
    evolveCommitBefore(slotIndex);
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

bool ProcessorBase::setVoiceGroove(size_t slotIndex, size_t voice, const std::optional<std::string>& templateId,
                                   std::optional<float> swing, bool mergeWithPrevious) {
    if (templateId && *templateId != mm::core::kGrooveStraight && *templateId != mm::core::kGrooveDrumReference &&
        embeddedGrooves().find(*templateId) == nullptr) {
        return false; // a template that does not exist
    }
    return editNotes(
        slotIndex,
        [&](mm::core::Pattern& pattern) -> size_t {
            if (voice >= pattern.voices.size()) {
                return 0;
            }
            auto& groove = pattern.voices[voice].groove;
            size_t changed = 0;
            if (templateId && groove.templateId != *templateId) {
                groove.templateId = *templateId;
                ++changed;
            }
            if (swing) {
                const float clamped = std::clamp(*swing, 0.5f, 0.75f);
                if (groove.swing != clamped) {
                    groove.swing = clamped;
                    ++changed;
                }
            }
            return changed;
        },
        mergeWithPrevious);
}

bool ProcessorBase::setVoiceLocked(size_t slotIndex, size_t voice, bool locked) {
    return editNotes(slotIndex,
                     [&](mm::core::Pattern& pattern) { return mm::core::setVoiceLocked(pattern, voice, locked); });
}

bool ProcessorBase::setVoiceLock(size_t slotIndex, size_t voice, mm::core::LockDimension dimension, bool locked) {
    return editNotes(slotIndex, [&](mm::core::Pattern& pattern) {
        return mm::core::setVoiceLock(pattern, voice, dimension, locked);
    });
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
        size_t second = mm::core::UndoStack::kNoSlot;
        if (!(back ? undo_.undo(slots_, &slot, &second) : undo_.redo(slots_, &slot, &second))) {
            return false;
        }
        if (second != mm::core::UndoStack::kNoSlot || !publisher_.publishEdit(slots_, slot)) {
            publisher_.sync(slots_); // two slots changed, or one is empty again: the engine drops its pattern at the next bar line
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
    if (slots_.origin() != snapshot->origin()) {
        voiceOverlay_.reset(); // the publisher starts over with another bank: nothing of the old stage is left
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
    settings_.evolve = mm::core::clampEvolveSettings(settings.evolve);
    publisher_.setOctave(settings_.octave);
    settings_.generation = mm::core::sanitize(settings.generation);
    outputVoice_.store(settings_.outputVoice);
    legato_.store(settings_.legato, std::memory_order_release);
    publisher_.setOutputVoice(settings_.outputVoice);
    publisher_.sync(slots_);
    // A new voice or octave published the slots again: the stage that sounds goes to the engine once more.
    if (evolveStagePublished_ && evolveLayer_.active()) {
        publisher_.publishPattern(evolveLayer_.slot(), evolveLayer_.current(), 4.0);
    } else if (voiceOverlay_ != nullptr && voiceOverlay_->pattern != nullptr &&
               settings_.role == mm::core::InstanceRole::Voice) {
        publisher_.publishPattern(voiceOverlay_->slot, *voiceOverlay_->pattern, 4.0);
    }
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
        if (slot != nullptr && slot->pattern && mm::core::hasAnyLock(*slot->pattern)) { // also locks of one dimension (D-231)
            job.lockedFrom = slot->pattern;
        }
        if (slot != nullptr && !job.settings.seed) { // a fixed seed asks for exactly that pattern: no protection (D-225)
            // the last results of the slot: a new pattern should not repeat them
            auto results = slot->history;
            if (slot->pattern) {
                results.push_back(*slot->pattern);
            }
            job.history = mm::core::historyEntriesOf(results);
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
    addReferences(job, true, !backend.cloud);
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
    addReferences(job, false, !backend.cloud); // a refinement sends the pattern, no examples; the set only protects
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
    if (request.toLibrary) {
        saveImportedToLibrary(request, outcome.plan);
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
        mm::core::applyRhythmReference(pattern, *reference, *style, settings.hatMode);
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

bool ProcessorBase::isHubOrSolo() const {
    const juce::ScopedLock lock(slotsLock_);
    return settings_.role != mm::core::InstanceRole::Voice;
}

bool ProcessorBase::saveToLibrary(const std::string& name, std::optional<size_t> slotChoice) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return false;
    }
    const size_t index =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    std::optional<mm::core::Pattern> pattern;
    std::string slotName;
    uint8_t color = 0;
    {
        const juce::ScopedLock lock(slotsLock_);
        if (const auto* slot = slots_.slot(index); slot != nullptr && slot->pattern) {
            pattern = slot->pattern;
            slotName = slot->name;
            color = slot->color;
        }
    }
    if (!pattern) {
        generationStatus_ = GenerationStatus::LibraryNothingToSave;
        return false;
    }
    const auto result = libraryStore().savePattern(name, *pattern, slotName, color);
    generationStatus_ = result.error == LibraryStore::SaveError::None ? GenerationStatus::LibrarySaved
                                                                      : GenerationStatus::LibraryFull;
    return result.error == LibraryStore::SaveError::None;
}

bool ProcessorBase::saveSetToLibrary(const std::string& name) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    LibraryStore::SaveResult result;
    {
        const juce::ScopedLock lock(slotsLock_);
        result = libraryStore().saveSet(name, slots_);
    }
    generationStatus_ = result.error == LibraryStore::SaveError::None   ? GenerationStatus::LibrarySetSaved
                        : result.error == LibraryStore::SaveError::Full ? GenerationStatus::LibraryFull
                                                                        : GenerationStatus::LibraryNothingToSave;
    return result.error == LibraryStore::SaveError::None;
}

bool ProcessorBase::loadFromLibrary(const std::string& id, std::optional<size_t> slotChoice) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    const size_t index =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    if (!libraryStore().info(id)) {
        generationStatus_ = GenerationStatus::LibraryLoadFailed;
        return false;
    }
    const auto alive = libraryAlive_;
    libraryStore().load(id, [this, alive, index](mm::core::LibraryLoadResult loaded) {
        if (*alive) {
            applyLibraryEntry(index, std::move(loaded));
        }
    });
    return true;
}

void ProcessorBase::applyLibraryEntry(size_t index, mm::core::LibraryLoadResult loaded) {
    if (!loaded.ok()) {
        generationStatus_ = GenerationStatus::LibraryLoadFailed;
        return;
    }
    const auto& entry = *loaded.entry;
    const auto put = [&](mm::core::SlotBank& bank, size_t target, mm::core::SlotContent content) {
        const auto* slot = bank.slot(target);
        std::optional<mm::core::Pattern> before = slot != nullptr ? slot->pattern : std::nullopt;
        const size_t cursor = slot != nullptr ? slot->cursor : 0;
        if (bank.paste(target, content)) {
            undo_.recordResult(target, std::move(before), cursor, *bank.slot(target)->pattern);
            return true;
        }
        return false;
    };
    bool any = false;
    editSlots([&](mm::core::SlotBank& bank) {
        if (entry.pattern) {
            mm::core::SlotContent content;
            content.pattern = entry.pattern;
            content.name = entry.slotName.value_or("");
            content.color = entry.slotColor;
            any = put(bank, index, std::move(content));
        } else if (entry.set) {
            for (size_t i = 0; i < mm::core::kSlotCount; ++i) {
                if (const auto content = entry.set->copy(i); content && content->pattern) {
                    any = put(bank, i, *content) || any;
                }
            }
        }
    });
    generationStatus_ = !any                   ? GenerationStatus::LibraryLoadFailed
                        : entry.set.has_value() ? GenerationStatus::LibrarySetLoaded
                                                : GenerationStatus::LibraryLoaded;
}

bool ProcessorBase::previewLibraryEntry(const std::string& id) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    if (!libraryStore().info(id)) {
        generationStatus_ = GenerationStatus::LibraryLoadFailed;
        return false;
    }
    const auto alive = libraryAlive_;
    libraryStore().load(id, [this, alive](mm::core::LibraryLoadResult loaded) {
        if (*alive) {
            startLibraryPreview(std::move(loaded));
        }
    });
    return true;
}

void ProcessorBase::startLibraryPreview(mm::core::LibraryLoadResult loaded) {
    if (!loaded.ok()) {
        generationStatus_ = GenerationStatus::LibraryLoadFailed;
        return;
    }
    const auto& entry = *loaded.entry;
    std::optional<mm::core::Pattern> pattern = entry.pattern;
    for (size_t i = 0; !pattern && entry.set && i < mm::core::kSlotCount; ++i) {
        if (const auto content = entry.set->copy(i); content && content->pattern) {
            pattern = content->pattern;
        }
    }
    if (!pattern) {
        generationStatus_ = GenerationStatus::LibraryLoadFailed;
        return;
    }
    endPreviewOverlay(); // another entry is playing: its slot comes back first
    const size_t index = static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    {
        const juce::ScopedLock lock(slotsLock_);
        publisher_.publishPattern(index, *pattern, 4.0);
        previewOverlaySlot_ = index;
    }
    setPreview(true);
}

void ProcessorBase::endPreviewOverlay() {
    if (!previewOverlaySlot_) {
        return;
    }
    const juce::ScopedLock lock(slotsLock_);
    publisher_.reset(); // what is in the slot comes back (and an empty slot becomes silent again)
    publisher_.sync(slots_);
    previewOverlaySlot_.reset();
}

void ProcessorBase::stopLibraryPreview() {
    setPreview(false);
    endPreviewOverlay();
}

void ProcessorBase::pollPreview() {
    if (previewOverlaySlot_ && !previewing()) {
        endPreviewOverlay();
    }
}

bool ProcessorBase::exportLibraryEntry(const std::string& id, const juce::File& target) {
    if (!libraryStore().info(id)) {
        generationStatus_ = GenerationStatus::LibraryExportFailed;
        return false;
    }
    const auto info = *libraryStore().info(id);
    const auto* style = styles_.findOrFallback(info.styleId);
    if (style == nullptr || info.kind != mm::core::LibraryKind::Pattern) {
        generationStatus_ = GenerationStatus::LibraryExportFailed;
        return false;
    }
    const auto alive = libraryAlive_;
    libraryStore().exportMidi(id, *style, lastKnownBpm(), target, [this, alive](LibraryStore::ExportResult result) {
        if (*alive) {
            generationStatus_ = result == LibraryStore::ExportResult::Done ? GenerationStatus::LibraryExported
                                                                          : GenerationStatus::LibraryExportFailed;
        }
    });
    return true;
}

bool ProcessorBase::importMidiToLibrary(const juce::File& file) {
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension != ".mid" && extension != ".midi") {
        generationStatus_ = GenerationStatus::ImportUnreadable;
        return false;
    }
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    ImportRequest request;
    request.file = file;
    request.toLibrary = true;
    importer_.request(request);
    return true;
}

void ProcessorBase::saveImportedToLibrary(const ImportRequest& request, const mm::core::ImportPlan& plan) {
    mm::core::GenerationSettings settings;
    {
        const juce::ScopedLock lock(slotsLock_);
        settings = settings_.generation;
    }
    const auto* style = styles_.findOrFallback(settings.styleId);
    const auto pattern = style != nullptr ? mm::core::importVoice(*style, std::nullopt, 0, plan) : std::nullopt;
    if (!pattern) {
        generationStatus_ = GenerationStatus::ImportUnreadable;
        return;
    }
    const auto result = libraryStore().savePattern(request.file.getFileNameWithoutExtension().toStdString(), *pattern,
                                                   "", 0);
    generationStatus_ = result.error == LibraryStore::SaveError::None ? GenerationStatus::LibraryImported
                                                                      : GenerationStatus::LibraryFull;
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
    evolveCommitBefore(slotIndex);
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
        request.settings.chromaticPct = settings_.generation.chromaticPct.value_or(-1);
    }
    if (isVoice) {
        generationStatus_ = GenerationStatus::UseHub; // voices only show (SPEC 8.1)
        return false;
    }
    const size_t slotIndex =
        slotChoice ? *slotChoice
                   : static_cast<size_t>(std::clamp(activeSlot(), 1, static_cast<int>(mm::core::kSlotCount)) - 1);
    evolveCommitBefore(slotIndex);
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

namespace {

double steadySeconds() {
    return juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

std::atomic<ProcessorBase::PlayheadClock> playheadClock{&steadySeconds};

double playheadSeconds() {
    return playheadClock.load(std::memory_order_relaxed)();
}

} // namespace

void ProcessorBase::setPlayheadClock(PlayheadClock clock) {
    playheadClock.store(clock != nullptr ? clock : &steadySeconds);
}

std::optional<double> ProcessorBase::playheadPpq() const {
    return playhead_.ppqAt(playheadSeconds());
}

bool ProcessorBase::hasSlotClipboard() const {
    return slotClipboard_.has_value() && slotClipboard_->pattern.has_value();
}

bool ProcessorBase::copySlot(size_t index) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    std::optional<mm::core::SlotContent> content;
    {
        const juce::ScopedLock lock(slotsLock_);
        content = slots_.copy(index);
    }
    if (!content || !content->pattern) {
        generationStatus_ = GenerationStatus::SlotNothingToCopy;
        return false;
    }
    slotClipboard_ = std::move(content);
    generationStatus_ = GenerationStatus::SlotCopied;
    return true;
}

bool ProcessorBase::pasteSlot(size_t index) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    if (!hasSlotClipboard() || index >= mm::core::kSlotCount) {
        generationStatus_ = GenerationStatus::SlotNothingToCopy;
        return false;
    }
    bool pasted = false;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* slot = bank.slot(index);
        std::optional<mm::core::Pattern> before = slot != nullptr ? slot->pattern : std::nullopt;
        const size_t cursor = slot != nullptr ? slot->cursor : 0;
        if (bank.paste(index, *slotClipboard_)) {
            undo_.recordResult(index, std::move(before), cursor, *bank.slot(index)->pattern);
            pasted = true;
        }
    });
    generationStatus_ = pasted ? GenerationStatus::SlotPasted : GenerationStatus::SlotNothingToCopy;
    return pasted;
}

bool ProcessorBase::copySlotTo(size_t from, size_t to) {
    if (from == to) {
        return false;
    }
    return copySlot(from) && pasteSlot(to);
}

bool ProcessorBase::clearSlot(size_t index) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    bool cleared = false;
    bool wasFilled = false;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* current = bank.slot(index);
        wasFilled = current != nullptr && current->pattern.has_value();
        std::optional<mm::core::Slot> before;
        if (current != nullptr) {
            before = *current;
        }
        cleared = bank.clear(index);
        if (cleared) {
            undo_.recordClear(index, std::move(*before)); // the way back: a slot is not lost for good
        }
    });
    if (cleared) { // the log says what was done to the slots, for a report like "my pattern is gone"
        AppLog::instance().write(LogLevel::Info, "slots",
                                 "clear slot " + juce::String(static_cast<int>(index) + 1) + (wasFilled ? " (was filled)" : " (was empty)"));
    }
    generationStatus_ = cleared ? GenerationStatus::SlotCleared : GenerationStatus::SlotNothingToCopy;
    return cleared;
}

bool ProcessorBase::swapSlots(size_t first, size_t second) {
    if (!isHubOrSolo()) {
        generationStatus_ = GenerationStatus::UseHub;
        return false;
    }
    bool swapped = false;
    bool firstFilled = false;
    bool secondFilled = false;
    editSlots([&](mm::core::SlotBank& bank) {
        const auto* a = bank.slot(first);
        const auto* b = bank.slot(second);
        firstFilled = a != nullptr && a->pattern.has_value();
        secondFilled = b != nullptr && b->pattern.has_value();
        swapped = first != second && bank.swap(first, second);
        if (swapped) {
            undo_.recordSwap(first, second); // the way back: undo exchanges them again
        }
    });
    if (swapped) {
        AppLog::instance().write(LogLevel::Info, "slots",
                                 "swap slots " + juce::String(static_cast<int>(first) + 1) + (firstFilled ? " (filled)" : " (empty)") +
                                     " and " + juce::String(static_cast<int>(second) + 1) + (secondFilled ? " (filled)" : " (empty)"));
        generationStatus_ = GenerationStatus::SlotsSwapped;
    }
    return swapped;
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
        // The answer belongs to the pattern that was sent, not to the slot number: a slot that was emptied, swapped,
        // replaced or edited while the AI worked holds something else now, and the answer would overwrite it.
        if (job.refine && (slot == nullptr || !slot->pattern || *slot->pattern != job.refine->pattern)) {
            discarded = true;
            return;
        }
        // Locked voices survive whatever happened while the job ran (SPEC 3.5): they are put back from the slot as it
        // is now. A result that no longer fits them (another length or number of voices) is dropped.
        if (slot != nullptr && slot->pattern && mm::core::hasAnyLock(*slot->pattern)) { // also locks of one dimension (D-231)
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
                mm::core::applyRhythmReference(pattern, *drumReference_, *style, job.settings.hatMode);
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
                hostBpmSeen_.store(true, std::memory_order_relaxed);
            }
            if (const auto loop = position->getLoopPoints()) {
                transport.loopStartPpq = loop->ppqStart;
                transport.loopEndPpq = loop->ppqEnd;
            }
        }
    }

    // The preview (SPEC 3.11, D-236): while the host stands still the slot plays with an internal clock; a voice plays
    // along when its hub previews. The start of the host ends it (its position takes over like after any jump).
    {
        const auto mode = static_cast<mm::engine::SlotMode>(slotMode_.load(std::memory_order_acquire));
        const bool isVoice = voiceRole_.load(std::memory_order_acquire);
        auto& channel = mm::engine::processGroupChannel();
        const bool hostRunning = transport.hasPosition && transport.isPlaying;
        const auto hubPreview = isVoice ? channel.hubPreview() : mm::engine::GroupChannel::HubPreview{};
        const bool voicePreview = isVoice && !hostRunning && hubPreview.active;
        if (hostRunning) {
            previewRequested_.store(false, std::memory_order_release);
            previewRunning_ = false;
        } else if ((!isVoice && previewRequested_.load(std::memory_order_acquire)) || voicePreview) {
            const double known = hostBpmSeen_.load(std::memory_order_relaxed) ? lastBpm_.load() : 0.0;
            const double bpm = voicePreview && hubPreview.bpm > 0.0 ? hubPreview.bpm : (known > 0.0 ? known : 125.0);
            if (!previewRunning_) {
                previewRunning_ = true;
                const auto hub = channel.hubState();
                previewPpq_ = voicePreview && hub.valid ? hub.ppq : 0.0; // a voice starts where the hub is
            }
            transport.hasPosition = true;
            transport.isPlaying = true;
            transport.isLooping = false;
            transport.ppq = previewPpq_;
            transport.bpm = bpm;
            previewPpq_ += audio.getNumSamples() * bpm / 60.0 / std::max(getSampleRate(), 1.0);
        } else {
            previewRunning_ = false;
        }
        if (mode == mm::engine::SlotMode::Hub && !isVoice) {
            channel.reportHubPreview(previewRunning_, previewRunning_ ? transport.bpm : 0.0);
        }
    }

    playhead_.publish(transport.hasPosition && transport.isPlaying, transport.ppq, transport.bpm, playheadSeconds());

    // The keyboard of the live transposition is read from the incoming buffer; nothing of it is passed on (SPEC 3.17).
    applyTransposition(midi, transport, audio.getNumSamples());
    if (((transposeConfig_.load(std::memory_order_acquire) >> kTransposeThruBit) & 1u) == 0) {
        midi.clear(); // "MIDI Thru" passes the incoming MIDI on; without it nothing of it goes out (SPEC 3.17)
    }
    // The host owns the buffer, so it cannot be reserved in prepareToPlay (O-23, D-134). Reserving the bound once
    // keeps addEvent from growing it; this allocates at most once per host buffer, in the first blocks.
    midi.ensureSize(kMidiBufferBytes);

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
    player_.setLegato(legato_.load(std::memory_order_acquire));
    player_.process(transport, audio.getNumSamples(), getSampleRate(), events_);
    writeEvents(midi);
}

void ProcessorBase::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    playhead_.publish(false, 0.0, 0.0, playheadSeconds());
    audio.clear();
    midi.clear();
    midi.ensureSize(kMidiBufferBytes);
    events_.clear();
    player_.releaseAll(events_, 0);
    player_.invalidateTransport();
    writeEvents(midi);
}

namespace {

constexpr uint32_t packTranspose(const mm::core::TransposeSettings& t) {
    return static_cast<uint32_t>(t.mode == mm::core::TransposeMode::Diatonic ? 1 : 0) |
           (static_cast<uint32_t>(t.behavior == mm::core::TransposeBehavior::Momentary ? 1 : 0) << 1) |
           (static_cast<uint32_t>(t.timing) << 2) | (static_cast<uint32_t>(t.target) << 4) |
           (static_cast<uint32_t>(std::clamp(t.channel, 0, 16)) << 6) |
           (static_cast<uint32_t>(t.referenceNote < 0 ? 0 : std::min(t.referenceNote, 127) + 1) << 11) |
           (static_cast<uint32_t>(t.exportTransposed ? 0 : 1) << 19) | (static_cast<uint32_t>(t.midiThru ? 1 : 0) << kTransposeThruBit);
}

mm::core::TransposeSettings unpackTranspose(uint32_t word) {
    mm::core::TransposeSettings t;
    t.mode = (word & 1u) != 0 ? mm::core::TransposeMode::Diatonic : mm::core::TransposeMode::Chromatic;
    t.behavior = ((word >> 1) & 1u) != 0 ? mm::core::TransposeBehavior::Momentary : mm::core::TransposeBehavior::Hold;
    t.timing = static_cast<mm::core::TransposeTiming>(std::min<uint32_t>((word >> 2) & 3u, 2u));
    t.target = static_cast<mm::core::TransposeTarget>(std::min<uint32_t>((word >> 4) & 3u, 2u));
    t.channel = static_cast<int>((word >> 6) & 31u);
    const auto reference = static_cast<int>((word >> 11) & 255u);
    t.referenceNote = reference == 0 ? -1 : reference - 1;
    t.exportTransposed = ((word >> 19) & 1u) == 0;
    t.midiThru = ((word >> kTransposeThruBit) & 1u) != 0;
    return t;
}

} // namespace

void ProcessorBase::setTransposeSettings(const mm::core::TransposeSettings& settings) {
    transposeConfig_.store(packTranspose(mm::core::sanitize(settings)), std::memory_order_release);
}

void ProcessorBase::syncTransposeSettings() {
    auto& store = globalSettings();
    if (!store.enabled() || !store.ready() || store.revision() == transposeRevision_) {
        return; // a store without file leaves what was set directly (the tests do that)
    }
    transposeRevision_ = store.revision();
    setTransposeSettings(store.get().transpose);
}

mm::core::TransposeSettings ProcessorBase::transposeSettings() const {
    return unpackTranspose(transposeConfig_.load(std::memory_order_acquire));
}

void ProcessorBase::applyTransposition(const juce::MidiBuffer& input, const mm::engine::TransportInfo& transport,
                                       int numSamples) {
    using namespace mm::core;
    const TransposeSettings config = unpackTranspose(transposeConfig_.load(std::memory_order_acquire));
    const TransposeInfo info = player_.transposeInfo();
    player_.setTransposeMode(config.mode);

    const auto mode = static_cast<mm::engine::SlotMode>(slotMode_.load(std::memory_order_acquire));
    const bool isVoice = voiceRole_.load(std::memory_order_acquire);
    const bool running = transport.hasPosition && transport.isPlaying && transport.bpm > 0.0 && getSampleRate() > 0.0;
    const double ppqPerSample = running ? transport.bpm / 60.0 / getSampleRate() : 0.0;
    const double blockEnd = transport.ppq + numSamples * ppqPerSample;
    const bool targeted = targetsVoice(config.target, info);
    const int parameter = static_cast<int>(std::lround(transposeValue_->load()));
    auto& channel = mm::engine::processGroupChannel();

    if (isVoice) {
        // A voice does not play the keyboard: it follows the group's transposition (the hub plans it, SPEC 3.16) or, set
        // to "own", its own host parameter (so that a bounce of the single track is reproducible, D-131).
        keyboard_.current = 0;
        keyboard_.pending = false;
        const auto effective = [&](int total) { return targeted ? clampTranspose(total) : 0; };
        int shown = effective(parameter);
        if (mode == mm::engine::SlotMode::FollowHub) {
            const int hub = channel.hub();
            const int member = groupSync_.member();
            if (hub != mm::engine::GroupChannel::kNone && hub != member) {
                if (hub != keyboard_.knownHub) {
                    keyboard_.knownHub = hub;
                    keyboard_.seenPlan = channel.transposePlan().sequence; // plans of before do not concern us
                    keyboard_.needSync = true;
                    keyboard_.groupPending = false;
                }
                if (player_.startsOrJumps(transport, numSamples, getSampleRate())) {
                    keyboard_.needSync = true;
                    keyboard_.groupPending = false;
                }
                const auto plan = channel.transposePlan();
                if (plan.sequence != keyboard_.seenPlan) {
                    keyboard_.seenPlan = plan.sequence;
                    keyboard_.needSync = false;
                    if (running) {
                        keyboard_.groupPending = true;
                        keyboard_.groupPendingValue = plan.offset;
                        keyboard_.groupPendingAtPpq = plan.stampPpq;
                    } else {
                        keyboard_.groupApplied = plan.offset;
                        keyboard_.groupPending = false;
                    }
                } else if (keyboard_.needSync) {
                    const auto state = channel.hubTranspose();
                    if (state.valid) {
                        keyboard_.groupApplied = state.offset;
                        keyboard_.needSync = false;
                    }
                }
            } else {
                keyboard_.knownHub = -2; // without a hub the voice keeps the transposition it has
            }
            if (keyboard_.groupPending && (!running || keyboard_.groupPendingAtPpq <= transport.ppq + 1e-9)) {
                keyboard_.groupApplied = keyboard_.groupPendingValue;
                keyboard_.groupPending = false;
            }
            player_.setTranspose(effective(keyboard_.groupApplied));
            shown = effective(keyboard_.groupApplied);
            if (keyboard_.groupPending && keyboard_.groupPendingAtPpq < blockEnd) {
                player_.setTransposeAt(effective(keyboard_.groupPendingValue), keyboard_.groupPendingAtPpq);
                keyboard_.groupApplied = keyboard_.groupPendingValue;
                keyboard_.groupPending = false;
                shown = effective(keyboard_.groupApplied);
            }
        } else {
            player_.setTranspose(shown);
        }
        effectiveTranspose_.store(shown, std::memory_order_relaxed);
        keyboardTranspose_.store(0, std::memory_order_relaxed);
        transposeTotal_.store(mode == mm::engine::SlotMode::FollowHub ? clampTranspose(keyboard_.groupApplied)
                                                                      : clampTranspose(parameter),
                              std::memory_order_relaxed);
        return;
    }

    // Hub and solo: the keyboard and the host parameter. A render that is not real-time plays the saved slots and the
    // parameter only.
    const bool keyboardAllowed = !isNonRealtime();
    const bool company = mode == mm::engine::SlotMode::Hub && channel.memberCount() > 1;

    // `value` is the keyboard offset to come, `param` the host parameter that goes with it.
    const auto schedule = [&](int value, int param, int sampleOffset) {
        if (!running) {
            keyboard_.current = value;
            keyboard_.appliedParam = param;
            keyboard_.pending = false;
            return;
        }
        if (company) {
            // With voices around, the change is planned for the whole group on a beat (or bar) line at least the lead
            // ahead, so that hub and voices transpose at the same position (SPEC 3.16). "Next note" does not exist here.
            const double grid = config.timing == TransposeTiming::NextBar ? 4.0 : 1.0;
            const double stamp = mm::engine::plannedStamp(channel, transport.bpm, blockEnd, grid)
                                     .value_or(std::ceil((blockEnd - 1e-9) / grid) * grid);
            keyboard_.pending = true;
            keyboard_.pendingValue = value;
            keyboard_.pendingParam = param;
            keyboard_.pendingAtPpq = stamp;
            channel.publishTransposePlan(stamp, clampTranspose(param + value));
            return;
        }
        if (config.timing == TransposeTiming::NextNote) {
            keyboard_.current = value;
            keyboard_.pending = false;
            return;
        }
        const double grid = config.timing == TransposeTiming::NextBeat ? 1.0 : 4.0;
        const double at = transport.ppq + sampleOffset * ppqPerSample;
        keyboard_.pending = true;
        keyboard_.pendingValue = value;
        keyboard_.pendingParam = param;
        keyboard_.pendingAtPpq = std::ceil(at / grid - 1e-9) * grid;
    };

    if (!company) {
        keyboard_.appliedParam = parameter; // alone, the host parameter acts from the next note
        keyboard_.paramSeen = parameter;
    }

    if (keyboardAllowed) {
        for (const auto metadata : input) {
            const auto message = metadata.getMessage();
            if (config.channel != 0 && message.getChannel() != config.channel) {
                continue;
            }
            const bool on = message.isNoteOn();
            const bool off = message.isNoteOff() || (message.isNoteOn() && message.getVelocity() == 0);
            if (on && message.getVelocity() > 0) {
                const int key = message.getNoteNumber();
                keyboard_.heldKey = key;
                schedule(offsetForKey(key, referenceNoteFor(config, info.root), config.mode, info), parameter,
                         metadata.samplePosition);
            } else if (off && config.behavior == TransposeBehavior::Momentary &&
                       message.getNoteNumber() == keyboard_.heldKey) {
                keyboard_.heldKey = -1;
                schedule(0, parameter, metadata.samplePosition);
            }
        }
    } else if (keyboard_.current != 0 || keyboard_.pending) {
        keyboard_.current = 0;
        keyboard_.pending = false; // an offline render: nothing of the keyboard stays
        keyboard_.heldKey = -1;
    }

    if (company && parameter != keyboard_.paramSeen) {
        // The host parameter changed (automation, a controller): in a group it is planned like a key
        keyboard_.paramSeen = parameter;
        schedule(keyboard_.pending ? keyboard_.pendingValue : keyboard_.current, parameter, 0);
    }

    // A planned change whose point lies behind the block (or the transport stands) is in effect now.
    if (keyboard_.pending && (!running || keyboard_.pendingAtPpq <= transport.ppq + 1e-9)) {
        keyboard_.current = keyboard_.pendingValue;
        keyboard_.appliedParam = company ? keyboard_.pendingParam : parameter;
        keyboard_.pending = false;
    }

    const auto effective = [&](int param, int keyboard) { return targeted ? clampTranspose(param + keyboard) : 0; };
    player_.setTranspose(effective(keyboard_.appliedParam, keyboard_.current));
    int shown = effective(keyboard_.appliedParam, keyboard_.current);
    if (keyboard_.pending && keyboard_.pendingAtPpq < blockEnd) {
        const int param = company ? keyboard_.pendingParam : parameter;
        player_.setTransposeAt(effective(param, keyboard_.pendingValue), keyboard_.pendingAtPpq);
        keyboard_.current = keyboard_.pendingValue;
        keyboard_.appliedParam = param;
        keyboard_.pending = false;
        shown = effective(param, keyboard_.current);
    }
    if (mode == mm::engine::SlotMode::Hub) {
        // What is in force for the group (before the target), for a voice that starts, jumps or joins
        channel.reportHubTranspose(clampTranspose(keyboard_.appliedParam + keyboard_.current));
    }
    effectiveTranspose_.store(shown, std::memory_order_relaxed);
    keyboardTranspose_.store(keyboard_.current, std::memory_order_relaxed);
    transposeTotal_.store(clampTranspose(keyboard_.appliedParam + keyboard_.current), std::memory_order_relaxed);
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
    // The audio thread names the playing slot (a switch waits for its bar line), but only while the transport runs: a
    // stopped host may not call processBlock at all, and the view must show the slot that was just chosen or edited.
    const bool running = playheadPpq().has_value();
    const int playing = running && handover_.activeVersion() != 0 ? handover_.activeSlot() : activeSlot();
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
    auto rendered = mm::core::renderVoiceForPlayback(pattern, *style, voiceIndex, view.octave, &embeddedGrooves());
    view.hasPattern = true;
    view.notes = std::move(rendered.notes);
    view.lengthTicks = rendered.lengthTicks;
    view.seed = rendered.seed;
    view.voiceName = voiceDisplayName(pattern, voiceIndex);
    view.melody = view.voiceName == "Melody";
    view.locked = voiceIndex < pattern.voices.size() && mm::core::isVoiceLocked(pattern.voices[voiceIndex]);
    view.phraseStarts.clear();
    for (size_t i = 1; i < pattern.phrases.size(); ++i) {
        view.phraseStarts.push_back(pattern.phrases[i].startBar * mm::core::kTicksPerBar);
    }
    view.kickTicks = mm::core::kickTicks(pattern);
    view.lockFlags = voiceIndex < pattern.voices.size() ? pattern.voices[voiceIndex].lock : mm::core::LockFlags{};
    view.root = pattern.context.root;
    view.scaleId = pattern.context.scaleId;
    view.transposeInfo = rendered.transpose;
    view.drumReference = pattern.rhythmRef.has_value();
    if (voiceIndex < pattern.voices.size()) {
        const auto& track = pattern.voices[voiceIndex];
        const auto groove = mm::core::effectiveGroove(pattern, track, &embeddedGrooves());
        view.grooveId = track.groove.templateId.empty() ? mm::core::kGrooveStraight : track.groove.templateId;
        view.swing = groove.swing;
        view.swingActive = groove.controlActive;
    }
    view.source.clear();
    if (voiceIndex < pattern.voices.size()) {
        for (const auto& note : pattern.voices[voiceIndex].notes) {
            view.source.push_back(
                {note.id, note.pitch, note.startTick, note.lengthTicks, note.velocity, note.accent, note.slide, note.ratchet,
                 note.lock, note.chance, note.condA, note.condB});
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
    AppLog::instance().write(LogLevel::Info, "group", "open hub asked");
}

bool ProcessorBase::bringEditorToFront() {
    frontRequests_.fetch_add(1);
    auto* editor = getActiveEditor();
    auto* top = editor != nullptr ? editor->getTopLevelComponent() : nullptr;
    // A host window that is closed cannot be opened from here: the log tells which case a report is about.
    AppLog::instance().write(LogLevel::Info, "group",
                             juce::String("hub asked to come to the front: ") +
                                 (editor == nullptr ? "its window is not open" : top == nullptr ? "no window" : "raised"));
    if (top == nullptr) {
        return false;
    }
    top->toFront(true);
    return true;
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
    const auto transposeConfig = transposeSettings();
    const int total = transposeConfig.exportTransposed ? transposeTotal() : 0;
    ExportKey key;
    key.transpose = total;
    key.mode = static_cast<int>(transposeConfig.mode) + 2 * static_cast<int>(transposeConfig.target);
    key.bpmCentis = std::llround(bpm * 100.0);
    key.origin = own.origin;
    key.revision = own.revision;
    key.slot = own.slot - 1;
    key.voice = own.voice;
    key.octave = own.octave;
    key.all = all;
    const bool allNotes = globalSettings().ready() && globalSettings().get().exportAllNotes;
    key.allNotes = allNotes;
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
        // A file holds the notes as they sound now, with the live transposition of the group (abschaltbar, SPEC 3.16)
        if (total != 0 && mm::core::targetsVoice(transposeConfig.target, view.transposeInfo)) {
            const int shift = mm::core::voiceOctaveShift(total, transposeConfig.mode, view.transposeInfo);
            for (auto& note : view.notes) {
                note.pitch = static_cast<uint8_t>(
                    mm::core::transposedPitch(note.pitch, total, transposeConfig.mode, view.transposeInfo, shift));
            }
        }
        // Chances and conditions are resolved: the file holds the passes that the group plays one after another (D-233)
        uint32_t lengthTicks = view.lengthTicks;
        request.notes = allNotes ? view.notes : mm::core::notesForExport(view.notes, lengthTicks, view.seed);
        request.lengthTicks = lengthTicks;
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
        if (settings_.generation.chromaticPct) { // only when chosen: states of the style's share stay as they were
            root.setAttribute("genChromatic", *settings_.generation.chromaticPct);
        }
        if (settings_.generation.hatMode != mm::core::HatMode::Free) { // only when chosen: older states stay as they were
            root.setAttribute("genHatMode", juce::String(std::string(mm::core::toString(settings_.generation.hatMode))));
        }
        root.setAttribute("genRoot", settings_.generation.root
                                         ? juce::String(static_cast<int>(*settings_.generation.root))
                                         : juce::String("auto"));
        root.setAttribute("genScale", settings_.generation.scaleId ? juce::String(*settings_.generation.scaleId)
                                                                   : juce::String("auto"));
        root.setAttribute("genPrompt", juce::String::fromUTF8(settings_.generation.prompt.c_str()));
        root.setAttribute("genSeed", settings_.generation.seed
                                         ? juce::String(std::to_string(*settings_.generation.seed))
                                         : juce::String("random"));
        if (uiScalePct() != mm::core::kDefaultUiScale) { // only when it differs: states of the standard size stay as they were
            root.setAttribute("uiScale", uiScalePct());
        }
        if (settings_.legato) { // only when on: states of the restart stay as they were
            root.setAttribute("switchLegato", 1);
        }
        if (settings_.evolve != mm::core::EvolveSettings{}) { // only when not the default (SPEC 6.7)
            root.setAttribute("evolveEvery", settings_.evolve.everyPasses);
            root.setAttribute("evolveFromOriginal", settings_.evolve.fromOriginal ? 1 : 0);
            root.setAttribute("evolveStrength", settings_.evolve.strengthPct);
        }
        if (followPlayhead()) { // only when on: states of the standard stay as they were
            root.setAttribute("followPlayhead", 1);
        }
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
    const auto migrated = migrateState(*root, kStateVersion, stateMigrations());
    if (!migrated.ok) {
        // an older state that cannot be brought up to date is not used; the instance keeps what it has
        const juce::ScopedLock lock(slotsLock_);
        slotLoadProblems_.push_back(migrated.error);
        return;
    }
    startKeyPending_ = false; // the project has its own key
    startKeyTimer_.reset();
    if (const auto* parameters = root->getChildByName(parameters_.state.getType())) {
        loadingState_.store(true);
        parameters_.replaceState(juce::ValueTree::fromXml(*parameters));
        loadingState_.store(false);
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
    settings.legato = root->getIntAttribute("switchLegato", 0) != 0;
    settings.evolve = mm::core::clampEvolveSettings(
        {root->getIntAttribute("evolveEvery", mm::core::kDefaultEvolvePasses),
         root->getIntAttribute("evolveFromOriginal", 0) != 0,
         root->getIntAttribute("evolveStrength", mm::core::kDefaultEvolveStrength)});
    settings.generation.energyPct = root->getIntAttribute("genEnergy", defaults.energyPct);
    settings.generation.creativityPct = root->getIntAttribute("genCreativity", defaults.creativityPct);
    if (root->hasAttribute("genChromatic")) {
        settings.generation.chromaticPct = root->getIntAttribute("genChromatic", 0);
    }
    settings.generation.hatMode = mm::core::parseHatMode(root->getStringAttribute("genHatMode").toStdString());
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
    setUiScalePct(root->getIntAttribute("uiScale", mm::core::kDefaultUiScale));
    setFollowPlayhead(root->getIntAttribute("followPlayhead", 0) != 0);

    mm::core::SlotBank bank;
    std::vector<std::string> problems;
    if (migrated.fromNewer) {
        problems.push_back("stateVersion: written by a newer plugin version, loaded as far as understood");
    }
    if (const auto* slotsElement = root->getChildByName("Slots")) {
        auto loaded = mm::core::loadSlotBank(slotsElement->getAllSubText().toStdString());
        if (loaded.ok()) {
            bank = std::move(*loaded.bank);
        }
        problems.insert(problems.end(), loaded.problems.begin(), loaded.problems.end());
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
