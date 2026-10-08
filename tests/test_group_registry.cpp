#include "core/GroupRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace mm::core;

namespace {

Pattern marked(uint8_t pitch) {
    Pattern pattern = makeEmptyPattern(1, "peak_time");
    Note note;
    note.id = allocateNoteId(pattern);
    note.pitch = pitch;
    note.lengthTicks = 240;
    pattern.voices[0].notes.push_back(note);
    return pattern;
}

/// A member that records what the registry tells it and owns a slot bank.
class FakeMember : public GroupMember {
public:
    explicit FakeMember(uint8_t pitch = 0) {
        if (pitch != 0) {
            bank.setResult(0, marked(pitch));
        }
    }

    void onSnapshot(const SlotSnapshot& snapshot, std::optional<double> stampPpq) override {
        snapshots.push_back(snapshot);
        stamps.push_back(stampPpq);
        if (onSnapshotHook) {
            onSnapshotHook();
        }
    }
    void onStatus(GroupStatus s, size_t v) override {
        statuses.push_back(s);
        voices = v;
    }
    void onAction(GroupAction action) override { actions.push_back(action); }
    SlotSnapshot currentSlots() override { return std::make_shared<const SlotBank>(bank); }

    GroupStatus status() const { return statuses.empty() ? GroupStatus::Solo : statuses.back(); }
    /// The first pitch of slot 1 in the last snapshot received, or -1.
    int adoptedPitch() const {
        if (snapshots.empty() || snapshots.back()->isEmpty(0)) {
            return -1;
        }
        return snapshots.back()->slot(0)->pattern->voices[0].notes[0].pitch;
    }

    SlotBank bank;
    std::vector<SlotSnapshot> snapshots;
    std::vector<std::optional<double>> stamps;
    std::vector<GroupStatus> statuses;
    std::vector<GroupAction> actions;
    size_t voices = 0;
    std::function<void()> onSnapshotHook;
};

std::shared_ptr<const SlotBank> bankWith(uint8_t pitch) {
    SlotBank bank;
    bank.setResult(0, marked(pitch));
    return std::make_shared<const SlotBank>(bank);
}

} // namespace

TEST_CASE("a new member is solo and hears its status", "[group]") {
    GroupRegistry registry;
    auto member = std::make_shared<FakeMember>();
    const auto id = registry.add(member);
    CHECK(registry.status(id) == GroupStatus::Solo);
    REQUIRE(member->statuses.size() == 1);
    CHECK(member->statuses[0] == GroupStatus::Solo);
    CHECK(registry.hub() == 0);
    CHECK(registry.memberCount() == 1);
}

TEST_CASE("a voice adopts the slot set of the hub, now and after each change", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voice = std::make_shared<FakeMember>(40);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    registry.add(voice, InstanceRole::Voice);
    CHECK(hub->status() == GroupStatus::Hub);
    CHECK(hub->voices == 1);
    CHECK(voice->status() == GroupStatus::VoiceConnected);
    CHECK(voice->adoptedPitch() == 60); // the hub's state, not its own

    CHECK(registry.publish(hubId, bankWith(62)));
    CHECK(voice->adoptedPitch() == 62);
    CHECK(hub->snapshots.empty()); // the hub does not hear its own slots
}

TEST_CASE("a voice that was there first keeps its slots until the hub appears", "[group]") {
    GroupRegistry registry;
    auto voice = std::make_shared<FakeMember>(40);
    registry.add(voice, InstanceRole::Voice);
    CHECK(voice->snapshots.empty());
    CHECK(voice->status() == GroupStatus::HubOffered); // the only voice is also the oldest

    auto hub = std::make_shared<FakeMember>(60);
    registry.add(hub, InstanceRole::Hub);
    CHECK(voice->status() == GroupStatus::VoiceConnected);
    CHECK(voice->adoptedPitch() == 60);
}

TEST_CASE("only one hub per group, the second stays solo and is told", "[group]") {
    GroupRegistry registry;
    auto first = std::make_shared<FakeMember>(60);
    auto second = std::make_shared<FakeMember>(70);
    const auto firstId = registry.add(first, InstanceRole::Hub);
    const auto secondId = registry.add(second, InstanceRole::Hub);
    CHECK(registry.hub() == firstId);
    CHECK(registry.status(secondId) == GroupStatus::HubRefused);
    CHECK(second->status() == GroupStatus::HubRefused);
    CHECK(registry.setRole(secondId, InstanceRole::Hub) == InstanceRole::Solo);
    CHECK(registry.setRole(secondId, InstanceRole::Solo) == InstanceRole::Solo);
    CHECK(registry.status(secondId) == GroupStatus::Solo); // asking for nothing clears the refusal
    CHECK_FALSE(registry.publish(secondId, bankWith(1)));
}

