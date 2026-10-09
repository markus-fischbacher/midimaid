#include "plugin/GlobalSettingsStore.h"

namespace mm::plugin {

namespace {

std::atomic<GlobalSettingsStore*> gOverride{nullptr};

} // namespace

juce::File GlobalSettingsStore::defaultFile() {
    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable("MIDIMAID_SETTINGS_FILE", {});
    if (fromEnvironment == "none") {
        return {};
    }
    if (fromEnvironment.isNotEmpty()) {
        return juce::File(fromEnvironment);
    }
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0) {
        folder = folder.getChildFile("Application Support"); // JUCE gives ~/Library there
    }
    return folder.getChildFile("Klirrwerk").getChildFile("MidiMaid").getChildFile("settings.json");
}

GlobalSettingsStore::GlobalSettingsStore(juce::File file, bool readNow) : file_(std::move(file)) {
    if (!enabled()) {
        ready_ = true;
        return;
    }
    if (readNow) {
        beginRead();
    }
}

void GlobalSettingsStore::beginRead() {
    if (!enabled() || readStarted_) {
        return;
    }
    readStarted_ = true;
    pool_.addJob([this] { read(); });
}

GlobalSettingsStore::~GlobalSettingsStore() {
    pool_.removeAllJobs(false, 5000);
}

bool GlobalSettingsStore::Pending::any() const {
    return expert || startRoot || startScaleId || variationStrength || grid || snapChromatic;
}

void GlobalSettingsStore::Pending::apply(mm::core::GlobalSettings& settings) const {
    if (expert) {
        settings.expert = *expert;
    }
    if (startRoot) {
        settings.startRoot = *startRoot;
    }
    if (startScaleId) {
        settings.startScaleId = *startScaleId;
    }
    if (variationStrength) {
        settings.variationStrength = *variationStrength;
    }
    if (grid) {
        settings.grid = *grid;
    }
    if (snapChromatic) {
        settings.snapChromatic = *snapChromatic;
    }
}

void GlobalSettingsStore::read() {
    mm::core::GlobalSettings loaded;
    if (file_.existsAsFile()) {
        loaded = mm::core::parseGlobalSettings(file_.loadFileAsString().toStdString());
    }
    bool changedMeanwhile = false;
    {
        const juce::ScopedLock lock(lock_);
        changedMeanwhile = pending_.any();
        pending_.apply(loaded);
        settings_ = mm::core::sanitize(loaded);
        pending_ = {};
        ready_ = true;
    }
    if (changedMeanwhile) {
        write(); // this is the pool's thread
    }
}

mm::core::GlobalSettings GlobalSettingsStore::get() const {
    const juce::ScopedLock lock(lock_);
    return settings_;
}

void GlobalSettingsStore::update(const std::function<void(mm::core::GlobalSettings&)>& change) {
    if (!enabled()) {
        return;
    }
    {
        const juce::ScopedLock lock(lock_);
        const auto before = settings_;
        auto next = settings_;
        change(next);
        next = mm::core::sanitize(next);
        if (next == before) {
            return;
        }
        settings_ = next;
        if (!ready_) {
            // Not read yet: remember what changed; the file is written once it was read.
            const auto note = [](auto& slot, const auto& was, const auto& now) {
                if (!(was == now)) {
                    slot = now;
                }
            };
            note(pending_.expert, before.expert, next.expert);
            note(pending_.startRoot, before.startRoot, next.startRoot);
            note(pending_.startScaleId, before.startScaleId, next.startScaleId);
            note(pending_.variationStrength, before.variationStrength, next.variationStrength);
            note(pending_.grid, before.grid, next.grid);
            note(pending_.snapChromatic, before.snapChromatic, next.snapChromatic);
            return;
        }
    }
    pool_.addJob([this] { write(); });
}

void GlobalSettingsStore::write() {
    mm::core::GlobalSettings snapshot;
    {
        const juce::ScopedLock lock(lock_);
        snapshot = settings_;
    }
    const auto text = mm::core::toJson(snapshot);
    if (!file_.getParentDirectory().createDirectory()) {
        return;
    }
    // A temporary file and a rename, so that another instance never reads half a file.
    const auto temporary = file_.getSiblingFile(file_.getFileName() + ".tmp");
    if (temporary.replaceWithText(text, false, false, "\n")) {
        temporary.moveFileTo(file_);
    }
}

void GlobalSettingsStore::waitForWrites() {
    while (pool_.getNumJobs() > 0) {
        juce::Thread::sleep(2);
    }
}

GlobalSettingsStore& globalSettings() {
    if (auto* replaced = gOverride.load()) {
        return *replaced;
    }
    static GlobalSettingsStore store(GlobalSettingsStore::defaultFile());
    return store;
}

void overrideGlobalSettings(GlobalSettingsStore* store) {
    gOverride = store;
}

} // namespace mm::plugin
