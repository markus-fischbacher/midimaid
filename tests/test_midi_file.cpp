#include "core/MidiFile.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace mm::core;

namespace {

struct ParsedEvent {
    uint32_t tick = 0;
    uint8_t status = 0; // 0x80/0x90 (channel stripped) or 0xff for meta events
    uint8_t channel = 0;
    uint8_t data1 = 0;            // pitch or meta type
    uint8_t data2 = 0;            // velocity
    std::vector<uint8_t> payload; // meta payload
};

struct ParsedFile {
    uint16_t format = 0, tracks = 0, division = 0;
    uint32_t trackLength = 0;
    size_t trackBytes = 0;
    std::vector<ParsedEvent> events;
};

uint32_t readVlq(const std::vector<uint8_t>& d, size_t& pos) {
    uint32_t value = 0;
    uint8_t byte;
    do {
        byte = d.at(pos++);
        value = (value << 7) | (byte & 0x7f);
    } while (byte & 0x80);
    return value;
}

uint32_t readU32(const std::vector<uint8_t>& d, size_t pos) {
    return (uint32_t(d.at(pos)) << 24) | (uint32_t(d.at(pos + 1)) << 16) | (uint32_t(d.at(pos + 2)) << 8) |
           d.at(pos + 3);
}

ParsedFile parse(const std::vector<uint8_t>& d) {
    ParsedFile f;
    REQUIRE(d.size() > 22);
    REQUIRE(std::string(d.begin(), d.begin() + 4) == "MThd");
    REQUIRE(readU32(d, 4) == 6);
    f.format = static_cast<uint16_t>((d[8] << 8) | d[9]);
    f.tracks = static_cast<uint16_t>((d[10] << 8) | d[11]);
    f.division = static_cast<uint16_t>((d[12] << 8) | d[13]);
    REQUIRE(std::string(d.begin() + 14, d.begin() + 18) == "MTrk");
    f.trackLength = readU32(d, 18);
    f.trackBytes = d.size() - 22;
    size_t pos = 22;
    uint32_t tick = 0;
    while (pos < d.size()) {
        tick += readVlq(d, pos);
        ParsedEvent e;
        e.tick = tick;
        const uint8_t status = d.at(pos++);
        if (status == 0xff) {
            e.status = 0xff;
            e.data1 = d.at(pos++);
            const uint32_t length = readVlq(d, pos);
            e.payload.assign(d.begin() + static_cast<long>(pos), d.begin() + static_cast<long>(pos + length));
            pos += length;
        } else {
            e.status = status & 0xf0;
            e.channel = status & 0x0f;
            e.data1 = d.at(pos++);
            e.data2 = d.at(pos++);
        }
        f.events.push_back(e);
    }
    return f;
}

std::vector<ParsedEvent> notes(const ParsedFile& f) {
    std::vector<ParsedEvent> result;
    for (const auto& e : f.events) {
        if (e.status == 0x80 || e.status == 0x90) {
            result.push_back(e);
        }
    }
    return result;
}

} // namespace

TEST_CASE("midi file: header and chunk length", "[core][midi]") {
    const auto bytes = writeMidiFile(placeholderPattern(), {120.0, 4, 4, "Bass"});
    const auto file = parse(bytes);
    CHECK(file.format == 0);
    CHECK(file.tracks == 1);
    CHECK(file.division == 960);
    CHECK(file.trackLength == file.trackBytes);
}

TEST_CASE("midi file: track name, tempo and time signature", "[core][midi]") {
    const auto file = parse(writeMidiFile(placeholderPattern(), {120.0, 4, 4, "MidiMaid Bass"}));
    REQUIRE(file.events.size() >= 3);

    CHECK(file.events[0].status == 0xff);
    CHECK(file.events[0].data1 == 0x03);
    CHECK(std::string(file.events[0].payload.begin(), file.events[0].payload.end()) == "MidiMaid Bass");

    CHECK(file.events[1].data1 == 0x51);
    CHECK(file.events[1].payload == std::vector<uint8_t>{0x07, 0xa1, 0x20}); // 500000 us per quarter = 120 BPM

    CHECK(file.events[2].data1 == 0x58);
    CHECK(file.events[2].payload == std::vector<uint8_t>{4, 2, 24, 8}); // 4/4
}

TEST_CASE("midi file: tempo follows the DAW tempo", "[core][midi]") {
    auto tempoOf = [](double bpm) {
        const auto file = parse(writeMidiFile(placeholderPattern(), {bpm, 4, 4, ""}));
        const auto& p = file.events[1].payload;
        return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    };
    CHECK(tempoOf(60.0) == 1'000'000);
    CHECK(tempoOf(128.0) == 468'750);
    CHECK(tempoOf(0.0) == 500'000); // invalid tempo falls back to 120 BPM
}

TEST_CASE("midi file: notes of the placeholder pattern", "[core][midi]") {
    const auto file = parse(writeMidiFile(placeholderPattern(), {120.0, 4, 4, ""}));
    const auto n = notes(file);
    REQUIRE(n.size() == 8);
    const uint32_t starts[] = {480, 1440, 2400, 3360};
    for (size_t i = 0; i < 4; ++i) {
        CHECK(n[2 * i].status == 0x90);
        CHECK(n[2 * i].tick == starts[i]);
        CHECK(n[2 * i].data1 == 45);
        CHECK(n[2 * i].channel == 0);
        CHECK(n[2 * i + 1].status == 0x80);
        CHECK(n[2 * i + 1].tick == starts[i] + 360);
    }
    CHECK(n[0].data2 == 100);
    CHECK(n[6].data2 == 124); // accent
}

TEST_CASE("midi file: end of track sits at the pattern end", "[core][midi]") {
    const auto file = parse(writeMidiFile(placeholderPattern(), {120.0, 4, 4, ""}));
    const auto& last = file.events.back();
    CHECK(last.status == 0xff);
    CHECK(last.data1 == 0x2f);
    CHECK(last.tick == 3840);
}

TEST_CASE("midi file: note-off sorts before note-on at the same tick", "[core][midi]") {
    const PatternNote pattern[] = {{0, 480, 1, 40, 100}, {480, 480, 1, 40, 100}};
    const auto n = notes(parse(writeMidiFile({pattern, 2, 3840}, {120.0, 4, 4, ""})));
    REQUIRE(n.size() == 4);
    CHECK(n[1].status == 0x80);
    CHECK(n[1].tick == 480);
    CHECK(n[2].status == 0x90);
    CHECK(n[2].tick == 480);
}

TEST_CASE("midi file: large delta times and channels", "[core][midi]") {
    const PatternNote pattern[] = {{200'000, 1000, 10, 60, 90}};
    const auto file = parse(writeMidiFile({pattern, 1, 300'000}, {120.0, 4, 4, ""}));
    const auto n = notes(file);
    REQUIRE(n.size() == 2);
    CHECK(n[0].tick == 200'000);
    CHECK(n[0].channel == 9);
    CHECK(file.events.back().tick == 300'000);
}

TEST_CASE("midi file: an empty pattern still has a valid track", "[core][midi]") {
    const auto file = parse(writeMidiFile({nullptr, 0, 3840}, {120.0, 4, 4, ""}));
    CHECK(notes(file).empty());
    CHECK(file.events.back().tick == 3840);
}

TEST_CASE("midi file: pattern end is extended by notes that run past it", "[core][midi]") {
    const PatternNote pattern[] = {{3800, 200, 1, 40, 100}};
    const auto file = parse(writeMidiFile({pattern, 1, 3840}, {120.0, 4, 4, ""}));
    CHECK(file.events.back().tick == 4000);
}
