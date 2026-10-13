#include "core/MidiFile.h"
#include "core/PatternGenerator.h"
#include "core/ReferenceCollection.h"
#include "core/ReferenceProfile.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace mm::core;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

namespace {

StyleProfile shipped(const std::string& name = "peak_time") {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

/// An analysed item of a bass line A A C G, `shift` semitones up; ids differ with `shift`.
ReferenceItem bassItem(int shift = 0, const std::string& name = "bass") {
    std::vector<PatternNote> list;
    const int roots[] = {45, 45, 48, 43};
    for (uint32_t bar = 0; bar < 4; ++bar) {
        for (uint32_t i = 0; i < 4; ++i) {
            list.push_back({bar * kTicksPerBar + i * 960, 200, 1, static_cast<uint8_t>(roots[bar] + shift), 100});
        }
    }
    const auto bytes = writeMidiFile(PatternView{list.data(), list.size(), 4 * kTicksPerBar}, MidiFileOptions{});
    ReferenceImportReport report;
    auto items = itemsFromMidiFile(name, bytes, shipped(), report);
    REQUIRE(items.size() == 1);
    return items[0];
}

ReferenceItem withId(ReferenceItem item, const std::string& id) {
    item.id = id;
    return item;
}

Pattern made(uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    const auto result = generatePattern(shipped(), request);
    REQUIRE(result.success);
    return result.pattern;
}

/// A collection with a set "A" (items i1, i2) and a set "B" (i2, i3).
ReferenceCollection twoSets() {
    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    const auto a = createSet(c, "A");
    const auto b = createSet(c, "B");
    ReferenceImportReport report;
    addItems(c, a, {withId(bassItem(0), "i1"), withId(bassItem(1), "i2")}, report);
    addItems(c, b, {withId(bassItem(1), "i2"), withId(bassItem(2), "i3")}, report);
    return c;
}

} // namespace

TEST_CASE("a new collection has the favorites and nothing else", "[reference-collection]") {
    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    REQUIRE(c.sets.size() == 1);
    CHECK(c.sets[0].id == kFavoritesSetId);
    CHECK(c.sets[0].name.empty());
    CHECK(c.activeSetId == kFavoritesSetId);
    CHECK(c.pool.empty());
    CHECK(activeView(c).items.empty());
    CHECK(activeView(c).setId == kFavoritesSetId);
    // an unknown active set falls back, the favorites come first even when they were saved last
    c.sets.push_back({"s5", "Mine", {}});
    c.sets.push_back({kFavoritesSetId, "ignored", {}});
    c.sets.erase(c.sets.begin());
    c.activeSetId = "gone";
    normalize(c);
    REQUIRE(c.sets.size() == 2);
    CHECK(c.sets[0].id == kFavoritesSetId);
    CHECK(c.sets[0].name.empty());
    CHECK(c.activeSetId == "s5");
}

TEST_CASE("sets are created with clean unique names", "[reference-collection]") {
    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    CHECK(createSet(c, "").empty());
    CHECK(createSet(c, "   \t ").empty());
    CHECK(createSet(c, "Warehouse") == "s1");
    CHECK(createSet(c, "warehouse").empty()); // the same name in another case
    CHECK(createSet(c, "  Live-Set Berlin  ") == "s2");
    CHECK(findSet(c, "s2")->name == "Live-Set Berlin");
    // the smallest free number
    REQUIRE(deleteSet(c, "s1"));
    CHECK(createSet(c, "Neu") == "s1");
    // a long name is cut
    const auto id = createSet(c, std::string(200, 'x'));
    REQUIRE_FALSE(id.empty());
    CHECK(findSet(c, id)->name.size() == kMaxReferenceSetNameBytes);
    // as many as allowed
    for (int i = 0; c.sets.size() < kMaxReferenceSets; ++i) {
        REQUIRE_FALSE(createSet(c, "set " + std::to_string(i)).empty());
    }
    CHECK(c.sets.size() == kMaxReferenceSets);
    CHECK(createSet(c, "one too many").empty());
}

TEST_CASE("sets are renamed and deleted, the favorites are not", "[reference-collection]") {
    auto c = twoSets();
    CHECK(renameSet(c, "s1", "Alpha"));
    CHECK(findSet(c, "s1")->name == "Alpha");
    CHECK_FALSE(renameSet(c, "s1", "B"));        // taken
    CHECK(renameSet(c, "s1", "ALPHA"));          // the own name in another case is fine
    CHECK_FALSE(renameSet(c, "s1", " "));
    CHECK_FALSE(renameSet(c, "nope", "x"));
    CHECK_FALSE(renameSet(c, kFavoritesSetId, "x"));
    CHECK_FALSE(deleteSet(c, kFavoritesSetId));
    CHECK_FALSE(deleteSet(c, "nope"));
    // the items only the deleted set held go with it, the shared one stays
    REQUIRE(setActiveSet(c, "s1"));
    CHECK(deleteSet(c, "s1"));
    CHECK(c.activeSetId == "s2");
    CHECK(findSet(c, "s1") == nullptr);
    std::vector<std::string> ids;
    for (const auto& item : c.pool) {
        ids.push_back(item.id);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(ids == std::vector<std::string>{"i2", "i3"});
    CHECK_FALSE(setActiveSet(c, "s1"));
    CHECK(setActiveSet(c, kFavoritesSetId));
}

TEST_CASE("each set has its own switch, weight and tags for an item", "[reference-collection]") {
    auto c = twoSets();
    CHECK(setMemberActive(c, "s1", "i2", false));
    CHECK(setMemberWeight(c, "s1", "i2", 9)); // clamped
    CHECK(setMemberTags(c, "s1", "i2", {" dark ", "Dark", "warehouse", "", "a", "b", "c", "d", "e"}));
    const auto a = viewOf(c, "s1");
    const auto b = viewOf(c, "s2");
    REQUIRE(a.items.size() == 2);
    REQUIRE(b.items.size() == 2);
    CHECK(a.setId == "s1");
    CHECK(a.setName == "A");
    const auto& inA = *std::find_if(a.items.begin(), a.items.end(), [](const auto& i) { return i.id == "i2"; });
    const auto& inB = *std::find_if(b.items.begin(), b.items.end(), [](const auto& i) { return i.id == "i2"; });
    CHECK_FALSE(inA.active);
    CHECK(inA.weight == kMaxReferenceWeight);
    CHECK(inA.tags == std::vector<std::string>{"dark", "warehouse", "a", "b", "c", "d"}); // cleaned, at most six
    CHECK(inB.active);
    CHECK(inB.weight == kDefaultReferenceWeight);
    CHECK(inB.tags.empty());
    CHECK(inA.notes == inB.notes); // the same item
    CHECK(setMemberWeight(c, "s1", "i2", -4));
    CHECK(viewOf(c, "s1").items[1].weight == kMinReferenceWeight);
    CHECK_FALSE(setMemberActive(c, "s1", "i3", false)); // not in the set
    CHECK_FALSE(setMemberActive(c, "nope", "i1", false));
    CHECK_FALSE(setMemberWeight(c, "s1", "i3", 2));
    CHECK_FALSE(setMemberTags(c, "s1", "i3", {"x"}));
    // the role belongs to the item and counts in both sets
    CHECK(setItemRole(c, "i2", TrackRole::Melody));
    CHECK(viewOf(c, "s1").items[1].role == TrackRole::Melody);
    CHECK(viewOf(c, "s2").items[0].role == TrackRole::Melody);
    CHECK_FALSE(setItemRole(c, "nope", TrackRole::Bass));
    // an unknown set is an empty view with its id
    const auto none = viewOf(c, "nope");
    CHECK(none.items.empty());
    CHECK(none.setId == "nope");
}

TEST_CASE("an item goes into another set and leaves the pool with its last set", "[reference-collection]") {
    auto c = twoSets();
    CHECK(addMember(c, "s1", "i3"));
    CHECK_FALSE(addMember(c, "s1", "i3")); // in it already
    CHECK_FALSE(addMember(c, "s1", "nope"));
    CHECK_FALSE(addMember(c, "nope", "i1"));
    CHECK(viewOf(c, "s1").items.size() == 3);
    CHECK(removeMember(c, "s1", "i3"));
    CHECK(c.pool.size() == 3); // s2 still has it
    CHECK(removeMember(c, "s2", "i3"));
    CHECK(c.pool.size() == 2);
    CHECK_FALSE(removeMember(c, "s2", "i3"));
    CHECK_FALSE(removeMember(c, "nope", "i1"));
}

TEST_CASE("an import fills the pool and a known file only joins a set", "[reference-collection]") {
    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    const auto a = createSet(c, "A");
    const auto b = createSet(c, "B");
    ReferenceImportReport first;
    addItems(c, a, {withId(bassItem(0), "hash1-1"), withId(bassItem(1), "hash2-1")}, first);
    CHECK(first.added == 2);
    CHECK(poolContainsFile(c, "hash1"));
    CHECK_FALSE(poolContainsFile(c, "hash"));
    CHECK_FALSE(poolContainsFile(c, "hash3"));
    ReferenceImportReport again;
    addItems(c, a, {withId(bassItem(0), "hash1-1")}, again);
    CHECK(again.duplicates == 1);
    CHECK(again.added == 0);
    ReferenceImportReport known;
    addKnownFile(c, b, "hash1", known);
    CHECK(known.added == 1);
    ReferenceImportReport twice;
    addKnownFile(c, b, "hash1", twice);
    CHECK(twice.duplicates == 1);
    CHECK(twice.added == 0);
    CHECK(c.pool.size() == 2); // the file is in the pool once
    CHECK(viewOf(c, b).items.size() == 1);
    // an item the pool has goes into the set without a second copy
    ReferenceImportReport shared;
    addItems(c, b, {withId(bassItem(1), "hash2-1")}, shared);
    CHECK(shared.added == 1);
    CHECK(c.pool.size() == 2);
}

TEST_CASE("a set holds at most the allowed number of items", "[reference-collection]") {
    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    const auto a = createSet(c, "A");
    std::vector<ReferenceItem> items;
    const auto base = bassItem(0);
    for (size_t i = 0; i < kMaxReferenceItems + 3; ++i) {
        items.push_back(withId(base, "x" + std::to_string(i)));
    }
    ReferenceImportReport report;
    addItems(c, a, std::move(items), report);
    CHECK(report.added == kMaxReferenceItems);
    CHECK(report.overLimit == 3);
    CHECK(findSet(c, a)->members.size() == kMaxReferenceItems);
    CHECK(c.pool.size() == kMaxReferenceItems); // what did not fit is not in the pool either
    // a file that is known but does not fit
    const auto b = createSet(c, "B");
    ReferenceImportReport fill;
    std::vector<ReferenceItem> more;
    for (size_t i = 0; i < kMaxReferenceItems; ++i) {
        more.push_back(withId(base, "x" + std::to_string(i)));
    }
    addItems(c, b, std::move(more), fill);
    CHECK(fill.added == kMaxReferenceItems);
    CHECK_FALSE(addMember(c, a, "nope"));
}

TEST_CASE("the voices of a pattern become favorites", "[reference-collection][favorites]") {
    const auto style = shipped();
    const auto pattern = made(7);
    const auto items = favoriteItemsOf(pattern, "entry1", "Rolling", style);
    REQUIRE_FALSE(items.empty());
    CHECK(items.size() <= pattern.voices.size());
    CHECK(items[0].id == "fav:entry1:0");
    CHECK(isFavoriteItem(items[0].id));
    CHECK_FALSE(isFavoriteItem("abc-1"));
    CHECK(items[0].role == (pattern.voices[0].role == VoiceRole::Bass ? TrackRole::Bass : TrackRole::Melody));
    CHECK(items[0].lengthBars == pattern.lengthBars);
    CHECK(items[0].notes.size() == pattern.voices[0].notes.size());
    CHECK(items[0].notes[0].pitch == pattern.voices[0].notes[0].pitch);
    CHECK(items[0].notes[0].startTick == pattern.voices[0].notes[0].startTick);
    CHECK(items[0].analysis.lengthBars == pattern.lengthBars);
    if (items.size() > 1) {
        CHECK(items[0].name == "Rolling (1)");
    }
    // a voice with fewer than four notes is no reference
    Pattern sparse = pattern;
    sparse.voices[0].notes.resize(3);
    const auto fewer = favoriteItemsOf(sparse, "entry1", "Rolling", style);
    CHECK(fewer.size() + 1 == items.size());
    CHECK(favoriteItemId("e", 3) == "fav:e:3");
}

TEST_CASE("favorites go into the favorites set and the active set, and come out again", "[reference-collection][favorites]") {
    const auto style = shipped();
    auto c = twoSets();
    REQUIRE(setActiveSet(c, "s1"));
    const auto items = favoriteItemsOf(made(8), "entry1", "One", style);
    REQUIRE_FALSE(items.empty());
    setFavorite(c, "entry1", items, true);
    CHECK(viewOf(c, kFavoritesSetId).items.size() == items.size());
    CHECK(viewOf(c, "s1").items.size() == 2 + items.size());
    CHECK(viewOf(c, "s2").items.size() == 2); // only the active set
    // without the option only the favorites
    setFavorite(c, "entry2", favoriteItemsOf(made(9), "entry2", "Two", style), false);
    CHECK(viewOf(c, "s1").items.size() == 2 + items.size());
    CHECK(viewOf(c, kFavoritesSetId).items.size() > items.size());
    // saved again: replaced, and what the musician set stays
    REQUIRE(setActiveSet(c, kFavoritesSetId));
    REQUIRE(setMemberWeight(c, kFavoritesSetId, "fav:entry1:0", 5));
    REQUIRE(setMemberActive(c, kFavoritesSetId, "fav:entry1:0", false));
    REQUIRE(setActiveSet(c, "s1"));
    setFavorite(c, "entry1", items, true);
    const auto fav = viewOf(c, kFavoritesSetId);
    const auto it = std::find_if(fav.items.begin(), fav.items.end(), [](const auto& i) { return i.id == "fav:entry1:0"; });
    REQUIRE(it != fav.items.end());
    CHECK(it->weight == 5);
    CHECK_FALSE(it->active);
    CHECK(std::count_if(fav.items.begin(), fav.items.end(), [](const auto& i) { return i.id == "fav:entry1:0"; }) == 1);
    // gone: out of the pool and out of every set
    removeFavorite(c, "entry1");
    for (const auto& set : c.sets) {
        for (const auto& member : set.members) {
            CHECK(member.id.rfind("fav:entry1:", 0) != 0);
        }
    }
    CHECK(std::none_of(c.pool.begin(), c.pool.end(), [](const auto& i) { return i.id.rfind("fav:entry1:", 0) == 0; }));
    CHECK(std::any_of(c.pool.begin(), c.pool.end(), [](const auto& i) { return i.id.rfind("fav:entry2:", 0) == 0; }));
    // an entry with no usable voice removes what it had
    setFavorite(c, "entry2", {}, true);
    CHECK(std::none_of(c.pool.begin(), c.pool.end(), [](const auto& i) { return isFavoriteItem(i.id); }));
}

TEST_CASE("the collection survives writing and reading", "[reference-collection][json]") {
    auto c = twoSets();
    REQUIRE(setActiveSet(c, "s2"));
    REQUIRE(setMemberActive(c, "s1", "i2", false));
    REQUIRE(setMemberWeight(c, "s2", "i3", 5));
    REQUIRE(setMemberTags(c, "s2", "i3", {"dark", "peak"}));
    setFavorite(c, "e", favoriteItemsOf(made(3), "e", "Fav", shipped()), true);
    const auto text = referenceCollectionToJson(c);
    size_t dropped = 99;
    const auto back = referenceCollectionFromJson(text, "peak_time", &dropped);
    CHECK(dropped == 0);
    CHECK(back == c);
    CHECK(back.activeSetId == "s2");
    // the text is stable
    CHECK(referenceCollectionToJson(back) == text);
}

TEST_CASE("a file of the first version becomes one set", "[reference-collection][json]") {
    ReferenceSet old;
    old.styleId = "peak_time";
    old.items = {withId(bassItem(0), "old1"), withId(bassItem(1), "old2")};
    old.items[1].active = false;
    old.items[1].weight = 5;
    const auto c = referenceCollectionFromJson(referenceSetToJson(old), "peak_time");
    REQUIRE(c.sets.size() == 2);
    CHECK(c.sets[0].id == kFavoritesSetId);
    CHECK(c.sets[1].id == "default"); // the id the patterns of v1.0 carry
    CHECK(c.sets[1].name == "Standard");
    CHECK(c.activeSetId == "default");
    const auto view = activeView(c);
    REQUIRE(view.items.size() == 2);
    CHECK(view.items[0].active);
    CHECK_FALSE(view.items[1].active);
    CHECK(view.items[1].weight == 5);
    CHECK(view.items[0].notes == old.items[0].notes);
    // an empty old file gives no set but the favorites
    ReferenceSet none;
    none.styleId = "peak_time";
    const auto empty = referenceCollectionFromJson(referenceSetToJson(none), "peak_time");
    CHECK(empty.sets.size() == 1);
    CHECK(empty.activeSetId == kFavoritesSetId);
}

TEST_CASE("bad collection files give what can be read", "[reference-collection][json]") {
    for (const char* text : {"", "not json", "[]", "{}", "{\"version\":2}", "{\"version\":\"2\"}", "{\"version\":0,\"items\":[]}"}) {
        INFO(text);
        const auto c = referenceCollectionFromJson(text, "peak_time");
        CHECK(c.styleId == "peak_time");
        REQUIRE(c.sets.size() == 1);
        CHECK(c.sets[0].id == kFavoritesSetId);
        CHECK(c.pool.empty());
    }
    // a member of an item that is not in the pool, a duplicate set, a broken set and unknown fields
    auto c = twoSets();
    auto root = nlohmann::json::parse(referenceCollectionToJson(c));
    root["sets"][1]["members"].push_back({{"id", "ghost"}, {"active", true}, {"weight", 3}, {"tags", nlohmann::json::array()}});
    root["sets"][1]["members"].push_back({{"id", "i1"}});
    root["sets"].push_back(root["sets"][1]);
    root["sets"].push_back({{"id", "broken"}});
    root["extra"] = 5;
    root["version"] = 3;
    size_t dropped = 0;
    const auto back = referenceCollectionFromJson(root.dump(), "peak_time", &dropped);
    CHECK(dropped >= 2);
    CHECK(back == c);
    // an active set that is not there
    root = nlohmann::json::parse(referenceCollectionToJson(c));
    root["active"] = "nope";
    CHECK(referenceCollectionFromJson(root.dump(), "peak_time").activeSetId == "s1");
}

TEST_CASE("a generation names the set it was made with", "[reference-collection][effect]") {
    auto c = twoSets();
    for (const char* id : {"s1", "s2"}) {
        for (const auto& item : viewOf(c, id).items) {
            CHECK(item.active);
        }
    }
    CHECK(referenceEffectOf(viewOf(c, "s2")).setId == "s2");
    CHECK(referenceEffectOf(viewOf(c, "s1")).setId == "s1");
    // an empty set is no set; a set made by hand is the default one
    CHECK(referenceEffectOf(viewOf(c, kFavoritesSetId)).setId.empty());
    ReferenceSet byHand;
    byHand.items.push_back(bassItem(0));
    CHECK(referenceEffectOf(byHand).setId == kReferenceSetId);
    // all items switched off: no set
    for (const auto& item : viewOf(c, "s1").items) {
        setMemberActive(c, "s1", item.id, false);
    }
    CHECK(referenceEffectOf(viewOf(c, "s1")).setId.empty());
}

TEST_CASE("removing the favorites of one entry leaves the entries whose id starts the same", "[reference-collection][favorites]") {
    const auto style = shipped();
    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    setFavorite(c, "e1", favoriteItemsOf(made(11), "e1", "One", style), false);
    setFavorite(c, "e10", favoriteItemsOf(made(12), "e10", "Ten", style), false);
    const auto both = viewOf(c, kFavoritesSetId).items.size();
    removeFavorite(c, "e1");
    const auto left = viewOf(c, kFavoritesSetId).items;
    CHECK(left.size() < both);
    REQUIRE_FALSE(left.empty());
    for (const auto& item : left) {
        CHECK(item.id.rfind("fav:e10:", 0) == 0);
    }
}

TEST_CASE("an imported item knows how sure the role was, and the musician's choice makes it sure", "[reference-collection][role]") {
    using Catch::Approx;
    // eight notes right at the limit between bass and melody: an unsure melody
    std::vector<PatternNote> list;
    for (uint32_t i = 0; i < 8; ++i) {
        list.push_back({i * 480, 240, 1, 55, 100});
    }
    const auto bytes = writeMidiFile(PatternView{list.data(), list.size(), kTicksPerBar}, MidiFileOptions{});
    ReferenceImportReport report;
    auto items = itemsFromMidiFile("limit", bytes, shipped(), report);
    REQUIRE(items.size() == 1);
    CHECK(items[0].role == TrackRole::Melody);
    CHECK(items[0].roleConfidence == Approx(0.5));
    CHECK(isRoleUnsure(items[0]));
    CHECK(bassItem(0).roleConfidence == Approx(0.5 + 0.5 * 10.0 / 12.0)); // median A2: ten semitones below the limit
    CHECK_FALSE(isRoleUnsure(bassItem(0)));

    ReferenceCollection c;
    c.styleId = "peak_time";
    normalize(c);
    const auto set = createSet(c, "A");
    ReferenceImportReport add;
    addItems(c, set, {withId(items[0], "u1"), withId(bassItem(0), "b1")}, add);
    CHECK(unsureRoleCount(viewOf(c, set)) == 1);
    CHECK(unsureRoleCount(viewOf(c, kFavoritesSetId)) == 0);
    // saved and read back
    const auto back = referenceCollectionFromJson(referenceCollectionToJson(c), "peak_time");
    CHECK(back == c);
    CHECK(viewOf(back, set).items[0].roleConfidence == Approx(0.5));
    // the musician sets the role: sure from then on, in the collection and in a set of the old kind
    REQUIRE(setItemRole(c, "u1", TrackRole::Bass));
    CHECK(viewOf(c, set).items[0].roleConfidence == Approx(1.0));
    CHECK(unsureRoleCount(viewOf(c, set)) == 0);
    ReferenceSet old = viewOf(back, set);
    REQUIRE(setItemRole(old, "u1", TrackRole::Chords));
    CHECK(old.items[0].roleConfidence == Approx(1.0));
    CHECK(unsureRoleCount(old) == 0);
    // a file written before the field existed counts as sure
    auto json = nlohmann::json::parse(referenceSetToJson(viewOf(back, set)));
    for (auto& item : json["items"]) {
        item.erase("roleConfidence");
    }
    const auto older = referenceSetFromJson(json.dump(), "peak_time");
    REQUIRE(older.items.size() == 2);
    CHECK(older.items[0].roleConfidence == Approx(1.0));
    // favorites come from the voices of a pattern: the role is known
    for (const auto& item : favoriteItemsOf(made(5), "e", "x", shipped())) {
        CHECK(item.roleConfidence == Approx(1.0));
    }
}
