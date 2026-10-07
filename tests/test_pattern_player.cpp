#include "engine/PatternPlayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

using namespace mm::engine;

namespace {

constexpr double kSampleRate = 48000.0;

struct Event {
    long long sample; // absolute
    double ppq;       // approximate position, from the tempo reported for the block
    uint8_t channel;
    uint8_t pitch;
    bool noteOn;
};

/// Drives a PatternPlayer like a host would and tracks the sounding notes.
class Simulation {
public:
    explicit Simulation(PatternView pattern = placeholderPattern()) : player_(pattern) {}

    /// One block. The host position continues from the previous block unless `ppq` is given.
    void block(int numSamples, bool playing = true, double bpm = 120.0, bool hasPosition = true) {
        run(numSamples, playing, bpm, hasPosition, nextPpq_, looping_, loopStart_, loopEnd_);
    }
    void blockAt(double ppq, int numSamples, bool playing = true, double bpm = 120.0) {
        run(numSamples, playing, bpm, true, ppq, looping_, loopStart_, loopEnd_);
    }
    /// Host-style tempo change inside a block: the block reports `bpm` (the value at its start), but the position
    /// advances at `newBpm` from `changeSample` on.
    void blockWithTempoChange(int numSamples, double bpm, double newBpm, int changeSample) {
        const double ppq = nextPpq_;
        run(numSamples, true, bpm, true, ppq, looping_, loopStart_, loopEnd_);
        nextPpq_ = ppq + (changeSample * bpm + (numSamples - changeSample) * newBpm) / 60.0 / kSampleRate;
    }
    void setLoop(double start, double end) {
        looping_ = true;
        loopStart_ = start;
        loopEnd_ = end;
    }
    /// Host-style loop: after the block that reaches the loop end, the next block starts at the loop start.
    void blockWithLoopWrap(int numSamples) {
        run(numSamples, true, 120.0, true, nextPpq_, true, loopStart_, loopEnd_);
        if (nextPpq_ > loopEnd_) {
            nextPpq_ = loopStart_ + (nextPpq_ - loopEnd_);
        } else if (std::abs(nextPpq_ - loopEnd_) < 1e-9) {
            nextPpq_ = loopStart_;
        }
    }
    void bypass() {
        out_.clear();
        player_.releaseAll(out_, 0);
        collect(0);
        player_.invalidateTransport();
    }

    void setPpq(double ppq) { nextPpq_ = ppq; }
    const std::vector<Event>& events() const { return events_; }
    size_t soundingCount() const { return sounding_.size(); }
    long long clock() const { return clock_; }

    std::vector<long long> noteOnSamples() const {
        std::vector<long long> samples;
        for (const auto& e : events_) {
            if (e.noteOn) {
                samples.push_back(e.sample);
            }
        }
        return samples;
    }

private:
    void run(int numSamples, bool playing, double bpm, bool hasPosition, double ppq, bool looping, double loopStart,
             double loopEnd) {
        TransportInfo info;
        info.hasPosition = hasPosition;
        info.isPlaying = playing;
        info.ppq = ppq;
        info.bpm = bpm;
        info.isLooping = looping;
        info.loopStartPpq = loopStart;
        info.loopEndPpq = loopEnd;
        blockPpq_ = ppq;
        blockPpqPerSample_ = bpm / 60.0 / kSampleRate;
        player_.process(info, numSamples, kSampleRate, out_);
        collect(numSamples);
        nextPpq_ = ppq + numSamples * bpm / 60.0 / kSampleRate;
        clock_ += numSamples;
    }

    void collect(int numSamples) {
        int previousOffset = 0;
        for (const auto& e : out_) {
            REQUIRE(e.sampleOffset >= 0);
            REQUIRE(e.sampleOffset <= std::max(numSamples - 1, 0));
            REQUIRE(e.sampleOffset >= previousOffset); // sorted
            previousOffset = e.sampleOffset;
            const auto key = std::make_pair(e.channel, e.pitch);
            if (e.noteOn) {
                REQUIRE(sounding_.count(key) == 0); // never a double note-on
                sounding_.insert(key);
            } else {
                REQUIRE(sounding_.count(key) == 1); // never a stray note-off
                sounding_.erase(key);
            }
            events_.push_back({clock_ + e.sampleOffset, blockPpq_ + e.sampleOffset * blockPpqPerSample_, e.channel,
                               e.pitch, e.noteOn});
        }
    }

