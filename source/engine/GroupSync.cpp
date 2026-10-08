#include "engine/GroupSync.h"

#include <algorithm>
#include <cmath>

namespace mm::engine {

namespace {

constexpr double kBarPpq = 4.0;
constexpr double kEpsilon = 1e-9;

double ceilToBar(double ppq) {
    return std::ceil((ppq - kEpsilon) / kBarPpq) * kBarPpq;
}

} // namespace

SlotDecision GroupSync::beginBlock(SlotMode mode, int ownSlot, const TransportInfo& transport, int numSamples,
                                   double sampleRate, bool restart) {
    ownSlot = std::clamp(ownSlot, 1, 16);
    const int member = member_.load(std::memory_order_acquire);
    const bool playing =
        transport.hasPosition && transport.isPlaying && transport.bpm > 0.0 && sampleRate > 0.0 && numSamples > 0;
    const double ppqPerSample = playing ? transport.bpm / 60.0 / sampleRate : 0.0;
    const double endPpq = transport.ppq + numSamples * ppqPerSample;

    if (!started_) {
        started_ = true;
        lastOwnSlot_ = ownSlot;
        current_ = ownSlot; // a voice starts with its stored slot (SPEC 6.1a, Z19)
    }
    // The hub plans from the furthest position of the group including its own block.
    if (member != GroupChannel::kNone) {
        channel_.reportBlock(member, endPpq, playing);
    }

    SlotDecision decision;
    decision.slot = ownSlot;

    switch (mode) {
    case SlotMode::Own:
        knownHub_ = GroupChannel::kNone;
        lastOwnSlot_ = ownSlot;
        current_ = ownSlot;
        return decision;

    case SlotMode::Hub: {
        channel_.reportHub(endPpq, ownSlot, playing);
        if (playing && ownSlot != lastOwnSlot_ && channel_.memberCount() > 1) {
            double furthest = endPpq;
            channel_.furthestPlaying(furthest);
            furthest = std::max(furthest, endPpq);
            const double leadPpq = channel_.minLeadMs() / 1000.0 * transport.bpm / 60.0;
            const double stamp = ceilToBar(furthest + leadPpq);
            channel_.publishPlan(stamp, ownSlot);
            decision.stamped = true;
            decision.stampPpq = stamp;
        }
        lastOwnSlot_ = ownSlot;
        current_ = ownSlot;
        return decision;
    }

    case SlotMode::FollowHub: {
        const int hub = channel_.hub();
        if (hub == GroupChannel::kNone || hub == member) {
            knownHub_ = GroupChannel::kNone;
            decision.slot = current_; // without a hub the voice plays on with the slot it has
            return decision;
        }
        if (hub != knownHub_) {
            knownHub_ = hub;
            seenSequence_ = channel_.plan().sequence; // plans of before do not concern us
            needSync_ = true;
        }
        if (restart) {
            needSync_ = true;
        }
        const GroupChannel::Plan plan = channel_.plan();
        if (plan.sequence != seenSequence_) {
            seenSequence_ = plan.sequence;
            current_ = plan.slot;
            needSync_ = false;
            decision.slot = current_;
            if (playing) {
                decision.stamped = true;
                decision.stampPpq = plan.stampPpq;
                if (plan.stampPpq < transport.ppq - kEpsilon) {
                    lateSwitches_.fetch_add(1, std::memory_order_relaxed); // the player takes the next bar line
                }
            }
            return decision;
        }
        if (needSync_) {
            const GroupChannel::HubState state = channel_.hubState();
            if (state.valid && state.ppq >= transport.ppq - kEpsilon) {
                current_ = state.slot;
                needSync_ = false;
            }
        }
        decision.slot = current_;
        return decision;
    }
    }
    return decision;
}

} // namespace mm::engine
