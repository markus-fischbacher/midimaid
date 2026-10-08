#include "core/SlotBank.h"
#include "core/SlotBankJson.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace mm::core;

namespace {

/// A valid one-bar pattern that tells itself apart by its style id and one bass note.
Pattern makePattern(const std::string& style, uint8_t pitch = 45) {
    Pattern pattern = makeEmptyPattern(1, style);
    Note note;
    note.id = allocateNoteId(pattern);
    note.pitch = pitch;
    note.startTick = 0;
    note.lengthTicks = 240;
    pattern.voices[0].notes.push_back(note);
    return pattern;
}

} // namespace

TEST_CASE("a new bank has 16 empty slots", "[slots-model]") {
    SlotBank bank;
    for (size_t i = 0; i < kSlotCount; ++i) {
        REQUIRE(bank.slot(i) != nullptr);
        CHECK(bank.isEmpty(i));
        CHECK(bank.slot(i)->name.empty());
        CHECK(bank.slot(i)->color == 0);
        CHECK(bank.slot(i)->history.empty());
    }
    CHECK(bank.slot(kSlotCount) == nullptr);
    CHECK(bank.nextVersion() == 1);
}

TEST_CASE("a result becomes the current pattern and gets the next version", "[slots-model]") {
    SlotBank bank;
    REQUIRE(bank.setResult(2, makePattern("a")));
    REQUIRE(bank.setResult(5, makePattern("b")));
    REQUIRE(bank.slot(2)->pattern.has_value());
    CHECK(bank.slot(2)->pattern->styleId == "a");
    CHECK(bank.slot(2)->pattern->version == 1);
    CHECK(bank.slot(5)->pattern->version == 2);
    CHECK(bank.nextVersion() == 3);
    CHECK(bank.slot(2)->history.size() == 1);
    CHECK_FALSE(bank.isEmpty(2));
    CHECK(bank.isEmpty(3));
}

TEST_CASE("invalid indices and invalid patterns change nothing", "[slots-model]") {
    SlotBank bank;
    CHECK_FALSE(bank.setResult(kSlotCount, makePattern("a")));
    CHECK_FALSE(bank.edit(kSlotCount, makePattern("a")));
    CHECK_FALSE(bank.historyBack(kSlotCount));
    CHECK_FALSE(bank.historyForward(kSlotCount));
    CHECK_FALSE(bank.setName(kSlotCount, "x"));
    CHECK_FALSE(bank.setColor(kSlotCount, 1));
    CHECK_FALSE(bank.copy(kSlotCount).has_value());
    CHECK_FALSE(bank.clear(kSlotCount));
    CHECK_FALSE(bank.swap(0, kSlotCount));
    CHECK_FALSE(bank.swap(kSlotCount, 0));

    Pattern broken = makePattern("a");
    broken.lengthBars = 3; // not a valid length
    CHECK_FALSE(bank.setResult(0, broken));
    CHECK(bank.isEmpty(0));
    CHECK(bank.nextVersion() == 1);
}

TEST_CASE("the history keeps the last 20 results", "[slots-model]") {
    SlotBank bank;
    for (int i = 1; i <= 21; ++i) {
        REQUIRE(bank.setResult(0, makePattern("r" + std::to_string(i))));
    }
    const Slot& slot = *bank.slot(0);
    REQUIRE(slot.history.size() == kMaxHistory);
    CHECK(slot.history.front().styleId == "r2"); // r1 fell out
    CHECK(slot.history.back().styleId == "r21");
    CHECK(slot.pattern->styleId == "r21");
    CHECK(slot.cursor == kMaxHistory - 1);
}

