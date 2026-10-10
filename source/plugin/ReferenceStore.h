#pragma once

#include "core/ReferenceSet.h"
#include "core/StyleLibrary.h"

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mm::plugin {

/// The reference sets of the user, one per style (SPEC 3.20, D-181): `<folder>/<style>.json`, shared by every instance
/// of the process. All file access and all analysis run on one background thread (CLAUDE.md); the sets live in memory
/// and are read and changed on the message thread. A store without folder keeps its sets in memory only (the tests of
/// the other parts run that way). Importing the same file twice finds it by its hash and does not analyse it again.
class ReferenceStore {
public:
    /// What the background work is doing, for the status line of the view (message thread).
    struct Progress {
        bool running = false;
        size_t done = 0;  ///< files looked at
        size_t total = 0; ///< files found
        mm::core::ReferenceImportReport report;
        bool operator==(const Progress&) const = default;
    };

    ReferenceStore(juce::File folder, const mm::core::StyleLibrary& styles);
    ~ReferenceStore();

    bool enabled() const { return folder_ != juce::File(); }
    /// True once the files of the folder were read (at once for a store without folder).
    bool ready() const { return ready_.load(); }

    /// The set of the style (empty for a style without references). Message thread.
    mm::core::ReferenceSet get(const std::string& styleId) const;

    /// Imports MIDI files and the MIDI files inside the folders (also deeper ones) into the set of the style, in the
    /// background and in the order given. A new request while one runs waits for it. At most `kMaxFiles` files.
    void importFiles(const std::string& styleId, std::vector<juce::File> paths);
    static constexpr size_t kMaxFiles = 2000;
    /// Stops the import that runs (what was analysed so far stays) and the ones that wait.
    void cancelImport();

    /// Changes of one item (message thread, saved in the background). False for an unknown style or id.
    bool setActive(const std::string& styleId, const std::string& id, bool active);
    bool setWeight(const std::string& styleId, const std::string& id, int weight);
    bool setRole(const std::string& styleId, const std::string& id, mm::core::TrackRole role);
    bool remove(const std::string& styleId, const std::string& id);

    Progress progress() const;
    /// Called on the message thread after every change of a set or of the progress. Set it once, before the first use.
    void setOnChanged(std::function<void()> callback);
    /// Waits until the background work that was asked for is done (for tests and the shutdown).
    void waitForIdle();

    /// `<folder of the settings file>/references`, or empty when the settings are disabled (MIDIMAID_SETTINGS_FILE).
    static juce::File defaultFolder();

private:
    juce::File fileFor(const std::string& styleId) const;
    static bool validStyleId(const std::string& styleId);
    void readAll();
    void write(const std::string& styleId, const mm::core::ReferenceSet& set);
    void changed();
    bool edit(const std::string& styleId, const std::function<bool(mm::core::ReferenceSet&)>& change);
    void runImport(const std::string& styleId, std::vector<juce::File> paths);

    juce::File folder_;
    const mm::core::StyleLibrary& styles_;
    mutable juce::CriticalSection lock_;
    std::map<std::string, mm::core::ReferenceSet> sets_;
    Progress progress_;
    std::function<void()> onChanged_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> cancel_{false};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool pool_{1};
};

/// The store of this process, created at the first call.
ReferenceStore& referenceStore();
/// For tests: makes `referenceStore()` return `store` (null: the process store again). The caller keeps ownership.
void overrideReferenceStore(ReferenceStore* store);

} // namespace mm::plugin
