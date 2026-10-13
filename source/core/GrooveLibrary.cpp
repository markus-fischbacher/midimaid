#include "core/GrooveLibrary.h"

#include "core/Random.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mm::core {

namespace {

using nlohmann::json;

constexpr int32_t kMaxOffsetTicks = 240;
constexpr int32_t kMaxHumanizeTicks = 60;
constexpr int32_t kMaxHumanizeVelocity = 200;

bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 40 &&
           std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

GrooveTemplateLoad fail(std::string error) {
    GrooveTemplateLoad result;
    result.error = std::move(error);
    return result;
}

uint64_t hashOf(uint64_t seed, uint32_t noteId, std::string_view templateId, uint64_t salt) {
    uint64_t h = deriveSeed(seed, noteId);
    for (const char c : templateId) {
        h = deriveSeed(h, static_cast<uint64_t>(static_cast<unsigned char>(c)));
    }
    return deriveSeed(h, salt);
}

int32_t spread(uint64_t hash, int32_t limit) {
    if (limit <= 0) {
        return 0;
    }
    const auto width = static_cast<uint64_t>(2 * limit + 1);
    return static_cast<int32_t>(hash % width) - limit;
}

} // namespace

GrooveTemplateLoad loadGrooveTemplate(std::string_view text) {
    const json root = json::parse(text.begin(), text.end(), nullptr, false);
    if (!root.is_object()) {
        return fail("$: not a JSON object");
    }
    GrooveTemplate groove;
    const auto id = root.find("id");
    if (id == root.end() || !id->is_string() || !validId(id->get<std::string>())) {
        return fail("$.id: missing or not lower case letters, digits and underscore");
    }
    groove.id = id->get<std::string>();
    const auto name = root.find("nameDe");
    if (name == root.end() || !name->is_string() || name->get<std::string>().empty()) {
        return fail("$.nameDe: missing");
    }
    groove.nameDe = name->get<std::string>();
    if (const auto english = root.find("nameEn"); english != root.end() && english->is_string()) {
        groove.nameEn = english->get<std::string>();
    }
    const auto bars = root.find("bars");
    if (bars == root.end() || !bars->is_number_integer() || (bars->get<long long>() != 1 && bars->get<long long>() != 2)) {
        return fail("$.bars: must be 1 or 2");
    }
    groove.bars = static_cast<uint32_t>(bars->get<long long>());
    const auto read = [&](const char* key, long long low, long long high, std::vector<int32_t>& out) -> std::string {
        const auto it = root.find(key);
        if (it == root.end() || !it->is_array() || it->size() != groove.steps()) {
            return std::string("$.") + key + ": must be a list of " + std::to_string(groove.steps()) + " numbers";
        }
        for (const auto& value : *it) {
            if (!value.is_number_integer() || value.get<long long>() < low || value.get<long long>() > high) {
                return std::string("$.") + key + ": a value is not a whole number from " + std::to_string(low) + " to " +
                       std::to_string(high);
            }
            out.push_back(static_cast<int32_t>(value.get<long long>()));
        }
        return {};
    };
    if (auto error = read("offsets", -kMaxOffsetTicks, kMaxOffsetTicks, groove.offsetTicks); !error.empty()) {
        return fail(std::move(error));
    }
    if (auto error = read("velocity", 500, 1500, groove.velocityPermille); !error.empty()) {
        return fail(std::move(error));
    }
    if (const auto swing = root.find("swing"); swing != root.end()) {
        if (!swing->is_boolean()) {
            return fail("$.swing: must be true or false");
        }
        groove.swing = swing->get<bool>();
    }
    if (const auto human = root.find("humanize"); human != root.end()) {
        if (!human->is_object()) {
            return fail("$.humanize: must be an object");
        }
        const auto ticks = human->find("ticks");
        const auto velocity = human->find("velocity");
        if (ticks != human->end()) {
            if (!ticks->is_number_integer() || ticks->get<long long>() < 0 || ticks->get<long long>() > kMaxHumanizeTicks) {
                return fail("$.humanize.ticks: must be a whole number from 0 to 60");
            }
            groove.humanizeTicks = static_cast<int32_t>(ticks->get<long long>());
        }
        if (velocity != human->end()) {
            if (!velocity->is_number_integer() || velocity->get<long long>() < 0 ||
                velocity->get<long long>() > kMaxHumanizeVelocity) {
                return fail("$.humanize.velocity: must be a whole number from 0 to 200");
            }
            groove.humanizeVelocityPermille = static_cast<int32_t>(velocity->get<long long>());
        }
    }
    GrooveTemplateLoad result;
    result.groove = std::move(groove);
    return result;
}

