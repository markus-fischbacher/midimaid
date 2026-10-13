#include "core/LibraryIndex.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>

namespace mm::core {

using nlohmann::json;

std::string libraryIndexToString(std::vector<LibraryIndexRecord> records) {
    std::sort(records.begin(), records.end(),
              [](const LibraryIndexRecord& a, const LibraryIndexRecord& b) { return a.info.id < b.info.id; });
    json entries = json::array();
    for (const auto& record : records) {
        const auto& i = record.info;
        entries.push_back({{"id", i.id},
                           {"kind", i.kind == LibraryKind::Set ? "set" : "pattern"},
                           {"name", i.name},
                           {"style", i.styleId},
                           {"root", static_cast<int>(i.root)},
                           {"scale", i.scaleId},
                           {"seed", i.seed},
                           {"prompt", i.prompt},
                           {"bars", i.lengthBars},
                           {"slotCount", i.slotCount},
                           {"created", i.createdUnixMs},
                           {"size", record.fileSize},
                           {"modified", record.modifiedMs}});
        if (!i.tags.empty()) {
            entries.back()["tags"] = i.tags;
        }
        if (i.rating > 0) {
            entries.back()["rating"] = static_cast<int>(i.rating);
        }
        if (i.bpmHigh > 0) {
            entries.back()["bpmLow"] = static_cast<int>(i.bpmLow);
            entries.back()["bpmHigh"] = static_cast<int>(i.bpmHigh);
        }
    }
    return json{{"indexVersion", kLibraryIndexVersion}, {"entries", std::move(entries)}}.dump(1);
}

LibraryIndexLoad loadLibraryIndex(std::string_view text) {
    LibraryIndexLoad result;
    const json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        return result;
    }
    const auto version = document.find("indexVersion");
    if (version == document.end() || !version->is_number_integer() || version->get<int64_t>() < 1 ||
        version->get<int64_t>() > kLibraryIndexVersion) {
        return result;
    }
    const auto entries = document.find("entries");
    if (entries == document.end() || !entries->is_array()) {
        return result;
    }
    for (const auto& item : *entries) {
        const auto str = [&](const char* key) -> const json* {
            const auto it = item.find(key);
            return it != item.end() && it->is_string() ? &*it : nullptr;
        };
        const auto integer = [&](const char* key) -> const json* {
            const auto it = item.find(key);
            return it != item.end() && it->is_number_integer() ? &*it : nullptr;
        };
        LibraryIndexRecord record;
        const json* id = item.is_object() ? str("id") : nullptr;
        const json* kind = item.is_object() ? str("kind") : nullptr;
        const json* name = item.is_object() ? str("name") : nullptr;
        const json* size = item.is_object() ? integer("size") : nullptr;
        const json* modified = item.is_object() ? integer("modified") : nullptr;
        if (id == nullptr || kind == nullptr || name == nullptr || size == nullptr || modified == nullptr ||
            !isValidLibraryId(id->get<std::string>()) || (kind->get<std::string>() != "pattern" && kind->get<std::string>() != "set")) {
            ++result.skipped;
            continue;
        }
        auto& info = record.info;
        info.id = id->get<std::string>();
        info.kind = kind->get<std::string>() == "set" ? LibraryKind::Set : LibraryKind::Pattern;
        info.name = sanitizeLibraryName(name->get<std::string>(), "?");
        if (const auto* v = str("style")) {
            info.styleId = v->get<std::string>();
        }
        if (const auto* v = str("scale")) {
            info.scaleId = v->get<std::string>();
        }
        if (const auto* v = str("prompt")) {
            info.prompt = v->get<std::string>();
        }
        if (const auto* v = integer("root")) {
            info.root = static_cast<PitchClass>(std::clamp<int64_t>(v->get<int64_t>(), 0, 11));
        }
        if (const auto it = item.find("seed"); it != item.end() && it->is_number_unsigned()) {
            info.seed = it->get<uint64_t>();
        }
        if (const auto* v = integer("bars")) {
            info.lengthBars = static_cast<uint32_t>(std::clamp<int64_t>(v->get<int64_t>(), 0, 16));
        }
        if (const auto* v = integer("slotCount")) {
            info.slotCount = static_cast<uint32_t>(std::clamp<int64_t>(v->get<int64_t>(), 0, kSlotCount));
        }
        if (const auto* v = integer("created")) {
            info.createdUnixMs = v->get<int64_t>();
        }
        {
            LibraryMeta meta;
            if (const auto it = item.find("tags"); it != item.end() && it->is_array()) {
                for (const auto& tag : *it) {
                    if (tag.is_string()) {
                        meta.tags.push_back(tag.get<std::string>());
                    }
                }
            }
            if (const auto* v = integer("rating")) {
                meta.rating = static_cast<uint8_t>(std::clamp<int64_t>(v->get<int64_t>(), 0, 5));
            }
            if (const auto* v = integer("bpmLow")) {
                meta.bpmLow = static_cast<uint16_t>(std::clamp<int64_t>(v->get<int64_t>(), 0, 65535));
            }
            if (const auto* v = integer("bpmHigh")) {
                meta.bpmHigh = static_cast<uint16_t>(std::clamp<int64_t>(v->get<int64_t>(), 0, 65535));
            }
            setMeta(info, meta);
        }
        record.fileSize = size->get<int64_t>();
        record.modifiedMs = modified->get<int64_t>();
        result.records.push_back(std::move(record));
    }
    result.ok = true;
    return result;
}

LibraryReconcile reconcileLibraryIndex(const std::vector<LibraryIndexRecord>& records,
                                       const std::vector<LibraryFileState>& files) {
    LibraryReconcile result;
    std::map<std::string, const LibraryIndexRecord*> byId;
    for (const auto& record : records) {
        byId.emplace(record.info.id, &record); // the first record of an id counts
    }
    std::map<std::string, bool> seen;
    for (const auto& file : files) {
        if (seen[file.id]) {
            continue;
        }
        seen[file.id] = true;
        const auto it = byId.find(file.id);
        if (it != byId.end() && it->second->fileSize == file.fileSize && it->second->modifiedMs == file.modifiedMs) {
            result.trusted.push_back(*it->second);
        } else {
            result.toRead.push_back(file.id);
        }
    }
    for (const auto& [id, record] : byId) {
        if (!seen[id]) {
            ++result.dropped;
        }
    }
    std::sort(result.toRead.begin(), result.toRead.end());
    std::sort(result.trusted.begin(), result.trusted.end(),
              [](const LibraryIndexRecord& a, const LibraryIndexRecord& b) { return a.info.id < b.info.id; });
    return result;
}

} // namespace mm::core
