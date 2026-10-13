#include "plugin/LibraryStore.h"

#include "plugin/AtomicFile.h"
#include "plugin/GlobalSettingsStore.h"

#include <algorithm>

namespace mm::plugin {

namespace {

std::atomic<LibraryStore*> gOverride{nullptr};

int64_t nowUnixMs() {
    return juce::Time::currentTimeMillis();
}

} // namespace

juce::File LibraryStore::defaultFolder() {
    const auto settings = GlobalSettingsStore::defaultFile();
    return settings == juce::File() ? juce::File() : settings.getParentDirectory().getChildFile("library");
}

juce::File LibraryStore::fileFor(const std::string& id) const {
    return folder_.getChildFile(juce::String(id) + ".json");
}

LibraryStore::LibraryStore(juce::File folder, size_t maxEntries)
    : folder_(std::move(folder)), maxEntries_(maxEntries) {
    if (!enabled()) {
        ready_ = true;
        return;
    }
    pool_.addJob([this] { readAll(); });
}

LibraryStore::~LibraryStore() {
    waitForIdle(); // what was saved is written before the plugin goes: a queued write is never dropped
    *alive_ = false;
    pool_.removeAllJobs(true, 10000);
}

void LibraryStore::readAll() {
    removeStaleTemporaries(folder_);
    std::vector<mm::core::LibraryFileState> files;
    for (const auto& file : folder_.findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto id = file.getFileNameWithoutExtension().toStdString();
        if (file.getFileName() == kIndexFileName || !mm::core::isValidLibraryId(id)) {
            continue; // the index, or not ours
        }
        files.push_back({id, file.getSize(), file.getLastModificationTime().toMilliseconds()});
    }
    const auto indexFile = folder_.getChildFile(kIndexFileName);
    const auto index = mm::core::loadLibraryIndex(indexFile.existsAsFile() ? indexFile.loadFileAsString().toStdString()
                                                                           : std::string());
    const auto plan = mm::core::reconcileLibraryIndex(index.records, files);

    std::map<std::string, mm::core::LibraryEntryInfo> found;
    for (const auto& record : plan.trusted) {
        found[record.info.id] = record.info;
    }
    size_t bad = 0;
    for (const auto& id : plan.toRead) {
        const auto loaded = mm::core::loadLibraryEntry(fileFor(id).loadFileAsString().toStdString(), id);
        if (loaded.ok()) {
            found[id] = loaded.entry->info;
        } else {
            ++bad;
        }
    }
    {
        const juce::ScopedLock lock(lock_);
        for (auto& [id, info] : found) {
            if (infos_.find(id) == infos_.end()) { // a change made before the folder was read wins
                infos_[id] = std::move(info);
            }
        }
        unreadable_ = bad;
        indexed_ = plan.trusted.size();
        parsed_ = plan.toRead.size();
        ready_ = true;
    }
    if (!index.ok || !plan.toRead.empty() || plan.dropped > 0 || index.skipped > 0) {
        scheduleIndex(); // the index did not tell the truth about the folder: write it again
    }
    changed();
}

void LibraryStore::scheduleIndex() {
    if (!enabled()) {
        return;
    }
    // One job per change, queued behind the job that wrote the change. A job that finds a later request waiting skips:
    // that later job runs after every change before it, so the last job of a burst writes the index once.
    const auto number = ++indexRequests_;
    pool_.addJob([this, number] {
        if (indexRequests_.load() == number) {
            writeIndex();
        }
    });
}

void LibraryStore::writeIndex() {
    std::vector<mm::core::LibraryIndexRecord> records;
    {
        const juce::ScopedLock lock(lock_);
        for (const auto& [id, info] : infos_) {
            mm::core::LibraryIndexRecord record;
            record.info = info;
            records.push_back(std::move(record));
        }
    }
    std::vector<mm::core::LibraryIndexRecord> existing;
    for (auto& record : records) {
        const auto file = fileFor(record.info.id);
        if (file.existsAsFile()) { // an entry whose file is not there is not in the index
            record.fileSize = file.getSize();
            record.modifiedMs = file.getLastModificationTime().toMilliseconds();
            existing.push_back(std::move(record));
        }
    }
    writeFileAtomically(folder_.getChildFile(kIndexFileName), mm::core::libraryIndexToString(std::move(existing)));
}

void LibraryStore::changed() {
    const auto alive = alive_;
    juce::MessageManager::callAsync([this, alive] {
        if (*alive && onChanged_) {
            onChanged_();
        }
    });
}

void LibraryStore::setOnChanged(std::function<void()> callback) {
    onChanged_ = std::move(callback);
}

std::vector<mm::core::LibraryEntryInfo> LibraryStore::list() const {
    std::vector<mm::core::LibraryEntryInfo> entries;
    {
        const juce::ScopedLock lock(lock_);
        for (const auto& [id, info] : infos_) {
            entries.push_back(info);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.createdUnixMs != b.createdUnixMs ? a.createdUnixMs > b.createdUnixMs : a.id < b.id;
    });
    return entries;
}

std::optional<mm::core::LibraryEntryInfo> LibraryStore::info(const std::string& id) const {
    const juce::ScopedLock lock(lock_);
    const auto it = infos_.find(id);
    return it == infos_.end() ? std::nullopt : std::optional(it->second);
}

std::string LibraryStore::newId(int64_t createdUnixMs) {
    for (;;) {
        const auto suffix = juce::String::toHexString(juce::Random::getSystemRandom().nextInt(0x10000)).paddedLeft('0', 4);
        auto id = (juce::String(createdUnixMs) + "-" + suffix).toLowerCase().toStdString();
        const juce::ScopedLock lock(lock_);
        if (infos_.find(id) == infos_.end()) {
            return id;
        }
    }
}

LibraryStore::SaveResult LibraryStore::add(mm::core::LibraryEntry entry) {
    {
        const juce::ScopedLock lock(lock_);
        if (infos_.size() >= maxEntries_) {
            return {{}, SaveError::Full};
        }
        infos_[entry.info.id] = entry.info;
        if (!enabled()) {
            memory_[entry.info.id] = entry;
        }
    }
    const auto id = entry.info.id;
    if (enabled()) {
        pool_.addJob([this, entry = std::move(entry)] {
            writeFileAtomically(fileFor(entry.info.id), mm::core::libraryEntryToString(entry));
        });
        scheduleIndex();
    }
    changed();
    return {id, SaveError::None};
}

LibraryStore::SaveResult LibraryStore::savePattern(const std::string& name, const mm::core::Pattern& pattern,
                                                   const std::string& slotName, uint8_t slotColor) {
    const auto created = nowUnixMs();
    return add(mm::core::makePatternEntry(newId(created), name, pattern, slotName, slotColor, created));
}

LibraryStore::SaveResult LibraryStore::saveSet(const std::string& name, const mm::core::SlotBank& bank) {
    const auto created = nowUnixMs();
    auto entry = mm::core::makeSetEntry(newId(created), name, bank, created);
    if (!entry) {
        return {{}, SaveError::NothingToSave};
    }
    return add(std::move(*entry));
}

void LibraryStore::load(const std::string& id, std::function<void(mm::core::LibraryLoadResult)> done) {
    const auto alive = alive_;
    const auto deliver = [alive, done = std::move(done)](mm::core::LibraryLoadResult result) {
        juce::MessageManager::callAsync([alive, done, result = std::move(result)]() mutable {
            if (*alive && done) {
                done(std::move(result));
            }
        });
    };
    {
        const juce::ScopedLock lock(lock_);
        if (infos_.find(id) == infos_.end()) {
            mm::core::LibraryLoadResult missing;
            missing.error = "id: no such entry";
            deliver(std::move(missing));
            return;
        }
        if (!enabled()) {
            mm::core::LibraryLoadResult result;
            result.entry = memory_.at(id);
            deliver(std::move(result));
            return;
        }
    }
    pool_.addJob([this, id, deliver] {
        const auto file = fileFor(id);
        if (!file.existsAsFile()) {
            mm::core::LibraryLoadResult missing;
            missing.error = "file: missing";
            deliver(std::move(missing));
            return;
        }
        deliver(mm::core::loadLibraryEntry(file.loadFileAsString().toStdString(), id));
    });
}

void LibraryStore::exportMidi(const std::string& id, mm::core::StyleProfile style, double bpm, juce::File target,
                              std::function<void(ExportResult)> done) {
    const auto alive = alive_;
    const auto deliver = [alive, done = std::move(done)](ExportResult result) {
        juce::MessageManager::callAsync([alive, done, result] {
            if (*alive && done) {
                done(result);
            }
        });
    };
    std::optional<mm::core::LibraryEntry> inMemory;
    {
        const juce::ScopedLock lock(lock_);
        if (infos_.find(id) == infos_.end()) {
            deliver(ExportResult::NoSuchEntry);
            return;
        }
        if (!enabled()) {
            inMemory = memory_.at(id);
        }
    }
    pool_.addJob([this, id, style = std::move(style), bpm, target = std::move(target), deliver,
                  inMemory = std::move(inMemory)]() mutable {
        std::optional<mm::core::LibraryEntry> entry = std::move(inMemory);
        if (!entry) {
            auto loaded = mm::core::loadLibraryEntry(fileFor(id).loadFileAsString().toStdString(), id);
            if (!loaded.ok()) {
                deliver(ExportResult::NoSuchEntry);
                return;
            }
            entry = std::move(loaded.entry);
        }
        if (!entry->pattern) {
            deliver(ExportResult::NotAPattern);
            return;
        }
        const auto bytes = mm::core::exportPatternMidi(*entry->pattern, style, bpm);
        if (bytes.empty()) {
            deliver(ExportResult::NoNotes);
            return;
        }
        const bool written = writeFileAtomically(target, std::string(bytes.begin(), bytes.end()));
        deliver(written ? ExportResult::Done : ExportResult::WriteFailed);
    });
}

bool LibraryStore::remove(const std::string& id) {
    {
        const juce::ScopedLock lock(lock_);
        if (infos_.erase(id) == 0) {
            return false;
        }
        memory_.erase(id);
    }
    if (enabled()) {
        pool_.addJob([this, id] { fileFor(id).deleteFile(); });
        scheduleIndex();
    }
    changed();
    return true;
}

bool LibraryStore::rename(const std::string& id, const std::string& name) {
    const auto cleaned = mm::core::sanitizeLibraryName(name);
    if (cleaned.empty()) {
        return false;
    }
    {
        const juce::ScopedLock lock(lock_);
        const auto it = infos_.find(id);
        if (it == infos_.end()) {
            return false;
        }
        it->second.name = cleaned;
        if (!enabled()) {
            memory_.at(id).info.name = cleaned;
        }
    }
    if (enabled()) {
        pool_.addJob([this, id, cleaned] {
            const auto file = fileFor(id);
            auto loaded = mm::core::loadLibraryEntry(file.loadFileAsString().toStdString(), id);
            if (loaded.ok()) {
                loaded.entry->info.name = cleaned;
                writeFileAtomically(file, mm::core::libraryEntryToString(*loaded.entry));
            }
        });
        scheduleIndex();
    }
    changed();
    return true;
}

bool LibraryStore::setMeta(const std::string& id, const mm::core::LibraryMeta& meta) {
    const auto clean = mm::core::sanitizeLibraryMeta(meta);
    {
        const juce::ScopedLock lock(lock_);
        const auto it = infos_.find(id);
        if (it == infos_.end()) {
            return false;
        }
        mm::core::setMeta(it->second, clean);
        if (!enabled()) {
            mm::core::setMeta(memory_.at(id).info, clean);
        }
    }
    if (enabled()) {
        pool_.addJob([this, id, clean] {
            const auto file = fileFor(id);
            auto loaded = mm::core::loadLibraryEntry(file.loadFileAsString().toStdString(), id);
            if (loaded.ok()) {
                mm::core::setMeta(loaded.entry->info, clean);
                writeFileAtomically(file, mm::core::libraryEntryToString(*loaded.entry));
            }
        });
        scheduleIndex();
    }
    changed();
    return true;
}

void LibraryStore::waitForIdle() {
    pool_.addJob([] {}); // one worker: when this has run, everything before it has
    while (pool_.getNumJobs() > 0) {
        juce::Thread::sleep(2);
    }
}

LibraryStore& libraryStore() {
    if (auto* override = gOverride.load()) {
        return *override;
    }
    static LibraryStore store(LibraryStore::defaultFolder());
    return store;
}

void overrideLibraryStore(LibraryStore* store) {
    gOverride = store;
}

} // namespace mm::plugin
