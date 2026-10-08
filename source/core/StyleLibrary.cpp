#include "core/StyleLibrary.h"

namespace mm::core {

namespace {
constexpr std::string_view kFallbackStyleId = "peak_time";
}

StyleLibrary StyleLibrary::fromTexts(const std::vector<std::string_view>& texts) {
    StyleLibrary library;
    for (size_t i = 0; i < texts.size(); ++i) {
        auto loaded = loadStyleProfile(texts[i]);
        if (!loaded.ok()) {
            library.problems_.push_back("style " + std::to_string(i) + ": " + loaded.error);
        } else if (library.find(loaded.profile->id) != nullptr) {
            library.problems_.push_back("style " + std::to_string(i) + ": duplicate id " + loaded.profile->id);
        } else {
            library.profiles_.push_back(std::move(*loaded.profile));
        }
    }
    return library;
}

const StyleProfile* StyleLibrary::find(std::string_view id) const {
    for (const auto& profile : profiles_) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

const StyleProfile* StyleLibrary::findOrFallback(std::string_view id) const {
    if (const auto* profile = find(id)) {
        return profile;
    }
    if (const auto* profile = find(kFallbackStyleId)) {
        return profile;
    }
    return profiles_.empty() ? nullptr : &profiles_.front();
}

} // namespace mm::core