    PatternPlayer player_;
    MidiEventList out_;
    std::vector<Event> events_;
    std::set<std::pair<uint8_t, uint8_t>> sounding_;
    double nextPpq_ = 0.0;
    double blockPpq_ = 0.0;
    double blockPpqPerSample_ = 0.0;
    long long clock_ = 0;
    bool looping_ = false;
    double loopStart_ = 0.0;
    double loopEnd_ = 0.0;
};

/// Sample position of a PPQ position at 120 BPM.
long long samplesAtPpq(double ppq) {
    return std::llround(ppq * 60.0 / 120.0 * kSampleRate);
}

} // namespace

TEST_CASE("placeholder pattern: offbeat notes land sample-accurately", "[engine]") {
    Simulation sim;
    for (int i = 0; i < 4 * 48000 / 256 + 1; ++i) { // two bars at 120 BPM
        sim.block(256);
    }
    const auto ons = sim.noteOnSamples();
    REQUIRE(ons.size() >= 8);
    const double stepsPpq[] = {0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5};
    for (size_t i = 0; i < 8; ++i) {
        CHECK(std::abs(ons[i] - samplesAtPpq(stepsPpq[i])) <= 1);
    }
}

TEST_CASE("every note-on gets a note-off", "[engine]") {
    Simulation sim;
    for (int i = 0; i < 400; ++i) {
        sim.block(256);
    }
    sim.block(256, false);
    CHECK(sim.soundingCount() == 0);
    size_t on = 0, off = 0;
    for (const auto& e : sim.events()) {
        (e.noteOn ? on : off)++;
    }
    CHECK(on == off);
    CHECK(on > 0);
}

TEST_CASE("stop while a note sounds ends it at once", "[engine]") {
    Simulation sim;
    sim.blockAt(0.45, 2400); // note starts at ppq 0.5, 0.25 s in; block covers 0.05 s
    sim.blockAt(0.5, 480);   // starts the note at offset 0
    REQUIRE(sim.soundingCount() == 1);
    sim.block(256, false);
    CHECK(sim.soundingCount() == 0);
    CHECK(sim.events().back().sample == sim.clock() - 256); // note-off at the first sample of the stop block
}

TEST_CASE("jumps release sounding notes (forward, backward)", "[engine]") {
    for (double target : {32.0, 0.0}) {
        Simulation sim;
        sim.blockAt(0.0, 14400); // runs through ppq 0.5, a note is sounding at the end
        REQUIRE(sim.soundingCount() == 1);
        sim.blockAt(target, 256);
        // The old note is gone before anything else; a new one may only be there if it starts in this block.
        CHECK(sim.soundingCount() <= 1);
    }
}

TEST_CASE("loop wrap inside a block is sample-accurate and ends cut notes", "[engine]") {
    Simulation sim;
    sim.setLoop(0.0, 0.75); // cuts the note that starts at ppq 0.5 and would end at 0.875
    sim.setPpq(0.0);
    for (int i = 0; i < 300; ++i) {
        sim.blockWithLoopWrap(300); // 300 does not divide the loop length
    }
    sim.block(256, false);
    CHECK(sim.soundingCount() == 0);
    // Every loop pass (0.75 ppq = 18000 samples) starts one note at ppq 0.5 into the pass.
    const auto ons = sim.noteOnSamples();
    REQUIRE(ons.size() >= 3);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(std::abs(ons[i] - (static_cast<long long>(i) * 18000 + 12000)) <= 1);
    }
}

TEST_CASE("loop end exactly on a block boundary", "[engine]") {
    Simulation sim;
    sim.setLoop(0.0, 0.75);
    sim.setPpq(0.0);
    for (int i = 0; i < 40; ++i) {
        sim.blockWithLoopWrap(18000); // one block per loop pass
    }
    sim.block(256, false);
    CHECK(sim.soundingCount() == 0);
    CHECK(sim.noteOnSamples().size() >= 39);
}

