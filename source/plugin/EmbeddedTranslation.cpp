#include "plugin/EmbeddedTranslation.h"

#include "MidiMaidI18n.h"
#include "core/GlobalSettings.h"
#include "plugin/GlobalSettingsStore.h"

#include <atomic>

namespace mm::plugin {

namespace {

mm::core::Translation load(const char* resource) {
    int size = 0;
    const char* data = MidiMaidI18n::getNamedResource(resource, size);
    return mm::core::Translation::fromJson(data != nullptr ? std::string_view(data, static_cast<size_t>(size))
                                                           : std::string_view());
}

const mm::core::Translation& german() {
    static const mm::core::Translation table = load("de_json");
    return table;
}

const mm::core::Translation& english() {
    static const mm::core::Translation table = load("en_json");
    return table;
}

std::atomic<const mm::core::Translation*> current{nullptr};

const mm::core::Translation* tableFor(const std::string& code) {
    return code == "en" ? &english() : &german();
}

std::string resolve(const std::string& setting) {
    return mm::core::resolveLanguage(setting, juce::SystemStats::getUserLanguage().toStdString());
}

} // namespace

std::string initialUiSetting() {
    const auto& store = globalSettings();
    if (!store.enabled()) {
        return "de";
    }
    return mm::core::parseGlobalSettings(store.file().loadFileAsString().toStdString()).language;
}

const mm::core::Translation& embeddedTranslation() {
    if (const auto* table = current.load()) {
        return *table;
    }
    const auto* table = tableFor(resolve(initialUiSetting()));
    const mm::core::Translation* expected = nullptr;
    current.compare_exchange_strong(expected, table);
    return *current.load();
}

void setUiLanguage(const std::string& setting) {
    current.store(tableFor(resolve(setting)));
}

std::string uiLanguage() {
    return &embeddedTranslation() == &english() ? "en" : "de";
}

const std::string& localized(const std::string& german, const std::string& english) {
    return uiLanguage() == "en" && !english.empty() ? english : german;
}

juce::String tr(std::string_view key) {
    return juce::String::fromUTF8(embeddedTranslation().tr(key).c_str());
}

juce::String tr(std::string_view key, mm::core::Translation::Args args) {
    return juce::String::fromUTF8(embeddedTranslation().tr(key, args).c_str());
}

} // namespace mm::plugin
