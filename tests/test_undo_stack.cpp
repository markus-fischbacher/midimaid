#include "core/PatternEdit.h"
#include "core/Random.h"
#include "core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace mm::core;

namespace {

Pattern result(const std::string& style) {
    auto pattern = makeEmptyPattern(1, style);
    addNote(pattern, 0, 45, 0, 240, 100);
    return pattern;
}

/// The same patterns apart from the version the bank hands out.
bool samePattern(const Pattern& a, const Pattern& b) {
    auto x = a;
    x.version = b.version;
    return x == b;
}

bool sameSlot(const Slot& a, const Slot& b) {
    if (a.pattern.has_value() != b.pattern.has_value() || a.history.size() != b.history.size() ||
        a.cursor != b.cursor) {
        return false;
    }
    if (a.pattern && !samePattern(*a.pattern, *b.pattern)) {
        return false;
    }
    for (size_t i = 0; i < a.history.size(); ++i) {
        if (!samePattern(a.history[i], b.history[i])) {
            return false;
        }
    }
    return true;
}

bool sameBank(const SlotBank& a, const SlotBank& b) {
    for (size_t i = 0; i < kSlotCount; ++i) {
        if (!sameSlot(*a.slot(i), *b.slot(i))) {
            return false;
        }
    }
    return true;
}

/// What the plug-in does for a note edit.
void editNote(SlotBank& bank, UndoStack& stack, size_t slot, uint8_t pitch, uint32_t start, bool merge = false) {
    const Pattern before = *bank.slot(slot)->pattern;
    Pattern after = before;
    REQUIRE(addNote(after, 0, pitch, start, 60, 90) != 0);
    REQUIRE(bank.edit(slot, after));
    stack.recordEdit(slot, before, after, merge);
}

/// What the plug-in does for a result.
void addResult(SlotBank& bank, UndoStack& stack, size_t slot, const std::string& style) {
    const auto before = bank.slot(slot)->pattern;
    const auto cursor = bank.slot(slot)->cursor;
    REQUIRE(bank.setResult(slot, result(style)));
    stack.recordResult(slot, before, cursor, *bank.slot(slot)->pattern);
}

} // namespace

TEST_CASE("undo and redo of a note edit", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    const auto original = *bank.slot(0)->pattern;
    editNote(bank, stack, 0, 50, 480);
    const auto edited = *bank.slot(0)->pattern;
    size_t slot = 99;
    REQUIRE(stack.undo(bank, &slot));
    CHECK(slot == 0);
    CHECK(samePattern(*bank.slot(0)->pattern, original));
    CHECK(bank.slot(0)->history.size() == 1); // an edit is not a history entry
    REQUIRE(stack.redo(bank));
    CHECK(samePattern(*bank.slot(0)->pattern, edited));
}

TEST_CASE("undoing a result takes it out of the history, redoing puts it back", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    const auto first = *bank.slot(0)->pattern;
    addResult(bank, stack, 0, "b");
    REQUIRE(bank.slot(0)->history.size() == 2);
    REQUIRE(stack.undo(bank));
    CHECK(bank.slot(0)->history.size() == 1);
    CHECK(bank.slot(0)->cursor == 0);
    CHECK(samePattern(*bank.slot(0)->pattern, first));
    REQUIRE(stack.undo(bank)); // the first result: the slot is empty again
    CHECK(bank.isEmpty(0));
    CHECK(bank.slot(0)->history.empty());
    REQUIRE(stack.redo(bank));
    REQUIRE(stack.redo(bank));
    CHECK(bank.slot(0)->history.size() == 2);
    CHECK(bank.slot(0)->pattern->styleId == "b");
    CHECK_FALSE(stack.redo(bank));
}

TEST_CASE("an edit before a result survives undoing the result", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    editNote(bank, stack, 0, 50, 480);
    const auto edited = *bank.slot(0)->pattern;
    addResult(bank, stack, 0, "b");
    REQUIRE(stack.undo(bank));
    CHECK(samePattern(*bank.slot(0)->pattern, edited)); // not the stored result without the edit
}

TEST_CASE("a new step ends the way back to the undone ones", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    editNote(bank, stack, 0, 50, 480);
    REQUIRE(stack.undo(bank));
    CHECK(stack.canRedo());
    editNote(bank, stack, 0, 52, 960);
    CHECK_FALSE(stack.canRedo());
    CHECK_FALSE(stack.redo(bank));
}