TEST_CASE("missing host data and bypass release everything", "[engine]") {
    Simulation sim;
    sim.blockAt(0.0, 14400);
    REQUIRE(sim.soundingCount() == 1);
    sim.block(256, true, 120.0, false); // no position
    CHECK(sim.soundingCount() == 0);

    Simulation bypassed;
    bypassed.blockAt(0.0, 14400);
    REQUIRE(bypassed.soundingCount() == 1);
    bypassed.bypass();
    CHECK(bypassed.soundingCount() == 0);
}

TEST_CASE("note-offs after invalidateTransport (prepareToPlay)", "[engine]") {
    PatternPlayer player;
    MidiEventList out;
    TransportInfo info;
    info.hasPosition = true;
    info.isPlaying = true;
    info.bpm = 120.0;
    info.ppq = 0.0;
    player.process(info, 14400, kSampleRate, out); // note on at ppq 0.5 still sounding
    player.invalidateTransport();                  // host calls prepareToPlay
    info.isPlaying = false;
    player.process(info, 256, kSampleRate, out);
    REQUIRE(out.size() == 1);
    CHECK_FALSE(out[0].noteOn);
    CHECK(out[0].sampleOffset == 0);
}

TEST_CASE("result does not depend on the block size", "[engine]") {
    std::vector<long long> reference;
    for (int size : {16, 64, 100, 256, 512, 1000, 1024, 4096}) {
        Simulation sim;
        long long total = 0;
        while (total < 4 * 4 * 24000) { // four bars at 120 BPM
            sim.block(size);
            total += size;
        }
        std::vector<long long> ons;
        for (auto sample : sim.noteOnSamples()) {
            if (sample < 4 * 2 * 48000) { // compare only the first 4 bars
                ons.push_back(sample);
            }
        }
        if (reference.empty()) {
            reference = ons;
        }
        REQUIRE(ons.size() == reference.size());
        for (size_t i = 0; i < ons.size(); ++i) {
            CHECK(std::abs(ons[i] - reference[i]) <= 1);
        }
        sim.block(size, false);
        CHECK(sim.soundingCount() == 0);
    }
}

TEST_CASE("tempo changes between blocks are not mistaken for jumps", "[engine]") {
    Simulation sim;
    double bpm = 100.0;
    for (int i = 0; i < 600; ++i) {
        sim.block(512, true, bpm);
        bpm += 0.1;
    }
    sim.block(256, false);
    CHECK(sim.soundingCount() == 0);
    CHECK(sim.noteOnSamples().size() > 8);
}

TEST_CASE("a tempo change inside a block is not a jump", "[engine]") {
    // The host reports the tempo at the block start, the position follows the new tempo mid-block. A large step
    // must neither cut sounding notes nor swallow a note start in the gap between the blocks.
    constexpr double kNoteLengthPpq = 360.0 / 960.0;
    const std::vector<std::pair<double, double>> steps = {{120.0, 180.0}, {180.0, 90.0}, {90.0, 240.0}, {240.0, 100.0}};

    for (int changeSample : {1, 128, 400}) {
        Simulation sim;
        double bpm = 120.0;
        size_t stepIndex = 0;
        for (int i = 0; i < 1500; ++i) {
            if (i % 37 == 36) {
                const auto step = steps[stepIndex++ % steps.size()];
                bpm = step.first;
                sim.blockWithTempoChange(512, bpm, step.second, changeSample);
                bpm = step.second;
            } else {
                sim.block(512, true, bpm);
            }
        }

        std::vector<double> onPpq;
        std::vector<double> offPpq;
        for (const auto& e : sim.events()) {
            (e.noteOn ? onPpq : offPpq).push_back(e.ppq);
        }
        REQUIRE(onPpq.size() >= 8);
        // Notes start at 0.5 ppq of every bar beat: none may be missing between the first and the last one.
        for (size_t i = 1; i < onPpq.size(); ++i) {
            const double beats = std::round(onPpq[i] - onPpq[i - 1]);
            CHECK(beats >= 1.0);
            CHECK(beats <= 1.0);
        }
        // No note is cut short (a note-off before its planned end would mean a spurious release).
        REQUIRE(offPpq.size() + 1 >= onPpq.size());
        for (size_t i = 0; i < offPpq.size(); ++i) {
            CHECK(offPpq[i] - onPpq[i] > kNoteLengthPpq - 0.02);
        }
    }
}

