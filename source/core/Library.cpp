#include "core/Library.h"

#include "core/MidiFile.h"
#include "core/PatternJson.h"
#include "core/PlaybackRender.h"
#include "core/Probability.h"
#include "core/SlotBankJson.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace mm::core {

using nlohmann::json;

namespace {

std::string cleanText(std::string_view text, size_t maxBytes) {
    std::string out;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) {
            out.push_back(c);
        }
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut; // never inside a UTF-8 character
        }
        out.resize(cut);
        const auto end = out.find_last_not_of(' ');
        out = end == std::string::npos ? std::string() : out.substr(0, end + 1);
    }
    return out;
}

void fillInfo(LibraryEntryInfo& info, const Pattern& pattern) {
    info.styleId = pattern.styleId;
    info.root = pattern.context.root;
    info.scaleId = pattern.context.scaleId;
    info.seed = pattern.info.seed;
    info.prompt = pattern.info.prompt;
    info.lengthBars = pattern.lengthBars;
}

const char* kindName(LibraryKind kind) {
    return kind == LibraryKind::Set ? "set" : "pattern";
}

} // namespace

bool isValidLibraryId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}

std::string sanitizeLibraryName(std::string_view name, std::string_view fallback) {
    auto cleaned = cleanText(name, kMaxLibraryNameBytes);
    return cleaned.empty() ? cleanText(fallback, kMaxLibraryNameBytes) : cleaned;
}

LibraryEntry makePatternEntry(std::string id, std::string_view name, const Pattern& pattern, std::string_view slotName,
                              uint8_t slotColor, int64_t createdUnixMs) {
    LibraryEntry entry;
    entry.info.id = std::move(id);
    entry.info.kind = LibraryKind::Pattern;
    entry.info.name = sanitizeLibraryName(name, pattern.styleId);
    fillInfo(entry.info, pattern);
    entry.info.slotCount = 1;
    entry.info.createdUnixMs = createdUnixMs;
    entry.pattern = pattern;
    entry.slotName = sanitizeSlotName(slotName);
    entry.slotColor = std::min(slotColor, kMaxColor);
    return entry;
}

std::optional<LibraryEntry> makeSetEntry(std::string id, std::string_view name, const SlotBank& bank,
                                         int64_t createdUnixMs) {
    LibraryEntry entry;
    entry.info.id = std::move(id);
    entry.info.kind = LibraryKind::Set;
    entry.info.slotCount = 0;
    bool first = true;
    SlotBank copy;
    for (size_t i = 0; i < kSlotCount; ++i) {
        const Slot* slot = bank.slot(i);
        if (slot == nullptr || !slot->pattern) {
            continue;
        }
        if (first) {
            fillInfo(entry.info, *slot->pattern);
            first = false;
        }
        ++entry.info.slotCount;
        Slot stored;
        stored.pattern = slot->pattern;
        stored.name = slot->name;
        stored.color = slot->color;
        copy.restore(i, std::move(stored));
    }
    if (first) {
        return std::nullopt;
    }
    entry.info.name = sanitizeLibraryName(name, entry.info.styleId);
    entry.info.createdUnixMs = createdUnixMs;
    entry.set = std::move(copy);
    return entry;
}

