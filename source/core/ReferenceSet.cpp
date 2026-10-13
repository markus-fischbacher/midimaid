#include "core/ReferenceSet.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>

namespace mm::core {

namespace {

constexpr std::array<std::pair<std::string_view, TrackRole>, 4> kRoleNames{{{"drums", TrackRole::Drums},
                                                                            {"chords", TrackRole::Chords},
                                                                            {"bass", TrackRole::Bass},
                                                                            {"melody", TrackRole::Melody}}};

AnalysisSettings analysisSettingsOf(const StyleProfile& style) {
    AnalysisSettings settings;
    for (const WeightedId& scale : style.scales) {
        settings.candidateScales.push_back(scale.id);
        settings.scaleWeights.push_back(scale.weight);
    }
    return settings;
}

} // namespace

bool ReferenceItem::operator==(const ReferenceItem& other) const {
    return id == other.id && name == other.name && role == other.role && roleConfidence == other.roleConfidence && active == other.active &&
           weight == other.weight && tags == other.tags && lengthBars == other.lengthBars && notes == other.notes &&
           analysisToJson(analysis) == analysisToJson(other.analysis);
}

bool ReferenceSet::operator==(const ReferenceSet& other) const {
    return styleId == other.styleId && setId == other.setId && setName == other.setName && items == other.items;
}

ReferenceImportReport& ReferenceImportReport::operator+=(const ReferenceImportReport& other) {
    added += other.added;
    duplicates += other.duplicates;
    notFourFour += other.notFourFour;
    unreadable += other.unreadable;
    tooSmall += other.tooSmall;
    overLimit += other.overLimit;
    truncated += other.truncated;
    return *this;
}

std::string_view toString(TrackRole role) {
    for (const auto& [name, each] : kRoleNames) {
        if (each == role) {
            return name;
        }
    }
    return "melody";
}

std::optional<TrackRole> trackRoleFromString(std::string_view text) {
    for (const auto& [name, each] : kRoleNames) {
        if (name == text) {
            return each;
        }
    }
    return std::nullopt;
}

std::string fileHash(const uint8_t* data, size_t size) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 1099511628211ULL;
    }
    static const char* const digits = "0123456789abcdef";
    std::string text;
    for (int shift = 60; shift >= 0; shift -= 4) {
        text += digits[(hash >> shift) & 0xf];
    }
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += digits[(static_cast<uint64_t>(size) >> shift) & 0xf]; // the length: 8 digits, files stay below 4 GB
    }
    return text;
}

std::vector<ReferenceItem> itemsFromMidiFile(const std::string& fileName, const std::vector<uint8_t>& bytes,
                                             const StyleProfile& style, ReferenceImportReport& report) {
    std::vector<ReferenceItem> items;
    const auto read = readMidiFile(bytes);
    if (!read.ok()) {
        if (read.error == MidiReadError::NoNotes) {
            ++report.tooSmall;
        } else {
            ++report.unreadable;
        }
        return items;
    }
    if (!read.clip.onlyFourFour) {
        ++report.notFourFour;
        return items;
    }
    const std::string hash = fileHash(bytes.data(), bytes.size());
    std::map<uint8_t, std::vector<MidiClipNote>> byChannel;
    for (const MidiClipNote& note : read.clip.notes) {
        byChannel[note.channel].push_back(note);
    }
    const AnalysisSettings settings = analysisSettingsOf(style);
    for (auto& [channel, notes] : byChannel) {
        MidiClip clip = read.clip;
        clip.notes = notes;
        const ImportPlan plan = planImport(clip);
        if (plan.status != ImportStatus::Ok || plan.notes.size() < kMinReferenceNotes) {
            ++report.tooSmall;
            continue;
        }
        ReferenceItem item;
        item.id = hash + "-" + std::to_string(channel);
        item.name = byChannel.size() > 1 ? fileName + " (" + std::to_string(channel) + ")" : fileName;
        item.lengthBars = plan.lengthBars;
        item.notes = plan.notes;
        item.analysis = analyzeTrack(plan.notes, plan.lengthBars, settings);
        item.role = item.analysis.role; // the same detection, with its confidence
        item.roleConfidence = item.analysis.roleConfidence;
        if (plan.truncated) {
            ++report.truncated;
        }
        items.push_back(std::move(item));
    }
    return items;
}

bool containsFile(const ReferenceSet& set, const std::string& hash) {
    return std::any_of(set.items.begin(), set.items.end(),
                       [&](const ReferenceItem& item) { return item.id.compare(0, hash.size() + 1, hash + "-") == 0; });
}

void addItems(ReferenceSet& set, std::vector<ReferenceItem> items, ReferenceImportReport& report) {
    for (ReferenceItem& item : items) {
        const bool known = std::any_of(set.items.begin(), set.items.end(),
                                       [&](const ReferenceItem& each) { return each.id == item.id; });
        if (known) {
            ++report.duplicates;
        } else if (set.items.size() >= kMaxReferenceItems) {
            ++report.overLimit;
        } else {
            set.items.push_back(std::move(item));
            ++report.added;
        }
    }
}

namespace {

ReferenceItem* find(ReferenceSet& set, const std::string& id) {
    const auto it = std::find_if(set.items.begin(), set.items.end(), [&](const ReferenceItem& e) { return e.id == id; });
    return it == set.items.end() ? nullptr : &*it;
}

} // namespace

bool setItemActive(ReferenceSet& set, const std::string& id, bool active) {
    auto* item = find(set, id);
    if (item == nullptr) {
        return false;
    }
    item->active = active;
    return true;
}

