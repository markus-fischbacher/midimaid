#include "ai/PromptBuilder.h"

#include "ai/AiSchema.h"
#include "ai/PatternCompact.h"
#include "core/Archetype.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace mm::ai {

namespace {

using namespace mm::core;

std::string readFile(const std::filesystem::path& path, bool& ok) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.good()) {
        ok = false;
        return {};
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string percentText(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string join(const std::vector<std::string>& items, const char* separator) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i == 0 ? "" : separator) + items[i];
    }
    return text;
}

std::string weighted(const std::vector<WeightedId>& list, bool onlyImplemented) {
    std::string text;
    for (const auto& entry : list) {
        if (onlyImplemented && findArchetype(entry.id) == nullptr) {
            continue;
        }
        text += (text.empty() ? "" : ", ") + entry.id + " (" + std::to_string(entry.weight) + ")";
    }
    return text.empty() ? "none" : text;
}

} // namespace

LoadedTemplates loadPromptTemplates(const std::filesystem::path& directory, const std::vector<std::string>& styleIds) {
    LoadedTemplates loaded;
    const auto read = [&](const std::string& name) {
        bool ok = true;
        auto text = readFile(directory / name, ok);
        if (!ok) {
            loaded.missing.push_back(name);
        }
        return text;
    };
    loaded.templates.system = read("system.md");
    loaded.templates.generate = read("generate.md");
    loaded.templates.refine = read("refine.md");
    for (const auto& id : styleIds) {
        loaded.templates.styles[id] = read("style_" + id + ".md");
    }
    return loaded;
}

std::vector<std::string> placeholdersOf(std::string_view text) {
    std::vector<std::string> names;
    for (size_t at = text.find("{{"); at != std::string_view::npos; at = text.find("{{", at)) {
        const auto close = text.find("}}", at + 2);
        if (close == std::string_view::npos) {
            break;
        }
        names.emplace_back(text.substr(at + 2, close - at - 2));
        at = close + 2;
    }
    return names;
}

std::string renderTemplate(std::string_view text, const std::map<std::string, std::string>& values,
                           std::vector<std::string>* missing) {
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        const auto at = text.find("{{", pos);
        const auto close = at == std::string_view::npos ? std::string_view::npos : text.find("}}", at + 2);
        if (at == std::string_view::npos || close == std::string_view::npos) {
            out.append(text.substr(pos));
            break;
        }
        out.append(text.substr(pos, at - pos));
        const std::string name(text.substr(at + 2, close - at - 2));
        if (const auto it = values.find(name); it != values.end()) {
            out += it->second;
        } else if (missing != nullptr) {
            missing->push_back(name);
        }
        pos = close + 2;
    }
    return out;
}

std::string sanitizeUserText(std::string_view text, size_t maxChars) {
    // The delimiters never survive, also not when taking one out makes another: remove until none is left. Control
    // characters are turned into spaces afterwards (they are not removed, so they cannot join anything).
    std::string raw(text);
    for (const char* delimiter : {"<<<", ">>>"}) {
        for (size_t at = raw.find(delimiter); at != std::string::npos; at = raw.find(delimiter)) {
            raw.erase(at, 3);
        }
    }
    for (size_t at = raw.find("<<<"); at != std::string::npos; at = raw.find("<<<")) {
        raw.erase(at, 3); // an erased ">>>" can leave "<<<" behind
    }
    for (size_t at = raw.find(">>>"); at != std::string::npos; at = raw.find(">>>")) {
        raw.erase(at, 3);
    }
    std::string cleaned;
    bool space = true; // swallows leading white space
    for (const char c : raw) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f || c == ' ') {
            if (!space) {
                cleaned += ' ';
                space = true;
            }
            continue;
        }
        cleaned += c;
        space = false;
    }
    while (!cleaned.empty() && cleaned.back() == ' ') {
        cleaned.pop_back();
    }
    if (cleaned.size() > maxChars) {
        size_t cut = maxChars;
        while (cut > 0 && (static_cast<unsigned char>(cleaned[cut]) & 0xC0) == 0x80) {
            --cut; // not in the middle of a UTF-8 character
        }
        cleaned.resize(cut);
        while (!cleaned.empty() && cleaned.back() == ' ') {
            cleaned.pop_back();
        }
    }
    return cleaned;
}

std::string creativityGuidance(int creativityPct) {
    const int pct = std::clamp(creativityPct, 0, 100);
    std::string band;
    if (pct < 34) {
        band = "stay close to the style: classic shapes, few surprises, a strict motif";
    } else if (pct < 67) {
        band = "a balance: stay within the style but give the motif a personal twist";
    } else {
        band = "experimental: unusual intervals, rhythms and harmonic turns are welcome as long as the pattern stays "
               "hypnotic and playable";
    }
    return std::to_string(pct) + " percent: " + band;
}

std::string styleRules(const PromptTemplates& templates, const StyleProfile& style) {
    std::ostringstream out;
    if (const auto it = templates.styles.find(style.id); it != templates.styles.end()) {
        out << it->second;
        if (!it->second.empty() && it->second.back() != '\n') {
            out << '\n';
        }
        out << '\n';
    }
    out << "Style profile (follow these numbers):\n";
    out << "- tempo " << style.tempoMin << " to " << style.tempoMax << " BPM\n";
    out << "- scales (weight): ";
    for (size_t i = 0; i < style.scales.size(); ++i) {
        out << (i == 0 ? "" : ", ") << style.scales[i].id << " (" << style.scales[i].weight << ")";
    }
    out << "\n- chromatic notes: about " << style.chromaticDefaultPercent << " percent of the notes may leave the scale\n";
    out << "- default kick grid: " << style.kickDefault << '\n';
    out << "- bass range MIDI " << style.bass.range.low << " to " << style.bass.range.high << ", melody range MIDI "
        << style.melody.range.low << " to " << style.melody.range.high << '\n';
    out << "- bass notes: root " << style.bass.movement.rootPercent << " percent, fifth or octave "
        << style.bass.movement.fifthOctavePercent << " percent, passing tones " << style.bass.movement.passingPercent
        << " percent\n";
    out << "- bass archetypes (weight): " << weighted(style.bass.archetypes, true) << '\n';
    out << "- melody archetypes (weight): " << weighted(style.melody.archetypes, true) << '\n';
    return out.str();
}

