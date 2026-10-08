#pragma once

#include "core/PlaybackPattern.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mm::engine {

/// An immutable pattern with its notes, built on the message thread and handed to the audio thread (SPEC 6.3).
/// The request data (`gridPpq`, `stampPpq`, `hasStamp`) is set before publishing and constant afterwards.
struct OwnedPattern {
    std::vector<core::PatternNote> notes;
    uint32_t lengthTicks = 0;
    uint64_t version = 0;
    double gridPpq = 4.0;  ///< quantization grid of a switch
    double stampPpq = 0.0; ///< planned switch point (hub), valid when `hasStamp`
    bool hasStamp = false;
    std::shared_ptr<const void> lifetimeToken; ///< kept alive until the pattern is freed (tests count with it)

    core::PatternView view() const { return {notes.data(), notes.size(), lengthTicks}; }
};

/// Number of pattern slots per instance (SPEC 3.10).
constexpr size_t kSlotCount = 16;
/// Mailbox index meaning "the slot that is selected when the audio thread takes the request".
constexpr size_t kActiveSlot = kSlotCount;

/// Lock-free handover of patterns between the message thread (producer) and one audio thread (consumer).
///
/// * Each slot has one mailbox for results (quantized, SPEC 3.4) and one for edits (at once, SPEC 6.3); the extra
///   mailbox `kActiveSlot` addresses whichever slot is selected when the audio thread looks. A newer request
///   replaces an older one for the same slot that the audio thread has not seen; the producer frees the older
///   one itself. Requests for different slots never displace each other.
/// * The audio thread never allocates or frees: patterns it retires go into a fixed-capacity SPSC queue that the
///   message thread empties with `collectReturned()` (timer). A full queue makes the audio thread postpone the
///   change (SPEC 6.3).
class PatternHandover {
public:
    static constexpr size_t kReturnCapacity = 8;

    PatternHandover() = default;
    ~PatternHandover();
    PatternHandover(const PatternHandover&) = delete;
    PatternHandover& operator=(const PatternHandover&) = delete;

    // --- message thread -------------------------------------------------------------------------------------
    /// Result for `slot` (0-based, or `kActiveSlot`): stored there; it plays at the next grid point when that slot is
    /// the selected one (`gridPpq`, rounded up to a bar line).
    void publishResult(size_t slot, std::unique_ptr<OwnedPattern> pattern, double gridPpq);
    /// Same, planned by the hub at a PPQ stamp; a stamp already behind falls back to the next grid point.
    void publishResultAt(size_t slot, std::unique_ptr<OwnedPattern> pattern, double stampPpq, double gridPpq);
    /// Edit of `slot`: replaces its pattern at once without restart when that pattern is the one playing.
    void publishEdit(size_t slot, std::unique_ptr<OwnedPattern> pattern);
    /// The same for the selected slot.
    void publishSwitch(std::unique_ptr<OwnedPattern> pattern, double gridPpq) {
        publishResult(kActiveSlot, std::move(pattern), gridPpq);
    }
    void publishSwitchAt(std::unique_ptr<OwnedPattern> pattern, double stampPpq, double gridPpq) {
        publishResultAt(kActiveSlot, std::move(pattern), stampPpq, gridPpq);
    }
    void publishEdit(std::unique_ptr<OwnedPattern> pattern) { publishEdit(kActiveSlot, std::move(pattern)); }
    size_t collectReturned();
    /// Version of the pattern that is playing now (set by the audio thread).
    uint64_t activeVersion() const { return activeVersion_.load(std::memory_order_acquire); }
    /// Slot that is playing now, 1 to 16 (set by the audio thread).
    int activeSlot() const { return activeSlot_.load(std::memory_order_acquire); }

    // --- audio thread ---------------------------------------------------------------------------------------
    /// The audio thread is the only consumer: once a mailbox is seen non-empty it stays so until it takes it.
    bool hasResult(size_t mailbox) const { return results_[mailbox].load(std::memory_order_acquire) != nullptr; }
    bool hasEdit(size_t mailbox) const { return edits_[mailbox].load(std::memory_order_acquire) != nullptr; }
    OwnedPattern* takeResult(size_t mailbox) { return results_[mailbox].exchange(nullptr, std::memory_order_acq_rel); }
    OwnedPattern* takeEdit(size_t mailbox) { return edits_[mailbox].exchange(nullptr, std::memory_order_acq_rel); }
    size_t freeReturnSlots() const;
    /// Hands a retired pattern back for freeing. Returns false when the queue is full (nothing is stored).
    bool giveBack(OwnedPattern* pattern);
    void reportActive(uint64_t version, int slot) {
        activeVersion_.store(version, std::memory_order_release);
        activeSlot_.store(slot, std::memory_order_release);
    }

private:
    std::array<std::atomic<OwnedPattern*>, kSlotCount + 1> results_{};
    std::array<std::atomic<OwnedPattern*>, kSlotCount + 1> edits_{};
    std::array<std::atomic<OwnedPattern*>, kReturnCapacity> returns_{};
    std::atomic<size_t> head_{0}; // consumer (message thread)
    std::atomic<size_t> tail_{0}; // producer (audio thread)
    std::atomic<uint64_t> activeVersion_{0};
    std::atomic<int> activeSlot_{1};
};

static_assert(std::atomic<OwnedPattern*>::is_always_lock_free);
static_assert(std::atomic<size_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);

} // namespace mm::engine