namespace {

std::string lowerCase(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

} // namespace

std::vector<std::string> sanitizeLibraryTags(std::vector<std::string> tags) {
    std::vector<std::string> result;
    for (auto& raw : tags) {
        std::string tag;
        for (const char ch : raw) {
            if (static_cast<unsigned char>(ch) >= 32 && ch != 127 && ch != ',' && ch != ';') {
                tag += ch;
            }
        }
        const auto first = tag.find_first_not_of(' ');
        if (first == std::string::npos) {
            continue;
        }
        tag = tag.substr(first, tag.find_last_not_of(' ') - first + 1);
        if (tag.size() > kMaxLibraryTagBytes) {
            size_t cut = kMaxLibraryTagBytes;
            while (cut > 0 && (static_cast<unsigned char>(tag[cut]) & 0xC0) == 0x80) {
                --cut; // not in the middle of a UTF-8 character
            }
            tag = tag.substr(0, cut);
        }
        if (tag.empty()) {
            continue;
        }
        const bool duplicate = std::any_of(result.begin(), result.end(), [&](const std::string& other) { return lowerCase(other) == lowerCase(tag); });
        if (!duplicate && result.size() < kMaxLibraryTags) {
            result.push_back(std::move(tag));
        }
    }
    return result;
}

std::vector<std::string> parseLibraryTags(std::string_view text) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : text) {
        if (ch == ',' || ch == ';') {
            parts.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    parts.push_back(current);
    return sanitizeLibraryTags(std::move(parts));
}

std::string libraryTagsToText(const std::vector<std::string>& tags) {
    std::string text;
    for (const auto& tag : tags) {
        text += (text.empty() ? "" : ", ") + tag;
    }
    return text;
}

LibraryMeta sanitizeLibraryMeta(LibraryMeta meta) {
    meta.tags = sanitizeLibraryTags(std::move(meta.tags));
    meta.rating = static_cast<uint8_t>(std::min<int>(meta.rating, 5));
    const auto inRange = [](int value) { return value >= kMinLibraryBpm && value <= kMaxLibraryBpm; };
    int low = meta.bpmLow;
    int high = meta.bpmHigh;
    if (!inRange(low) && !inRange(high)) {
        low = high = 0;
    } else if (!inRange(low)) {
        low = high;
    } else if (!inRange(high)) {
        high = low;
    }
    if (low > high) {
        std::swap(low, high);
    }
    meta.bpmLow = static_cast<uint16_t>(low);
    meta.bpmHigh = static_cast<uint16_t>(high);
    return meta;
}

LibraryMeta metaOf(const LibraryEntryInfo& info) {
    return {info.tags, info.rating, info.bpmLow, info.bpmHigh};
}

void setMeta(LibraryEntryInfo& info, const LibraryMeta& meta) {
    const LibraryMeta clean = sanitizeLibraryMeta(meta);
    info.tags = clean.tags;
    info.rating = clean.rating;
    info.bpmLow = clean.bpmLow;
    info.bpmHigh = clean.bpmHigh;
}

bool matchesFilter(const LibraryEntryInfo& info, const LibraryFilter& filter) {
    if (!filter.styleId.empty() && info.styleId != filter.styleId) {
        return false;
    }
    if (info.rating < filter.minRating) {
        return false;
    }
    if (filter.bpm.has_value() && (info.bpmHigh == 0 || *filter.bpm < info.bpmLow || *filter.bpm > info.bpmHigh)) {
        return false;
    }
    if (!filter.tag.empty()) {
        const std::string wanted = lowerCase(filter.tag);
        if (std::none_of(info.tags.begin(), info.tags.end(), [&](const std::string& tag) { return lowerCase(tag) == wanted; })) {
            return false;
        }
    }
    if (!filter.text.empty()) {
        const std::string wanted = lowerCase(filter.text);
        const auto has = [&](const std::string& field) { return lowerCase(field).find(wanted) != std::string::npos; };
        const bool found = has(info.name) || has(info.prompt) || has(info.styleId) ||
                           std::any_of(info.tags.begin(), info.tags.end(), has);
        if (!found) {
            return false;
        }
    }
    return true;
}

std::vector<LibraryEntryInfo> filterLibrary(const std::vector<LibraryEntryInfo>& entries, const LibraryFilter& filter) {
    std::vector<LibraryEntryInfo> result;
    for (const auto& entry : entries) {
        if (matchesFilter(entry, filter)) {
            result.push_back(entry);
        }
    }
    return result;
}

std::string libraryEntryToString(const LibraryEntry& entry) {
    const auto& i = entry.info;
    json root{{"libraryVersion", kLibraryEntryVersion},
              {"kind", kindName(i.kind)},
              {"name", i.name},
              {"style", i.styleId},
              {"root", static_cast<int>(i.root)},
              {"scale", i.scaleId},
              {"seed", i.seed},
              {"prompt", i.prompt},
              {"bars", i.lengthBars},
              {"slotCount", i.slotCount},
              {"created", i.createdUnixMs}};
    if (!i.tags.empty()) { // only when set: entries of before and entries without meta data stay as they are
        root["tags"] = i.tags;
    }
    if (i.rating > 0) {
        root["rating"] = static_cast<int>(i.rating);
    }
    if (i.bpmHigh > 0) {
        root["bpmLow"] = static_cast<int>(i.bpmLow);
        root["bpmHigh"] = static_cast<int>(i.bpmHigh);
    }
    if (i.kind == LibraryKind::Pattern && entry.pattern) {
        root["pattern"] = patternToJson(*entry.pattern);
        root["slotName"] = entry.slotName.value_or("");
        root["slotColor"] = static_cast<int>(entry.slotColor);
    } else if (i.kind == LibraryKind::Set && entry.set) {
        root["slots"] = slotBankToJson(*entry.set, 0);
    }
    return root.dump(1);
}

LibraryLoadResult loadLibraryEntry(std::string_view text, std::string_view idFromFileName) {
    LibraryLoadResult result;
    if (!isValidLibraryId(idFromFileName)) {
        result.error = "id: not a valid library id";
        return result;
    }
    const json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        result.error = "document: expected an object";
        return result;
    }
    const auto number = [&](const char* key, int64_t fallback) {
        const auto it = document.find(key);
        return it != document.end() && it->is_number_integer() ? it->get<int64_t>() : fallback;
    };
    const auto string = [&](const char* key) {
        const auto it = document.find(key);
        return it != document.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const int64_t version = number("libraryVersion", 0);
    if (version < 1) {
        result.error = "libraryVersion: missing or below 1";
        return result;
    }
    result.fromNewerVersion = version > kLibraryEntryVersion;

    LibraryEntry entry;
    auto& info = entry.info;
    info.id = std::string(idFromFileName);
    const auto kind = string("kind");
    if (kind != "pattern" && kind != "set") {
        result.error = "kind: expected \"pattern\" or \"set\"";
        return result;
    }
    info.kind = kind == "set" ? LibraryKind::Set : LibraryKind::Pattern;
    info.name = sanitizeLibraryName(string("name"), "?");
    info.styleId = string("style");
    info.root = static_cast<PitchClass>(std::clamp<int64_t>(number("root", 0), 0, 11));
    info.scaleId = string("scale");
    const auto seed = document.find("seed");
    info.seed = seed != document.end() && seed->is_number_unsigned() ? seed->get<uint64_t>() : 0;
    info.prompt = string("prompt");
    info.lengthBars = static_cast<uint32_t>(std::clamp<int64_t>(number("bars", 0), 0, 16));
    info.slotCount = static_cast<uint32_t>(std::clamp<int64_t>(number("slotCount", 1), 0, kSlotCount));
    info.createdUnixMs = number("created", 0);
    {
        LibraryMeta meta;
        if (const auto it = document.find("tags"); it != document.end() && it->is_array()) {
            for (const auto& tag : *it) {
                if (tag.is_string()) {
                    meta.tags.push_back(tag.get<std::string>());
                }
            }
        }
        meta.rating = static_cast<uint8_t>(std::clamp<int64_t>(number("rating", 0), 0, 5));
        meta.bpmLow = static_cast<uint16_t>(std::clamp<int64_t>(number("bpmLow", 0), 0, 65535));
        meta.bpmHigh = static_cast<uint16_t>(std::clamp<int64_t>(number("bpmHigh", 0), 0, 65535));
        setMeta(info, meta);
    }

    if (info.kind == LibraryKind::Pattern) {
        const auto it = document.find("pattern");
        if (it == document.end()) {
            result.error = "pattern: missing";
            return result;
        }
        auto loaded = loadPatternJson(*it);
        if (!loaded.ok()) {
            result.error = "pattern: " + loaded.error;
            return result;
        }
        result.fromNewerVersion = result.fromNewerVersion || loaded.fromNewerVersion;
        entry.pattern = std::move(*loaded.pattern);
        entry.slotName = sanitizeSlotName(string("slotName"));
        entry.slotColor = static_cast<uint8_t>(std::clamp<int64_t>(number("slotColor", 0), 0, kMaxColor));
    } else {
        const auto it = document.find("slots");
        if (it == document.end()) {
            result.error = "slots: missing";
            return result;
        }
        auto loaded = loadSlotBankJson(*it);
        if (!loaded.ok()) {
            result.error = "slots: " + loaded.error;
            return result;
        }
        result.fromNewerVersion = result.fromNewerVersion || loaded.fromNewerVersion;
        result.problems = std::move(loaded.problems);
        entry.set = std::move(*loaded.bank);
        uint32_t filled = 0;
        for (size_t i = 0; i < kSlotCount; ++i) {
            filled += entry.set->slot(i) != nullptr && entry.set->slot(i)->pattern ? 1 : 0;
        }
        if (filled == 0) {
            result.error = "slots: no pattern in the set";
            return result;
        }
        info.slotCount = filled;
    }
    result.entry = std::move(entry);
    return result;
}

std::vector<uint8_t> exportPatternMidi(const Pattern& pattern, const StyleProfile& style, double bpm) {
    std::vector<PatternNote> notes;
    for (size_t voice = 0; voice < pattern.voices.size(); ++voice) {
        const auto rendered = renderVoiceForPlayback(pattern, style, voice);
        notes.insert(notes.end(), rendered.notes.begin(), rendered.notes.end()); // already on the voice's channel
    }
    if (notes.empty()) {
        return {};
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const PatternNote& a, const PatternNote& b) { return a.startTick < b.startTick; });
    MidiFileOptions options;
    options.bpm = bpm;
    options.trackName = "MidiMaid";
    uint32_t lengthTicks = pattern.lengthBars * kTicksPerBar;
    notes = notesForExport(notes, lengthTicks, pattern.info.winnerSeed ^ pattern.info.seed); // passes resolved (D-233)
    return writeMidiFile(PatternView{notes.data(), notes.size(), lengthTicks}, options);
}

} // namespace mm::core
