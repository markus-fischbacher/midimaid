#include "plugin/ReferenceStore.h"

#include "plugin/EmbeddedStyles.h"
#include "plugin/GlobalSettingsStore.h"

#include <algorithm>

namespace mm::plugin {

namespace {

std::atomic<ReferenceStore*> gOverride{nullptr};

bool isMidiName(const juce::File& file) {
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".mid" || extension == ".midi";
}

/// The MIDI files behind the paths: a file as it is, a folder with everything below it (sorted by path, so that the
/// order does not depend on the file system).
std::vector<juce::File> expand(const std::vector<juce::File>& paths, size_t limit, const std::atomic<bool>& cancel) {
    std::vector<juce::File> files;
    for (const auto& path : paths) {
        if (cancel.load() || files.size() >= limit) {
            break;
        }
        if (path.isDirectory()) {
            auto found = path.findChildFiles(juce::File::findFiles, true, "*");
            std::sort(found.begin(), found.end(),
                      [](const juce::File& a, const juce::File& b) { return a.getFullPathName() < b.getFullPathName(); });
            for (const auto& file : found) {
                if (isMidiName(file) && !file.isHidden() && files.size() < limit) {
                    files.push_back(file);
                }
            }
        } else if (path.existsAsFile()) {
            files.push_back(path);
        }
    }
    return files;
}

} // namespace

juce::File ReferenceStore::defaultFolder() {
    const auto settings = GlobalSettingsStore::defaultFile();
    return settings == juce::File() ? juce::File() : settings.getParentDirectory().getChildFile("references");
}

bool ReferenceStore::validStyleId(const std::string& styleId) {
    return !styleId.empty() && styleId.size() <= 64 &&
           std::all_of(styleId.begin(), styleId.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
           });
}

juce::File ReferenceStore::fileFor(const std::string& styleId) const {
    return folder_.getChildFile(juce::String(styleId) + ".json");
}

ReferenceStore::ReferenceStore(juce::File folder, const mm::core::StyleLibrary& styles)
    : folder_(std::move(folder)), styles_(styles) {
    if (!enabled()) {
        ready_ = true;
        return;
    }
    pool_.addJob([this] { readAll(); });
}

ReferenceStore::~ReferenceStore() {
    *alive_ = false;
    cancel_ = true;
    pool_.removeAllJobs(true, 10000);
}

void ReferenceStore::readAll() {
    std::map<std::string, mm::core::ReferenceSet> loaded;
    for (const auto& file : folder_.findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto styleId = file.getFileNameWithoutExtension().toStdString();
        if (validStyleId(styleId)) {
            loaded[styleId] = mm::core::referenceSetFromJson(file.loadFileAsString().toStdString(), styleId);
        }
    }
    {
        const juce::ScopedLock lock(lock_);
        for (auto& [styleId, set] : loaded) {
            if (sets_.find(styleId) == sets_.end()) { // a change made before the files were read wins
                sets_[styleId] = std::move(set);
            }
        }
        ready_ = true;
    }
    changed();
}

void ReferenceStore::write(const std::string& styleId, const mm::core::ReferenceSet& set) {
    if (!enabled() || !validStyleId(styleId)) {
        return;
    }
    folder_.createDirectory();
    const auto target = fileFor(styleId);
    juce::TemporaryFile temporary(target); // written next to the file and moved over it: never a half file
    if (temporary.getFile().replaceWithText(juce::String::fromUTF8(mm::core::referenceSetToJson(set).c_str()))) {
        temporary.overwriteTargetFileWithTemporary();
    }
}

void ReferenceStore::changed() {
    const auto alive = alive_;
    juce::MessageManager::callAsync([this, alive] {
        if (*alive && onChanged_) {
            onChanged_();
        }
    });
}

void ReferenceStore::setOnChanged(std::function<void()> callback) {
    onChanged_ = std::move(callback);
}

mm::core::ReferenceSet ReferenceStore::get(const std::string& styleId) const {
    const juce::ScopedLock lock(lock_);
    const auto it = sets_.find(styleId);
    if (it != sets_.end()) {
        return it->second;
    }
    mm::core::ReferenceSet empty;
    empty.styleId = styleId;
    return empty;
}

ReferenceStore::Progress ReferenceStore::progress() const {
    const juce::ScopedLock lock(lock_);
    return progress_;
}

