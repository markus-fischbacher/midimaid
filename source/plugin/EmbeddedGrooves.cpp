#include "plugin/EmbeddedGrooves.h"

#include "MidiMaidGrooves.h"

#include <string_view>
#include <vector>

namespace mm::plugin {

const mm::core::GrooveLibrary& embeddedGrooves() {
    static const mm::core::GrooveLibrary library = [] {
        std::vector<std::string_view> texts;
        for (int i = 0; i < MidiMaidGrooves::namedResourceListSize; ++i) {
            int size = 0;
            const char* data = MidiMaidGrooves::getNamedResource(MidiMaidGrooves::namedResourceList[i], size);
            if (data != nullptr) {
                texts.emplace_back(data, static_cast<size_t>(size));
            }
        }
        return mm::core::GrooveLibrary::fromTexts(texts);
    }();
    return library;
}

} // namespace mm::plugin
