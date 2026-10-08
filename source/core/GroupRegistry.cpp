#include "core/GroupRegistry.h"

#include <algorithm>

namespace mm::core {

GroupRegistry::Entry* GroupRegistry::find(MemberId id) {
    for (auto& entry : members_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const GroupRegistry::Entry* GroupRegistry::find(MemberId id) const {
    return const_cast<GroupRegistry*>(this)->find(id);
}

MemberId GroupRegistry::oldestVoice() const {
    for (const auto& entry : members_) {
        if (entry.role == InstanceRole::Voice) {
            return entry.id;
        }
    }
    return 0;
}

size_t GroupRegistry::voiceCount() const {
    return static_cast<size_t>(std::count_if(members_.begin(), members_.end(),
                                             [](const Entry& entry) { return entry.role == InstanceRole::Voice; }));
}

GroupStatus GroupRegistry::statusOf(const Entry& entry) const {
    switch (entry.role) {
    case InstanceRole::Hub:
        return GroupStatus::Hub;
    case InstanceRole::Voice:
        if (hub_ != 0) {
            return GroupStatus::VoiceConnected;
        }
        return oldestVoice() == entry.id ? GroupStatus::HubOffered : GroupStatus::HubMissing;
    case InstanceRole::Solo:
        break;
    }
    return entry.refused ? GroupStatus::HubRefused : GroupStatus::Solo;
}

GroupStatus GroupRegistry::status(MemberId id) const {
    const Entry* entry = find(id);
    return entry != nullptr ? statusOf(*entry) : GroupStatus::Solo;
}

MemberId GroupRegistry::add(std::shared_ptr<GroupMember> member, InstanceRole role, int outputVoice) {
    Entry entry;
    entry.id = nextId_++;
    entry.member = std::move(member);
    entry.outputVoice = clampOutputVoice(outputVoice);
    const MemberId id = entry.id;
    members_.push_back(std::move(entry));
    setRole(id, role);
    return id;
}

void GroupRegistry::remove(MemberId id) {
    const auto it = std::find_if(members_.begin(), members_.end(), [id](const Entry& e) { return e.id == id; });
    if (it == members_.end()) {
        return;
    }
    members_.erase(it);
    if (hub_ == id) {
        hub_ = 0;
        hubSnapshot_.reset();
    }
    notify();
}

InstanceRole GroupRegistry::setRole(MemberId id, InstanceRole role) {
    Entry* entry = find(id);
    if (entry == nullptr) {
        return InstanceRole::Solo;
    }
    entry->refused = false;
    entry->sentSnapshot.reset(); // a voice that joins (again) hears the hub's slot set
    if (role == InstanceRole::Hub) {
        if (hub_ != 0 && hub_ != id) {
            entry->role = InstanceRole::Solo;
            entry->refused = true;
        } else {
            // The hub's slot set is the state of the group from now on.
            entry->role = InstanceRole::Hub;
            hub_ = id;
            hubSnapshot_ = entry->member->currentSlots();
            entry = find(id); // the callback may have changed the member list
        }
    } else {
        if (hub_ == id) {
            hub_ = 0;
            hubSnapshot_.reset();
        }
        entry->role = role;
    }
    const InstanceRole result = entry != nullptr ? entry->role : InstanceRole::Solo;
    notify();
    return result;
}

void GroupRegistry::setOutputVoice(MemberId id, int outputVoice) {
    if (Entry* entry = find(id)) {
        entry->outputVoice = clampOutputVoice(outputVoice);
    }
}

bool GroupRegistry::publish(MemberId id, SlotSnapshot snapshot) {
    if (id == 0 || id != hub_) {
        return false;
    }
    hubSnapshot_ = std::move(snapshot);
    notify();
    return true;
}

bool GroupRegistry::acceptOffer(MemberId id) {
    const Entry* entry = find(id);
    if (entry == nullptr || statusOf(*entry) != GroupStatus::HubOffered) {
        return false;
    }
    return setRole(id, InstanceRole::Hub) == InstanceRole::Hub;
}

void GroupRegistry::notify() {
    struct Call {
        MemberId id;
        std::shared_ptr<GroupMember> member;
        bool status = false;
        GroupStatus newStatus = GroupStatus::Solo;
        size_t voices = 0;
        SlotSnapshot snapshot;
    };
    std::vector<Call> calls;
    const size_t voices = voiceCount();
    for (auto& entry : members_) {
        Call call{entry.id, entry.member, false, GroupStatus::Solo, 0, nullptr};
        const GroupStatus now = statusOf(entry);
        const size_t count = entry.role == InstanceRole::Hub ? voices : 0;
        if (!entry.statusSent || entry.sentStatus != now || entry.sentVoices != count) {
            entry.statusSent = true;
            entry.sentStatus = now;
            entry.sentVoices = count;
            call.status = true;
            call.newStatus = now;
            call.voices = count;
        }
        if (entry.role == InstanceRole::Voice && hubSnapshot_ != nullptr && entry.sentSnapshot != hubSnapshot_) {
            entry.sentSnapshot = hubSnapshot_;
            call.snapshot = hubSnapshot_;
        }
        if (call.status || call.snapshot) {
            calls.push_back(std::move(call));
        }
    }
    // The state is consistent now; a callback may call the registry again.
    for (const Call& call : calls) {
        if (find(call.id) == nullptr) {
            continue; // removed by an earlier callback
        }
        if (call.snapshot) {
            call.member->onSnapshot(call.snapshot);
        }
        if (call.status) {
            call.member->onStatus(call.newStatus, call.voices);
        }
    }
}

} // namespace mm::core