bool ReferenceStore::edit(const std::string& styleId, const std::function<bool(mm::core::ReferenceSet&)>& change) {
    if (!validStyleId(styleId)) {
        return false;
    }
    mm::core::ReferenceSet copy;
    {
        const juce::ScopedLock lock(lock_);
        const auto it = sets_.find(styleId);
        if (it == sets_.end() || !change(it->second)) {
            return false;
        }
        copy = it->second;
    }
    pool_.addJob([this, styleId, copy] { write(styleId, copy); });
    changed();
    return true;
}

bool ReferenceStore::setActive(const std::string& styleId, const std::string& id, bool active) {
    return edit(styleId, [&](mm::core::ReferenceSet& set) { return mm::core::setItemActive(set, id, active); });
}

bool ReferenceStore::setWeight(const std::string& styleId, const std::string& id, int weight) {
    return edit(styleId, [&](mm::core::ReferenceSet& set) { return mm::core::setItemWeight(set, id, weight); });
}

bool ReferenceStore::setRole(const std::string& styleId, const std::string& id, mm::core::TrackRole role) {
    return edit(styleId, [&](mm::core::ReferenceSet& set) { return mm::core::setItemRole(set, id, role); });
}

bool ReferenceStore::remove(const std::string& styleId, const std::string& id) {
    return edit(styleId, [&](mm::core::ReferenceSet& set) { return mm::core::removeItem(set, id); });
}

void ReferenceStore::importFiles(const std::string& styleId, std::vector<juce::File> paths) {
    if (!validStyleId(styleId) || paths.empty()) {
        return;
    }
    cancel_ = false;
    {
        const juce::ScopedLock lock(lock_);
        progress_ = {};
        progress_.running = true;
    }
    pool_.addJob([this, styleId, paths = std::move(paths)]() mutable { runImport(styleId, std::move(paths)); });
    changed();
}

void ReferenceStore::cancelImport() {
    cancel_ = true;
}

void ReferenceStore::runImport(const std::string& styleId, std::vector<juce::File> paths) {
    const auto* style = styles_.findOrFallback(styleId);
    const auto files = expand(paths, kMaxFiles, cancel_);
    {
        const juce::ScopedLock lock(lock_);
        progress_.total = files.size();
    }
    changed();
    for (const auto& file : files) {
        if (cancel_.load()) {
            break;
        }
        mm::core::ReferenceImportReport report;
        juce::MemoryBlock block;
        if (style == nullptr || file.getSize() > static_cast<juce::int64>(mm::core::kMaxMidiFileBytes) ||
            !file.loadFileAsData(block)) {
            ++report.unreadable;
        } else {
            const std::vector<uint8_t> bytes(static_cast<const uint8_t*>(block.getData()),
                                             static_cast<const uint8_t*>(block.getData()) + block.getSize());
            const auto hash = mm::core::fileHash(bytes.data(), bytes.size());
            bool known;
            {
                const juce::ScopedLock lock(lock_);
                known = mm::core::containsFile(sets_[styleId], hash);
            }
            if (known) {
                ++report.duplicates; // the cache: the analysis of this file is in the set
            } else {
                auto items = mm::core::itemsFromMidiFile(file.getFileName().toStdString(), bytes, *style, report);
                const juce::ScopedLock lock(lock_);
                auto& set = sets_[styleId];
                set.styleId = styleId;
                mm::core::addItems(set, std::move(items), report);
            }
        }
        {
            const juce::ScopedLock lock(lock_);
            progress_.report += report;
            ++progress_.done;
        }
        changed();
    }
    mm::core::ReferenceSet copy;
    {
        const juce::ScopedLock lock(lock_);
        copy = sets_[styleId];
        progress_.running = false;
    }
    write(styleId, copy);
    changed();
}

void ReferenceStore::waitForIdle() {
    while (pool_.getNumJobs() > 0) {
        juce::Thread::sleep(5);
    }
}

ReferenceStore& referenceStore() {
    if (auto* override = gOverride.load()) {
        return *override;
    }
    static ReferenceStore store(ReferenceStore::defaultFolder(), embeddedStyles());
    return store;
}

void overrideReferenceStore(ReferenceStore* store) {
    gOverride = store;
}

} // namespace mm::plugin
