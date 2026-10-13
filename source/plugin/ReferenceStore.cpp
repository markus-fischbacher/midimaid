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
    std::map<std::string, mm::core::ReferenceCollection> loaded;
    for (const auto& file : folder_.findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto styleId = file.getFileNameWithoutExtension().toStdString();
        if (validStyleId(styleId)) {
            loaded[styleId] = mm::core::referenceCollectionFromJson(file.loadFileAsString().toStdString(), styleId);
        }
    }
    {
        const juce::ScopedLock lock(lock_);
        for (auto& [styleId, collection] : loaded) {
            if (collections_.find(styleId) == collections_.end()) { // a change made before the files were read wins
                collections_[styleId] = std::move(collection);
            }
        }
        ready_ = true;
    }
    changed();
}

void ReferenceStore::write(const std::string& styleId, const mm::core::ReferenceCollection& collection) {
    if (!enabled() || !validStyleId(styleId)) {
        return;
    }
    folder_.createDirectory();
    const auto target = fileFor(styleId);
    juce::TemporaryFile temporary(target); // written next to the file and moved over it: never a half file
    if (temporary.getFile().replaceWithText(juce::String::fromUTF8(mm::core::referenceCollectionToJson(collection).c_str()))) {
        temporary.overwriteTargetFileWithTemporary();
    }
}

void ReferenceStore::changed() {
    ++revision_;
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

mm::core::ReferenceCollection ReferenceStore::collection(const std::string& styleId) const {
    const juce::ScopedLock lock(lock_);
    const auto it = collections_.find(styleId);
    if (it != collections_.end()) {
        return it->second;
    }
    mm::core::ReferenceCollection empty;
    empty.styleId = styleId;
    mm::core::normalize(empty);
    return empty;
}

mm::core::ReferenceSet ReferenceStore::get(const std::string& styleId) const {
    return mm::core::activeView(collection(styleId));
}

mm::core::ReferenceSet ReferenceStore::view(const std::string& styleId, const std::string& setId) const {
    return mm::core::viewOf(collection(styleId), setId);
}

ReferenceStore::Progress ReferenceStore::progress() const {
    const juce::ScopedLock lock(lock_);
    return progress_;
}

bool ReferenceStore::edit(const std::string& styleId, const std::function<bool(mm::core::ReferenceCollection&)>& change,
                          bool create) {
    if (!validStyleId(styleId)) {
        return false;
    }
    mm::core::ReferenceCollection copy;
    {
        const juce::ScopedLock lock(lock_);
        auto it = collections_.find(styleId);
        if (it == collections_.end()) {
            if (!create) {
                return false;
            }
            it = collections_.emplace(styleId, mm::core::ReferenceCollection{}).first;
            it->second.styleId = styleId;
            mm::core::normalize(it->second);
        }
        if (!change(it->second)) {
            return false;
        }
        mm::core::normalize(it->second);
        copy = it->second;
    }
    pool_.addJob([this, styleId, copy] { write(styleId, copy); });
    changed();
    return true;
}

bool ReferenceStore::setActive(const std::string& styleId, const std::string& id, bool active) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::setMemberActive(c, c.activeSetId, id, active); });
}

bool ReferenceStore::setWeight(const std::string& styleId, const std::string& id, int weight) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::setMemberWeight(c, c.activeSetId, id, weight); });
}

bool ReferenceStore::setTags(const std::string& styleId, const std::string& id, std::vector<std::string> tags) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::setMemberTags(c, c.activeSetId, id, std::move(tags)); });
}

bool ReferenceStore::setRole(const std::string& styleId, const std::string& id, mm::core::TrackRole role) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::setItemRole(c, id, role); });
}

bool ReferenceStore::remove(const std::string& styleId, const std::string& id) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::removeMember(c, c.activeSetId, id); });
}

std::string ReferenceStore::createSet(const std::string& styleId, const std::string& name) {
    std::string id;
    edit(
        styleId,
        [&](mm::core::ReferenceCollection& c) {
            id = mm::core::createSet(c, name);
            return !id.empty() && mm::core::setActiveSet(c, id);
        },
        true);
    return id;
}

