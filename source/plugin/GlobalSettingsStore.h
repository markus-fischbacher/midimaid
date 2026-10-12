#pragma once

#include "core/GlobalSettings.h"

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <optional>

namespace mm::plugin {

/// The user's settings file shared by every instance of the process (SPEC 8.1, 8.4, D-162). The file is read and
/// written in a background thread only (CLAUDE.md); the values live in memory and are read and changed on the message
/// thread. A store without file (empty `file`) is disabled: it reads and writes nothing, `get` gives the defaults and
/// `update` is ignored (the tests of the other parts run that way).
class GlobalSettingsStore {
public:
    /// Starts reading the file at once, or later with `beginRead` (`readNow` false: for tests of a late read).
    explicit GlobalSettingsStore(juce::File file, bool readNow = true);
    ~GlobalSettingsStore();

    /// Starts the background read when the store was created with `readNow` false. Once only.
    void beginRead();
    bool enabled() const { return file_ != juce::File(); }
    /// True once the file was read (at once for a disabled store). A file that does not exist or is bad counts as read.
    bool ready() const { return ready_.load(); }
    mm::core::GlobalSettings get() const;
    /// Counts every change of the values (a read of the file, an `update` that changed something): who keeps a copy
    /// asks whether it is still current without copying the settings.
    uint64_t revision() const { return revision_.load(); }
    /// Changes the settings (sanitized afterwards) and writes the file in the background when something changed. A
    /// change made before the file was read wins over the file, field by field; the other fields of the file stay as
    /// they are (nothing is written before the file was read).
    void update(const std::function<void(mm::core::GlobalSettings&)>& change);
    /// Waits until the writes that were asked for are done (for tests and for the shutdown).
    void waitForWrites();
    const juce::File& file() const { return file_; }

    /// `<user data folder>/Klirrwerk/MidiMaid/settings.json` (macOS: Application Support), unless the environment
    /// variable MIDIMAID_SETTINGS_FILE names another file or says "none" (disabled).
    static juce::File defaultFile();

private:
    void read();
    void write();

    juce::File file_;
    bool readStarted_ = false; // message thread
    mutable juce::CriticalSection lock_;
    mm::core::GlobalSettings settings_;
    /// The fields changed before the file was read (under `lock_`); they are applied to what the file says.
    struct Pending {
        std::optional<bool> expert;
        std::optional<mm::core::PitchClass> startRoot;
        std::optional<std::string> startScaleId;
        std::optional<int> variationStrength;
        std::optional<mm::core::EditGrid> grid;
        std::optional<bool> snapChromatic;
        std::optional<int> middleC;
        /// The transposition settings are several fields: what was changed is applied field by field, so a change of one
        /// of them before the read leaves the others of the file alone.
        struct TransposeChange {
            mm::core::TransposeSettings before;
            mm::core::TransposeSettings now;
        };
        std::optional<TransposeChange> transpose;
        std::optional<mm::core::AiSettings> ai;
        bool any() const;
        void apply(mm::core::GlobalSettings& settings) const;
    };
    Pending pending_;
    std::atomic<bool> ready_{false};
    std::atomic<uint64_t> revision_{0};
    juce::ThreadPool pool_{1};
};

/// The store of this process, created at the first call (it starts reading the file at once).
GlobalSettingsStore& globalSettings();
/// For tests: makes `globalSettings()` return `store` (null: the process store again). The caller keeps ownership.
void overrideGlobalSettings(GlobalSettingsStore* store);

} // namespace mm::plugin