TEST_CASE("edits of one gesture merge into one step", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    const auto original = *bank.slot(0)->pattern;
    editNote(bank, stack, 0, 50, 480);
    editNote(bank, stack, 0, 52, 960, true);
    editNote(bank, stack, 0, 54, 1440, true);
    CHECK(stack.undoSteps() == 2); // the result and the gesture
    REQUIRE(stack.undo(bank));
    CHECK(samePattern(*bank.slot(0)->pattern, original));
    REQUIRE(stack.redo(bank));
    CHECK(bank.slot(0)->pattern->voices[0].notes.size() == 4);
    // Another slot or a step in between is not merged.
    addResult(bank, stack, 1, "b");
    editNote(bank, stack, 1, 50, 480, true);
    editNote(bank, stack, 0, 50, 2000, true);
    CHECK(stack.undoSteps() == 5);
}

TEST_CASE("steps of different slots undo in order", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    addResult(bank, stack, 3, "b");
    size_t slot = 0;
    REQUIRE(stack.undo(bank, &slot));
    CHECK(slot == 3);
    CHECK(bank.isEmpty(3));
    CHECK_FALSE(bank.isEmpty(0));
}

TEST_CASE("the stack keeps at most 100 steps", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    for (int i = 0; i < 150; ++i) {
        editNote(bank, stack, 0, 40, 0);
    }
    CHECK(stack.undoSteps() == kMaxUndoSteps);
    size_t undone = 0;
    while (stack.undo(bank)) {
        ++undone;
    }
    CHECK(undone == kMaxUndoSteps);
    CHECK(bank.slot(0)->pattern->voices[0].notes.size() == 1 + 50); // the 50 oldest edits stay
}

TEST_CASE("discardSlot forgets the steps of that slot only", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    addResult(bank, stack, 1, "b");
    editNote(bank, stack, 0, 50, 480);
    REQUIRE(stack.undo(bank));
    stack.discardSlot(0);
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoSteps() == 1);
    size_t slot = 0;
    REQUIRE(stack.undo(bank, &slot));
    CHECK(slot == 1);
    CHECK_FALSE(stack.canUndo());
    stack.clear();
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("a step that no longer fits is dropped and reported", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    editNote(bank, stack, 0, 50, 480);
    bank.clear(0);                 // somebody else cleared the slot
    CHECK_FALSE(stack.undo(bank)); // an edit cannot be put into an empty slot
    CHECK(stack.undoSteps() == 1);
    CHECK_FALSE(stack.undo(bank)); // the result: the history is empty
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("random series of edits and results go all the way back and forth", "[undo][series]") {
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        SlotBank bank;
        UndoStack stack;
        const SlotBank initial = bank;
        for (int step = 0; step < 60; ++step) {
            const auto slot = static_cast<size_t>(rng.bounded(3));
            if (bank.isEmpty(slot) || rng.chance(30)) {
                addResult(bank, stack, slot, "s" + std::to_string(step));
            } else {
                editNote(bank, stack, slot, static_cast<uint8_t>(30 + rng.bounded(60)), rng.bounded(3000),
                         rng.chance(30));
            }
        }
        const SlotBank final = bank;
        size_t undone = 0;
        while (stack.undo(bank)) {
            ++undone;
        }
        INFO("seed " << seed);
        CHECK(undone == stack.redoSteps());
        CHECK(sameBank(bank, initial));
        while (stack.redo(bank)) {
        }
        CHECK(sameBank(bank, final));
        CHECK(stack.undoSteps() == undone);
    }
}

TEST_CASE("undoing a result puts the history cursor back where it was", "[undo]") {
    SlotBank bank;
    UndoStack stack;
    addResult(bank, stack, 0, "a");
    addResult(bank, stack, 0, "b");
    REQUIRE(bank.historyBack(0)); // looking at "a", the cursor is 0
    stack.discardSlot(0);         // what browsing does to the steps of the slot
    addResult(bank, stack, 0, "c");
    REQUIRE(bank.slot(0)->cursor == 2);
    REQUIRE(stack.undo(bank));
    CHECK(bank.slot(0)->cursor == 0);
    CHECK(bank.slot(0)->pattern->styleId == "a");
    CHECK(bank.slot(0)->history.size() == 2);
}
