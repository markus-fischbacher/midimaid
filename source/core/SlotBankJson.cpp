#include "core/SlotBankJson.h"

#include "core/JsonReader.h"
#include "core/PatternJson.h"

#include <algorithm>

namespace mm::core {

using detail::Reader;
using nlohmann::json;

json slotBankToJson(const SlotBank& bank, size_t historyLimit) {
    json slots = json::array();
    for (size_t i = 0; i < kSlotCount; ++i) {
        const Slot& slot = *bank.slot(i);
        const size_t dropped = slot.history.size() > historyLimit ? slot.history.size() - historyLimit : 0;
        json history = json::array();
        for (size_t k = dropped; k < slot.history.size(); ++k) {
            history.push_back(patternToJson(slot.history[k]));
        }
        slots.push_back(json{{"name", slot.name},
                             {"color", slot.color},
                             {"pattern", slot.pattern ? patternToJson(*slot.pattern) : json(nullptr)},
                             {"history", std::move(history)},
                             {"cursor", slot.cursor >= dropped ? slot.cursor - dropped : 0}});
    }
    return json{
        {"stateVersion", kSlotBankStateVersion}, {"nextVersion", bank.nextVersion()}, {"slots", std::move(slots)}};
}

std::string slotBankToString(const SlotBank& bank, size_t historyLimit) {
    return slotBankToJson(bank, historyLimit).dump(2);
}

namespace {

bool readPatternAt(const json& value, const std::string& path, Pattern& out, std::string& error) {
    const LoadResult loaded = loadPatternJson(value);
    if (!loaded.ok()) {
        error = path + ": " + loaded.error;
        return false;
    }
    out = *loaded.pattern;
    return true;
}

bool readSlot(const json& object, const std::string& path, Slot& slot, std::string& error) {
    if (!object.is_object()) {
        error = path + ": expected an object";
        return false;
    }
    Reader reader;
    std::string name;
    reader.stringValue(object, "name", path, false, name);
    reader.uintValue(object, "color", path, false, slot.color);
    if (reader.failed()) {
        error = reader.error();
        return false;
    }
    if (slot.color > kMaxColor) {
        error = Reader::child(path, "color") + ": out of range";
        return false;
    }
    slot.name = sanitizeSlotName(name);

    const auto pattern = object.find("pattern");
    if (pattern != object.end() && !pattern->is_null()) {
        Pattern loaded;
        if (!readPatternAt(*pattern, Reader::child(path, "pattern"), loaded, error)) {
            return false;
        }
        slot.pattern = std::move(loaded);
    }
    const json* history = reader.arrayMember(object, "history", path, false);
    if (reader.failed()) {
        error = reader.error();
        return false;
    }
    if (history != nullptr) {
        const std::string historyPath = Reader::child(path, "history");
        for (size_t i = 0; i < history->size() && i < kMaxHistory; ++i) {
            Pattern entry;
            if (!readPatternAt((*history)[i], Reader::element(historyPath, i), entry, error)) {
                return false;
            }
            slot.history.push_back(std::move(entry));
        }
    }
    size_t cursor = 0;
    reader.uintValue(object, "cursor", path, false, cursor);
    if (reader.failed()) {
        error = reader.error();
        return false;
    }
    slot.cursor = slot.history.empty() ? 0 : std::min(cursor, slot.history.size() - 1);
    return true;
}

} // namespace

SlotBankLoadResult loadSlotBankJson(const json& document) {
    SlotBankLoadResult result;
    if (!document.is_object()) {
        result.error = "document: expected an object";
        return result;
    }
    Reader reader;
    int version = 0;
    reader.intValue(document, "stateVersion", "", true, version);
    uint64_t nextVersion = 1;
    reader.uintValue(document, "nextVersion", "", false, nextVersion);
    const json* slots = reader.arrayMember(document, "slots", "", true);
    if (reader.failed()) {
        result.error = reader.error();
        return result;
    }
    if (version > kSlotBankStateVersion) {
        result.fromNewerVersion = true; // best effort: unknown fields are ignored
    } else if (version < kSlotBankStateVersion) {
        result.error = "stateVersion: unsupported version " + std::to_string(version);
        return result;
    }

    SlotBank bank;
    uint64_t highest = 0;
    for (size_t i = 0; i < slots->size() && i < kSlotCount; ++i) {
        Slot slot;
        std::string error;
        const std::string path = Reader::element("slots", i);
        if (!readSlot((*slots)[i], path, slot, error)) {
            result.problems.push_back(error);
            continue;
        }
        if (slot.pattern) {
            highest = std::max(highest, slot.pattern->version);
        }
        for (const Pattern& entry : slot.history) {
            highest = std::max(highest, entry.version);
        }
        bank.restore(i, std::move(slot));
    }
    bank.restoreNextVersion(std::max(nextVersion, highest + 1));
    result.bank = std::move(bank);
    return result;
}

SlotBankLoadResult loadSlotBank(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded()) {
        SlotBankLoadResult result;
        result.error = "document: not valid JSON";
        return result;
    }
    return loadSlotBankJson(document);
}

} // namespace mm::core
