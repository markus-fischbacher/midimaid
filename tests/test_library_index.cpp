#include "core/LibraryIndex.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace mm::core;
using nlohmann::json;

namespace {

LibraryIndexRecord record(const std::string& id, int64_t size = 100, int64_t modified = 5000) {
    LibraryIndexRecord r;
    r.info.id = id;
    r.info.kind = id.rfind("s", 0) == 0 ? LibraryKind::Set : LibraryKind::Pattern;
    r.info.name = "name " + id;
    r.info.styleId = "peak_time";
    r.info.root = 9;
    r.info.scaleId = "natural_minor";
    r.info.seed = 18446744073709551615ull; // the largest seed
    r.info.prompt = "dark \"quoted\" \xC3\xBC";
    r.info.lengthBars = 8;
    r.info.slotCount = r.info.kind == LibraryKind::Set ? 5 : 1;
    r.info.createdUnixMs = 1700000000123;
    r.fileSize = size;
    r.modifiedMs = modified;
    return r;
}

LibraryFileState file(const std::string& id, int64_t size = 100, int64_t modified = 5000) {
    return {id, size, modified};
}

} // namespace

TEST_CASE("the index keeps every record exactly and does not depend on the order", "[library-index]") {
    const std::vector<LibraryIndexRecord> records{record("b"), record("s1"), record("a")};
    const auto text = libraryIndexToString(records);
    const auto reversed = libraryIndexToString({records[2], records[1], records[0]});
    CHECK(text == reversed);
    const auto loaded = loadLibraryIndex(text);
    REQUIRE(loaded.ok);
    CHECK(loaded.skipped == 0);
    REQUIRE(loaded.records.size() == 3);
    CHECK(loaded.records[0] == record("a"));
    CHECK(loaded.records[1] == record("b"));
    CHECK(loaded.records[2] == record("s1"));
    CHECK(libraryIndexToString(loaded.records) == text);
    // an empty library is an index, too
    const auto empty = loadLibraryIndex(libraryIndexToString({}));
    CHECK(empty.ok);
    CHECK(empty.records.empty());
}

TEST_CASE("text that is no index gives no records", "[library-index]") {
    CHECK_FALSE(loadLibraryIndex("").ok);
    CHECK_FALSE(loadLibraryIndex("not json").ok);
    CHECK_FALSE(loadLibraryIndex("[]").ok);
    CHECK_FALSE(loadLibraryIndex("{}").ok);
    const auto good = libraryIndexToString({record("a")});
    CHECK_FALSE(loadLibraryIndex(good.substr(0, good.size() / 2)).ok);
    const auto mutate = [&](const std::function<void(json&)>& change) {
        json document = json::parse(good);
        change(document);
        return loadLibraryIndex(document.dump());
    };
    CHECK_FALSE(mutate([](json& d) { d.erase("indexVersion"); }).ok);
    CHECK_FALSE(mutate([](json& d) { d["indexVersion"] = 0; }).ok);
    CHECK_FALSE(mutate([](json& d) { d["indexVersion"] = "1"; }).ok);
    CHECK_FALSE(mutate([](json& d) { d["indexVersion"] = kLibraryIndexVersion + 1; }).ok); // newer: not read
    CHECK_FALSE(mutate([](json& d) { d.erase("entries"); }).ok);
    CHECK_FALSE(mutate([](json& d) { d["entries"] = json::object(); }).ok);
    CHECK(mutate([](json& d) { d["indexVersion"] = kLibraryIndexVersion; }).ok);
}

TEST_CASE("a record that is not usable is left out and counted, the others stay", "[library-index]") {
    json document = json::parse(libraryIndexToString({record("a"), record("b")}));
    auto& entries = document["entries"];
    const json base = entries[0];
    const auto bad = [&](const std::function<void(json&)>& change) {
        json item = base;
        change(item);
        entries.push_back(item);
    };
    bad([](json& i) { i.erase("id"); });
    bad([](json& i) { i["id"] = "../x"; });
    bad([](json& i) { i["id"] = 5; });
    bad([](json& i) { i["kind"] = "folder"; });
    bad([](json& i) { i.erase("kind"); });
    bad([](json& i) { i.erase("name"); });
    bad([](json& i) { i.erase("size"); });
    bad([](json& i) { i["size"] = "100"; });
    bad([](json& i) { i.erase("modified"); });
    entries.push_back(5);
    entries.push_back(json::array());
    const auto loaded = loadLibraryIndex(document.dump());
    REQUIRE(loaded.ok);
    CHECK(loaded.skipped == 11);
    CHECK(loaded.records.size() == 2);
}

