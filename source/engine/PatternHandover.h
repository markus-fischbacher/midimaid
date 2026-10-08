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

/// Lock-free handover of patterns between the message thread (producer) and one audio thread (consumer).
///
/// * At most one pending switch and one pending edit exist. A newer one replaces an older one that the audio thread
///   has not seen; the producer frees the older one itself.
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
    /// Quantized switch at the next grid point (`gridPpq`, rounded up to a bar line).
    void publishSwitch(std::unique_ptr<OwnedPattern> pattern, double gridPpq);
    /// Planned switch at a PPQ stamp (hub); a stamp that is already behind falls back to the next grid point.
    void publishSwitchAt(std::unique_ptr<OwnedPattern> pattern, double stampPpq, double gridPpq);
    /// Edit without quantization and without restart (SPEC 6.3).
    void publishEdit(std::unique_ptr<OwnedPattern> pattern);
    /// Frees the patterns the audio thread has retired; returns how many.
    size_t collectReturned();
    /// Version of the pattern that is playing now (set by the audio thread).
    uint64_t activeVersion() const { return activeVersion_.load(std::memory_order_acquire); }

    // --- audio thread ---------------------------------------------------------------------------------------
    OwnedPattern* takeSwitch() { return switch_.exchange(nullptr, std::memory_order_acq_rel); }
    OwnedPattern* takeEdit() { return edit_.exchange(nullptr, std::memory_order_acq_rel); }
    size_t freeReturnSlots() const;
    /// Hands a retired pattern back for freeing. Returns false when the queue is full (nothing is stored).
    bool giveBack(OwnedPattern* pattern);
    void reportActiveVersion(uint64_t version) { activeVersion_.store(version, std::memory_order_release); }

private:
    std::atomic<OwnedPattern*> switch_{nullptr};
    std::atomic<OwnedPattern*> edit_{nullptr};
    std::array<std::atomic<OwnedPattern*>, kReturnCapacity> returns_{};
    std::atomic<size_t> head_{0}; // consumer (message thread)
    std::atomic<size_t> tail_{0}; // producer (audio thread)
    std::atomic<uint64_t> activeVersion_{0};
};

static_assert(std::atomic<OwnedPattern*>::is_always_lock_free);
static_assert(std::atomic<size_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);

} // namespace mm::engine
