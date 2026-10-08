#include "engine/GroupChannel.h"
#include "engine/GroupSync.h"
#include "engine/PatternHandover.h"
#include "engine/PatternPlayer.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace mm::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr int kBlock = 480; // 0.02 PPQ at 120 BPM and 48 kHz: bar lines fall on block boundaries
constexpr double kSamplesPerPpq = 24000.0;

struct NoteOn {
    long long sample;
    int pitch;
};

/// A group member: its own player with three slots (slot n plays pitch 40 + n on every bar line), its own
/// transport clock and the group logic of the plugin's audio thread.
class Member {
public:
    Member(GroupChannel& channel, SlotMode mode, int slotParam = 1)
        : channel_(channel), id_(channel.acquire()), sync_(channel, id_), mode_(mode), slotParam_(slotParam) {
        player_.attach(&handover_);
        for (size_t slot = 0; slot < 3; ++slot) {
            auto pattern = std::make_unique<OwnedPattern>();
            pattern->notes = {{0, 240, 1, static_cast<uint8_t>(41 + slot), 100}};
            pattern->lengthTicks = 4 * 960;
            pattern->version = slot + 1;
            handover_.publishResult(slot, std::move(pattern), 4.0);
        }
    }
    ~Member() { channel_.release(id_); }

    int id() const { return id_; }
    void setSlotParam(int slot) { slotParam_ = slot; }
    void setMode(SlotMode mode) { mode_ = mode; }
    void setPpq(double ppq) { ppq_ = ppq; }
    void setPlaying(bool playing) { playing_ = playing; }
    double ppq() const { return ppq_; }
    GroupSync& sync() { return sync_; }
    const std::vector<NoteOn>& notes() const { return notes_; }
    PatternPlayer& player() { return player_; }

    /// One block at the current position; the clock moves on only while playing.
    void step(int numSamples = kBlock) {
        TransportInfo transport;
        transport.hasPosition = true;
        transport.isPlaying = playing_;
        transport.ppq = ppq_;
        transport.bpm = kBpm;
        const bool restart = player_.startsOrJumps(transport, numSamples, kSampleRate);
        const SlotDecision decision = sync_.beginBlock(mode_, slotParam_, transport, numSamples, kSampleRate, restart);
        if (decision.stamped) {
            player_.setSlotAt(decision.slot, decision.stampPpq);
        } else {
            player_.setSlot(decision.slot);
        }
        player_.process(transport, numSamples, kSampleRate, out_);
        for (const auto& event : out_) {
            if (event.noteOn) {
                notes_.push_back({std::llround(ppq_ * kSamplesPerPpq) + event.sampleOffset, event.pitch});
            }
        }
        if (playing_) {
            ppq_ += numSamples / kSamplesPerPpq;
        }
    }

    /// The first note with `pitch` at or after `fromSample`, or -1.
    long long firstWith(int pitch, long long fromSample = 0) const {
        for (const auto& note : notes_) {
            if (note.pitch == pitch && note.sample >= fromSample) {
                return note.sample;
            }
        }
        return -1;
    }
    bool played(int pitch) const { return firstWith(pitch) >= 0; }

private:
    GroupChannel& channel_;
    int id_;
    PatternHandover handover_;
    PatternPlayer player_{PatternView{nullptr, 0, 4 * 960}};
    GroupSync sync_;
    SlotMode mode_;
    int slotParam_;
    bool playing_ = true;
    double ppq_ = 0.0;
    MidiEventList out_;
    std::vector<NoteOn> notes_;
};

/// Steps all members block by block until the first one reaches `ppq`; `hubFirst` sets the order inside a cycle.
void runTo(std::vector<Member*> members, double ppq, bool hubFirst = true) {
    if (!hubFirst) {
        std::reverse(members.begin(), members.end());
    }
    while (members.front()->ppq() < ppq - 1e-9 && members.back()->ppq() < ppq - 1e-9) {
        for (auto* member : members) {
            member->step();
        }
    }
}

long long at(double ppq) {
    return std::llround(ppq * kSamplesPerPpq);
}

} // namespace

// ---- channel ----------------------------------------------------------------------------------------------

