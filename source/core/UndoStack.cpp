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

void UndoStack::recordSwap(size_t first, size_t second) {
    Step step;
    step.kind = Step::Kind::Swap;
    step.slot = first;
    step.other = second;
    push(std::move(step));
}

void UndoStack::recordClear(size_t slot, Slot before) {
    Step step;
    step.kind = Step::Kind::Clear;
    step.slot = slot;
    step.clearedSlot = std::move(before);
    push(std::move(step));
}

bool UndoStack::undo(SlotBank& bank, size_t* slot, size_t* second) {
    if (undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    bool done = false;
    switch (step.kind) {
    case Step::Kind::Edit:
        done = step.before.has_value() && bank.edit(step.slot, *step.before);
        break;
    case Step::Kind::Result:
        done = bank.undoResult(step.slot, step.before, step.beforeCursor);
        break;
    case Step::Kind::Swap:
        done = bank.swap(step.slot, step.other); // a swap undoes itself
        break;
    case Step::Kind::Clear:
        done = step.clearedSlot.has_value() && bank.slot(step.slot) != nullptr;
        if (done) {
            bank.restore(step.slot, *step.clearedSlot);
        }
        break;
    }
    if (!done) {
        return false;
    }
    if (slot != nullptr) {
        *slot = step.slot;
    }
    if (second != nullptr) {
        *second = step.other;
    }
    redo_.push_back(std::move(step));
    return true;
}

bool UndoStack::redo(SlotBank& bank, size_t* slot, size_t* second) {
    if (redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    bool done = false;
    switch (step.kind) {
    case Step::Kind::Edit:
        done = bank.edit(step.slot, step.after);
        break;
    case Step::Kind::Result:
        done = bank.setResult(step.slot, step.after);
        break;
    case Step::Kind::Swap:
        done = bank.swap(step.slot, step.other);
        break;
    case Step::Kind::Clear:
        done = bank.clear(step.slot);
        break;
    }
    if (!done) {
        return false;
    }
    if (slot != nullptr) {
        *slot = step.slot;
    }
    if (second != nullptr) {
        *second = step.other;
    }
    undo_.push_back(std::move(step));
    return true;
}

void UndoStack::discardSlot(size_t slot) {
    const auto of = [slot](const Step& step) { return step.slot == slot || step.other == slot; };
    undo_.erase(std::remove_if(undo_.begin(), undo_.end(), of), undo_.end());
    redo_.erase(std::remove_if(redo_.begin(), redo_.end(), of), redo_.end());
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace mm::core
