#pragma once

#include "core/InstanceSettings.h"
#include "core/SlotBank.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mm::core {

/// Where an instance stands in its group (SPEC 6.5).
enum class GroupStatus {
    Solo,           ///< no group
    Hub,            ///< is the hub; see `voices`
    VoiceConnected, ///< a hub exists and its slot set is adopted
    HubMissing,     ///< a voice without hub: keeps playing what it has
    HubOffered,     ///< a voice without hub that is asked to become the hub (the oldest voice)
    HubRefused      ///< asked to be hub, but the group has one: stays solo
};

/// An immutable copy of the slot set that the hub hands to the voices (CLAUDE.md: only immutable patterns cross
/// instances).
using SlotSnapshot = std::shared_ptr<const SlotBank>;

/// What a voice asks of the hub (SPEC 6.5: voice actions are forwarded to the hub).
enum class GroupAction { Generate };

/// An instance as the registry sees it. All calls come on the message thread.
class GroupMember {
public:
    virtual ~GroupMember() = default;
    /// Voice: the hub's slot set to adopt (sent whenever it changes and when the voice joins). `stampPpq` is the PPQ
    /// position at which the hub's changes take effect, when the hub planned one (D-144); it is only sent with the
    /// change itself, never to a voice that joins later.
    virtual void onSnapshot(const SlotSnapshot& snapshot, std::optional<double> stampPpq) = 0;
    /// The status or, for a hub, the number of voices changed.
    virtual void onStatus(GroupStatus status, size_t voices) = 0;
    /// Hub: a voice asked for `action`. The default ignores it.
    virtual void onAction(GroupAction action) { (void)action; }
    /// The slot set of this member as it is now (the registry asks when a member becomes hub).
    virtual SlotSnapshot currentSlots() = 0;
};

using MemberId = uint64_t;

/// The group of instances that share one process (SPEC 6.5, D-141): v1.0 knows one group with at most one hub.
/// Message thread only, no JUCE, no audio thread. The plugin owns the one process-wide registry; the class itself has
/// no global state, so tests build their own.
///
/// * Hub: its slot set goes to every voice, now and after each change (`publish`). A second hub is refused.
/// * Voice: adopts the hub's slot set; without a hub it keeps its own and reports `HubMissing`.
/// * Hub gone (removed or no longer hub): voices keep playing; the oldest voice is offered the hub role and takes it
///   only through `acceptOffer` (no silent takeover). A new hub's slot set replaces the voices' sets.
class GroupRegistry {
public:
    /// Adds a member (its id grows with every call, so a lower id is older) and applies `role`.
    MemberId add(std::shared_ptr<GroupMember> member, InstanceRole role = InstanceRole::Solo, int outputVoice = 1);
    void remove(MemberId id);

    /// Sets the role. A hub request while another hub exists is refused: the member becomes solo with status
    /// `HubRefused`. Returns the role the member has now.
    InstanceRole setRole(MemberId id, InstanceRole role);
    void setOutputVoice(MemberId id, int outputVoice);
    /// The hub reports a new slot set, optionally with the PPQ stamp at which it takes effect. False when `id` is not
    /// the hub.
    bool publish(MemberId id, SlotSnapshot snapshot, std::optional<double> stampPpq = std::nullopt);
    /// A voice forwards `action` to the hub. False when `id` is not a voice or there is no hub.
    bool forward(MemberId id, GroupAction action);
    /// A member with status `HubOffered` becomes the hub. False when it is not offered the role.
    bool acceptOffer(MemberId id);

    GroupStatus status(MemberId id) const;
    size_t voiceCount() const;
    size_t memberCount() const { return members_.size(); }
    /// The hub, or 0.
    MemberId hub() const { return hub_; }

private:
    struct Entry {
        MemberId id = 0;
        std::shared_ptr<GroupMember> member;
        InstanceRole role = InstanceRole::Solo;
        int outputVoice = 1;
        bool refused = false;
        bool statusSent = false;
        GroupStatus sentStatus = GroupStatus::Solo;
        size_t sentVoices = 0;
        SlotSnapshot sentSnapshot;
    };

    Entry* find(MemberId id);
    const Entry* find(MemberId id) const;
    MemberId oldestVoice() const;
    GroupStatus statusOf(const Entry& entry) const;
    /// Tells every member what changed since it last heard (after the state is consistent, so callbacks may call back).
    void notify();

    std::vector<Entry> members_; ///< in order of id
    MemberId hub_ = 0;
    SlotSnapshot hubSnapshot_;
    std::optional<double> hubStamp_; ///< stamp of the change being distributed; only set during `publish`
    MemberId nextId_ = 1;
};

} // namespace mm::core
