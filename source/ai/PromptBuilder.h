#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::ai {

/// Version of the prompt templates (`resources/prompts/v1/`), stored in `GenerationInfo::promptVersion`.
constexpr uint32_t kPromptVersion = 1;

/// The texts of `resources/prompts/v1/`: the general system prompt, the request and refine templates and one addition
/// per style (by style id). They contain `{{name}}` placeholders that the builders fill.
struct PromptTemplates {
    std::string system;
    std::string generate;
    std::string refine;
    std::map<std::string, std::string> styles;
};

struct LoadedTemplates {
    PromptTemplates templates;
    std::vector<std::string> missing; ///< files that could not be read
};

/// Reads `system.md`, `generate.md`, `refine.md` and `style_<id>.md` for each id from `directory` (for tests and tools;
/// the plugin gets the same texts embedded in its binary).
LoadedTemplates loadPromptTemplates(const std::filesystem::path& directory, const std::vector<std::string>& styleIds);

/// The names of the `{{...}}` placeholders in `text`, in order of appearance.
std::vector<std::string> placeholdersOf(std::string_view text);

/// Replaces every `{{name}}` with its value; a placeholder without value is replaced by nothing and its name added to
/// `missing`. The values are inserted as they are and not searched for placeholders again.
std::string renderTemplate(std::string_view text, const std::map<std::string, std::string>& values,
                           std::vector<std::string>* missing = nullptr);

/// Makes the musician's text safe to put between the `<<<` and `>>>` of a template: control characters become spaces,
/// runs of white space shrink to one space, the delimiters are taken out and the text is cut to `maxChars` bytes at a
/// UTF-8 character boundary.
std::string sanitizeUserText(std::string_view text, size_t maxChars = 2000);

/// What the creativity (0-100) tells the model, from faithful to the style up to experimental (SPEC 7.6).
std::string creativityGuidance(int creativityPct);

/// The style addition of the template plus the numbers of the profile (tempo, scales, ranges, bass movement,
/// archetypes with weights, chord colours): the profile is the source, the text only explains it.
std::string styleRules(const PromptTemplates& templates, const mm::core::StyleProfile& style);

struct Prompt {
    std::string system;
    std::string user;
};

struct GeneratePromptInput {
    const mm::core::StyleProfile* style = nullptr;
    uint32_t lengthBars = 4;
    std::optional<mm::core::PitchClass> root;     ///< fixed key, else the model chooses
    std::optional<std::string> scaleId;           ///< fixed scale, else the model chooses
    std::optional<std::string> bassArchetype;     ///< manual choice, else the model chooses from the style
    std::optional<std::string> melodyArchetype;
    int energyPct = 50;
    int creativityPct = 40;
    std::string request; ///< the musician's description (may be empty: the style alone decides)
};

/// The request for "Generate" with an AI provider (SPEC 7.4). Nullopt without a style.
std::optional<Prompt> buildGeneratePrompt(const PromptTemplates& templates, const GeneratePromptInput& input);

struct RefinePromptInput {
    const mm::core::StyleProfile* style = nullptr;
    const mm::core::Pattern* pattern = nullptr;
    std::string instruction;
    std::vector<std::string> history; ///< earlier refinements of this pattern, oldest first (the last 5 are used)
    std::optional<size_t> voice;      ///< only this voice (index into `Pattern::voices`)
    std::optional<size_t> phrase;     ///< only this phrase (index into `Pattern::phrases`)
    int energyPct = 50;
    int creativityPct = 40;
};

/// The request for "Refine" (SPEC 3.15): the current pattern in schema v1 with its note ids, the scope, the locked
/// voices, the earlier refinements and the instruction. Nullopt without style or pattern, or when the pattern cannot be
/// written in the schema.
std::optional<Prompt> buildRefinePrompt(const PromptTemplates& templates, const RefinePromptInput& input);

} // namespace mm::ai
