#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mm::engine {

/// Members per group channel (v1.0: one group per process, at most 8 voices plus the hub; the rest is reserve).
constexpr size_t kMaxGroupMembers = 64;

/// The lock-free channel between the audio threads of one hub group (SPEC 6.5, D-142). It is the one exception to
/// "instances only exchange immutable patterns on the message thread": it carries positions and planned slot changes
/// that audio threads read at every block. Fixed size, only atomics, no allocation, no lock.
///
/// * Every member reports the PPQ position its last block reached and whether it is playing.
/// * The hub additionally reports its slot (for start and jump) and publishes planned slot changes (stamp Q, slot,
///   sequence number); the sequence number grows by one with every plan and wraps.
/// * Members come and go on any thread (`acquire`/`release`); who the hub is gets set from the message thread.
///
/// Positions are stored in 1/960 PPQ ticks. A reader sees a position that is at most one block old.
class GroupChannel {
public:
    static constexpr int kNone = -1;

    GroupChannel();

    // --- any thread -----------------------------------------------------------------------------------------
    /// Reserves a member slot; `kNone` when all are taken.
    int acquire();
    void release(int member);
    size_t memberCount() const { return count_.load(std::memory_order_acquire); }

    // --- message thread -------------------------------------------------------------------------------------
    /// Names the hub (`kNone` clears). Clears the hub state; the plan sequence continues.
    void setHub(int member);
    /// The planning lead (SPEC 3.4, D-90): milliseconds a planned change must lie ahead of the furthest position.
    void setMinLeadMs(double milliseconds) { minLeadMs_.store(milliseconds, std::memory_order_relaxed); }

    // --- audio thread ---------------------------------------------------------------------------------------
    /// The member's block reached `endPpq`; `playing` false when the transport stands still.
    void reportBlock(int member, double endPpq, bool playing);
    /// The furthest block end among the playing members; false when none plays.
    bool furthestPlaying(double& endPpq) const;
    int hub() const { return hub_.load(std::memory_order_acquire); }
    double minLeadMs() const { return minLeadMs_.load(std::memory_order_relaxed); }

    /// What the hub reported last: the position its last block reached and the slot it plays or is about to play.
    struct HubState {
        bool valid = false; ///< false until the hub reported (after `setHub`)
        double ppq = 0.0;
        int slot = 0; ///< 1 to 16
        bool playing = false;
    };
    void reportHub(double endPpq, int slot, bool playing);
    HubState hubState() const;

    /// A slot change planned by the hub for `stampPpq`.
    struct Plan {
        uint32_t sequence = 0;
        double stampPpq = 0.0;
        int slot = 0; ///< 1 to 16
    };
    void publishPlan(double stampPpq, int slot);
    Plan plan() const;

private:
    static constexpr int64_t kNoHubState = INT64_MIN;

    std::array<std::atomic<bool>, kMaxGroupMembers> used_{};
    std::array<std::atomic<int64_t>, kMaxGroupMembers> position_{}; // (ticks << 1) | playing
    std::atomic<size_t> count_{0};
    std::atomic<int> hub_{kNone};
    std::atomic<int64_t> hubState_{kNoHubState}; // (ticks << 6) | (slot - 1) << 1 | playing
    std::atomic<uint64_t> plan_{0};              // (ticks << 21) | (slot - 1) << 16 | sequence
    std::atomic<double> minLeadMs_{150.0};
};

static_assert(std::atomic<int64_t>::is_always_lock_free, "the channel must stay lock-free");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "the channel must stay lock-free");
static_assert(std::atomic<int>::is_always_lock_free, "the channel must stay lock-free");
static_assert(std::atomic<bool>::is_always_lock_free, "the channel must stay lock-free");
static_assert(std::atomic<double>::is_always_lock_free, "the channel must stay lock-free");

/// The one channel of this plugin binary (like the registry: a group never spans bundles).
GroupChannel& processGroupChannel();

} // namespace mm::engine