TEST_CASE("browsing the history sets the current pattern and stops at the ends", "[slots-model]") {
    SlotBank bank;
    for (const char* name : {"a", "b", "c"}) {
        REQUIRE(bank.setResult(0, makePattern(name)));
    }
    CHECK_FALSE(bank.historyForward(0)); // already at the newest
    REQUIRE(bank.historyBack(0));
    CHECK(bank.slot(0)->pattern->styleId == "b");
    REQUIRE(bank.historyBack(0));
    CHECK(bank.slot(0)->pattern->styleId == "a");
    CHECK_FALSE(bank.historyBack(0)); // at the oldest
    REQUIRE(bank.historyForward(0));
    CHECK(bank.slot(0)->pattern->styleId == "b");

    // A new result is appended at the end, nothing is cut off.
    REQUIRE(bank.setResult(0, makePattern("d")));
    CHECK(bank.slot(0)->history.size() == 4);
    CHECK(bank.slot(0)->pattern->styleId == "d");
    CHECK_FALSE(bank.historyForward(0));
    CHECK_FALSE(bank.historyBack(1)); // empty slot
}

TEST_CASE("an edit replaces the current pattern without a history entry", "[slots-model]") {
    SlotBank bank;
    CHECK_FALSE(bank.edit(0, makePattern("x"))); // empty slot
    REQUIRE(bank.setResult(0, makePattern("a", 45)));
    REQUIRE(bank.edit(0, makePattern("a", 50)));
    const Slot& slot = *bank.slot(0);
    CHECK(slot.pattern->voices[0].notes[0].pitch == 50);
    CHECK(slot.history.size() == 1);
    CHECK(slot.history[0].voices[0].notes[0].pitch == 45);
    CHECK(slot.pattern->version == 2); // an edit is a new version
}

TEST_CASE("names are cleaned and cut at a character boundary", "[slots-model]") {
    CHECK(sanitizeSlotName("  Drop  ") == "Drop");
    CHECK(sanitizeSlotName("a\tb\nc\x7F") == "abc");
    CHECK(sanitizeSlotName("   ").empty());
    CHECK(sanitizeSlotName(std::string(40, 'x')) == std::string(kMaxNameBytes, 'x'));
    // 15 two-byte characters (30 bytes) plus two more would exceed 32 bytes in the middle of a character.
    std::string umlauts;
    for (int i = 0; i < 20; ++i) {
        umlauts += "\xC3\xA4"; // ä
    }
    const std::string cut = sanitizeSlotName(umlauts);
    CHECK(cut.size() == kMaxNameBytes); // 16 characters of two bytes each
    CHECK(cut.size() % 2 == 0);
    std::string odd = "x" + umlauts; // 1 + 2n: the 32nd byte starts a character
    const std::string cutOdd = sanitizeSlotName(odd);
    CHECK(cutOdd.size() == 31);
    CHECK(cutOdd.substr(0, 1) == "x");
    // Trailing spaces left by the cut are removed.
    CHECK(sanitizeSlotName(std::string(31, 'x') + "  y") == std::string(31, 'x'));

    SlotBank bank;
    REQUIRE(bank.setName(3, "  Intro "));
    CHECK(bank.slot(3)->name == "Intro");
}

TEST_CASE("colour marks are 0 to 8", "[slots-model]") {
    SlotBank bank;
    CHECK(bank.setColor(0, 8));
    CHECK(bank.slot(0)->color == 8);
    CHECK_FALSE(bank.setColor(0, 9));
    CHECK(bank.slot(0)->color == 8);
    CHECK(bank.setColor(0, 0));
}

