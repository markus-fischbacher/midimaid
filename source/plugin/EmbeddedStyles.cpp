#include "plugin/EmbeddedStyles.h"

#include "MidiMaidStyles.h"

#include <string_view>
#include <vector>

namespace mm::plugin {

const mm::core::StyleLibrary& embeddedStyles() {
    static const mm::core::StyleLibrary library = [] {
        std::vector<std::string_view> texts;
        for (int i = 0; i < MidiMaidStyles::namedResourceListSize; ++i) {
            int size = 0;
            const char* data = MidiMaidStyles::getNamedResource(MidiMaidStyles::namedResourceList[i], size);
            if (data != nullptr) {
                texts.emplace_back(data, static_cast<size_t>(size));
            }
        }
        return mm::core::StyleLibrary::fromTexts(texts);
    }();
    return library;
}

} // namespace mm::plugin
