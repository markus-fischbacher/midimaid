#pragma once

#include "core/GrooveLibrary.h"

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <string>
#include <vector>

namespace mm::plugin {

/// The groove templates the user saved from a voice (SPEC 3.9, D-245): one JSON file per template in
/// `<folder>/<id>.json`, shared by every instance of the process. The library an instance works with is the embedded one
/// followed by these. It is an immutable snapshot (`library()`); an `add` replaces it at once and writes the file on a
/// background thread (CLAUDE.md). A store without folder keeps its templates in memory only.
class UserGrooveStore {
public:
    UserGrooveStore(juce::File folder, const mm::core::GrooveLibrary& base);
    ~UserGrooveStore();

    bool enabled() const { return folder_ != juce::File(); }
    /// True once the files of the folder were read (at once for a store without folder).
    bool ready() const { return ready_.load(); }
    /// Counts every change of the library.
    uint64_t revision() const { return revision_.load(); }

    /// The templates now (embedded and own). The pointer stays valid as long as the caller holds it. Message thread.
    std::shared_ptr<const mm::core::GrooveLibrary> library() const;

    /// Adds `groove` under the id the store chooses from `nameDe` (the id in `groove` is replaced). Returns that id, or
    /// "" when the name is empty or too long or the template is invalid. Message thread.
    std::string add(mm::core::GrooveTemplate groove);

    static constexpr size_t kMaxNameLength = 40;
    static constexpr size_t kMaxTemplates = 100;

    /// Called on the message thread after every change of the library. Set it once, before the first use.
    void setOnChanged(std::function<void()> callback);
    /// Waits until the background work is done (for tests and the shutdown).
    void waitForIdle();

    /// `<folder of the settings file>/grooves`, or empty when the settings are disabled (MIDIMAID_SETTINGS_FILE).
    static juce::File defaultFolder();

private:
    void readAll();
    void rebuildLocked();
    void changed();

    juce::File folder_;
    const mm::core::GrooveLibrary& base_;
    mutable juce::CriticalSection lock_;
    std::vector<std::string> texts_; ///< the own templates as JSON, in the order they were made
    std::shared_ptr<const mm::core::GrooveLibrary> library_;
    std::function<void()> onChanged_;
    std::atomic<bool> ready_{false};
    std::atomic<uint64_t> revision_{0};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool pool_{1};
};

/// The store of this process, created at the first call.
UserGrooveStore& userGrooves();
/// For tests: makes `userGrooves()` return `store` (null: the process store again). The caller keeps ownership.
void overrideUserGrooves(UserGrooveStore* store);

} // namespace mm::plugin