std::optional<Prompt> buildGeneratePrompt(const PromptTemplates& templates, const GeneratePromptInput& input) {
    if (input.style == nullptr) {
        return std::nullopt;
    }
    const uint32_t bars = input.lengthBars;
    std::map<std::string, std::string> values;
    values["schema"] = schemaV1Json();
    values["style"] = styleRules(templates, *input.style);
    Prompt prompt;
    prompt.system = renderTemplate(templates.system, values);

    std::vector<std::string> scaleIds;
    for (const auto& scale : input.style->scales) {
        scaleIds.push_back(scale.id);
    }
    std::map<std::string, std::string> request;
    request["bars"] = std::to_string(bars);
    request["last_step"] = std::to_string(bars * 16 - 1);
    request["key"] = input.root ? "fixed, use " + rootName(*input.root) + " as `context.root`"
                                : "your choice: pick a root that suits the style (minor keys are typical)";
    request["scale"] = input.scaleId ? "fixed, use " + *input.scaleId + " as `context.scale`"
                                     : "your choice from: " + join(scaleIds, ", ");
    request["energy"] = percentText(std::clamp(input.energyPct, 0, 100) / 100.0);
    request["creativity"] = creativityGuidance(input.creativityPct);
    request["voices"] = "bass: " +
                        (input.bassArchetype ? "use the archetype " + *input.bassArchetype
                                             : std::string("choose an archetype of the style")) +
                        "; melody: " +
                        (input.melodyArchetype ? "use the archetype " + *input.melodyArchetype
                                               : std::string("choose an archetype of the style"));
    request["motif_note"] =
        bars > 8 ? "This pattern is " + std::to_string(bars) +
                       " bars long: do not write every bar. Give a motif of 4 to 8 bars per voice in `notes` (steps "
                       "counted from bar 0, with `motif_bars` on the voice) and a `phrases` plan (`start_bar`, `bars` "
                       "of 4, 8 or 16, `role`) that covers all bars without gaps; the plugin builds the phrases "
                       "from your motif."
                 : "";
    const auto text = sanitizeUserText(input.request);
    request["request"] = text.empty() ? "(none: let the style decide)" : text;
    prompt.user = renderTemplate(templates.generate, request);
    return prompt;
}

std::optional<Prompt> buildRefinePrompt(const PromptTemplates& templates, const RefinePromptInput& input) {
    if (input.style == nullptr || input.pattern == nullptr) {
        return std::nullopt;
    }
    const Pattern& pattern = *input.pattern;
    const auto compact = patternToSchemaJson(pattern, input.style, true);
    if (!compact) {
        return std::nullopt;
    }
    std::map<std::string, std::string> values;
    values["schema"] = schemaV1Json();
    values["style"] = styleRules(templates, *input.style);
    Prompt prompt;
    prompt.system = renderTemplate(templates.system, values);

    std::string scope = "the whole pattern";
    if (input.voice && *input.voice < pattern.voices.size()) {
        scope = std::string("only the ") + (pattern.voices[*input.voice].role == VoiceRole::Bass ? "bass" : "melody") +
                " voice; leave the other voice exactly as it is";
    } else if (input.phrase && *input.phrase < pattern.phrases.size()) {
        const auto& phrase = pattern.phrases[*input.phrase];
        scope = "only the bars " + std::to_string(phrase.startBar) + " to " +
                std::to_string(phrase.startBar + phrase.lengthBars - 1) + " (steps " +
                std::to_string(phrase.startBar * 16) + " to " + std::to_string((phrase.startBar + phrase.lengthBars) * 16 - 1) +
                "); leave every other bar exactly as it is";
    }
    std::vector<std::string> lockedVoices;
    for (const auto& track : pattern.voices) {
        if (isVoiceLocked(track)) {
            lockedVoices.push_back(track.role == VoiceRole::Bass ? "bass" : "melody");
        }
    }
    std::string history;
    const size_t from = input.history.size() > 5 ? input.history.size() - 5 : 0;
    for (size_t i = from; i < input.history.size(); ++i) {
        const auto text = sanitizeUserText(input.history[i], 400);
        if (!text.empty()) {
            history += (history.empty() ? "Earlier refinements of this pattern, oldest first (for context only):\n" : "") +
                       std::to_string(i - from + 1) + ". <<<" + text + ">>>\n";
        }
    }
    std::map<std::string, std::string> request;
    request["bars"] = std::to_string(pattern.lengthBars);
    request["last_step"] = std::to_string(pattern.lengthBars * 16 - 1);
    request["scope"] = scope;
    request["locked"] = lockedVoices.empty()
                            ? "No voice is locked."
                            : "Locked, return these voices unchanged: " + join(lockedVoices, ", ") + ".";
    request["energy"] = percentText(std::clamp(input.energyPct, 0, 100) / 100.0);
    request["creativity"] = creativityGuidance(input.creativityPct);
    request["history"] = history;
    request["pattern"] = compact->json;
    request["instruction"] = sanitizeUserText(input.instruction);
    prompt.user = renderTemplate(templates.refine, request);
    return prompt;
}

} // namespace mm::ai
