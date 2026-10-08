#pragma once

#include "core/StyleProfile.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// The style profiles an instance knows (STYLES.md 5), loaded from JSON texts. The plugin gets the texts from the
/// embedded resources, the tools and tests from files. A text that does not load is left out and reported.
class StyleLibrary {
public:
    /// Loads every text; the first valid profile with an id that is already present wins.
    static StyleLibrary fromTexts(const std::vector<std::string_view>& texts);

    size_t size() const { return profiles_.size(); }
    /// All profiles in load order (for pick lists).
    const std::vector<StyleProfile>& profiles() const { return profiles_; }
    /// The profile with this id, or null.
    const StyleProfile* find(std::string_view id) const;
    /// The profile for `id`; an unknown id gives the fallback (`peak_time`, else the first one). Null when empty.
    const StyleProfile* findOrFallback(std::string_view id) const;
    /// One message per text that could not be loaded.
    const std::vector<std::string>& problems() const { return problems_; }

private:
    std::vector<StyleProfile> profiles_;
    std::vector<std::string> problems_;
};

} // namespace mm::core
