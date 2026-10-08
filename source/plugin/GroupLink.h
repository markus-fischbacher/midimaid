#pragma once

#include "core/GroupRegistry.h"

#include <atomic>
#include <juce_events/juce_events.h>
#include <memory>

namespace mm::plugin {

class ProcessorBase;

/// The one process-wide registry of this plugin binary (SPEC 6.5, D-141). Message thread only. Every plugin bundle
/// is its own binary and has its own registry, so a group never spans formats or variants (O-28).
mm::core::GroupRegistry& processGroup();

/// Connects one processor to the registry. The registry only lives on the message thread, but hosts create and
/// destroy instances on other threads too, so the link moves every registry call there and keeps the processor out
/// of reach once it is being destroyed: `detach()` returns only when no callback is running anymore.
class GroupLink : public mm::core::GroupMember, public std::enable_shared_from_this<GroupLink> {
public:
    static std::shared_ptr<GroupLink> create(ProcessorBase& owner);
    ~GroupLink() override;

    /// Any thread: role and voice this instance wants; applied on the message thread (at once when already there).
    void setRole(mm::core::InstanceRole role, int outputVoice);
    /// Any thread: the hub's new slot set.
    void publish(mm::core::SlotSnapshot snapshot);
    /// Message thread: takes the hub role that was offered.
    void acceptOffer();
    /// Owner is going away: no callback reaches it after this returns; leaves the registry.
    void detach();

    mm::core::GroupStatus status() const { return status_.load(); }
    size_t voices() const { return voices_.load(); }

    // GroupMember (message thread)
    void onSnapshot(const mm::core::SlotSnapshot& snapshot) override;
    void onStatus(mm::core::GroupStatus status, size_t voices) override;
    mm::core::SlotSnapshot currentSlots() override;

private:
    explicit GroupLink(ProcessorBase& owner);
    void sync();                                    // message thread: applies the wanted role to the registry
    void leave();                                   // message thread
    void onMessageThread(std::function<void()> fn); // runs `fn` now or posts it

    juce::CriticalSection lock_; // guards `owner_` and the wanted state; never held while calling into the registry
    ProcessorBase* owner_;
    mm::core::InstanceRole wantedRole_ = mm::core::InstanceRole::Solo;
    int wantedVoice_ = 1;
    mm::core::MemberId id_ = 0; // message thread
    bool left_ = false;         // message thread
    std::atomic<mm::core::GroupStatus> status_{mm::core::GroupStatus::Solo};
    std::atomic<size_t> voices_{0};
};

} // namespace mm::plugin
