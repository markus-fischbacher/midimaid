#include "core/ReferenceCollection.h"

#include "core/Library.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace mm::core {

namespace {

AnalysisSettings analysisSettingsOf(const StyleProfile& style) {
    AnalysisSettings settings;
    for (const WeightedId& scale : style.scales) {
        settings.candidateScales.push_back(scale.id);
        settings.scaleWeights.push_back(scale.weight);
    }
    return settings;
}

NamedReferenceSet* mutableSet(ReferenceCollection& collection, std::string_view id) {
    const auto it = std::find_if(collection.sets.begin(), collection.sets.end(),
                                 [&](const NamedReferenceSet& s) { return s.id == id; });
    return it == collection.sets.end() ? nullptr : &*it;
}

ReferenceMember* findMember(ReferenceCollection& collection, std::string_view setId, std::string_view itemId) {
    auto* set = mutableSet(collection, setId);
    if (set == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(set->members.begin(), set->members.end(),
                                 [&](const ReferenceMember& m) { return m.id == itemId; });
    return it == set->members.end() ? nullptr : &*it;
}

bool poolHas(const ReferenceCollection& collection, std::string_view itemId) {
    return std::any_of(collection.pool.begin(), collection.pool.end(),
                       [&](const ReferenceItem& i) { return i.id == itemId; });
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool nameTaken(const ReferenceCollection& collection, std::string_view name, std::string_view exceptId) {
    const auto wanted = lower(name);
    return std::any_of(collection.sets.begin(), collection.sets.end(), [&](const NamedReferenceSet& s) {
        return s.id != exceptId && s.id != kFavoritesSetId && lower(s.name) == wanted;
    });
}

} // namespace

bool ReferenceCollection::operator==(const ReferenceCollection& other) const {
    return styleId == other.styleId && pool == other.pool && sets == other.sets && activeSetId == other.activeSetId;
}

void normalize(ReferenceCollection& collection) {
    // the ids of the sets are unique; the favorites come first and exist
    std::vector<NamedReferenceSet> sets;
    NamedReferenceSet favorites;
    favorites.id = kFavoritesSetId;
    for (auto& set : collection.sets) {
        const bool duplicate = std::any_of(sets.begin(), sets.end(), [&](const auto& s) { return s.id == set.id; }) ||
                               (set.id == kFavoritesSetId && !favorites.members.empty());
        if (set.id.empty() || duplicate) {
            continue;
        }
        if (set.id == kFavoritesSetId) {
            favorites = std::move(set);
            favorites.name.clear();
        } else if (sets.size() + 1 < kMaxReferenceSets) {
            sets.push_back(std::move(set));
        }
    }
    sets.insert(sets.begin(), std::move(favorites));
    collection.sets = std::move(sets);
    // members: an item of the pool, once per set, within the limit
    for (auto& set : collection.sets) {
        std::vector<ReferenceMember> members;
        for (auto& member : set.members) {
            if (poolHas(collection, member.id) && members.size() < kMaxReferenceItems &&
                std::none_of(members.begin(), members.end(), [&](const auto& m) { return m.id == member.id; })) {
                member.weight = std::clamp(member.weight, kMinReferenceWeight, kMaxReferenceWeight);
                member.tags = sanitizeLibraryTags(std::move(member.tags));
                member.tags.resize(std::min(member.tags.size(), kMaxMemberTags));
                members.push_back(std::move(member));
            }
        }
        set.members = std::move(members);
    }
    // an item that no set holds is lost
    collection.pool.erase(std::remove_if(collection.pool.begin(), collection.pool.end(),
                                         [&](const ReferenceItem& item) {
                                             return std::none_of(collection.sets.begin(), collection.sets.end(),
                                                                 [&](const NamedReferenceSet& s) {
                                                                     return std::any_of(s.members.begin(), s.members.end(),
                                                                                        [&](const auto& m) { return m.id == item.id; });
                                                                 });
                                         }),
                          collection.pool.end());
    if (findSet(collection, collection.activeSetId) == nullptr) {
        collection.activeSetId = collection.sets.size() > 1 ? collection.sets[1].id : std::string(kFavoritesSetId);
    }
}

const NamedReferenceSet* findSet(const ReferenceCollection& collection, std::string_view setId) {
    const auto it = std::find_if(collection.sets.begin(), collection.sets.end(),
                                 [&](const NamedReferenceSet& s) { return s.id == setId; });
    return it == collection.sets.end() ? nullptr : &*it;
}

ReferenceSet viewOf(const ReferenceCollection& collection, std::string_view setId) {
    ReferenceSet view;
    view.styleId = collection.styleId;
    view.setId = std::string(setId);
    const auto* set = findSet(collection, setId);
    if (set == nullptr) {
        return view;
    }
    view.setName = set->name;
    for (const auto& member : set->members) {
        const auto it = std::find_if(collection.pool.begin(), collection.pool.end(),
                                     [&](const ReferenceItem& i) { return i.id == member.id; });
        if (it != collection.pool.end()) {
            ReferenceItem item = *it;
            item.active = member.active;
            item.weight = member.weight;
            item.tags = member.tags;
            view.items.push_back(std::move(item));
        }
    }
    return view;
}

ReferenceSet activeView(const ReferenceCollection& collection) {
    return viewOf(collection, collection.activeSetId);
}

std::string cleanSetName(std::string_view name) {
    return sanitizeLibraryName(name.substr(0, std::min<size_t>(name.size(), 4 * kMaxReferenceSetNameBytes)))
        .substr(0, kMaxReferenceSetNameBytes);
}

std::string createSet(ReferenceCollection& collection, std::string_view name) {
    const auto clean = cleanSetName(name);
    if (clean.empty() || nameTaken(collection, clean, {}) || collection.sets.size() >= kMaxReferenceSets) {
        return {};
    }
    int number = 1;
    while (findSet(collection, "s" + std::to_string(number)) != nullptr) {
        ++number;
    }
    NamedReferenceSet set;
    set.id = "s" + std::to_string(number);
    set.name = clean;
    collection.sets.push_back(set);
    normalize(collection);
    return set.id;
}

bool renameSet(ReferenceCollection& collection, std::string_view setId, std::string_view name) {
    auto* set = mutableSet(collection, setId);
    const auto clean = cleanSetName(name);
    if (set == nullptr || setId == kFavoritesSetId || clean.empty() || nameTaken(collection, clean, setId)) {
        return false;
    }
    set->name = clean;
    return true;
}

bool deleteSet(ReferenceCollection& collection, std::string_view setId) {
    if (setId == kFavoritesSetId || mutableSet(collection, setId) == nullptr) {
        return false;
    }
    collection.sets.erase(std::remove_if(collection.sets.begin(), collection.sets.end(),
                                         [&](const NamedReferenceSet& s) { return s.id == setId; }),
                          collection.sets.end());
    normalize(collection);
    return true;
}

bool setActiveSet(ReferenceCollection& collection, std::string_view setId) {
    if (findSet(collection, setId) == nullptr) {
        return false;
    }
    collection.activeSetId = std::string(setId);
    return true;
}

bool setMemberActive(ReferenceCollection& collection, std::string_view setId, std::string_view itemId, bool active) {
    auto* member = findMember(collection, setId, itemId);
    if (member == nullptr) {
        return false;
    }
    member->active = active;
    return true;
}

bool setMemberWeight(ReferenceCollection& collection, std::string_view setId, std::string_view itemId, int weight) {
    auto* member = findMember(collection, setId, itemId);
    if (member == nullptr) {
        return false;
    }
    member->weight = std::clamp(weight, kMinReferenceWeight, kMaxReferenceWeight);
    return true;
}

bool setMemberTags(ReferenceCollection& collection, std::string_view setId, std::string_view itemId,
                   std::vector<std::string> tags) {
    auto* member = findMember(collection, setId, itemId);
    if (member == nullptr) {
        return false;
    }
    member->tags = sanitizeLibraryTags(std::move(tags));
    member->tags.resize(std::min(member->tags.size(), kMaxMemberTags));
    return true;
}

bool setItemRole(ReferenceCollection& collection, std::string_view itemId, TrackRole role) {
    const auto it = std::find_if(collection.pool.begin(), collection.pool.end(),
                                 [&](const ReferenceItem& i) { return i.id == itemId; });
    if (it == collection.pool.end()) {
        return false;
    }
    it->role = role;
    it->roleConfidence = 1.0; // the musician decided
    return true;
}

bool addMember(ReferenceCollection& collection, std::string_view setId, std::string_view itemId) {
    auto* set = mutableSet(collection, setId);
    if (set == nullptr || !poolHas(collection, itemId) || set->members.size() >= kMaxReferenceItems ||
        findMember(collection, setId, itemId) != nullptr) {
        return false;
    }
    ReferenceMember member;
    member.id = std::string(itemId);
    set->members.push_back(std::move(member));
    return true;
}

bool removeMember(ReferenceCollection& collection, std::string_view setId, std::string_view itemId) {
    auto* set = mutableSet(collection, setId);
    if (set == nullptr) {
        return false;
    }
    const auto it = std::remove_if(set->members.begin(), set->members.end(),
                                   [&](const ReferenceMember& m) { return m.id == itemId; });
    const bool removed = it != set->members.end();
    set->members.erase(it, set->members.end());
    if (removed) {
        normalize(collection);
    }
    return removed;
}

bool poolContainsFile(const ReferenceCollection& collection, const std::string& hash) {
    return std::any_of(collection.pool.begin(), collection.pool.end(),
                       [&](const ReferenceItem& item) { return item.id.compare(0, hash.size() + 1, hash + "-") == 0; });
}

void addKnownFile(ReferenceCollection& collection, std::string_view setId, const std::string& hash,
                  ReferenceImportReport& report) {
    std::vector<std::string> ids;
    for (const auto& item : collection.pool) {
        if (item.id.compare(0, hash.size() + 1, hash + "-") == 0) {
            ids.push_back(item.id);
        }
    }
    for (const auto& id : ids) {
        if (findMember(collection, setId, id) != nullptr) {
            ++report.duplicates;
        } else if (addMember(collection, setId, id)) {
            ++report.added;
        } else {
            ++report.overLimit;
        }
    }
}

void addItems(ReferenceCollection& collection, std::string_view setId, std::vector<ReferenceItem> items,
              ReferenceImportReport& report) {
    for (ReferenceItem& item : items) {
        if (findMember(collection, setId, item.id) != nullptr) {
            ++report.duplicates;
            continue;
        }
        const auto* set = findSet(collection, setId);
        if (set == nullptr || set->members.size() >= kMaxReferenceItems ||
            (!poolHas(collection, item.id) && collection.pool.size() >= kMaxPoolItems)) {
            ++report.overLimit;
            continue;
        }
        const std::string id = item.id;
        if (!poolHas(collection, id)) {
            item.active = true;
            item.weight = kDefaultReferenceWeight;
            collection.pool.push_back(std::move(item));
        }
        addMember(collection, setId, id);
        ++report.added;
    }
}

std::string favoriteItemId(std::string_view entryId, size_t voiceIndex) {
    return std::string(kFavoriteIdPrefix) + std::string(entryId) + ":" + std::to_string(voiceIndex);
}

bool isFavoriteItem(std::string_view itemId) {
    return itemId.substr(0, std::string_view(kFavoriteIdPrefix).size()) == kFavoriteIdPrefix;
}

std::vector<ReferenceItem> favoriteItemsOf(const Pattern& pattern, std::string_view entryId, const std::string& name,
                                           const StyleProfile& style) {
    std::vector<ReferenceItem> items;
    const AnalysisSettings settings = analysisSettingsOf(style);
    for (size_t v = 0; v < pattern.voices.size(); ++v) {
        const Track& voice = pattern.voices[v];
        std::vector<MidiClipNote> notes;
        for (const Note& note : voice.notes) {
            MidiClipNote out;
            out.channel = std::clamp<uint8_t>(voice.midiChannel, 1, 16);
            out.pitch = note.pitch;
            out.velocity = std::clamp<uint8_t>(note.velocity, 1, 127);
            out.startTick = note.startTick;
            out.lengthTicks = std::max<uint32_t>(note.lengthTicks, 1);
            notes.push_back(out);
        }
        if (notes.size() < kMinReferenceNotes) {
            continue;
        }
        ReferenceItem item;
        item.id = favoriteItemId(entryId, v);
        item.name = pattern.voices.size() > 1 ? name + " (" + std::to_string(v + 1) + ")" : name;
        item.role = voice.role == VoiceRole::Bass ? TrackRole::Bass : TrackRole::Melody;
        item.lengthBars = pattern.lengthBars;
        item.analysis = analyzeTrack(notes, pattern.lengthBars, settings);
        item.notes = std::move(notes);
        items.push_back(std::move(item));
    }
    return items;
}

void removeFavorite(ReferenceCollection& collection, std::string_view entryId) {
    const std::string prefix = std::string(kFavoriteIdPrefix) + std::string(entryId) + ":";
    for (auto& set : collection.sets) {
        set.members.erase(std::remove_if(set.members.begin(), set.members.end(),
                                         [&](const ReferenceMember& m) { return m.id.compare(0, prefix.size(), prefix) == 0; }),
                          set.members.end());
    }
    normalize(collection);
}

void setFavorite(ReferenceCollection& collection, std::string_view entryId, std::vector<ReferenceItem> items,
                 bool alsoActive) {
    // what the musician set on an item (weight, switch, tags in a set) stays when the entry is saved again
    std::vector<std::pair<std::string, ReferenceMember>> kept;
    const std::string prefix = std::string(kFavoriteIdPrefix) + std::string(entryId) + ":";
    for (const auto& set : collection.sets) {
        for (const auto& member : set.members) {
            if (member.id.compare(0, prefix.size(), prefix) == 0) {
                kept.emplace_back(set.id, member);
            }
        }
    }
    removeFavorite(collection, entryId);
    for (auto& item : items) {
        const std::string id = item.id;
        item.active = true;
        item.weight = kDefaultReferenceWeight;
        if (!poolHas(collection, id) && collection.pool.size() < kMaxPoolItems) {
            collection.pool.push_back(std::move(item));
        }
        const auto restore = [&](std::string_view setId) {
            if (addMember(collection, setId, id)) {
                const auto old = std::find_if(kept.begin(), kept.end(),
                                              [&](const auto& k) { return k.first == setId && k.second.id == id; });
                if (old != kept.end()) {
                    *findMember(collection, setId, id) = old->second;
                }
            }
        };
        restore(kFavoritesSetId);
        if (alsoActive && collection.activeSetId != kFavoritesSetId) {
            restore(collection.activeSetId);
        }
    }
    normalize(collection);
}

std::string referenceCollectionToJson(const ReferenceCollection& collection) {
    using nlohmann::json;
    ReferenceSet pool;
    pool.styleId = collection.styleId;
    pool.items = collection.pool;
    json root;
    root["version"] = 2;
    root["style"] = collection.styleId;
    root["active"] = collection.activeSetId;
    root["pool"] = json::parse(referenceSetToJson(pool));
    json sets = json::array();
    for (const auto& set : collection.sets) {
        json members = json::array();
        for (const auto& member : set.members) {
            members.push_back({{"id", member.id}, {"active", member.active}, {"weight", member.weight}, {"tags", member.tags}});
        }
        sets.push_back({{"id", set.id}, {"name", set.name}, {"members", members}});
    }
    root["sets"] = sets;
    return root.dump(1) + "\n";
}

ReferenceCollection referenceCollectionFromJson(std::string_view text, const std::string& styleId, size_t* dropped) {
    using nlohmann::json;
    ReferenceCollection collection;
    collection.styleId = styleId;
    size_t bad = 0;
    const json root = json::parse(text.begin(), text.end(), nullptr, false);
    const auto version = root.is_object() ? root.find("version") : root.end();
    if (version != root.end() && version->is_number_integer() && version->get<int>() == 1) {
        size_t items = 0;
        const ReferenceSet old = referenceSetFromJson(text, styleId, &items);
        bad = items;
        NamedReferenceSet set;
        set.id = "default";
        set.name = "Standard";
        for (const auto& item : old.items) {
            ReferenceItem pooled = item;
            pooled.active = true;
            pooled.weight = kDefaultReferenceWeight;
            collection.pool.push_back(pooled);
            ReferenceMember member;
            member.id = item.id;
            member.active = item.active;
            member.weight = item.weight;
            set.members.push_back(member);
        }
        if (!set.members.empty()) {
            collection.sets.push_back(std::move(set));
            collection.activeSetId = "default";
        }
    } else if (version != root.end() && version->is_number_integer() && version->get<int>() >= 2 &&
               root.contains("pool") && root.contains("sets") && root["sets"].is_array()) {
        size_t items = 0;
        collection.pool = referenceSetFromJson(root["pool"].dump(), styleId, &items, kMaxPoolItems).items;
        bad = items;
        for (const json& entry : root["sets"]) {
            try {
                NamedReferenceSet set;
                set.id = entry.at("id").get<std::string>();
                set.name = entry.at("name").get<std::string>();
                for (const json& m : entry.at("members")) {
                    try {
                        ReferenceMember member;
                        member.id = m.at("id").get<std::string>();
                        member.active = m.at("active").get<bool>();
                        member.weight = m.at("weight").get<int>();
                        if (m.contains("tags") && m["tags"].is_array()) {
                            for (const json& tag : m["tags"]) {
                                if (tag.is_string()) {
                                    member.tags.push_back(tag.get<std::string>());
                                }
                            }
                        }
                        set.members.push_back(std::move(member));
                    } catch (const json::exception&) {
                        ++bad;
                    }
                }
                collection.sets.push_back(std::move(set));
            } catch (const json::exception&) {
                ++bad;
            }
        }
        if (root.contains("active") && root["active"].is_string()) {
            collection.activeSetId = root["active"].get<std::string>();
        }
    }
    normalize(collection);
    if (dropped != nullptr) {
        *dropped = bad;
    }
    return collection;
}

} // namespace mm::core