bool ReferenceStore::renameSet(const std::string& styleId, const std::string& setId, const std::string& name) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::renameSet(c, setId, name); });
}

bool ReferenceStore::deleteSet(const std::string& styleId, const std::string& setId) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::deleteSet(c, setId); });
}

bool ReferenceStore::setActiveSet(const std::string& styleId, const std::string& setId) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::setActiveSet(c, setId); }, true);
}

bool ReferenceStore::addToSet(const std::string& styleId, const std::string& setId, const std::string& itemId) {
    return edit(styleId, [&](mm::core::ReferenceCollection& c) { return mm::core::addMember(c, setId, itemId); });
}

bool ReferenceStore::setFavorite(const std::string& styleId, const std::string& entryId, const std::string& name,
                                 const mm::core::Pattern& pattern, bool alsoActive) {
    const auto* style = styles_.findOrFallback(styleId);
    if (style == nullptr || !validStyleId(styleId)) {
        return false;
    }
    auto items = mm::core::favoriteItemsOf(pattern, entryId, name, *style);
    return edit(
        styleId,
        [&](mm::core::ReferenceCollection& c) {
            mm::core::setFavorite(c, entryId, std::move(items), alsoActive);
            return true;
        },
        true);
}

void ReferenceStore::removeFavorite(const std::string& entryId) {
    std::vector<std::string> styleIds;
    {
        const juce::ScopedLock lock(lock_);
        for (const auto& [styleId, collection] : collections_) {
            styleIds.push_back(styleId);
        }
    }
    for (const auto& styleId : styleIds) {
        edit(styleId, [&](mm::core::ReferenceCollection& c) {
            const auto before = c;
            mm::core::removeFavorite(c, entryId);
            return !(before == c);
        });
    }
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
                known = mm::core::poolContainsFile(collectionLocked(styleId), hash);
            }
            if (known) { // the cache: the analysis of this file is in the pool, it only goes into the set
                const juce::ScopedLock lock(lock_);
                auto& collection = collectionLocked(styleId);
                mm::core::addKnownFile(collection, importTargetLocked(collection), hash, report);
                mm::core::normalize(collection);
            } else {
                auto items = mm::core::itemsFromMidiFile(file.getFileName().toStdString(), bytes, *style, report);
                const juce::ScopedLock lock(lock_);
                auto& collection = collectionLocked(styleId);
                mm::core::addItems(collection, importTargetLocked(collection), std::move(items), report);
                mm::core::normalize(collection);
            }
        }
        {
            const juce::ScopedLock lock(lock_);
            progress_.report += report;
            ++progress_.done;
        }
        changed();
    }
    mm::core::ReferenceCollection copy;
    {
        const juce::ScopedLock lock(lock_);
        copy = collectionLocked(styleId);
        progress_.running = false;
    }
    write(styleId, copy);
    changed();
}

mm::core::ReferenceCollection& ReferenceStore::collectionLocked(const std::string& styleId) {
    auto it = collections_.find(styleId);
    if (it == collections_.end()) {
        it = collections_.emplace(styleId, mm::core::ReferenceCollection{}).first;
        it->second.styleId = styleId;
        mm::core::normalize(it->second);
    }
    return it->second;
}

std::string ReferenceStore::importTargetLocked(mm::core::ReferenceCollection& collection) {
    // files go into the active set; the automatic favorites are not one to import into: a set "Standard" takes them
    if (collection.activeSetId != mm::core::kFavoritesSetId) {
        return collection.activeSetId;
    }
    auto id = mm::core::createSet(collection, "Standard");
    if (id.empty()) { // the name is taken (it is a set the musician made): use it
        for (const auto& set : collection.sets) {
            if (set.name == "Standard") {
                id = set.id;
            }
        }
    }
    mm::core::setActiveSet(collection, id);
    return id;
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