TEST_CASE("the channel hands out member slots until they are used up", "[group-channel]") {
    GroupChannel channel;
    std::vector<int> members;
    for (size_t i = 0; i < kMaxGroupMembers; ++i) {
        const int member = channel.acquire();
        REQUIRE(member >= 0);
        members.push_back(member);
    }
    CHECK(channel.memberCount() == kMaxGroupMembers);
    CHECK(channel.acquire() == GroupChannel::kNone);
    channel.release(members[5]);
    CHECK(channel.memberCount() == kMaxGroupMembers - 1);
    CHECK(channel.acquire() == members[5]); // the free one is reused
    channel.release(GroupChannel::kNone);   // harmless
    channel.release(1000);
    channel.release(members[5]);
    channel.release(members[5]); // twice: counted once
    CHECK(channel.memberCount() == kMaxGroupMembers - 1);
}

TEST_CASE("the furthest position counts only members that play", "[group-channel]") {
    GroupChannel channel;
    const int a = channel.acquire();
    const int b = channel.acquire();
    const int c = channel.acquire();
    double furthest = 0.0;
    CHECK_FALSE(channel.furthestPlaying(furthest));
    channel.reportBlock(a, 7.5, true);
    channel.reportBlock(b, 9.25, true);
    channel.reportBlock(c, 200.0, false); // stopped: its position means nothing
    REQUIRE(channel.furthestPlaying(furthest));
    CHECK(std::abs(furthest - 9.25) < 1.0 / 960.0);
    channel.reportBlock(b, 9.25, false);
    REQUIRE(channel.furthestPlaying(furthest));
    CHECK(std::abs(furthest - 7.5) < 1.0 / 960.0);
    channel.release(a);
    CHECK_FALSE(channel.furthestPlaying(furthest));
}

TEST_CASE("positions can be negative (pre-roll) and are kept to a tick", "[group-channel]") {
    GroupChannel channel;
    const int a = channel.acquire();
    channel.reportBlock(a, -3.5, true);
    double furthest = 0.0;
    REQUIRE(channel.furthestPlaying(furthest));
    CHECK(std::abs(furthest - (-3.5)) < 1.0 / 960.0);
    channel.reportBlock(a, 123456.789, true);
    REQUIRE(channel.furthestPlaying(furthest));
    CHECK(std::abs(furthest - 123456.789) < 1.0 / 960.0);
}

TEST_CASE("the hub state is empty until the hub reports and after a change of hub", "[group-channel]") {
    GroupChannel channel;
    const int hub = channel.acquire();
    CHECK(channel.hub() == GroupChannel::kNone);
    channel.setHub(hub);
    CHECK(channel.hub() == hub);
    CHECK_FALSE(channel.hubState().valid);
    channel.reportHub(148.25, 3, true);
    auto state = channel.hubState();
    REQUIRE(state.valid);
    CHECK(state.slot == 3);
    CHECK(state.playing);
    CHECK(std::abs(state.ppq - 148.25) < 1.0 / 960.0);
    channel.reportHub(-2.0, 16, false);
    state = channel.hubState();
    CHECK(state.slot == 16);
    CHECK_FALSE(state.playing);
    CHECK(std::abs(state.ppq + 2.0) < 1.0 / 960.0);
    channel.setHub(hub);
    CHECK_FALSE(channel.hubState().valid);
}

TEST_CASE("releasing the hub clears it", "[group-channel]") {
    GroupChannel channel;
    const int hub = channel.acquire();
    channel.setHub(hub);
    channel.release(hub);
    CHECK(channel.hub() == GroupChannel::kNone);
}

TEST_CASE("plans carry stamp and slot and a sequence number that wraps", "[group-channel]") {
    GroupChannel channel;
    CHECK(channel.plan().sequence == 0);
    channel.publishPlan(12.0, 2);
    auto plan = channel.plan();
    CHECK(plan.sequence == 1);
    CHECK(plan.slot == 2);
    CHECK(std::abs(plan.stampPpq - 12.0) < 1.0 / 960.0);
    channel.publishPlan(-4.0, 16); // before the start of the song (pre-roll)
    plan = channel.plan();
    CHECK(plan.sequence == 2);
    CHECK(plan.slot == 16);
    CHECK(std::abs(plan.stampPpq + 4.0) < 1.0 / 960.0);
    for (int i = 0; i < 65534; ++i) {
        channel.publishPlan(8.0, 1);
    }
    CHECK(channel.plan().sequence == 0); // wrapped after 65536 plans
    channel.publishPlan(1.0e6, 5);
    plan = channel.plan();
    CHECK(plan.sequence == 1);
    CHECK(plan.slot == 5);
    CHECK(std::abs(plan.stampPpq - 1.0e6) < 1.0 / 960.0);
}

