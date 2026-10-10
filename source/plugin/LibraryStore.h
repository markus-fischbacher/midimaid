#pragma once

#include "core/Library.h"
#include "core/LibraryIndex.h"

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mm::plugin {

/// The library of the user (SPEC 3.12, 9.2, D-184): one JSON file per entry in `<settings folder>/library`, shared by
/// every instance of the process. All file access runs on one background thread (CLAUDE.md); the list lives in memory
/// and is read and changed on the message thread. A store without folder keeps its entries in memory only (the tests
/// of the other parts run that way). The list is built at the start from the index file (`index.json`) and the folder: a
/// file that has the size and time the index remembers is not read, any other file is (D-185).
class LibraryStore {
public:
    /// `maxEntries` is the limit of the list (the tests use a small one).
    explicit LibraryStore(juce::File folder, size_t maxEntries = mm::core::kMaxLibraryEntries);
    ~LibraryStore();

    bool enabled() const { return folder_ != juce::File(); }
    /// True once the folder was read (at once for a store without folder).
    bool ready() const { return ready_.load(); }

    /// The entries, newest first (then by id). Message thread.
    std::vector<mm::core::LibraryEntryInfo> list() const;
    /// The info of one entry, or nothing.
    std::optional<mm::core::LibraryEntryInfo> info(const std::string& id) const;

    enum class SaveError { None, Full, NothingToSave };
    struct SaveResult {
        std::string id; ///< empty on an error
        SaveError error = SaveError::None;
    };
    /// Adds an entry (the list shows it at once) and writes it in the background. `name` may be empty (the style is
    /// used). Message thread.
    SaveResult savePattern(const std::string& name, const mm::core::Pattern& pattern, const std::string& slotName,
                           uint8_t slotColor);
    SaveResult saveSet(const std::string& name, const mm::core::SlotBank& bank);

    /// Reads an entry in the background and calls `done` on the message thread: with the entry, or with a reason. The
    /// store must outlive the call or be destroyed (then nothing is delivered).
    void load(const std::string& id, std::function<void(mm::core::LibraryLoadResult)> done);

    enum class ExportResult { Done, NoSuchEntry, NotAPattern, NoNotes, WriteFailed };
    /// Reads a pattern entry in the background, renders it as the engine plays it (`style`, `bpm`) and writes the .mid
    /// file to `target`; `done` runs on the message thread. A set cannot be exported as one file.
    void exportMidi(const std::string& id, mm::core::StyleProfile style, double bpm, juce::File target,
                    std::function<void(ExportResult)> done);

    /// Changes of the list (message thread, written in the background). False for an unknown id.
    bool remove(const std::string& id);
    bool rename(const std::string& id, const std::string& name);

    /// Called on the message thread after every change of the list. Set it once, before the first use.
    void setOnChanged(std::function<void()> callback);
    /// Waits until the background work that was asked for is done (for tests and the shutdown).
    void waitForIdle();

    /// Files of the folder that were no usable entry when it was read (for the log and the tests).
    size_t unreadableCount() const { return unreadable_.load(); }
    /// How the folder was read at the start: entries taken from the index without reading their files, and files that
    /// were read (new, changed, or all of them without a usable index).
    size_t indexedCount() const { return indexed_.load(); }
    size_t parsedCount() const { return parsed_.load(); }
    static constexpr const char* kIndexFileName = "index.json";

    /// `<folder of the settings file>/library`, or empty when the settings are disabled (MIDIMAID_SETTINGS_FILE).
    static juce::File defaultFolder();

private:
    juce::File fileFor(const std::string& id) const;
    std::string newId(int64_t createdUnixMs);
    SaveResult add(mm::core::LibraryEntry entry);
    void readAll();
    void scheduleIndex();
    void writeIndex();
    void changed();

    juce::File folder_;
    size_t maxEntries_;
    mutable juce::CriticalSection lock_;
    std::map<std::string, mm::core::LibraryEntryInfo> infos_;
    std::map<std::string, mm::core::LibraryEntry> memory_; ///< only without folder
    std::function<void()> onChanged_;
    std::atomic<bool> ready_{false};
    std::atomic<size_t> unreadable_{0};
    std::atomic<size_t> indexed_{0};
    std::atomic<size_t> parsed_{0};
    std::atomic<uint64_t> indexRequests_{0}; ///< index writes asked for so far
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    juce::ThreadPool pool_{1};
};

/// The store of this process, created at the first call.
LibraryStore& libraryStore();
/// For tests: makes `libraryStore()` return `store` (null: the process store again). The caller keeps ownership.
void overrideLibraryStore(LibraryStore* store);

} // namespace mm::plugin