bool setItemWeight(ReferenceSet& set, const std::string& id, int weight) {
    auto* item = find(set, id);
    if (item == nullptr) {
        return false;
    }
    item->weight = std::clamp(weight, kMinReferenceWeight, kMaxReferenceWeight);
    return true;
}

bool setItemRole(ReferenceSet& set, const std::string& id, TrackRole role) {
    auto* item = find(set, id);
    if (item == nullptr) {
        return false;
    }
    item->role = role;
    item->roleConfidence = 1.0; // the musician decided
    return true;
}

bool removeItem(ReferenceSet& set, const std::string& id) {
    const auto it = std::remove_if(set.items.begin(), set.items.end(), [&](const ReferenceItem& e) { return e.id == id; });
    const bool removed = it != set.items.end();
    set.items.erase(it, set.items.end());
    return removed;
}

size_t activeCount(const ReferenceSet& set) {
    return static_cast<size_t>(std::count_if(set.items.begin(), set.items.end(), [](const ReferenceItem& i) { return i.active; }));
}

size_t countOfRole(const ReferenceSet& set, TrackRole role) {
    return static_cast<size_t>(
        std::count_if(set.items.begin(), set.items.end(), [&](const ReferenceItem& i) { return i.role == role; }));
}

bool isRoleUnsure(const ReferenceItem& item) {
    return item.roleConfidence < kUnsureRoleConfidence;
}

size_t unsureRoleCount(const ReferenceSet& set) {
    return static_cast<size_t>(std::count_if(set.items.begin(), set.items.end(), isRoleUnsure));
}

std::string referenceSetToJson(const ReferenceSet& set) {
    using nlohmann::json;
    json root;
    root["version"] = kReferenceFormatVersion;
    root["style"] = set.styleId;
    json items = json::array();
    for (const ReferenceItem& item : set.items) {
        json notes = json::array();
        for (const MidiClipNote& note : item.notes) {
            notes.push_back({note.channel, note.pitch, note.velocity, note.startTick, note.lengthTicks});
        }
        items.push_back({{"id", item.id},
                         {"name", item.name},
                         {"role", std::string(toString(item.role))},
                         {"roleConfidence", item.roleConfidence},
                         {"active", item.active},
                         {"weight", item.weight},
                         {"bars", item.lengthBars},
                         {"notes", notes},
                         {"analysis", json::parse(analysisToJson(item.analysis))}});
    }
    root["items"] = items;
    return root.dump(1) + "\n";
}

ReferenceSet referenceSetFromJson(std::string_view text, const std::string& styleId, size_t* dropped, size_t limit) {
    using nlohmann::json;
    ReferenceSet set;
    set.styleId = styleId;
    size_t bad = 0;
    const json root = json::parse(text.begin(), text.end(), nullptr, false);
    const auto version = root.is_object() ? root.find("version") : root.end();
    if (!root.is_object() || version == root.end() || !version->is_number_integer() || version->get<int>() < 1 ||
        !root.contains("items") || !root["items"].is_array()) {
        if (dropped != nullptr) {
            *dropped = 0;
        }
        return set;
    }
    for (const json& entry : root["items"]) {
        try {
            ReferenceItem item;
            item.id = entry.at("id").get<std::string>();
            item.name = entry.at("name").get<std::string>();
            const auto role = trackRoleFromString(entry.at("role").get<std::string>());
            item.lengthBars = entry.at("bars").get<uint32_t>();
            const auto analysis = analysisFromJson(entry.at("analysis").dump());
            if (item.id.empty() || !role || !analysis || !isValidPatternLength(item.lengthBars) ||
                set.items.size() >= limit ||
                std::any_of(set.items.begin(), set.items.end(), [&](const ReferenceItem& e) { return e.id == item.id; })) {
                ++bad;
                continue;
            }
            item.role = *role;
            item.roleConfidence = entry.contains("roleConfidence") && entry["roleConfidence"].is_number() ? std::clamp(entry["roleConfidence"].get<double>(), 0.0, 1.0) : 1.0;
            item.analysis = *analysis;
            item.active = entry.at("active").get<bool>();
            item.weight = std::clamp(entry.at("weight").get<int>(), kMinReferenceWeight, kMaxReferenceWeight);
            const uint64_t end = static_cast<uint64_t>(item.lengthBars) * 3840;
            bool valid = true;
            for (const json& n : entry.at("notes")) {
                MidiClipNote note;
                const int channel = n.at(0).get<int>();
                const int pitch = n.at(1).get<int>();
                const int velocity = n.at(2).get<int>();
                const uint64_t start = n.at(3).get<uint64_t>();
                const uint64_t length = n.at(4).get<uint64_t>();
                if (channel < 1 || channel > 16 || pitch < 0 || pitch > 127 || velocity < 1 || velocity > 127 ||
                    length < 1 || start >= end) {
                    valid = false;
                    break;
                }
                note.channel = static_cast<uint8_t>(channel);
                note.pitch = static_cast<uint8_t>(pitch);
                note.velocity = static_cast<uint8_t>(velocity);
                note.startTick = static_cast<uint32_t>(start);
                note.lengthTicks = static_cast<uint32_t>(std::min<uint64_t>(length, end - start));
                item.notes.push_back(note);
            }
            if (!valid || item.notes.empty()) {
                ++bad;
                continue;
            }
            set.items.push_back(std::move(item));
        } catch (const json::exception&) {
            ++bad;
        }
    }
    if (dropped != nullptr) {
        *dropped = bad;
    }
    return set;
}

} // namespace mm::core