TEST_CASE("copy and paste carry pattern, ids, name and colour", "[slots-model]") {
    SlotBank bank;
    Pattern source = makePattern("src");
    REQUIRE(bank.setResult(0, source));
    REQUIRE(bank.setName(0, "Drop"));
    REQUIRE(bank.setColor(0, 3));
    const auto content = bank.copy(0);
    REQUIRE(content.has_value());
    REQUIRE(content->pattern.has_value());

    REQUIRE(bank.paste(4, *content));
    const Slot& target = *bank.slot(4);
    CHECK(target.name == "Drop");
    CHECK(target.color == 3);
    CHECK(target.pattern->styleId == "src");
    CHECK(target.pattern->nextNoteId == bank.slot(0)->pattern->nextNoteId); // ids and counter unchanged
    CHECK(target.pattern->voices[0].notes[0].id == bank.slot(0)->pattern->voices[0].notes[0].id);
    CHECK(target.pattern->version != bank.slot(0)->pattern->version); // its own version
    CHECK(target.history.size() == 1);                                // a paste counts as a result

    // Pasting over a filled slot keeps the old pattern reachable.
    REQUIRE(bank.setResult(6, makePattern("old")));
    REQUIRE(bank.paste(6, *content));
    CHECK(bank.slot(6)->history.size() == 2);
    REQUIRE(bank.historyBack(6));
    CHECK(bank.slot(6)->pattern->styleId == "old");

    // Content of an empty slot has no pattern and cannot be pasted.
    const auto empty = bank.copy(9);
    REQUIRE(empty.has_value());
    CHECK_FALSE(empty->pattern.has_value());
    CHECK_FALSE(bank.paste(1, *empty));
    CHECK(bank.isEmpty(1));
}

TEST_CASE("clear resets the slot completely", "[slots-model]") {
    SlotBank bank;
    REQUIRE(bank.setResult(2, makePattern("a")));
    REQUIRE(bank.setResult(2, makePattern("b")));
    REQUIRE(bank.setName(2, "Break"));
    REQUIRE(bank.setColor(2, 5));
    REQUIRE(bank.clear(2));
    const Slot& slot = *bank.slot(2);
    CHECK_FALSE(slot.pattern.has_value());
    CHECK(slot.history.empty());
    CHECK(slot.name.empty());
    CHECK(slot.color == 0);
    CHECK(slot.revision > 0);
}

TEST_CASE("swap exchanges everything of two slots", "[slots-model]") {
    SlotBank bank;
    REQUIRE(bank.setResult(0, makePattern("a")));
    REQUIRE(bank.setResult(0, makePattern("a2")));
    REQUIRE(bank.setName(0, "A"));
    REQUIRE(bank.setColor(0, 1));
    REQUIRE(bank.setResult(7, makePattern("b")));
    REQUIRE(bank.setName(7, "B"));
    REQUIRE(bank.swap(0, 7));
    CHECK(bank.slot(7)->pattern->styleId == "a2");
    CHECK(bank.slot(7)->name == "A");
    CHECK(bank.slot(7)->color == 1);
    CHECK(bank.slot(7)->history.size() == 2);
    CHECK(bank.slot(0)->pattern->styleId == "b");
    CHECK(bank.slot(0)->name == "B");
    CHECK(bank.slot(0)->history.size() == 1);
    REQUIRE(bank.swap(3, 3)); // with itself: nothing happens
    CHECK(bank.isEmpty(3));
    // Swapping with an empty slot moves the content.
    REQUIRE(bank.swap(0, 12));
    CHECK(bank.isEmpty(0));
    CHECK(bank.slot(12)->pattern->styleId == "b");
}

TEST_CASE("every change raises the revision of the slots it touches", "[slots-model]") {
    SlotBank bank;
    const uint64_t start = bank.slot(1)->revision;
    REQUIRE(bank.setResult(1, makePattern("a")));
    const uint64_t afterResult = bank.slot(1)->revision;
    CHECK(afterResult > start);
    REQUIRE(bank.setName(1, "x"));
    CHECK(bank.slot(1)->revision > afterResult);
    const uint64_t other = bank.slot(2)->revision;
    REQUIRE(bank.setResult(2, makePattern("b")));
    CHECK(bank.slot(2)->revision > other);
    const uint64_t before1 = bank.slot(1)->revision;
    const uint64_t before2 = bank.slot(2)->revision;
    REQUIRE(bank.swap(1, 2));
    CHECK(bank.slot(1)->revision > before1);
    CHECK(bank.slot(2)->revision > before2);
    const uint64_t untouched = bank.slot(5)->revision;
    REQUIRE(bank.setResult(1, makePattern("c")));
    CHECK(bank.slot(5)->revision == untouched);
}