TEST_CASE("when the hub is removed the voices keep playing and the oldest is offered the role", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto older = std::make_shared<FakeMember>(40);
    auto younger = std::make_shared<FakeMember>(41);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    const auto olderId = registry.add(older, InstanceRole::Voice);
    registry.add(younger, InstanceRole::Voice);
    const auto adoptedBefore = older->snapshots.size();

    registry.remove(hubId);
    CHECK(registry.hub() == 0);
    CHECK(older->status() == GroupStatus::HubOffered);
    CHECK(younger->status() == GroupStatus::HubMissing);
    CHECK(older->snapshots.size() == adoptedBefore); // nothing changes for the voices
    CHECK(older->adoptedPitch() == 60);              // they keep the last hub state
    CHECK(registry.status(olderId) == GroupStatus::HubOffered);
}

TEST_CASE("the offer is not taken silently, accepting makes the voice the hub and the others adopt its slots",
          "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto older = std::make_shared<FakeMember>(40);
    auto younger = std::make_shared<FakeMember>(41);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    const auto olderId = registry.add(older, InstanceRole::Voice);
    const auto youngerId = registry.add(younger, InstanceRole::Voice);
    registry.remove(hubId);
    CHECK(registry.hub() == 0); // no takeover without acceptOffer

    CHECK_FALSE(registry.acceptOffer(youngerId)); // not offered to the younger one
    CHECK(registry.acceptOffer(olderId));
    CHECK(registry.hub() == olderId);
    CHECK(older->status() == GroupStatus::Hub);
    CHECK(older->voices == 1);
    CHECK(younger->status() == GroupStatus::VoiceConnected);
    CHECK(younger->adoptedPitch() == 40);       // the new hub's state replaces the old one
    CHECK_FALSE(registry.acceptOffer(olderId)); // already hub
}

TEST_CASE("a hub that gives up its role is a lost hub too", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voice = std::make_shared<FakeMember>(40);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    registry.add(voice, InstanceRole::Voice);
    registry.setRole(hubId, InstanceRole::Solo);
    CHECK(registry.hub() == 0);
    CHECK(hub->status() == GroupStatus::Solo);
    CHECK(voice->status() == GroupStatus::HubOffered);
    CHECK_FALSE(registry.publish(hubId, bankWith(1)));
}

TEST_CASE("the offer moves on when the offered voice goes away or stops being a voice", "[group]") {
    GroupRegistry registry;
    auto first = std::make_shared<FakeMember>();
    auto second = std::make_shared<FakeMember>();
    const auto firstId = registry.add(first, InstanceRole::Voice);
    const auto secondId = registry.add(second, InstanceRole::Voice);
    CHECK(registry.status(firstId) == GroupStatus::HubOffered);
    CHECK(registry.status(secondId) == GroupStatus::HubMissing);
    registry.remove(firstId);
    CHECK(registry.status(secondId) == GroupStatus::HubOffered);
    CHECK(second->status() == GroupStatus::HubOffered);
    registry.setRole(secondId, InstanceRole::Solo);
    CHECK(registry.voiceCount() == 0);
}

TEST_CASE("a voice that becomes solo and then voice again hears the hub again", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voice = std::make_shared<FakeMember>(40);
    registry.add(hub, InstanceRole::Hub);
    const auto voiceId = registry.add(voice, InstanceRole::Voice);
    const auto before = voice->snapshots.size();
    registry.setRole(voiceId, InstanceRole::Solo);
    CHECK(voice->status() == GroupStatus::Solo);
    registry.setRole(voiceId, InstanceRole::Voice);
    CHECK(voice->snapshots.size() == before + 1);
    CHECK(voice->adoptedPitch() == 60);
}

TEST_CASE("members are told only when something changed", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voice = std::make_shared<FakeMember>(40);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    registry.add(voice, InstanceRole::Voice);
    const auto hubStatuses = hub->statuses.size();
    const auto voiceStatuses = voice->statuses.size();
    registry.publish(hubId, bankWith(61));
    registry.setOutputVoice(hubId, 2);
    CHECK(hub->statuses.size() == hubStatuses);
    CHECK(voice->statuses.size() == voiceStatuses);
}

TEST_CASE("the hub learns how many voices it has", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>();
    registry.add(hub, InstanceRole::Hub);
    CHECK(hub->voices == 0);
    const auto a = registry.add(std::make_shared<FakeMember>(), InstanceRole::Voice);
    registry.add(std::make_shared<FakeMember>(), InstanceRole::Voice);
    CHECK(hub->voices == 2);
    registry.remove(a);
    CHECK(hub->voices == 1);
    CHECK(registry.voiceCount() == 1);
}

