#include "engine/PatternPlayer.h"

#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <set>
#include <thread>
#include <utility>
#include <vector>

using namespace mm::engine;

// Counts the allocations made through operator new on the thread that asked for it (the engine uses the standard
// library only and must not allocate while processing, SPEC 6.3).
namespace {
thread_local bool tCounting = false;
std::atomic<long> gAllocations{0};
} // namespace

void* operator new(std::size_t size) {
    if (tCounting) {
        ++gAllocations;
    }
    if (void* p = std::malloc(size > 0 ? size : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

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
    PatternPlayer& player() { return player_; }
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

// Patterns for the switch tests (all one channel, 4 PPQ long).
const PatternNote kLongNotes[] = {{3000, 900, 1, 40, 100}}; // 7.125 to 8.0625 in the second cycle: sounds at 8.0
const PatternNote kNextNotes[] = {{0, 240, 1, 50, 100}, {1920, 240, 1, 50, 100}}; // position 0 and 2
const PatternNote kEdgeNotes[] = {
    {3839, 240, 1, 41, 100}}; // starts a fraction of a sample before the bar line at very high tempo

PatternView viewOf(const PatternNote* notes, size_t count) {
    return {notes, count, 4 * kTicksPerQuarter};
}
PatternView longPattern() {
    return viewOf(kLongNotes, 1);
}
PatternView nextPattern() {
    return viewOf(kNextNotes, 2);
}
PatternView edgePattern() {
    return viewOf(kEdgeNotes, 1);
}

std::vector<Event> ofPitch(const std::vector<Event>& events, uint8_t pitch, bool noteOn) {
    std::vector<Event> result;
    for (const auto& e : events) {
        if (e.pitch == pitch && e.noteOn == noteOn) {
            result.push_back(e);
        }
    }
    return result;
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

TEST_CASE("a switch waits for the next bar line and cuts sounding notes there", "[engine][switch]") {
    Simulation sim(longPattern());
    sim.setPpq(5.3);
    sim.block(512); // running; the long note of the old pattern starts at 7.125
    sim.player().requestSwitch(nextPattern(), 4.0);
    while (sim.clock() < 4 * 24000) {
        sim.block(512);
    }
    const long long bar = std::llround((8.0 - 5.3) * kSampleRate / 2.0);
    const auto offs = ofPitch(sim.events(), 40, false);
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(offs.size() >= 1);
    REQUIRE(ons.size() >= 1);
    CHECK(offs.front().sample == Catch::Approx(static_cast<double>(bar)).margin(1.0)); // cut at the bar line
    CHECK(ons.front().sample == Catch::Approx(static_cast<double>(bar)).margin(1.0));  // new pattern from position 0
    CHECK(ons.front().sample >= offs.front().sample);
    CHECK_FALSE(sim.player().switchPending());
    // The old pattern played up to the bar line only.
    for (const auto& e : ofPitch(sim.events(), 40, true)) {
        CHECK(e.sample < bar);
    }
}

TEST_CASE("a switch read in a block that starts on the bar line acts at its first sample", "[engine][switch]") {
    Simulation sim(longPattern());
    sim.blockAt(6.0, 24000);
    sim.blockAt(7.0, 24000); // ends exactly at 8.0
    sim.player().requestSwitch(nextPattern(), 4.0);
    sim.block(512);
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(ons.size() == 1);
    CHECK(ons.front().sample == 48000);
}

TEST_CASE("a switch is never applied retroactively", "[engine][switch]") {
    Simulation sim(longPattern());
    sim.blockAt(7.0, 24512); // ends 512 samples after the bar line at 8.0
    sim.player().requestSwitch(nextPattern(), 4.0);
    while (sim.clock() < 7 * 24000) {
        sim.block(512);
    }
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(ons.size() >= 1);
    CHECK(ons.front().sample == Catch::Approx(5.0 * 24000).margin(1.0)); // bar line at 12.0
    CHECK(sim.soundingCount() <= 1);
}

TEST_CASE("grids shorter than a bar are rounded up to the bar line", "[engine][switch]") {
    Simulation sim(longPattern());
    sim.setPpq(5.3);
    sim.block(512);
    sim.player().requestSwitch(nextPattern(), 1.0); // next beat would be 6.0
    while (sim.clock() < 4 * 24000) {
        sim.block(512);
    }
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(ons.size() >= 1);
    CHECK(ons.front().sample == Catch::Approx((8.0 - 5.3) * 24000).margin(1.0));
}

TEST_CASE("a planned switch uses its stamp, a stale stamp falls back to the next grid point", "[engine][switch]") {
    SECTION("stamp ahead of the block") {
        Simulation sim(longPattern());
        sim.setPpq(5.3);
        sim.block(512);
        sim.player().requestSwitchAt(nextPattern(), 12.0, 4.0);
        while (sim.clock() < 8 * 24000) {
            sim.block(512);
        }
        const auto ons = ofPitch(sim.events(), 50, true);
        REQUIRE(ons.size() >= 1);
        CHECK(ons.front().sample == Catch::Approx((12.0 - 5.3) * 24000).margin(1.0));
    }
    SECTION("stamp behind the block start") {
        Simulation sim(longPattern());
        sim.blockAt(7.0, 24000 + 2400); // ends at 8.1
        sim.player().requestSwitchAt(nextPattern(), 8.0, 4.0);
        while (sim.clock() < 7 * 24000) {
            sim.block(512);
        }
        const auto ons = ofPitch(sim.events(), 50, true);
        REQUIRE(ons.size() >= 1);
        CHECK(ons.front().sample == Catch::Approx(5.0 * 24000).margin(1.0)); // 12.0
    }
}

TEST_CASE("start and jump apply a pending switch at once", "[engine][switch]") {
    SECTION("jump") {
        Simulation sim(longPattern());
        sim.setPpq(5.3);
        sim.block(512);
        sim.player().requestSwitch(nextPattern(), 4.0); // point 8.0
        sim.blockAt(7.0, 512);
        sim.blockAt(20.5, 24000 * 2);
        const auto ons = ofPitch(sim.events(), 50, true);
        REQUIRE(ons.size() >= 1);
        CHECK(ons.front().sample == 1024 + samplesAtPpq(22.0) - samplesAtPpq(20.5)); // position 2.0 of the new pattern
        CHECK_FALSE(sim.player().switchPending());
    }
    SECTION("stop keeps it for the next start") {
        Simulation sim(longPattern());
        sim.setPpq(5.3);
        sim.block(512);
        sim.player().requestSwitch(nextPattern(), 4.0);
        sim.block(512, false);
        CHECK(sim.player().switchPending());
        sim.blockAt(0.5, 24000 * 2);
        CHECK_FALSE(sim.player().switchPending());
        const auto ons = ofPitch(sim.events(), 50, true);
        REQUIRE(ons.size() >= 1); // the new pattern plays from the start, not from the next bar line (4.0)
        CHECK(ons.front().sample == 1024 + samplesAtPpq(2.0) - samplesAtPpq(0.5));
    }
    SECTION("start in the middle of the arrangement") {
        Simulation sim(longPattern());
        sim.player().requestSwitch(nextPattern(), 4.0);
        sim.blockAt(148.0, 24000 * 3);
        const auto ons = ofPitch(sim.events(), 50, true);
        REQUIRE(ons.size() >= 1);
        CHECK(ons.front().sample == 0); // position 0 of the new pattern at 148.0
    }
}

TEST_CASE("a loop wrap applies a pending switch whose point is out of reach", "[engine][switch]") {
    Simulation sim(longPattern());
    sim.setLoop(0.0, 3.75);
    sim.setPpq(0.0);
    for (int i = 0; i < 150; ++i) { // a little over 3 PPQ
        sim.blockWithLoopWrap(512);
    }
    sim.player().requestSwitch(nextPattern(), 4.0); // point 4.0 lies behind the loop end
    for (int i = 0; i < 60; ++i) {
        sim.blockWithLoopWrap(512);
    }
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(ons.size() >= 1);
    CHECK(ons.front().sample == Catch::Approx(3.75 * 24000).margin(1.0)); // the wrap sample, position 0
    CHECK_FALSE(sim.player().switchPending());
}

TEST_CASE("a note that starts in the sample of the switch point is never left hanging", "[engine][switch]") {
    // At 8000 BPM one tick is 0.375 samples, so the note at tick 3839 starts in the sample of the bar line.
    Simulation sim(edgePattern());
    sim.setPpq(5.3);
    sim.block(512, true, 8000.0);
    sim.player().requestSwitch(nextPattern(), 4.0);
    for (int i = 0; i < 12; ++i) {
        sim.block(512, true, 8000.0);
    }
    CHECK(ofPitch(sim.events(), 41, true).size() >= 1);
    CHECK(sim.soundingCount() <= 2); // only notes of the new pattern may still sound
    for (const auto& e : sim.events()) {
        if (e.pitch == 41 && !e.noteOn) {
            CHECK(e.sample > 0);
        }
    }
}

// ---- lock-free handover (SPEC 6.3, D-133) ----

namespace {

std::unique_ptr<OwnedPattern> owned(std::vector<PatternNote> notes, uint64_t version,
                                    std::shared_ptr<const void> token = {},
                                    uint32_t lengthTicks = 4 * kTicksPerQuarter) {
    auto pattern = std::make_unique<OwnedPattern>();
    pattern->notes = std::move(notes);
    pattern->lengthTicks = lengthTicks;
    pattern->version = version;
    pattern->lifetimeToken = std::move(token);
    return pattern;
}

std::vector<PatternNote> longNotes() {
    return {{3000, 900, 1, 40, 100}};
}
std::vector<PatternNote> nextNotes() {
    return {{0, 240, 1, 50, 100}, {1920, 240, 1, 50, 100}};
}

} // namespace

TEST_CASE("a published switch plays like a requested one and the old pattern is retired", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    sim.setPpq(5.3);
    sim.block(512);
    handover.publishSwitch(owned(nextNotes(), 7), 4.0);
    while (sim.clock() < 4 * 24000) {
        sim.block(512);
    }
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(ons.size() >= 1);
    CHECK(ons.front().sample == Catch::Approx((8.0 - 5.3) * 24000).margin(1.0));
    CHECK(handover.activeVersion() == 7);
    CHECK(handover.collectReturned() == 0); // the first pattern had no owner

    handover.publishSwitch(owned(longNotes(), 8), 4.0);
    while (sim.clock() < 10 * 24000) {
        sim.block(512);
    }
    CHECK(handover.activeVersion() == 8);
    CHECK(handover.collectReturned() == 1); // version 7 came back
}

TEST_CASE("a newer switch replaces an older one the audio thread has not seen", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    auto tokenA = std::make_shared<int>(0);
    handover.publishSwitch(owned(longNotes(), 1, tokenA), 4.0);
    CHECK(tokenA.use_count() == 2);
    handover.publishSwitch(owned(nextNotes(), 2), 4.0);
    CHECK(tokenA.use_count() == 1); // freed by the producer, never seen by the audio thread
    sim.setPpq(5.3);
    while (sim.clock() < 4 * 24000) {
        sim.block(512);
    }
    CHECK(handover.activeVersion() == 2);
    CHECK(handover.collectReturned() == 0);
}

TEST_CASE("a pending switch replaced inside the player is handed back", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    auto tokenA = std::make_shared<int>(0);
    sim.setPpq(5.3);
    sim.block(512);
    handover.publishSwitch(owned(longNotes(), 1, tokenA), 4.0);
    sim.block(512); // taken, the point 8.0 is fixed
    handover.publishSwitch(owned(nextNotes(), 2), 4.0);
    sim.block(512); // replaces it
    CHECK(handover.collectReturned() == 1);
    CHECK(tokenA.use_count() == 1);
    while (sim.clock() < 4 * 24000) {
        sim.block(512);
    }
    CHECK(handover.activeVersion() == 2);
}

TEST_CASE("a full return queue postpones the switch to the next grid point", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    sim.setPpq(5.3);
    // Each edit retires the previous pattern; the first one had no owner, so 9 edits fill the 8 places.
    for (uint64_t version = 1; version <= PatternHandover::kReturnCapacity + 1; ++version) {
        handover.publishEdit(owned(longNotes(), version));
        sim.block(512);
    }
    REQUIRE(handover.freeReturnSlots() == 0);
    handover.publishSwitch(owned(nextNotes(), 100), 4.0);
    while (sim.clock() < 3 * 24000) { // past the bar line at 8.0
        sim.block(512);
    }
    CHECK(ofPitch(sim.events(), 50, true).empty()); // the old pattern keeps playing
    CHECK(handover.activeVersion() == PatternHandover::kReturnCapacity + 1);
    CHECK(handover.collectReturned() == PatternHandover::kReturnCapacity);
    while (sim.clock() < 8 * 24000) {
        sim.block(512);
    }
    const auto ons = ofPitch(sim.events(), 50, true);
    REQUIRE(ons.size() >= 1);
    CHECK(ons.front().sample == Catch::Approx((12.0 - 5.3) * 24000).margin(1.0)); // the next bar line
    CHECK(handover.activeVersion() == 100);
}

TEST_CASE("a full return queue holds an edit in the mailbox", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    for (uint64_t version = 1; version <= PatternHandover::kReturnCapacity + 1; ++version) {
        handover.publishEdit(owned(longNotes(), version));
        sim.block(512);
    }
    handover.publishEdit(owned(longNotes(), 50));
    sim.block(512);
    CHECK(handover.activeVersion() == PatternHandover::kReturnCapacity + 1);
    handover.collectReturned();
    sim.block(512);
    CHECK(handover.activeVersion() == 50);
}

TEST_CASE("an edit keeps the position and ends only the notes that changed", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    sim.blockAt(7.0, 12000); // 7.0 to 7.5: the long note (7.125 to 8.0625) sounds
    REQUIRE(sim.soundingCount() == 1);

    SECTION("an unchanged note keeps sounding, a new note plays at its place in the cycle") {
        auto notes = longNotes();
        notes.push_back({3600, 120, 1, 60, 100}); // position 3.75, so 7.75
        handover.publishEdit(owned(notes, 2));
        while (sim.clock() < 28800) { // up to 8.2
            sim.block(512);
        }
        CHECK(ofPitch(sim.events(), 40, false).size() == 1);
        CHECK(ofPitch(sim.events(), 40, false).front().sample == Catch::Approx(25500.0).margin(1.0)); // 8.0625
        const auto ons = ofPitch(sim.events(), 60, true);
        REQUIRE(ons.size() == 1);
        CHECK(ons.front().sample == Catch::Approx(18000.0).margin(1.0)); // 7.75
        CHECK(ofPitch(sim.events(), 40, true).size() == 1);              // not retriggered
    }
    SECTION("a removed note ends at once") {
        handover.publishEdit(owned(nextNotes(), 2));
        const long long before = sim.clock();
        sim.block(512);
        const auto offs = ofPitch(sim.events(), 40, false);
        REQUIRE(offs.size() == 1);
        CHECK(offs.front().sample == before);
        CHECK(sim.soundingCount() == 0);
    }
    SECTION("a note with a changed length ends at once") {
        handover.publishEdit(owned({{3000, 500, 1, 40, 100}}, 2));
        const long long before = sim.clock();
        sim.block(512);
        const auto offs = ofPitch(sim.events(), 40, false);
        REQUIRE(offs.size() == 1);
        CHECK(offs.front().sample == before);
    }
    SECTION("a different pattern length ends everything") {
        handover.publishEdit(owned(longNotes(), 2, {}, 8 * kTicksPerQuarter));
        const long long before = sim.clock();
        sim.block(512);
        const auto offs = ofPitch(sim.events(), 40, false);
        REQUIRE(offs.size() == 1);
        CHECK(offs.front().sample == before);
    }
}

TEST_CASE("an edit is taken while the transport stands", "[engine][handover]") {
    PatternHandover handover;
    Simulation sim(longPattern());
    sim.player().attach(&handover);
    handover.publishEdit(owned(nextNotes(), 3));
    sim.block(512, false);
    CHECK(handover.activeVersion() == 3);
}

TEST_CASE("two threads hand patterns over without losing or leaking any", "[engine][handover]") {
    auto token = std::make_shared<int>(0);
    {
        PatternHandover handover;
        PatternPlayer player;
        player.attach(&handover);
        std::atomic<bool> stop{false};
        std::atomic<bool> failed{false};

        std::thread audio([&] {
            MidiEventList out;
            std::set<std::pair<uint8_t, uint8_t>> sounding;
            double ppq = 0.0;
            while (!stop.load()) {
                TransportInfo info;
                info.hasPosition = true;
                info.isPlaying = true;
                info.ppq = ppq;
                info.bpm = 120.0;
                player.process(info, 256, kSampleRate, out);
                ppq += 256 * 120.0 / 60.0 / kSampleRate;
                for (const auto& e : out) {
                    const auto key = std::make_pair(e.channel, e.pitch);
                    if (e.noteOn ? !sounding.insert(key).second : sounding.erase(key) == 0) {
                        failed = true;
                    }
                }
            }
            out.clear();
            player.releaseAll(out, 0);
            for (const auto& e : out) {
                if (sounding.erase(std::make_pair(e.channel, e.pitch)) == 0) {
                    failed = true;
                }
            }
            if (!sounding.empty()) {
                failed = true;
            }
        });

        for (uint64_t i = 1; i <= 4000; ++i) {
            auto pattern = owned(i % 2 == 0 ? nextNotes() : longNotes(), i, token);
            if (i % 3 == 0) {
                handover.publishEdit(std::move(pattern));
            } else {
                handover.publishSwitch(std::move(pattern), 1.0);
            }
            handover.collectReturned();
            if (i % 8 == 0) {
                std::this_thread::yield();
            }
        }
        stop = true;
        audio.join();
        CHECK_FALSE(failed.load());
    }
    CHECK(token.use_count() == 1); // everything was freed exactly once
}

TEST_CASE("the audio side never allocates, also while taking, applying and retiring patterns", "[engine][handover]") {
    PatternHandover handover;
    PatternPlayer player;
    player.attach(&handover);
    MidiEventList out;
    double ppq = 0.0;
    auto process = [&] {
        TransportInfo info;
        info.hasPosition = true;
        info.isPlaying = true;
        info.ppq = ppq;
        info.bpm = 120.0;
        player.process(info, 512, kSampleRate, out);
        ppq += 512 * 120.0 / 60.0 / kSampleRate;
    };
    long allocations = 0;
    for (int round = 0; round < 12; ++round) {
        if (round % 2 == 0) {
            handover.publishSwitch(owned(round % 4 == 0 ? nextNotes() : longNotes(), round + 1), 4.0);
        } else {
            handover.publishEdit(owned(longNotes(), round + 1));
        }
        gAllocations = 0;
        tCounting = true;
        for (int i = 0; i < 200; ++i) {
            process();
        }
        tCounting = false;
        allocations += gAllocations;
        handover.collectReturned();
    }
    CHECK(allocations == 0);
    CHECK(handover.activeVersion() >= 11);
}

TEST_CASE("a start in the middle of a bar skips the notes before it (Z2)", "[engine]") {
    Simulation sim;
    sim.blockAt(5.0, 512 * 40); // position 1.0 of the pattern
    const auto ons = sim.noteOnSamples();
    REQUIRE(ons.size() >= 1);
    CHECK(ons.front() == 12000); // the note at 5.5; the one at 4.5 is skipped
}

TEST_CASE("the loop wrap cuts a note that sounds at the loop end (Z6)", "[engine]") {
    Simulation sim;
    sim.setLoop(0.0, 3.75);
    sim.blockAt(3.0, 16800); // the note at 3.5 starts in this block
    REQUIRE(sim.soundingCount() == 1);
    const long long start = sim.clock();
    sim.blockAt(3.7, 2048); // wraps at 3.75, 1200 samples into the block
    CHECK(sim.soundingCount() == 0);
    long long off = -1;
    for (const auto& e : sim.events()) {
        if (!e.noteOn && e.sample >= start) {
            off = e.sample;
        }
    }
    CHECK(off == start + 1200);
}

TEST_CASE("data returning after a gap start the pattern anew (Z8)", "[engine]") {
    Simulation sim;
    sim.blockAt(0.0, 14400);
    REQUIRE(sim.soundingCount() == 1);
    sim.block(256, true, 120.0, false); // no position: note-off
    CHECK(sim.soundingCount() == 0);
    const long long before = sim.clock();
    sim.blockAt(8.0, 512 * 40);
    long long firstOn = -1;
    for (const auto& e : sim.events()) {
        if (e.noteOn && e.sample >= before) {
            firstOn = e.sample;
            break;
        }
    }
    CHECK(firstOn == before + 12000); // position 0 at 8.0: the first note is at 8.5
}