// ---- serialisation ----

namespace {

SlotBank filledBank() {
    SlotBank bank;
    for (int i = 1; i <= 8; ++i) { // more than the state keeps
        bank.setResult(0, makePattern("hist" + std::to_string(i), static_cast<uint8_t>(40 + i)));
    }
    bank.setName(0, "Drop");
    bank.setColor(0, 2);
    bank.setResult(3, makePattern("other"));
    bank.historyBack(0);
    return bank;
}

} // namespace

TEST_CASE("a bank survives a round trip through JSON with byte-identical text", "[slots-model]") {
    const SlotBank bank = filledBank();
    const std::string text = slotBankToString(bank);
    const SlotBankLoadResult loaded = loadSlotBank(text);
    REQUIRE(loaded.ok());
    CHECK(loaded.problems.empty());
    CHECK_FALSE(loaded.fromNewerVersion);
    CHECK(slotBankToString(*loaded.bank) == text);
    CHECK(loaded.bank->slot(0)->name == "Drop");
    CHECK(loaded.bank->slot(0)->color == 2);
    CHECK(loaded.bank->slot(0)->pattern == bank.slot(0)->pattern);
    CHECK(loaded.bank->nextVersion() == bank.nextVersion());
    CHECK(loaded.bank->isEmpty(1));
}

TEST_CASE("the state keeps the current pattern and the last 5 results", "[slots-model]") {
    const SlotBank bank = filledBank(); // slot 0 has 8 results, the cursor sits on the 7th
    REQUIRE(bank.slot(0)->history.size() == 8);
    const SlotBankLoadResult loaded = loadSlotBank(slotBankToString(bank));
    REQUIRE(loaded.ok());
    const Slot& slot = *loaded.bank->slot(0);
    REQUIRE(slot.history.size() == kStateHistory);
    CHECK(slot.history.front().styleId == "hist4"); // hist1 to hist3 were dropped
    CHECK(slot.history.back().styleId == "hist8");
    CHECK(slot.pattern->styleId == "hist7"); // the current one (browsed back) is kept as it was
    CHECK(slot.cursor == 3);                 // hist7 is the 4th of the stored five

    const SlotBankLoadResult longer = loadSlotBank(slotBankToString(bank, kMaxHistory));
    REQUIRE(longer.ok());
    CHECK(longer.bank->slot(0)->history.size() == 8);
}

TEST_CASE("the version counter is restored and never falls behind a stored pattern", "[slots-model]") {
    SlotBank bank = filledBank();
    nlohmann::json document = slotBankToJson(bank);
    document["nextVersion"] = 1; // a damaged counter
    const SlotBankLoadResult loaded = loadSlotBankJson(document);
    REQUIRE(loaded.ok());
    CHECK(loaded.bank->nextVersion() >= bank.nextVersion());
}

TEST_CASE("loading ignores unknown fields and tolerates a newer version", "[slots-model]") {
    nlohmann::json document = slotBankToJson(filledBank());
    document["somethingNew"] = {1, 2, 3};
    document["slots"][0]["extra"] = "x";
    document["stateVersion"] = kSlotBankStateVersion + 1;
    const SlotBankLoadResult loaded = loadSlotBankJson(document);
    REQUIRE(loaded.ok());
    CHECK(loaded.fromNewerVersion);
    CHECK(loaded.problems.empty());
    CHECK(loaded.bank->slot(0)->name == "Drop");
}