TEST_CASE("optional fields fall back and values stay in their ranges", "[library-index]") {
    json document = json::parse(libraryIndexToString({record("a")}));
    auto& item = document["entries"][0];
    item["root"] = 99;
    item["bars"] = 500;
    item["slotCount"] = -3;
    item["name"] = std::string(200, 'n');
    item["seed"] = "text";
    item.erase("style");
    item["scale"] = 4;
    item.erase("prompt");
    item.erase("created");
    const auto loaded = loadLibraryIndex(document.dump());
    REQUIRE(loaded.ok);
    REQUIRE(loaded.records.size() == 1);
    const auto& info = loaded.records[0].info;
    CHECK(info.root == 11);
    CHECK(info.lengthBars == 16);
    CHECK(info.slotCount == 0);
    CHECK(info.name.size() == kMaxLibraryNameBytes);
    CHECK(info.seed == 0);
    CHECK(info.styleId.empty());
    CHECK(info.scaleId.empty());
    CHECK(info.prompt.empty());
    CHECK(info.createdUnixMs == 0);
}

TEST_CASE("a file that is unchanged is trusted, anything else is read again", "[library-index]") {
    const std::vector<LibraryIndexRecord> records{record("a"), record("b"), record("c"), record("d")};
    const std::vector<LibraryFileState> files{file("a"), file("b", 101), file("c", 100, 5001), file("e"), file("d")};
    const auto result = reconcileLibraryIndex(records, files);
    REQUIRE(result.trusted.size() == 2);
    CHECK(result.trusted[0].info.id == "a");
    CHECK(result.trusted[1].info.id == "d");
    CHECK(result.toRead == std::vector<std::string>{"b", "c", "e"});
    CHECK(result.dropped == 0);
}

TEST_CASE("a record whose file is gone is dropped", "[library-index]") {
    const auto result = reconcileLibraryIndex({record("a"), record("b"), record("c")}, {file("b")});
    CHECK(result.trusted.size() == 1);
    CHECK(result.toRead.empty());
    CHECK(result.dropped == 2);
}

TEST_CASE("without an index every file is read, without files nothing is", "[library-index]") {
    const auto none = reconcileLibraryIndex({}, {file("z"), file("a"), file("m")});
    CHECK(none.trusted.empty());
    CHECK(none.toRead == std::vector<std::string>{"a", "m", "z"}); // sorted
    CHECK(none.dropped == 0);
    const auto nothing = reconcileLibraryIndex({}, {});
    CHECK(nothing.trusted.empty());
    CHECK(nothing.toRead.empty());
    CHECK(nothing.dropped == 0);
}

TEST_CASE("twice the same id counts once, the first record wins", "[library-index]") {
    auto first = record("a", 100, 5000);
    auto second = record("a", 999, 1);
    second.info.name = "other";
    const auto result = reconcileLibraryIndex({first, second}, {file("a"), file("a", 999, 1)});
    REQUIRE(result.trusted.size() == 1);
    CHECK(result.trusted[0].info.name == first.info.name);
    CHECK(result.toRead.empty());
    CHECK(result.dropped == 0);
}

TEST_CASE("a negative or fractional seed is no seed", "[library-index]") {
    for (const json& seed : {json(-5), json(1.5), json("7"), json(nullptr)}) {
        json document = json::parse(libraryIndexToString({record("a")}));
        document["entries"][0]["seed"] = seed;
        const auto loaded = loadLibraryIndex(document.dump());
        REQUIRE(loaded.ok);
        CHECK(loaded.records[0].info.seed == 0);
    }
}

TEST_CASE("the meta data of an entry are in the index and an index of before reads as none", "[library-index][meta]") {
    auto withMeta = record("m1");
    setMeta(withMeta.info, {{"dark", "Warehouse"}, 4, 126, 132});
    const auto loaded = loadLibraryIndex(libraryIndexToString({withMeta, record("m2")}));
    REQUIRE(loaded.ok);
    REQUIRE(loaded.records.size() == 2);
    CHECK(loaded.records[0] == withMeta);
    CHECK(loaded.records[0].info.tags == std::vector<std::string>{"dark", "Warehouse"});
    CHECK(loaded.records[0].info.rating == 4);
    CHECK(loaded.records[0].info.bpmLow == 126);
    CHECK(loaded.records[0].info.bpmHigh == 132);
    CHECK(loaded.records[1].info.tags.empty()); // nothing is written for what is not set
    const std::string text = libraryIndexToString({record("m2")});
    CHECK(text.find("tags") == std::string::npos);
    CHECK(text.find("rating") == std::string::npos);
    CHECK(text.find("bpmLow") == std::string::npos);
    // bad values are brought into range
    json document = json::parse(libraryIndexToString({withMeta}));
    document["entries"][0]["rating"] = 12;
    document["entries"][0]["bpmLow"] = 400;
    document["entries"][0]["tags"] = json::array({"a", 3, "a"});
    const auto clean = loadLibraryIndex(document.dump());
    REQUIRE(clean.ok);
    CHECK(clean.records[0].info.rating == 5);
    CHECK(clean.records[0].info.bpmLow == 132);
    CHECK(clean.records[0].info.tags == std::vector<std::string>{"a"});
}
