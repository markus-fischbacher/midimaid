#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// A groove template (SPEC 3.9, v1.1): a timing offset and a velocity profile per 16th step over one or two bars, and
/// optionally a small deterministic deviation per note ("Humanize"). Notes keep their grid in the pattern; the output
/// stage applies the template (stage 2 of SPEC 4.2a).
struct GrooveTemplate {
    std::string id;     ///< lower case, digits and underscore ("laid_back")
    std::string nameDe; ///< the name in the pick list
    std::string nameEn; ///< the same in English; empty: the German name
    uint32_t bars = 1;  ///< 1 or 2: the template repeats every `bars` bars
    std::vector<int32_t> offsetTicks;       ///< per 16th: delay (positive) or push (negative) in ticks, +-240 at most
    std::vector<int32_t> velocityPermille;  ///< per 16th: factor on the velocity, 500 to 1500 (1000 = unchanged)
    bool swing = false;                     ///< the template has swing of its own: the swing control is off (SPEC 3.9)
    int32_t humanizeTicks = 0;              ///< every note moves by up to this many ticks either way (0 to 60) ...
    int32_t humanizeVelocityPermille = 0;   ///< ... and its velocity by up to this many per mille (0 to 200)

    size_t steps() const { return static_cast<size_t>(bars) * 16; }
    bool operator==(const GrooveTemplate&) const = default;
};

struct GrooveTemplateLoad {
    std::optional<GrooveTemplate> groove;
    std::string error; ///< empty on success; otherwise path and reason
    bool ok() const { return groove.has_value(); }
};

/// Reads a template from its JSON text. Never throws; a text that is no valid template gives an error with the path.
GrooveTemplateLoad loadGrooveTemplate(std::string_view text);

/// The templates an instance knows, loaded from JSON texts (the plug-in gets them from the embedded resources, the
/// tests from files). A text that does not load is left out and reported; the first template with an id wins.
class GrooveLibrary {
public:
    static GrooveLibrary fromTexts(const std::vector<std::string_view>& texts);

    size_t size() const { return templates_.size(); }
    /// All templates in load order (for the pick list).
    const std::vector<GrooveTemplate>& templates() const { return templates_; }
    /// The template with this id, or null (the ids "straight" and "drum_reference" are no templates of the library).
    const GrooveTemplate* find(std::string_view id) const;
    const std::vector<std::string>& problems() const { return problems_; }

private:
    std::vector<GrooveTemplate> templates_;
    std::vector<std::string> problems_;
};

/// The deviation of one note from the grid for a template with `humanize` (deterministic: a hash of the seed, the note
/// and the template, so a bounce is the same every time): ticks in [-limit, +limit].
int32_t humanizeOffset(uint64_t seed, uint32_t noteId, std::string_view templateId, int32_t limit);
/// The same for the velocity factor: permille in [-limit, +limit].
int32_t humanizeVelocity(uint64_t seed, uint32_t noteId, std::string_view templateId, int32_t limit);

} // namespace mm::core
