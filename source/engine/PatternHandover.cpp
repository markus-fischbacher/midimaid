#include "engine/PatternHandover.h"

namespace mm::engine {

PatternHandover::~PatternHandover() {
    // The audio thread no longer runs (host contract): free everything that is still in flight.
    for (auto& mailbox : results_) {
        std::unique_ptr<OwnedPattern>(mailbox.exchange(nullptr));
    }
    for (auto& mailbox : edits_) {
        std::unique_ptr<OwnedPattern>(mailbox.exchange(nullptr));
    }
    collectReturned();
}

void PatternHandover::publishResult(size_t slot, std::unique_ptr<OwnedPattern> pattern, double gridPpq) {
    pattern->gridPpq = gridPpq;
    pattern->hasStamp = false;
    // A result the audio thread has not taken yet is replaced; it was never seen there and is freed here.
    std::unique_ptr<OwnedPattern> replaced(results_[slot].exchange(pattern.release(), std::memory_order_acq_rel));
}

void PatternHandover::publishResultAt(size_t slot, std::unique_ptr<OwnedPattern> pattern, double stampPpq,
                                      double gridPpq) {
    pattern->gridPpq = gridPpq;
    pattern->stampPpq = stampPpq;
    pattern->hasStamp = true;
    std::unique_ptr<OwnedPattern> replaced(results_[slot].exchange(pattern.release(), std::memory_order_acq_rel));
}

void PatternHandover::publishEdit(size_t slot, std::unique_ptr<OwnedPattern> pattern) {
    std::unique_ptr<OwnedPattern> replaced(edits_[slot].exchange(pattern.release(), std::memory_order_acq_rel));
}

size_t PatternHandover::collectReturned() {
    size_t count = 0;
    size_t head = head_.load(std::memory_order_relaxed);
    while (head != tail_.load(std::memory_order_acquire)) {
        std::unique_ptr<OwnedPattern> retired(returns_[head % kReturnCapacity].load(std::memory_order_relaxed));
        ++head;
        head_.store(head, std::memory_order_release);
        ++count;
    }
    return count;
}

size_t PatternHandover::freeReturnSlots() const {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    return kReturnCapacity - (tail - head);
}

bool PatternHandover::giveBack(OwnedPattern* pattern) {
    if (freeReturnSlots() == 0) {
        return false;
    }
    const size_t tail = tail_.load(std::memory_order_relaxed);
    returns_[tail % kReturnCapacity].store(pattern, std::memory_order_relaxed);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

} // namespace mm::engine
