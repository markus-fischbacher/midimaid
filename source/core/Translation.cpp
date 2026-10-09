#include "core/Translation.h"

#include <nlohmann/json.hpp>

namespace mm::core {

std::string fillPlaceholders(std::string_view text, Translation::Args args) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            const auto close = text.find('}', i + 1);
            if (close != std::string_view::npos) {
                const auto name = text.substr(i + 1, close - i - 1);
                bool found = false;
                for (const auto& [argName, value] : args) {
                    if (argName == name) {
                        out.append(value);
                        found = true;
                        break;
                    }
                }
                if (found) {
                    i = close + 1;
                    continue;
                }
            }
        }
        out.push_back(text[i++]);
    }
    return out;
}

Translation Translation::fromJson(std::string_view json) {
    Translation table;
    const auto parsed = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (!parsed.is_object()) {
        table.problems_.emplace_back("the translation is not a JSON object");
        return table;
    }
    for (const auto& [key, value] : parsed.items()) {
        if (value.is_string()) {
            table.texts_[key] = value.get<std::string>();
        } else {
            table.problems_.push_back("'" + key + "' is not a text");
        }
    }
    return table;
}

std::string Translation::tr(std::string_view key) const {
    const auto found = texts_.find(key);
    return found == texts_.end() ? std::string(key) : found->second;
}

std::string Translation::tr(std::string_view key, Args args) const {
    const auto found = texts_.find(key);
    return found == texts_.end() ? std::string(key) : fillPlaceholders(found->second, args);
}

bool Translation::has(std::string_view key) const {
    return texts_.find(key) != texts_.end();
}

std::set<std::string> Translation::keys() const {
    std::set<std::string> out;
    for (const auto& entry : texts_) {
        out.insert(entry.first);
    }
    return out;
}

} // namespace mm::core
