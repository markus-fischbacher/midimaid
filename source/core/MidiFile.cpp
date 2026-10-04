#include "core/MidiFile.h"

#include <algorithm>
#include <cmath>

namespace mm::core {

namespace {

void putBytes(std::vector<uint8_t>& out, std::initializer_list<uint8_t> bytes) {
    out.insert(out.end(), bytes);
}

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    putBytes(out, {static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                   static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)});
}

/// Variable-length quantity (delta times, meta event lengths).
void putVlq(std::vector<uint8_t>& out, uint32_t value) {
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

struct TimedEvent {
    uint32_t tick;
    bool noteOn;
    uint8_t channel;
    uint8_t pitch;
    uint8_t velocity;
};

uint8_t log2Exact(uint8_t value) {
    uint8_t power = 0;
    while ((1u << (power + 1)) <= value) {
        ++power;
    }
    return power;
}

} // namespace

std::vector<uint8_t> writeMidiFile(const PatternView& pattern, const MidiFileOptions& options) {
    // Note-offs sort before note-ons at the same tick, so a repeated pitch is never swallowed.
    std::vector<TimedEvent> events;
    events.reserve(pattern.count * 2);
    uint32_t endTick = pattern.lengthTicks;
    for (size_t i = 0; i < pattern.count; ++i) {
        const PatternNote& note = pattern.notes[i];
        const uint8_t channel = static_cast<uint8_t>((note.channel - 1) & 0x0f);
        events.push_back({note.startTick, true, channel, note.pitch, note.velocity});
        events.push_back({note.startTick + note.lengthTicks, false, channel, note.pitch, 0});
        endTick = std::max(endTick, note.startTick + note.lengthTicks);
    }
    std::stable_sort(events.begin(), events.end(), [](const TimedEvent& a, const TimedEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : (!a.noteOn && b.noteOn);
    });

    std::vector<uint8_t> track;
    // Track name
    putVlq(track, 0);
    putBytes(track, {0xff, 0x03});
    putVlq(track, static_cast<uint32_t>(options.trackName.size()));
    track.insert(track.end(), options.trackName.begin(), options.trackName.end());
    // Tempo (microseconds per quarter note)
    const double bpm = options.bpm > 0.0 ? options.bpm : 120.0;
    const auto microseconds = static_cast<uint32_t>(std::llround(60'000'000.0 / bpm)) & 0x00ffffffu;
    putVlq(track, 0);
    putBytes(track, {0xff, 0x51, 0x03, static_cast<uint8_t>(microseconds >> 16),
                     static_cast<uint8_t>(microseconds >> 8), static_cast<uint8_t>(microseconds)});
    // Time signature
    putVlq(track, 0);
    putBytes(track, {0xff, 0x58, 0x04, options.timeSigNumerator, log2Exact(options.timeSigDenominator), 24, 8});

    uint32_t previousTick = 0;
    for (const TimedEvent& event : events) {
        putVlq(track, event.tick - previousTick);
        previousTick = event.tick;
        putBytes(track,
                 {static_cast<uint8_t>((event.noteOn ? 0x90 : 0x80) | event.channel), event.pitch, event.velocity});
    }
    // End of track at the pattern end
    putVlq(track, endTick - previousTick);
    putBytes(track, {0xff, 0x2f, 0x00});

    std::vector<uint8_t> file;
    file.reserve(22 + track.size());
    putBytes(file, {'M', 'T', 'h', 'd'});
    putU32(file, 6);
    putBytes(file, {0x00, 0x00, 0x00, 0x01}); // format 0, one track
    putBytes(file, {static_cast<uint8_t>(kTicksPerQuarter >> 8), static_cast<uint8_t>(kTicksPerQuarter)});
    putBytes(file, {'M', 'T', 'r', 'k'});
    putU32(file, static_cast<uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

} // namespace mm::core