TEST_CASE("tempo change inside a block: note start in the gap is played at the first sample", "[engine]") {
    // Expected end of block 1 is just before the note at 0.5 ppq; the host position has moved past it (tempo up).
    Simulation sim;
    sim.setPpq(0.5 - 1e-4 - 512 * 120.0 / 60.0 / kSampleRate);
    sim.blockWithTempoChange(512, 120.0, 240.0, 1);
    sim.block(512, true, 240.0);
    REQUIRE(sim.noteOnSamples().size() == 1);
    CHECK(sim.noteOnSamples()[0] == 512);
}

TEST_CASE("tempo change inside a block: note start in the overlap is not played twice", "[engine]") {
    // Tempo down: the note at 0.5 ppq was already started in block 1, the host position lies before it again.
    Simulation sim;
    sim.setPpq(0.5 + 1e-4 - 512 * 240.0 / 60.0 / kSampleRate);
    sim.blockWithTempoChange(512, 240.0, 120.0, 1);
    sim.block(512, true, 120.0);
    CHECK(sim.noteOnSamples().size() == 1);
}

TEST_CASE("tempo change inside a block: note end in the gap is sent at the first sample", "[engine]") {
    // The note started at 0.5 ppq ends at 0.875 ppq, between the expected end of block 1 and the host position.
    Simulation sim;
    sim.setPpq(0.8749 - 9600 * 120.0 / 60.0 / kSampleRate - 512 * 120.0 / 60.0 / kSampleRate);
    sim.block(9600, true, 120.0);
    REQUIRE(sim.soundingCount() == 1);
    sim.blockWithTempoChange(512, 120.0, 240.0, 1);
    REQUIRE(sim.soundingCount() == 1);
    sim.block(512, true, 240.0);
    REQUIRE(sim.soundingCount() == 0);
    CHECK(sim.events().back().sample == 9600 + 512);
}

TEST_CASE("tempo change inside a block: a note in the gap is kept in the block that wraps the loop", "[engine]") {
    Simulation sim;
    sim.setLoop(0.0, 4.0);
    sim.setPpq(1.0);
    sim.blockWithTempoChange(24000, 120.0, 240.0, 1); // expected end 2.0, host position 3.0: note at 2.5 is in the gap
    sim.block(24000, true, 240.0);                    // wraps at 4.0
    // 1.5 (block 1); 2.5 (gap), 3.5 and, after the wrap, 0.5 (block 2)
    CHECK(sim.noteOnSamples().size() == 4);
}

TEST_CASE("tempo change inside a block: sounding notes keep sounding, up or down", "[engine]") {
    for (const auto& [bpm, newBpm] : {std::make_pair(240.0, 120.0), std::make_pair(120.0, 240.0)}) {
        Simulation sim;
        sim.setPpq(0.45); // the note at 0.5 ppq sounds until 0.875 ppq
        sim.block(2400, true, bpm);
        REQUIRE(sim.soundingCount() == 1);
        sim.blockWithTempoChange(512, bpm, newBpm, 1);
        sim.block(512, true, newBpm);
        CHECK(sim.soundingCount() == 1);
    }
}

TEST_CASE("jumps are still detected after the tolerance change", "[engine]") {
    constexpr double kPpqPerSample = 120.0 / 60.0 / kSampleRate;
    {
        // constant tempo, the host position is off by 0.9 samples: no jump, the note keeps sounding
        Simulation sim;
        sim.setPpq(0.45);
        sim.block(2400, true, 120.0);
        REQUIRE(sim.soundingCount() == 1);
        sim.blockAt(0.45 + 2400 * kPpqPerSample + 0.9 * kPpqPerSample, 512, true, 120.0);
        CHECK(sim.soundingCount() == 1);
    }
    {
        // constant tempo, the position jumps by 0.01 ppq (240 samples): a jump
        Simulation sim;
        sim.setPpq(0.45);
        sim.block(2400, true, 120.0);
        REQUIRE(sim.soundingCount() == 1);
        sim.blockAt(0.45 + 2400 * kPpqPerSample + 0.01, 512, true, 120.0);
        CHECK(sim.soundingCount() == 0);
    }
    {
        // after a tempo change: a deviation beyond what the change can cause is a jump too
        Simulation sim;
        sim.setPpq(0.45);
        sim.block(2400, true, 120.0);
        const double expectedEnd = 0.45 + 2400 * kPpqPerSample + 512 * kPpqPerSample;
        sim.blockWithTempoChange(512, 120.0, 240.0, 1); // may deviate by 512 * 2 / 48000 = 0.0213 ppq
        REQUIRE(sim.soundingCount() == 1);
        sim.blockAt(expectedEnd + 0.04, 512, true, 240.0);
        CHECK(sim.soundingCount() == 0);
    }
}

