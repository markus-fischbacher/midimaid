#include "plugin/GroupLink.h"

#include "plugin/ProcessorBase.h"

namespace mm::plugin {

mm::core::GroupRegistry& processGroup() {
    static mm::core::GroupRegistry registry;
    return registry;
}

std::shared_ptr<GroupLink> GroupLink::create(ProcessorBase& owner) {
    return std::shared_ptr<GroupLink>(new GroupLink(owner));
}

GroupLink::GroupLink(ProcessorBase& owner) : owner_(&owner) {}

GroupLink::~GroupLink() = default;

void GroupLink::onMessageThread(std::function<void()> fn) {
    if (juce::MessageManager::existsAndIsCurrentThread()) {
        fn();
    } else if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
        juce::MessageManager::callAsync(std::move(fn));
    } // without a message manager (unit tests without JUCE initialised) there is no group
}

void GroupLink::setRole(mm::core::InstanceRole role, int outputVoice) {
    {
        const juce::ScopedLock lock(lock_);
        wantedRole_ = role;
        wantedVoice_ = outputVoice;
    }
    onMessageThread([self = shared_from_this()] { self->sync(); });
}

void GroupLink::sync() {
    mm::core::InstanceRole role;
    int voice;
    {
        const juce::ScopedLock lock(lock_);
        if (owner_ == nullptr) {
            return; // detached before this ran
        }
        role = wantedRole_;
        voice = wantedVoice_;
    }
    auto& registry = processGroup();
    if (id_ == 0) {
        id_ = registry.add(shared_from_this(), role, voice);
    } else {
        registry.setOutputVoice(id_, voice);
        registry.setRole(id_, role);
    }
}

void GroupLink::publish(mm::core::SlotSnapshot snapshot, std::optional<double> stampPpq) {
    onMessageThread([self = shared_from_this(), snapshot = std::move(snapshot), stampPpq] {
        if (self->id_ != 0) {
            processGroup().publish(self->id_, snapshot, stampPpq);
        }
    });
}

void GroupLink::forward(mm::core::GroupAction action) {
    onMessageThread([self = shared_from_this(), action] {
        if (self->id_ != 0) {
            processGroup().forward(self->id_, action);
        }
    });
}

void GroupLink::acceptOffer() {
    onMessageThread([self = shared_from_this()] {
        if (self->id_ != 0) {
            processGroup().acceptOffer(self->id_);
        }
    });
}

void GroupLink::leave() {
    if (!left_ && id_ != 0) {
        processGroup().remove(id_);
    }
    left_ = true;
}

void GroupLink::detach() {
    {
        const juce::ScopedLock lock(lock_); // waits for a callback that is running right now
        owner_ = nullptr;
    }
    onMessageThread([self = shared_from_this()] { self->leave(); });
}

void GroupLink::onSnapshot(const mm::core::SlotSnapshot& snapshot, std::optional<double> stampPpq) {
    const juce::ScopedLock lock(lock_);
    if (owner_ != nullptr) {
        owner_->adoptHubSlots(snapshot, stampPpq);
    }
}

void GroupLink::onStatus(mm::core::GroupStatus status, size_t voices) {
    status_.store(status);
    voices_.store(voices);
    const juce::ScopedLock lock(lock_);
    if (owner_ != nullptr) {
        owner_->onGroupStatusChanged(status);
    }
}

void GroupLink::onAction(mm::core::GroupAction action) {
    const juce::ScopedLock lock(lock_);
    if (owner_ != nullptr && action == mm::core::GroupAction::Generate) {
        owner_->generate();
    }
}

mm::core::SlotSnapshot GroupLink::currentSlots() {
    const juce::ScopedLock lock(lock_);
    if (owner_ != nullptr) {
        return std::make_shared<const mm::core::SlotBank>(owner_->slotsSnapshot());
    }
    return std::make_shared<const mm::core::SlotBank>();
}

} // namespace mm::plugin
