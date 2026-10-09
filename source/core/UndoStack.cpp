#include "core/UndoStack.h"

#include <algorithm>
#include <utility>

namespace mm::core {

void UndoStack::push(Step step) {
    redo_.clear(); // a new step ends the way back to the undone ones
    undo_.push_back(std::move(step));
    if (undo_.size() > kMaxUndoSteps) {
        undo_.erase(undo_.begin());
    }
}

void UndoStack::recordEdit(size_t slot, Pattern before, Pattern after, bool mergeWithPrevious) {
    if (mergeWithPrevious && !undo_.empty() && redo_.empty() && undo_.back().kind == Step::Kind::Edit &&
        undo_.back().slot == slot) {
        undo_.back().after = std::move(after);
        return;
    }
    Step step;
    step.kind = Step::Kind::Edit;
    step.slot = slot;
    step.before = std::move(before);
    step.after = std::move(after);
    push(std::move(step));
}

void UndoStack::recordResult(size_t slot, std::optional<Pattern> before, size_t beforeCursor, Pattern result) {
    Step step;
    step.kind = Step::Kind::Result;
    step.slot = slot;
    step.before = std::move(before);
    step.beforeCursor = beforeCursor;
    step.after = std::move(result);
    push(std::move(step));
}

bool UndoStack::undo(SlotBank& bank, size_t* slot) {
    if (undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    const bool done = step.kind == Step::Kind::Edit ? step.before.has_value() && bank.edit(step.slot, *step.before)
                                                    : bank.undoResult(step.slot, step.before, step.beforeCursor);
    if (!done) {
        return false;
    }
    if (slot != nullptr) {
        *slot = step.slot;
    }
    redo_.push_back(std::move(step));
    return true;
}

bool UndoStack::redo(SlotBank& bank, size_t* slot) {
    if (redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    const bool done =
        step.kind == Step::Kind::Edit ? bank.edit(step.slot, step.after) : bank.setResult(step.slot, step.after);
    if (!done) {
        return false;
    }
    if (slot != nullptr) {
        *slot = step.slot;
    }
    undo_.push_back(std::move(step));
    return true;
}

void UndoStack::discardSlot(size_t slot) {
    const auto of = [slot](const Step& step) { return step.slot == slot; };
    undo_.erase(std::remove_if(undo_.begin(), undo_.end(), of), undo_.end());
    redo_.erase(std::remove_if(redo_.begin(), redo_.end(), of), redo_.end());
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace mm::core
