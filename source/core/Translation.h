#pragma once

#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mm::core {

/// The table of UI texts of one language (SPEC 8.2): a flat JSON object `key -> text`. A text may hold placeholders
/// `{name}`. Standard library only; the plugin embeds `resources/i18n/de.json` and builds the table once.
class Translation {
public:
    using Args = std::initializer_list<std::pair<std::string_view, std::string_view>>;

    /// Loads a flat JSON object of strings. An entry that is not a string is left out and reported; a text that is not
    /// a JSON object gives an empty table and one problem.
    static Translation fromJson(std::string_view json);

    /// The text for `key`, or the key itself when the table has none (a gap shows up on screen and in tests).
    std::string tr(std::string_view key) const;
    /// The same with every `{name}` replaced by its value from `args`. A placeholder without a value stays as it is;
    /// a value is never searched for placeholders again.
    std::string tr(std::string_view key, Args args) const;

    bool has(std::string_view key) const;
    size_t size() const { return texts_.size(); }
    std::set<std::string> keys() const;
    const std::vector<std::string>& problems() const { return problems_; }

private:
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
    };
    std::unordered_map<std::string, std::string, Hash, std::equal_to<>> texts_;
    std::vector<std::string> problems_;
};

/// Replaces `{name}` in `text` as described at `Translation::tr`.
std::string fillPlaceholders(std::string_view text, Translation::Args args);

} // namespace mm::core
