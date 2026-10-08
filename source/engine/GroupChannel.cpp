#include "engine/GroupChannel.h"

#include <algorithm>
#include <cmath>

namespace mm::engine {

namespace {

constexpr double kTicksPerPpq = 960.0;
constexpr int64_t kMaxPositionTicks = (int64_t{1} << 56) - 1; // fits (ticks << 6) and (ticks << 1)
constexpr int64_t kMaxPlanTicks = (int64_t{1} << 42) - 1;     // fits (ticks << 21) in 64 bits

int64_t toTicks(double ppq, int64_t limit) {
    double ticks = std::floor(ppq * kTicksPerPpq);
    if (ticks != ticks) {
        return 0; // NaN
    }
    ticks = std::clamp(ticks, -static_cast<double>(limit), static_cast<double>(limit));
    return static_cast<int64_t>(ticks);
}

double toPpq(int64_t ticks) {
    return static_cast<double>(ticks) / kTicksPerPpq;
}

int slotBits(int slot) {
    return std::clamp(slot, 1, 16) - 1;
}

} // namespace

GroupChannel::GroupChannel() {
    for (auto& flag : used_) {
        flag.store(false, std::memory_order_relaxed);
    }
    for (auto& position : position_) {
        position.store(0, std::memory_order_relaxed);
    }
}

int GroupChannel::acquire() {
    for (size_t i = 0; i < kMaxGroupMembers; ++i) {
        bool expected = false;
        if (used_[i].compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            position_[i].store(0, std::memory_order_release); // not playing
            count_.fetch_add(1, std::memory_order_acq_rel);
            return static_cast<int>(i);
        }
    }
    return kNone;
}

void GroupChannel::release(int member) {
    if (member < 0 || member >= static_cast<int>(kMaxGroupMembers)) {
        return;
    }
    position_[static_cast<size_t>(member)].store(0, std::memory_order_release);
    bool expected = true;
    if (used_[static_cast<size_t>(member)].compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        count_.fetch_sub(1, std::memory_order_acq_rel);
        int hub = member;
        hub_.compare_exchange_strong(hub, kNone, std::memory_order_acq_rel); // the hub left
    }
}

void GroupChannel::setHub(int member) {
    hubState_.store(kNoHubState, std::memory_order_release);
    hub_.store(member, std::memory_order_release);
}

void GroupChannel::reportBlock(int member, double endPpq, bool playing) {
    if (member < 0 || member >= static_cast<int>(kMaxGroupMembers)) {
        return;
    }
    const int64_t ticks = toTicks(endPpq, kMaxPositionTicks);
    position_[static_cast<size_t>(member)].store((ticks << 1) | (playing ? 1 : 0), std::memory_order_release);
}

bool GroupChannel::furthestPlaying(double& endPpq) const {
    bool any = false;
    int64_t best = 0;
    for (size_t i = 0; i < kMaxGroupMembers; ++i) {
        if (!used_[i].load(std::memory_order_acquire)) {
            continue;
        }
        const int64_t value = position_[i].load(std::memory_order_acquire);
        if ((value & 1) == 0) {
            continue;
        }
        const int64_t ticks = value >> 1;
        if (!any || ticks > best) {
            best = ticks;
            any = true;
        }
    }
    if (any) {
        endPpq = toPpq(best);
    }
    return any;
}

void GroupChannel::reportHub(double endPpq, int slot, bool playing) {
    const int64_t ticks = toTicks(endPpq, kMaxPositionTicks);
    hubState_.store((ticks << 6) | (static_cast<int64_t>(slotBits(slot)) << 1) | (playing ? 1 : 0),
                    std::memory_order_release);
}

GroupChannel::HubState GroupChannel::hubState() const {
    const int64_t value = hubState_.load(std::memory_order_acquire);
    HubState state;
    if (value == kNoHubState) {
        return state;
    }
    state.valid = true;
    state.playing = (value & 1) != 0;
    state.slot = static_cast<int>((value >> 1) & 0x1F) + 1;
    state.ppq = toPpq(value >> 6);
    return state;
}

void GroupChannel::publishPlan(double stampPpq, int slot) {
    const uint64_t previous = plan_.load(std::memory_order_acquire);
    const uint64_t sequence = ((previous & 0xFFFF) + 1) & 0xFFFF;
    const int64_t ticks = toTicks(stampPpq, kMaxPlanTicks);
    const uint64_t value =
        (static_cast<uint64_t>(ticks) << 21) | (static_cast<uint64_t>(slotBits(slot)) << 16) | sequence;
    plan_.store(value, std::memory_order_release);
}

GroupChannel::Plan GroupChannel::plan() const {
    const uint64_t value = plan_.load(std::memory_order_acquire);
    Plan plan;
    plan.sequence = static_cast<uint32_t>(value & 0xFFFF);
    plan.slot = static_cast<int>((value >> 16) & 0x1F) + 1;
    plan.stampPpq = toPpq(static_cast<int64_t>(value) >> 21);
    return plan;
}

GroupChannel& processGroupChannel() {
    static GroupChannel channel;
    return channel;
}

} // namespace mm::engine