TEST_CASE("missing and surplus slots are handled", "[slots-model]") {
    nlohmann::json document = slotBankToJson(filledBank());
    SECTION("fewer than 16 slots: the rest stays empty") {
        auto& slots = document["slots"];
        while (slots.size() > 2) {
            slots.erase(slots.size() - 1);
        }
        const SlotBankLoadResult loaded = loadSlotBankJson(document);
        REQUIRE(loaded.ok());
        CHECK(loaded.bank->slot(0)->name == "Drop");
        CHECK(loaded.bank->isEmpty(3)); // slot 3 was cut off
        CHECK(loaded.problems.empty());
    }
    SECTION("more than 16 slots: the extras are ignored") {
        for (int i = 0; i < 4; ++i) {
            document["slots"].push_back(document["slots"][0]);
        }
        const SlotBankLoadResult loaded = loadSlotBankJson(document);
        REQUIRE(loaded.ok());
        CHECK(loaded.problems.empty());
    }
}

TEST_CASE("a bad slot stays empty and is reported while the others load", "[slots-model]") {
    nlohmann::json document = slotBankToJson(filledBank());
    SECTION("a broken pattern") {
        document["slots"][3]["pattern"]["lengthBars"] = 3;
        const SlotBankLoadResult loaded = loadSlotBankJson(document);
        REQUIRE(loaded.ok());
        REQUIRE(loaded.problems.size() == 1);
        CHECK(loaded.problems[0].rfind("slots[3].pattern", 0) == 0);
        CHECK(loaded.bank->isEmpty(3));
        CHECK_FALSE(loaded.bank->isEmpty(0)); // the others are intact
    }
    SECTION("a colour out of range") {
        document["slots"][0]["color"] = 9;
        const SlotBankLoadResult loaded = loadSlotBankJson(document);
        REQUIRE(loaded.ok());
        REQUIRE(loaded.problems.size() == 1);
        CHECK(loaded.problems[0].rfind("slots[0].color", 0) == 0);
        CHECK(loaded.bank->isEmpty(0));
        CHECK_FALSE(loaded.bank->isEmpty(3));
    }
    SECTION("a broken history entry") {
        document["slots"][0]["history"][1] = "not a pattern";
        const SlotBankLoadResult loaded = loadSlotBankJson(document);
        REQUIRE(loaded.ok());
        REQUIRE(loaded.problems.size() == 1);
        CHECK(loaded.problems[0].rfind("slots[0].history[1]", 0) == 0);
        CHECK(loaded.bank->isEmpty(0)); // the whole slot stays empty, not a half-loaded one
        CHECK(loaded.bank->slot(0)->history.empty());
    }
    SECTION("a slot that is not an object") {
        document["slots"][5] = 7;
        const SlotBankLoadResult loaded = loadSlotBankJson(document);
        REQUIRE(loaded.ok());
        REQUIRE(loaded.problems.size() == 1);
        CHECK(loaded.problems[0].rfind("slots[5]", 0) == 0);
    }
}

TEST_CASE("a damaged document gives an error and never crashes", "[slots-model]") {
    CHECK_FALSE(loadSlotBank("").ok());
    CHECK_FALSE(loadSlotBank("not json").ok());
    CHECK_FALSE(loadSlotBank("[1,2]").ok());
    CHECK_FALSE(loadSlotBank("{}").ok());
    CHECK_FALSE(loadSlotBank("{\"stateVersion\": 1}").ok());
    CHECK_FALSE(loadSlotBank("{\"stateVersion\": 1, \"slots\": 3}").ok());
    CHECK_FALSE(loadSlotBank("{\"stateVersion\": 0, \"slots\": []}").ok());
    const SlotBankLoadResult empty = loadSlotBank("{\"stateVersion\": 1, \"slots\": []}");
    REQUIRE(empty.ok());
    CHECK(empty.bank->isEmpty(0));
    CHECK_FALSE(loadSlotBank("{\"stateVersion\": 1, \"slots\": [{\"name\": 4}]}").problems.empty());
}
