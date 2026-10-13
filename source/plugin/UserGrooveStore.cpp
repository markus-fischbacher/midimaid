#include "plugin/UserGrooveStore.h"

#include "plugin/AtomicFile.h"
#include "plugin/EmbeddedGrooves.h"
#include "plugin/GlobalSettingsStore.h"

#include <algorithm>

namespace mm::plugin {

namespace {

std::atomic<UserGrooveStore*> gOverride{nullptr};

} // namespace

juce::File UserGrooveStore::defaultFolder() {
    const auto settings = GlobalSettingsStore::defaultFile();
    return settings == juce::File() ? juce::File() : settings.getParentDirectory().getChildFile("grooves");
}

UserGrooveStore::UserGrooveStore(juce::File folder, const mm::core::GrooveLibrary& base)
    : folder_(std::move(folder)), base_(base) {
    library_ = std::make_shared<const mm::core::GrooveLibrary>(base_);
    if (!enabled()) {
        ready_ = true;
        return;
    }
    pool_.addJob([this] { readAll(); });
}

UserGrooveStore::~UserGrooveStore() {
    *alive_ = false;
    pool_.removeAllJobs(true, 10000);
}

void UserGrooveStore::readAll() {
    std::vector<std::string> loaded;
    auto files = folder_.findChildFiles(juce::File::findFiles, false, "*.json");
    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    for (const auto& file : files) {
        if (loaded.size() < kMaxTemplates) {
            loaded.push_back(file.loadFileAsString().toStdString());
        }
    }
    {
        const juce::ScopedLock lock(lock_);
        // what was added before the files were read comes after them; a text whose id is taken is left out by the library
        loaded.insert(loaded.end(), texts_.begin(), texts_.end());
        texts_ = std::move(loaded);
        rebuildLocked();
        ready_ = true;
    }
    changed();
}

void UserGrooveStore::rebuildLocked() {
    std::vector<std::string_view> views(texts_.begin(), texts_.end());
    library_ = std::make_shared<const mm::core::GrooveLibrary>(mm::core::GrooveLibrary::withTexts(base_, views));
}

void UserGrooveStore::changed() {
    ++revision_;
    const auto alive = alive_;
    juce::MessageManager::callAsync([this, alive] {
        if (*alive && onChanged_) {
            onChanged_();
        }
    });
}

void UserGrooveStore::setOnChanged(std::function<void()> callback) {
    onChanged_ = std::move(callback);
}

std::shared_ptr<const mm::core::GrooveLibrary> UserGrooveStore::library() const {
    const juce::ScopedLock lock(lock_);
    return library_;
}

std::string UserGrooveStore::add(mm::core::GrooveTemplate groove) {
    const auto name = juce::String(groove.nameDe).trim();
    if (name.isEmpty() || static_cast<size_t>(name.length()) > kMaxNameLength) {
        return {};
    }
    groove.nameDe = name.toStdString();
    std::string id;
    std::string text;
    {
        const juce::ScopedLock lock(lock_);
        if (texts_.size() >= kMaxTemplates) {
            return {};
        }
        groove.id = mm::core::grooveIdFromName(groove.nameDe, *library_);
        text = mm::core::grooveTemplateToJson(groove);
        if (!mm::core::loadGrooveTemplate(text).ok()) {
            return {};
        }
        texts_.push_back(text);
        rebuildLocked();
        id = groove.id;
    }
    if (enabled()) {
        const auto file = folder_.getChildFile(juce::String(id) + ".json");
        pool_.addJob([file, text] { writeFileAtomically(file, text); });
    }
    changed();
    return id;
}

void UserGrooveStore::waitForIdle() {
    while (pool_.getNumJobs() > 0) {
        juce::Thread::sleep(5);
    }
}

UserGrooveStore& userGrooves() {
    if (auto* override = gOverride.load()) {
        return *override;
    }
    static UserGrooveStore store(UserGrooveStore::defaultFolder(), embeddedGrooves());
    return store;
}

void overrideUserGrooves(UserGrooveStore* store) {
    gOverride = store;
}

} // namespace mm::plugin