TEST_CASE("the minimum lead defaults to 150 ms and can be set", "[group-channel]") {
    GroupChannel channel;
    CHECK(channel.minLeadMs() == 150.0);
    channel.setMinLeadMs(400.0);
    CHECK(channel.minLeadMs() == 400.0);
}

// ---- Z16 to Z19 and friends ---------------------------------------------------------------------------------

TEST_CASE("Z16: a hub change 100 ms before the bar line moves to the bar after for hub and voices", "[group-sync]") {
    for (const bool hubFirst : {true, false}) {
        GroupChannel channel;
        Member hub(channel, SlotMode::Hub, 1);
        Member voiceA(channel, SlotMode::FollowHub, 1);
        Member voiceB(channel, SlotMode::FollowHub, 1);
        channel.setHub(hub.id());
        runTo({&hub, &voiceA, &voiceB}, 7.8, hubFirst);
        hub.setSlotParam(2); // the slot changes at 7.8: bar 8.0 is 0.2 PPQ = 100 ms ahead, less than the lead
        runTo({&hub, &voiceA, &voiceB}, 13.0, hubFirst);
        for (const Member* member : {&hub, &voiceA, &voiceB}) {
            INFO("hubFirst " << hubFirst);
            CHECK(member->firstWith(42) == at(12.0));
            CHECK(member->firstWith(41, at(12.0)) == -1);     // the old slot does not sound again from 12.0
            CHECK(member->firstWith(41, at(8.0)) == at(8.0)); // it still sounded at 8.0
        }
    }
}

TEST_CASE("a hub change with enough lead switches at the next bar line for everybody", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 6.0);
    hub.setSlotParam(2); // 2.0 PPQ = 1 s ahead of bar 8.0
    runTo({&hub, &voice}, 13.0);
    CHECK(hub.firstWith(42) == at(8.0));
    CHECK(voice.firstWith(42) == at(8.0));
}

TEST_CASE("the lead is measured from the member that is furthest ahead", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    // The voice runs 2 PPQ ahead of the hub (a long buffer or another thread that is further along).
    voice.setPpq(2.0);
    runTo({&hub}, 5.0);
    voice.step(kBlock);
    hub.setSlotParam(2);
    hub.step();
    const auto plan = channel.plan();
    CHECK(plan.sequence == 1);
    CHECK(plan.stampPpq == 8.0); // furthest ~2.0 + 0.3 lead would allow 4.0, the hub itself is at 5.0: first bar >= 5.3
}

TEST_CASE("a hub without other members changes at the plain next bar line and plans nothing", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    channel.setHub(hub.id());
    runTo({&hub}, 7.8);
    hub.setSlotParam(2);
    runTo({&hub}, 13.0);
    CHECK(hub.firstWith(42) == at(8.0)); // no 150 ms lead rule for a hub on its own
    CHECK(channel.plan().sequence == 0);
}

TEST_CASE("Z17: a plan whose stamp is already behind the voice goes to the next bar line and is counted",
          "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 7.0);
    voice.setPpq(8.1); // the voice is ahead of the planned point (a jump of its own, a long buffer)
    channel.publishPlan(8.0, 2);
    voice.step();
    CHECK(voice.sync().lateSwitches() == 1);
    while (voice.ppq() < 13.0) {
        voice.step();
    }
    CHECK(voice.firstWith(42) == at(12.0));
}

TEST_CASE("a plan that arrives in time is not counted as late", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 5.0);
    hub.setSlotParam(2);
    runTo({&hub, &voice}, 9.0);
    CHECK(voice.sync().lateSwitches() == 0);
    CHECK(voice.firstWith(42) == at(8.0));
}

TEST_CASE("Z18: a voice set to own follows its own slot parameter and not the hub", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::Own, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 7.0);
    voice.setSlotParam(2); // at 7.0: Q = 8.0, whatever the hub does
    runTo({&hub, &voice}, 9.0);
    CHECK(voice.firstWith(42) == at(8.0));
    hub.setSlotParam(3);
    runTo({&hub, &voice}, 17.0);
    CHECK(hub.played(43));
    CHECK_FALSE(voice.played(43)); // the voice does not listen to the hub
}

