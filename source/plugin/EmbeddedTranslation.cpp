#include "plugin/EmbeddedTranslation.h"

#include "MidiMaidI18n.h"

namespace mm::plugin {

const mm::core::Translation& embeddedTranslation() {
    static const mm::core::Translation table = [] {
        int size = 0;
        const char* data = MidiMaidI18n::getNamedResource("de_json", size);
        return mm::core::Translation::fromJson(data != nullptr ? std::string_view(data, static_cast<size_t>(size))
                                                               : std::string_view());
    }();
    return table;
}

juce::String tr(std::string_view key) {
    return juce::String::fromUTF8(embeddedTranslation().tr(key).c_str());
}

juce::String tr(std::string_view key, mm::core::Translation::Args args) {
    return juce::String::fromUTF8(embeddedTranslation().tr(key, args).c_str());
}

} // namespace mm::plugin