TEST_CASE("a real jump right after a tempo change still releases the notes", "[engine]") {
    Simulation sim;
    // Run into the first note (starts at 0.5 ppq), change the tempo inside a block, then jump a bar ahead.
    sim.block(512, true, 120.0);
    sim.blockWithTempoChange(512, 120.0, 130.0, 128);
    while (sim.soundingCount() == 0) {
        sim.block(512, true, 130.0);
    }
    sim.blockAt(40.0, 512, true, 130.0);
    CHECK(sim.events().back().noteOn == false);
    CHECK(sim.soundingCount() == 0);
}

TEST_CASE("the count-in with negative positions is silent", "[engine]") {
    // Two bars of count-in, then the arrangement starts at ppq 0.
    Simulation sim;
    sim.setPpq(-8.0);
    for (int i = 0; i < 400; ++i) { // 400 * 512 samples at 120 bpm: about 8.5 ppq
        sim.block(512);
    }
    REQUIRE(!sim.events().empty());
    for (const auto& e : sim.events()) {
        CHECK(e.ppq >= -1e-9);
    }
    // The first note of the pattern sits at 0.5 ppq.
    CHECK(sim.events().front().noteOn);
    CHECK(sim.events().front().ppq == Catch::Approx(0.5).margin(0.01));
}

TEST_CASE("a block that crosses zero plays from zero on", "[engine]") {
    Simulation sim;
    sim.setPpq(-0.7);
    sim.block(31200); // 1.3 ppq at 120 bpm: from -0.7 to 0.6, contains the notes at -0.5 and 0.5
    REQUIRE(sim.noteOnSamples().size() == 1);
    // 1.2 ppq after the block start: 1.2 / (2 / 48000) = 28800 samples
    CHECK(sim.noteOnSamples().front() == 28800);
}

TEST_CASE("a jump into the count-in releases the notes and stays silent", "[engine]") {
    Simulation sim;
    sim.blockAt(0.4, 512);
    sim.blockAt(0.5, 256); // note at 0.5 sounding
    REQUIRE(sim.soundingCount() == 1);
    const size_t before = sim.events().size();
    sim.blockAt(-0.6, 4800); // 0.2 ppq: covers the note position -0.5 of the previous cycle
    CHECK(sim.soundingCount() == 0);
    for (size_t i = before; i < sim.events().size(); ++i) {
        CHECK(!sim.events()[i].noteOn);
    }
    sim.block(512);
    CHECK(sim.soundingCount() == 0);
}

TEST_CASE("note-off never lands before or on its note-on", "[engine]") {
    // 1-tick note: shorter than a sample
    static const PatternNote notes[] = {{0, 1, 1, 40, 100}, {960, 1, 1, 40, 100}};
    Simulation sim(PatternView{notes, 2, 4 * kTicksPerQuarter});
    for (int i = 0; i < 400; ++i) {
        sim.block(64);
    }
    sim.block(64, false);
    CHECK(sim.soundingCount() == 0);
}

TEST_CASE("event list sorting is stable and puts note-offs first", "[engine]") {
    MidiEventList list;
    list.push({10, 1, 40, 100, true});
    list.push({5, 1, 41, 100, true});
    list.push({10, 1, 42, 0, false});
    list.push({5, 1, 43, 0, false});
    list.sort();
    CHECK(list[0].pitch == 43);
    CHECK(list[1].pitch == 41);
    CHECK(list[2].pitch == 42);
    CHECK(list[3].pitch == 40);
}