TEST_CASE("Z19: a voice that starts mid-arrangement takes the slot the hub already reported at once", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 3);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    hub.setPpq(148.0);
    voice.setPpq(148.0);
    hub.step(); // the hub processes first
    voice.step();
    CHECK(std::llabs(voice.firstWith(43) - at(148.0)) <= 1); // slot 3 from the first note
    CHECK(voice.firstWith(43) > 0);
    CHECK_FALSE(voice.played(41));
}

TEST_CASE("Z19: a voice that is processed before the hub starts with its stored slot and joins at the next bar",
          "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 3);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    hub.setPpq(148.0);
    voice.setPpq(148.0);
    runTo({&hub, &voice}, 153.0, false);     // voice first in every cycle: the hub has not reported at its start
    CHECK(voice.firstWith(41) == at(148.0)); // the stored slot
    CHECK(std::llabs(voice.firstWith(43) - at(152.0)) <= 1); // the hub's slot from the first bar line after it reported
    CHECK(voice.firstWith(43) > 0);
}

TEST_CASE("an unstamped hub slot that is adopted later waits for the bar line", "[group-sync]") {
    GroupChannel channel;
    Member voice(channel, SlotMode::FollowHub, 1);
    runTo({&voice}, 5.0); // no hub yet: plays slot 1
    Member hub(channel, SlotMode::Hub, 2);
    hub.setPpq(voice.ppq());
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 13.0);
    CHECK(voice.played(41));
    CHECK(voice.firstWith(42) == at(8.0)); // the hub appeared mid-bar: the next bar line
}

TEST_CASE("without a hub the voice plays on with the slot it has", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 6.0);
    hub.setSlotParam(2);
    runTo({&hub, &voice}, 9.0);
    REQUIRE(voice.played(42));
    channel.setHub(GroupChannel::kNone); // the hub is gone
    voice.setSlotParam(3);               // the own parameter does nothing in "follows hub"
    runTo({&voice}, 21.0);
    CHECK_FALSE(voice.played(43));
    CHECK(voice.firstWith(42, at(16.0)) == at(16.0)); // slot 2 keeps sounding
}

TEST_CASE("a newer plan replaces an older one that has not happened yet", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 5.0);
    hub.setSlotParam(2);
    runTo({&hub, &voice}, 6.0);
    hub.setSlotParam(3);
    runTo({&hub, &voice}, 13.0);
    for (const Member* member : {&hub, &voice}) {
        CHECK(member->firstWith(43) == at(8.0));
        CHECK_FALSE(member->played(42));
    }
}

TEST_CASE("a stop keeps the planned change for the next start", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 7.8);
    hub.setSlotParam(2); // planned for 12.0
    runTo({&hub, &voice}, 9.0);
    for (auto* member : {&hub, &voice}) {
        member->setPlaying(false);
        member->step();
    }
    for (auto* member : {&hub, &voice}) {
        member->setPpq(20.0);
        member->setPlaying(true);
    }
    runTo({&hub, &voice}, 21.0);
    for (const Member* member : {&hub, &voice}) {
        CHECK(member->firstWith(42, at(20.0)) == at(20.0)); // the new slot from the start
    }
}

TEST_CASE("a change of the hub's slot while the transport stands is picked up at the next start", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 3.0);
    for (auto* member : {&hub, &voice}) {
        member->setPlaying(false);
        member->step();
    }
    hub.setSlotParam(2); // no plan: nobody plays
    hub.step();
    CHECK(channel.plan().sequence == 0);
    for (auto* member : {&hub, &voice}) {
        member->setPpq(40.0);
        member->setPlaying(true);
    }
    runTo({&hub, &voice}, 41.0);
    CHECK(voice.firstWith(42, at(40.0)) == at(40.0));
}

TEST_CASE("the same inputs give the same notes", "[group-sync]") {
    const auto scenario = [] {
        GroupChannel channel;
        Member hub(channel, SlotMode::Hub, 1);
        Member voice(channel, SlotMode::FollowHub, 1);
        channel.setHub(hub.id());
        runTo({&hub, &voice}, 6.3);
        hub.setSlotParam(2);
        runTo({&hub, &voice}, 14.0);
        std::vector<NoteOn> all = hub.notes();
        all.insert(all.end(), voice.notes().begin(), voice.notes().end());
        return all;
    };
    const auto first = scenario();
    const auto second = scenario();
    REQUIRE(first.size() == second.size());
    for (size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].sample == second[i].sample);
        CHECK(first[i].pitch == second[i].pitch);
    }
}

