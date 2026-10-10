#include "plugin/EmbeddedPrompts.h"

#include "MidiMaidPrompts.h"

#include <string>
#include <string_view>

namespace mm::plugin {

const mm::ai::PromptTemplates& embeddedPrompts() {
    static const mm::ai::PromptTemplates templates = [] {
        mm::ai::PromptTemplates result;
        for (int i = 0; i < MidiMaidPrompts::namedResourceListSize; ++i) {
            const char* name = MidiMaidPrompts::namedResourceList[i];
            int size = 0;
            const char* data = MidiMaidPrompts::getNamedResource(name, size);
            if (data == nullptr) {
                continue;
            }
            const std::string text(data, static_cast<size_t>(size));
            const std::string_view key(name);
            // JUCE names a resource after its file: "style_peak_time.md" becomes "style_peak_time_md".
            constexpr std::string_view stylePrefix = "style_";
            constexpr std::string_view suffix = "_md";
            if (key == "system_md") {
                result.system = text;
            } else if (key == "generate_md") {
                result.generate = text;
            } else if (key == "refine_md") {
                result.refine = text;
            } else if (key.substr(0, stylePrefix.size()) == stylePrefix &&
                       key.size() > stylePrefix.size() + suffix.size() &&
                       key.substr(key.size() - suffix.size()) == suffix) {
                result.styles[std::string(
                    key.substr(stylePrefix.size(), key.size() - stylePrefix.size() - suffix.size()))] = text;
            }
        }
        return result;
    }();
    return templates;
}

} // namespace mm::plugin