TEST_CASE("a callback may call the registry and a member removed meanwhile is not called", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto first = std::make_shared<FakeMember>();
    auto second = std::make_shared<FakeMember>();
    registry.add(hub, InstanceRole::Hub);
    const auto firstId = registry.add(first, InstanceRole::Voice);
    const auto secondId = registry.add(second, InstanceRole::Voice);
    first->onSnapshotHook = [&] { registry.remove(secondId); };
    const auto secondSnapshots = second->snapshots.size();
    registry.publish(registry.hub(), bankWith(70));
    CHECK(second->snapshots.size() == secondSnapshots); // removed by the first callback before its turn
    CHECK(registry.status(firstId) == GroupStatus::VoiceConnected);
    CHECK(registry.memberCount() == 2);
}

TEST_CASE("unknown ids are ignored", "[group]") {
    GroupRegistry registry;
    CHECK(registry.setRole(99, InstanceRole::Hub) == InstanceRole::Solo);
    CHECK_FALSE(registry.publish(99, bankWith(1)));
    CHECK_FALSE(registry.acceptOffer(99));
    CHECK(registry.status(99) == GroupStatus::Solo);
    registry.remove(99);
    registry.setOutputVoice(99, 3);
    CHECK(registry.memberCount() == 0);
}

TEST_CASE("the hub's stamp travels with the change to every voice, and only with it", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voiceA = std::make_shared<FakeMember>(40);
    auto voiceB = std::make_shared<FakeMember>(41);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    registry.add(voiceA, InstanceRole::Voice);
    registry.add(voiceB, InstanceRole::Voice);
    REQUIRE(voiceA->stamps.size() == 1);
    CHECK_FALSE(voiceA->stamps.back().has_value()); // adopting the hub's state on joining has no stamp

    CHECK(registry.publish(hubId, bankWith(62), 12.0));
    REQUIRE(voiceA->stamps.size() == 2);
    REQUIRE(voiceB->stamps.size() == 2);
    CHECK(voiceA->stamps.back() == 12.0);
    CHECK(voiceB->stamps.back() == 12.0);

    CHECK(registry.publish(hubId, bankWith(63))); // a change without plan carries no stamp
    CHECK_FALSE(voiceA->stamps.back().has_value());

    // A voice that joins later hears the current state, not the stamp of an earlier change.
    CHECK(registry.publish(hubId, bankWith(64), 20.0));
    auto late = std::make_shared<FakeMember>();
    registry.add(late, InstanceRole::Voice);
    REQUIRE(late->stamps.size() == 1);
    CHECK_FALSE(late->stamps.back().has_value());
}

TEST_CASE("a voice's action goes to the hub, nobody else", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voiceA = std::make_shared<FakeMember>(40);
    auto voiceB = std::make_shared<FakeMember>(41);
    const auto hubId = registry.add(hub, InstanceRole::Hub);
    const auto voiceId = registry.add(voiceA, InstanceRole::Voice);
    registry.add(voiceB, InstanceRole::Voice);

    CHECK(registry.forward(voiceId, GroupAction::Generate));
    REQUIRE(hub->actions.size() == 1);
    CHECK(hub->actions[0] == GroupAction::Generate);
    CHECK(voiceA->actions.empty());
    CHECK(voiceB->actions.empty());
    CHECK_FALSE(registry.forward(hubId, GroupAction::Generate)); // the hub asks nobody
    CHECK_FALSE(registry.forward(9999, GroupAction::Generate));
    CHECK(hub->actions.size() == 1);
}

TEST_CASE("an action without a hub is not forwarded", "[group]") {
    GroupRegistry registry;
    auto voice = std::make_shared<FakeMember>(40);
    auto solo = std::make_shared<FakeMember>();
    const auto voiceId = registry.add(voice, InstanceRole::Voice);
    const auto soloId = registry.add(solo, InstanceRole::Solo);
    CHECK_FALSE(registry.forward(voiceId, GroupAction::Generate));
    CHECK_FALSE(registry.forward(soloId, GroupAction::Generate));
}

TEST_CASE("a voice can ask the hub to show itself", "[group]") {
    GroupRegistry registry;
    auto hub = std::make_shared<FakeMember>(60);
    auto voice = std::make_shared<FakeMember>(40);
    registry.add(hub, InstanceRole::Hub);
    const auto voiceId = registry.add(voice, InstanceRole::Voice);
    CHECK(registry.forward(voiceId, GroupAction::ShowHub));
    REQUIRE(hub->actions.size() == 1);
    CHECK(hub->actions[0] == GroupAction::ShowHub);
    CHECK(voice->actions.empty());
}
