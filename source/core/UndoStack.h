#pragma once

#include "core/Pattern.h"
#include "core/SlotBank.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace mm::core {

/// Most steps the stack keeps; older ones drop out.
constexpr size_t kMaxUndoSteps = 100;

/// Undo and redo of the slot bank (SPEC 3.5, 3.6, D-153). One stack per instance for all slots; no JUCE, message
/// thread only. A step is either a note edit (pattern before and after) or a result (generate, later variation and
/// refine: appended to the history of its slot). Undoing a result takes it out of the history again and redoing it
/// appends it again; with a full history (20 results) the entry that fell out for it does not come back. Browsing the
/// history is no step: the steps of that slot stop fitting and `discardSlot` removes them. The stack is not saved in
/// the project.
///
/// Steps put the stored patterns back; the `Pattern::version` that the bank hands out is new each time, everything
/// else of the pattern is as it was.
class UndoStack {
public:
    /// A note edit of `slot` from `before` to `after`. With `mergeWithPrevious` the edit continues the previous step
    /// (one mouse gesture is one step) when that is an edit of the same slot; only its end result changes.
    void recordEdit(size_t slot, Pattern before, Pattern after, bool mergeWithPrevious = false);
    /// A result that was appended to `slot`: `before` is the pattern the slot had (none: it was empty),
    /// `beforeCursor` its history cursor.
    void recordResult(size_t slot, std::optional<Pattern> before, size_t beforeCursor, Pattern result);

    /// Two slots were exchanged (clear and swap are steps too, D-202: a dropped slot must never be lost for good).
    void recordSwap(size_t first, size_t second);
    /// `slot` was cleared; `before` is the whole slot with its history, name and colour.
    void recordClear(size_t slot, Slot before);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoSteps() const { return undo_.size(); }
    size_t redoSteps() const { return redo_.size(); }

    /// Takes the last step back on `bank`. False when there is none or the bank no longer fits (the step is dropped
    /// then). `slot` receives the slot that changed; `second` the other slot of a swap (`kNoSlot` for any other step).
    static constexpr size_t kNoSlot = static_cast<size_t>(-1);
    bool undo(SlotBank& bank, size_t* slot = nullptr, size_t* second = nullptr);
    bool redo(SlotBank& bank, size_t* slot = nullptr, size_t* second = nullptr);

    /// Forgets every step of the slot (after browsing the history).
    void discardSlot(size_t slot);
    void clear();

private:
    struct Step {
        enum class Kind { Edit, Result, Swap, Clear } kind = Kind::Edit;
        size_t slot = 0;
        size_t other = kNoSlot;          ///< `Swap`: the second slot
        std::optional<Slot> clearedSlot; ///< `Clear`: the slot as it was
        std::optional<Pattern> before;
        size_t beforeCursor = 0;
        Pattern after;
    };

    void push(Step step);

    std::vector<Step> undo_;
    std::vector<Step> redo_;
};

} // namespace mm::core
