#include "core/MidiFile.h"
#include "core/MidiImport.h"
#include "core/Pattern.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace mm::core;

namespace {

using Bytes = std::vector<uint8_t>;

void put(Bytes& out, std::initializer_list<uint8_t> bytes) {
    out.insert(out.end(), bytes);
}

void vlq(Bytes& out, uint32_t value) {
    uint8_t groups[5];
    int count = 0;
    do {
        groups[count++] = static_cast<uint8_t>(value & 0x7f);
        value >>= 7;
    } while (value != 0);
    while (count > 0) {
        const uint8_t group = groups[--count];
        out.push_back(count > 0 ? static_cast<uint8_t>(group | 0x80) : group);
    }
}

Bytes header(uint16_t format, uint16_t tracks, uint16_t division) {
    Bytes out = {'M', 'T', 'h', 'd', 0, 0, 0, 6};
    put(out, {static_cast<uint8_t>(format >> 8), static_cast<uint8_t>(format), static_cast<uint8_t>(tracks >> 8),
              static_cast<uint8_t>(tracks), static_cast<uint8_t>(division >> 8), static_cast<uint8_t>(division)});
    return out;
}

Bytes chunk(const Bytes& body) {
    Bytes out = {'M', 'T', 'r', 'k'};
    const uint32_t n = static_cast<uint32_t>(body.size());
    put(out, {static_cast<uint8_t>(n >> 24), static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 8),
              static_cast<uint8_t>(n)});
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void endOfTrack(Bytes& body, uint32_t delta = 0) {
    vlq(body, delta);
    put(body, {0xff, 0x2f, 0x00});
}

Bytes file(uint16_t division, const Bytes& track) {
    Bytes out = header(0, 1, division);
    const Bytes body = chunk(track);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

MidiReadResult read(const Bytes& bytes) {
    return readMidiFile(bytes);
}

} // namespace

TEST_CASE("a pattern written by the exporter reads back exactly", "[midi-import]") {
    const PatternNote notes[] = {{0, 240, 1, 45, 96},    {480, 700, 1, 48, 80}, {1920, 5, 1, 52, 127},
                                 {2400, 240, 2, 60, 64}, {3839, 1, 2, 62, 100}, {3840, 1920, 1, 40, 90}};
    const PatternView view{notes, 6, 2 * kTicksPerBar};
    MidiFileOptions options;
    options.bpm = 133.0;
    const auto result = read(writeMidiFile(view, options));
    REQUIRE(result.ok());
    REQUIRE(result.clip.notes.size() == 6);
    for (size_t i = 0; i < 6; ++i) {
        INFO(i);
        const auto& note = result.clip.notes[i];
        CHECK(note.startTick == notes[i].startTick);
        CHECK(note.lengthTicks == notes[i].lengthTicks);
        CHECK(note.pitch == notes[i].pitch);
        CHECK(note.velocity == notes[i].velocity);
        CHECK(note.channel == notes[i].channel);
    }
    CHECK(result.clip.lengthTicks == 2 * kTicksPerBar);
    CHECK(result.clip.onlyFourFour);
    CHECK(result.clip.bpm > 132.9);
    CHECK(result.clip.bpm < 133.1);
    CHECK(result.clip.sourcePpq == 960);
}

TEST_CASE("running status, note on with velocity 0, meta events and sysex are understood", "[midi-import]") {
    Bytes track;
    vlq(track, 0);
    put(track, {0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}); // 120 BPM
    vlq(track, 0);
    put(track, {0xf0, 0x03, 0x7d, 0x01, 0xf7}); // a sysex message
    vlq(track, 0);
    put(track, {0xc0, 0x05}); // program change (one data byte)
    vlq(track, 0);
    put(track, {0x91, 40, 100}); // note on, channel 2
    vlq(track, 240);
    put(track, {40, 0}); // running status: note on with velocity 0 = off
    vlq(track, 240);
    put(track, {43, 90}); // running status: note on
    vlq(track, 480);
    put(track, {0x81, 43, 0}); // note off
    endOfTrack(track, 240);
    const auto result = read(file(480, track));
    REQUIRE(result.ok());
    REQUIRE(result.clip.notes.size() == 2);
    CHECK(result.clip.notes[0].channel == 2);
    CHECK(result.clip.notes[0].pitch == 40);
    CHECK(result.clip.notes[0].startTick == 0);
    CHECK(result.clip.notes[0].lengthTicks == 480); // 240 ticks at 480 PPQ
    CHECK(result.clip.notes[1].pitch == 43);
    CHECK(result.clip.notes[1].startTick == 960);
    CHECK(result.clip.notes[1].lengthTicks == 960);
    CHECK(result.clip.notes[1].velocity == 90);
    CHECK(result.clip.bpm > 119.9);
    CHECK(result.clip.bpm < 120.1);
    CHECK(result.clip.lengthTicks == 2400); // the end-of-track event, 1200 ticks at 480 PPQ
}

TEST_CASE("other divisions are converted to 960 PPQ without losing triplets", "[midi-import]") {
    for (const uint16_t division : {96, 192, 480, 960, 1920}) {
        Bytes track;
        // a note on the first triplet of a beat: a third of a quarter note, one eighth long
        const uint32_t third = division / 3;
        vlq(track, third);
        put(track, {0x90, 36, 100});
        vlq(track, division / 2);
        put(track, {0x80, 36, 0});
        endOfTrack(track, division);
        const auto result = read(file(division, track));
        INFO(division);
        REQUIRE(result.ok());
        REQUIRE(result.clip.notes.size() == 1);
        CHECK(result.clip.notes[0].startTick == (third * 960 + division / 2) / division);
        CHECK(result.clip.notes[0].lengthTicks == 480);
        CHECK(result.clip.lengthTicks >= 960); // the end-of-track event is one quarter note after the note off
    }
    // 96 PPQ: a tick is 10 ticks of 960
    Bytes track;
    vlq(track, 7);
    put(track, {0x90, 36, 100});
    vlq(track, 3);
    put(track, {0x80, 36, 0});
    endOfTrack(track);
    const auto result = read(file(96, track));
    REQUIRE(result.ok());
    CHECK(result.clip.notes[0].startTick == 70);
    CHECK(result.clip.notes[0].lengthTicks == 30);
}

TEST_CASE("ticks that do not divide evenly are rounded to the nearest tick of 960 PPQ", "[midi-import]") {
    // 700 ticks per quarter note: the factor to 960 is 1.371...
    for (const auto& [tick, expected] :
         std::vector<std::pair<uint32_t, uint32_t>>{{1, 1}, {3, 4}, {4, 5}, {5, 7}, {7, 10}}) {
        Bytes track;
        vlq(track, tick);
        put(track, {0x90, 36, 100});
        vlq(track, 700);
        put(track, {0x80, 36, 0});
        endOfTrack(track);
        const auto result = read(file(700, track));
        INFO(tick);
        REQUIRE(result.ok());
        CHECK(result.clip.notes[0].startTick == expected);
    }
}

TEST_CASE("the tracks of a format 1 file are merged", "[midi-import]") {
    Bytes conductor;
    vlq(conductor, 0);
    put(conductor, {0xff, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08}); // 4/4
    endOfTrack(conductor, 3840);
    Bytes bass;
    vlq(bass, 0);
    put(bass, {0x90, 33, 100});
    vlq(bass, 240);
    put(bass, {0x80, 33, 0});
    endOfTrack(bass);
    Bytes lead;
    vlq(lead, 120);
    put(lead, {0x91, 69, 80});
    vlq(lead, 120);
    put(lead, {0x81, 69, 0});
    endOfTrack(lead);
    Bytes data = header(1, 3, 960);
    for (const auto* part : {&conductor, &bass, &lead}) {
        const Bytes c = chunk(*part);
        data.insert(data.end(), c.begin(), c.end());
    }
    const auto result = read(data);
    REQUIRE(result.ok());
    REQUIRE(result.clip.notes.size() == 2);
    CHECK(result.clip.notes[0].pitch == 33);
    CHECK(result.clip.notes[1].pitch == 69);
    CHECK(result.clip.notes[1].startTick == 120);
    CHECK(result.clip.onlyFourFour);
    CHECK(result.clip.lengthTicks == 3840); // the conductor track says how long the clip is
}

TEST_CASE("a time signature other than 4/4 is noted", "[midi-import]") {
    const std::vector<std::pair<uint8_t, uint8_t>> signatures = {{3, 2}, {6, 3}, {4, 3}, {5, 2}, {2, 1}};
    for (const auto& [numerator, power] : signatures) {
        Bytes track;
        vlq(track, 0);
        put(track, {0xff, 0x58, 0x04, numerator, power, 0x18, 0x08});
        vlq(track, 0);
        put(track, {0x90, 36, 100});
        vlq(track, 240);
        put(track, {0x80, 36, 0});
        endOfTrack(track);
        const auto result = read(file(960, track));
        INFO(int(numerator) << "/" << (1 << power));
        REQUIRE(result.ok());
        CHECK_FALSE(result.clip.onlyFourFour);
    }
    // a second event that goes back to 4/4 does not hide the first
    Bytes track;
    vlq(track, 0);
    put(track, {0xff, 0x58, 0x04, 3, 2, 0x18, 0x08});
    vlq(track, 0);
    put(track, {0xff, 0x58, 0x04, 4, 2, 0x18, 0x08});
    vlq(track, 0);
    put(track, {0x90, 36, 100});
    vlq(track, 240);
    put(track, {0x80, 36, 0});
    endOfTrack(track);
    CHECK_FALSE(read(file(960, track)).clip.onlyFourFour);
}

TEST_CASE("open notes end with their track, empty notes get one tick, equal pitches pair up in order",
          "[midi-import]") {
    Bytes track;
    vlq(track, 0);
    put(track, {0x90, 36, 100}); // never closed
    vlq(track, 0);
    put(track, {0x90, 40, 100}); // on and off at the same tick
    vlq(track, 0);
    put(track, {0x80, 40, 0});
    vlq(track, 240);
    put(track, {0x90, 50, 90}); // two overlapping notes of one pitch
    vlq(track, 240);
    put(track, {0x90, 50, 70});
    vlq(track, 240);
    put(track, {0x80, 50, 0});
    vlq(track, 240);
    put(track, {0x80, 50, 0});
    put(track, {0x00, 0x80, 99, 0}); // a note off without a note on: ignored
    endOfTrack(track, 960);
    const auto result = read(file(960, track));
    REQUIRE(result.ok());
    REQUIRE(result.clip.notes.size() == 4);
    CHECK(result.clip.notes[0].pitch == 36);
    CHECK(result.clip.notes[0].lengthTicks == 240 * 4 + 960); // to the end of the track
    CHECK(result.clip.notes[1].pitch == 40);
    CHECK(result.clip.notes[1].lengthTicks == 1);
    CHECK(result.clip.notes[2].pitch == 50);
    CHECK(result.clip.notes[2].velocity == 90);
    CHECK(result.clip.notes[2].lengthTicks == 480); // the first on pairs with the first off
    CHECK(result.clip.notes[3].velocity == 70);
    CHECK(result.clip.notes[3].lengthTicks == 480);
}

TEST_CASE("files that are not usable are refused with the reason", "[midi-import]") {
    CHECK(read({}).error == MidiReadError::Empty);
    CHECK(read({'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}).error == MidiReadError::NotMidi);
    CHECK(read({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0}).error == MidiReadError::NotMidi); // cut-off header
    CHECK(read(header(2, 1, 480)).error == MidiReadError::Unsupported);
    CHECK(read(header(0, 1, 0xe728)).error == MidiReadError::Unsupported); // SMPTE time code
    CHECK(read(header(0, 1, 0)).error == MidiReadError::Unsupported);
    CHECK(read(header(1, 1000, 480)).error == MidiReadError::TooBig);

    Bytes silent;
    endOfTrack(silent);
    CHECK(read(file(480, silent)).error == MidiReadError::NoNotes);

    Bytes noStatus; // data without a status byte at the start
    vlq(noStatus, 0);
    put(noStatus, {0x40, 0x40});
    CHECK(read(file(480, noStatus)).error == MidiReadError::Broken);

    Bytes unknown;
    vlq(unknown, 0);
    put(unknown, {0xf1, 0x00});
    CHECK(read(file(480, unknown)).error == MidiReadError::Broken);

    Bytes cutMeta;
    vlq(cutMeta, 0);
    put(cutMeta, {0xff, 0x51, 0x03, 0x07});
    CHECK(read(file(480, cutMeta)).error == MidiReadError::Broken);

    Bytes cutMessage;
    vlq(cutMessage, 0);
    put(cutMessage, {0x90, 36});
    CHECK(read(file(480, cutMessage)).error == MidiReadError::Broken);

    Bytes cutDelta;
    put(cutDelta, {0x81});
    CHECK(read(file(480, cutDelta)).error == MidiReadError::Broken);

    Bytes tooLong; // nine text events 268 million ticks apart: far beyond what a clip can be
    for (int i = 0; i < 9; ++i) {
        vlq(tooLong, 0x0fffffff);
        put(tooLong, {0xff, 0x01, 0x00});
    }
    CHECK(read(file(480, tooLong)).error == MidiReadError::TooBig);

    const Bytes huge(kMaxMidiFileBytes + 1, 0);
    CHECK(readMidiFile(huge).error == MidiReadError::TooBig);
    CHECK(readMidiFile(nullptr, 10).error == MidiReadError::Empty);
}

TEST_CASE("a track chunk that is longer than the file is read as far as it goes", "[midi-import]") {
    Bytes track;
    vlq(track, 0);
    put(track, {0x90, 36, 100});
    vlq(track, 480);
    put(track, {0x80, 36, 0});
    Bytes data = file(480, track);
    data[18] = 0x7f; // the chunk claims far more bytes than there are
    const auto result = read(data);
    REQUIRE(result.ok());
    REQUIRE(result.clip.notes.size() == 1);
    CHECK(result.clip.notes[0].lengthTicks == 960);
}

TEST_CASE("broken files never crash the reader", "[midi-import]") {
    const PatternNote notes[] = {{0, 240, 1, 45, 96}, {480, 700, 1, 48, 80}, {3840, 240, 2, 60, 64}};
    const Bytes good = writeMidiFile(PatternView{notes, 3, 2 * kTicksPerBar}, MidiFileOptions{});
    uint32_t state = 7;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    for (int round = 0; round < 4000; ++round) {
        Bytes mutated = good;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits; ++e) {
            mutated[next() % mutated.size()] = static_cast<uint8_t>(next());
        }
        if (round % 5 == 0) {
            mutated.resize(next() % mutated.size());
        }
        const auto result = read(mutated); // only the absence of a crash is checked
        CHECK((result.ok() || result.error != MidiReadError::None));
    }
}

TEST_CASE("the import plan rounds the length up to an allowed pattern length", "[midi-import]") {
    CHECK(roundUpPatternBars(1) == 1);
    CHECK(roundUpPatternBars(2) == 2);
    CHECK(roundUpPatternBars(3) == 4);
    CHECK(roundUpPatternBars(4) == 4);
    CHECK(roundUpPatternBars(5) == 8);
    CHECK(roundUpPatternBars(8) == 8);
    CHECK(roundUpPatternBars(9) == 16);
    CHECK(roundUpPatternBars(16) == 16);
    CHECK(roundUpPatternBars(40) == 16);

    for (const auto& [bars, expected] : std::vector<std::pair<uint32_t, uint32_t>>{{1, 1}, {3, 4}, {5, 8}, {9, 16}}) {
        MidiClip clip;
        clip.notes.push_back({1, 36, 100, 0, 240});
        clip.lengthTicks = bars * kTicksPerBar;
        const auto plan = planImport(clip);
        INFO(bars);
        CHECK(plan.status == ImportStatus::Ok);
        CHECK(plan.lengthBars == expected);
        CHECK(plan.sourceBars == bars);
        CHECK_FALSE(plan.truncated);
    }
    {
        MidiClip exact; // exactly 16 bars fit; 16 bars and a tick are cut
        exact.notes.push_back({1, 36, 100, 0, 240});
        exact.lengthTicks = 16 * kTicksPerBar;
        CHECK_FALSE(planImport(exact).truncated);
        exact.lengthTicks = 16 * kTicksPerBar + 1;
        CHECK(planImport(exact).truncated);
    }
    MidiClip partial; // 3 bars and one tick: 4 bars of material
    partial.notes.push_back({1, 36, 100, 0, 240});
    partial.lengthTicks = 3 * kTicksPerBar + 1;
    CHECK(planImport(partial).sourceBars == 4);
}

TEST_CASE("material beyond 16 bars is cut with a notice, notes are cut at the end", "[midi-import]") {
    MidiClip clip;
    clip.notes.push_back({1, 36, 100, 0, 240});
    clip.notes.push_back({1, 40, 100, 16 * kTicksPerBar - 100, 960}); // reaches over the end
    clip.notes.push_back({1, 43, 100, 16 * kTicksPerBar, 240});       // starts after the last bar
    clip.notes.push_back({1, 45, 100, 20 * kTicksPerBar, 240});
    clip.lengthTicks = 24 * kTicksPerBar;
    const auto plan = planImport(clip);
    REQUIRE(plan.status == ImportStatus::Ok);
    CHECK(plan.truncated);
    CHECK(plan.sourceBars == 24);
    CHECK(plan.lengthBars == 16);
    CHECK(plan.droppedNotes == 2);
    REQUIRE(plan.notes.size() == 2);
    CHECK(plan.notes[1].lengthTicks == 100);
}

TEST_CASE("the import plan keeps positions exactly and refuses other time signatures and empty clips",
          "[midi-import]") {
    MidiClip clip;
    clip.notes.push_back({3, 45, 77, 321, 159}); // triplet-like, not on the grid
    clip.lengthTicks = kTicksPerBar;
    auto plan = planImport(clip);
    REQUIRE(plan.notes.size() == 1);
    CHECK(plan.notes[0] == clip.notes[0]);

    clip.onlyFourFour = false;
    plan = planImport(clip);
    CHECK(plan.status == ImportStatus::NotFourFour);
    CHECK(plan.notes.empty());

    CHECK(planImport(MidiClip{}).status == ImportStatus::NoNotes);
}
