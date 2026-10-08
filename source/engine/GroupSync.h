#pragma once

#include "engine/GroupChannel.h"
#include "engine/PatternPlayer.h"

#include <cstdint>

namespace mm::engine {

/// What an instance does with the slot in a hub group (SPEC 6.1a, D-131).
enum class SlotMode {
    Own,       ///< Solo, a voice set to "own", or a member without a group: the own slot parameter decides
    Hub,       ///< the hub: plans slot changes for the whole group
    FollowHub, ///< a voice set to "follows hub": plays the hub's slot
};

/// The slot the player is to take this block; `stamped` carries the planned switch point.
struct SlotDecision {
    int slot = 1; ///< 1 to 16
    bool stamped = false;
    double stampPpq = 0.0;
};

/// Audio-thread logic of a group member (one per instance): reports the block position to the channel, lets the hub
/// plan slot changes and lets a voice follow them. No allocation, no lock. Call `beginBlock` once per block before
/// `PatternPlayer::process` and hand the decision to the player (`setSlot` or `setSlotAt`).
///
/// Hub (SPEC 3.4, 6.1a): when the slot parameter changes while playing and other members exist, the switch point Q is
/// the first bar line at or after (furthest block end of the group + minimum lead); the hub switches there itself
/// and publishes the plan. Voice (follows hub): switches at the planned Q; when Q is already behind its block start
/// it takes the next bar line instead (the player does that) and counts it in `lateSwitches`. At start and jump, and
/// when a hub appears, it takes the hub's slot as soon as the hub's reported position has reached its block start:
/// at once at a start or jump, else at the next bar line. Without a hub it keeps the slot it has.
class GroupSync {
public:
    GroupSync(GroupChannel& channel, int member) : channel_(channel), member_(member) {}

    /// `restart`: this block is a start or a jump (`PatternPlayer::startsOrJumps`). `ownSlot` is the slot parameter.
    SlotDecision beginBlock(SlotMode mode, int ownSlot, const TransportInfo& transport, int numSamples,
                            double sampleRate, bool restart);

    /// Planned switches that reached this voice too late for their point (shown or logged outside the audio thread).
    uint32_t lateSwitches() const { return lateSwitches_.load(std::memory_order_relaxed); }

private:
    GroupChannel& channel_;
    int member_;
    bool started_ = false;
    int lastOwnSlot_ = 1;
    int current_ = 1; // the slot a following voice plays
    int knownHub_ = GroupChannel::kNone;
    uint32_t seenSequence_ = 0;
    bool needSync_ = false; // take the hub's slot as soon as it has reported a position that has reached us
    std::atomic<uint32_t> lateSwitches_{0};
};

} // namespace mm::engine