TEST_CASE("a hub that gets a longer lead plans further ahead", "[group-sync]") {
    GroupChannel channel;
    channel.setMinLeadMs(1500.0); // 3 PPQ at 120 BPM
    Member hub(channel, SlotMode::Hub, 1);
    Member voice(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &voice}, 6.0);
    hub.setSlotParam(2); // 6.0 + 3.0 = 9.0: bar 12.0
    runTo({&hub, &voice}, 13.0);
    CHECK(hub.firstWith(42) == at(12.0));
    CHECK(voice.firstWith(42) == at(12.0));
}

TEST_CASE("a slot parameter outside 1 to 16 is limited", "[group-sync]") {
    GroupChannel channel;
    GroupSync sync(channel, channel.acquire());
    TransportInfo transport;
    transport.hasPosition = true;
    transport.isPlaying = true;
    transport.bpm = 120.0;
    CHECK(sync.beginBlock(SlotMode::Own, 99, transport, 480, 48000.0, false).slot == 16);
    CHECK(sync.beginBlock(SlotMode::Own, -3, transport, 480, 48000.0, false).slot == 1);
}

// ---- threads ----------------------------------------------------------------------------------------------

TEST_CASE("hub and voices on their own threads, with members coming and going, stay consistent",
          "[group-sync][stress]") {
    GroupChannel channel;
    std::atomic<bool> stop{false};
    std::atomic<long> violations{0};
    std::atomic<long> plans{0};

    const auto audioThread = [&](bool isHub, unsigned seed) {
        const int id = channel.acquire();
        GroupSync sync(channel, id);
        std::mt19937 random(seed);
        double ppq = 0.0;
        int slot = 1;
        bool restartNext = true;
        for (int block = 0; block < 20000 && !stop.load(); ++block) {
            const int size = 64 + static_cast<int>(random() % 1985);
            TransportInfo transport;
            transport.hasPosition = true;
            transport.isPlaying = true;
            transport.ppq = ppq;
            transport.bpm = 120.0;
            if (isHub && random() % 400 == 0) {
                slot = 1 + static_cast<int>(random() % 16);
            }
            const auto decision = sync.beginBlock(isHub ? SlotMode::Hub : SlotMode::FollowHub, slot, transport, size,
                                                  48000.0, restartNext);
            restartNext = false;
            if (decision.slot < 1 || decision.slot > 16) {
                ++violations;
            }
            if (decision.stamped) {
                ++plans;
                if (std::fmod(decision.stampPpq, 4.0) != 0.0 &&
                    std::abs(std::fmod(decision.stampPpq, 4.0) - 4.0) > 1e-9) {
                    ++violations; // always a bar line
                }
                if (isHub && decision.stampPpq <= ppq) {
                    ++violations; // the hub plans ahead of its own position
                }
            }
            ppq += size / 24000.0;
            if (random() % 3000 == 0) {
                ppq += 8.0; // a jump
                restartNext = true;
            }
        }
        channel.release(id);
    };

    std::thread hub(audioThread, true, 1u);
    std::thread voiceA(audioThread, false, 2u);
    std::thread voiceB(audioThread, false, 3u);
    std::thread message([&] {
        std::mt19937 random(9);
        while (!stop.load()) {
            const int extra = channel.acquire(); // instances that join and leave
            channel.setMinLeadMs(100.0 + static_cast<double>(random() % 200));
            channel.release(extra);
        }
    });
    hub.join();
    voiceA.join();
    voiceB.join();
    stop.store(true);
    message.join();
    CHECK(violations.load() == 0);
    CHECK(plans.load() > 0);
}

TEST_CASE("a voice that joins later ignores the plans of before", "[group-sync]") {
    GroupChannel channel;
    Member hub(channel, SlotMode::Hub, 1);
    Member early(channel, SlotMode::FollowHub, 1);
    channel.setHub(hub.id());
    runTo({&hub, &early}, 2.0);
    hub.setSlotParam(2); // plan 1 for bar 4.0
    runTo({&hub, &early}, 10.0);
    REQUIRE(channel.plan().sequence == 1);

    Member late(channel, SlotMode::FollowHub, 1);
    late.setPpq(early.ppq());
    runTo({&hub, &early, &late}, 14.0);
    CHECK(late.sync().lateSwitches() == 0); // the old stamp 4.0 was not applied behind its back
    CHECK(late.played(42));                 // it still joins the hub's slot, from the hub's state
}