GrooveLibrary GrooveLibrary::fromTexts(const std::vector<std::string_view>& texts) {
    GrooveLibrary library;
    for (size_t i = 0; i < texts.size(); ++i) {
        auto loaded = loadGrooveTemplate(texts[i]);
        if (!loaded.ok()) {
            library.problems_.push_back("groove " + std::to_string(i) + ": " + loaded.error);
        } else if (library.find(loaded.groove->id) != nullptr) {
            library.problems_.push_back("groove " + std::to_string(i) + ": duplicate id " + loaded.groove->id);
        } else {
            library.templates_.push_back(std::move(*loaded.groove));
        }
    }
    // a stable order for the pick list whatever the order the resources come in
    std::stable_sort(library.templates_.begin(), library.templates_.end(),
                     [](const GrooveTemplate& a, const GrooveTemplate& b) { return a.id < b.id; });
    return library;
}

GrooveLibrary GrooveLibrary::withTexts(const GrooveLibrary& base, const std::vector<std::string_view>& texts) {
    GrooveLibrary library = base;
    std::vector<GrooveTemplate> added;
    for (size_t i = 0; i < texts.size(); ++i) {
        auto loaded = loadGrooveTemplate(texts[i]);
        const bool taken = loaded.ok() && (library.find(loaded.groove->id) != nullptr ||
                                           std::any_of(added.begin(), added.end(), [&](const GrooveTemplate& other) {
                                               return other.id == loaded.groove->id;
                                           }));
        if (!loaded.ok()) {
            library.problems_.push_back("own groove " + std::to_string(i) + ": " + loaded.error);
        } else if (taken) {
            library.problems_.push_back("own groove " + std::to_string(i) + ": duplicate id " + loaded.groove->id);
        } else {
            added.push_back(std::move(*loaded.groove));
        }
    }
    std::stable_sort(added.begin(), added.end(), [](const GrooveTemplate& a, const GrooveTemplate& b) { return a.id < b.id; });
    for (auto& groove : added) {
        library.templates_.push_back(std::move(groove));
    }
    return library;
}

std::string grooveTemplateToJson(const GrooveTemplate& groove) {
    json root;
    root["id"] = groove.id;
    root["nameDe"] = groove.nameDe;
    if (!groove.nameEn.empty()) {
        root["nameEn"] = groove.nameEn;
    }
    root["bars"] = groove.bars;
    root["offsets"] = groove.offsetTicks;
    root["velocity"] = groove.velocityPermille;
    root["swing"] = groove.swing;
    if (groove.humanizeTicks > 0 || groove.humanizeVelocityPermille > 0) {
        root["humanize"] = {{"ticks", groove.humanizeTicks}, {"velocity", groove.humanizeVelocityPermille}};
    }
    return root.dump(2);
}

std::string grooveIdFromName(std::string_view name, const GrooveLibrary& library) {
    std::string slug;
    const auto add = [&slug](const char* text) { slug += text; };
    for (size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c >= 'A' && c <= 'Z') {
            slug += static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            slug += static_cast<char>(c);
        } else if (c == 0xC3 && i + 1 < name.size()) { // the umlauts of German names (UTF-8)
            const auto next = static_cast<unsigned char>(name[++i]);
            if (next == 0xA4 || next == 0x84) {
                add("ae");
            } else if (next == 0xB6 || next == 0x96) {
                add("oe");
            } else if (next == 0xBC || next == 0x9C) {
                add("ue");
            } else if (next == 0x9F) {
                add("ss");
            }
        } else if (!slug.empty() && slug.back() != '_') {
            slug += '_'; // a space or a sign (a letter of another script is a gap)
        }
    }
    while (!slug.empty() && slug.back() == '_') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = "groove";
    }
    const std::string base = "user_" + slug.substr(0, 30);
    std::string id = base;
    for (int n = 2; library.find(id) != nullptr; ++n) {
        id = base + "_" + std::to_string(n);
    }
    return id;
}

const GrooveTemplate* GrooveLibrary::find(std::string_view id) const {
    for (const auto& groove : templates_) {
        if (groove.id == id) {
            return &groove;
        }
    }
    return nullptr;
}

int32_t humanizeOffset(uint64_t seed, uint32_t noteId, std::string_view templateId, int32_t limit) {
    return spread(hashOf(seed, noteId, templateId, 1), std::clamp(limit, 0, kMaxHumanizeTicks));
}

int32_t humanizeVelocity(uint64_t seed, uint32_t noteId, std::string_view templateId, int32_t limit) {
    return spread(hashOf(seed, noteId, templateId, 2), std::clamp(limit, 0, kMaxHumanizeVelocity));
}

} // namespace mm::core
