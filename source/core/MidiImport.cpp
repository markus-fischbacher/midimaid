#include "core/MidiImport.h"

#include "core/Pattern.h"
#include "core/PlaybackPattern.h"

#include <algorithm>
#include <deque>
#include <map>

namespace mm::core {

namespace {

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t position = 0;

    bool has(size_t count) const { return size - position >= count; }
    uint8_t u8() { return data[position++]; }
    uint16_t u16() {
        const uint16_t value = static_cast<uint16_t>((data[position] << 8) | data[position + 1]);
        position += 2;
        return value;
    }
    uint32_t u32() {
        const uint32_t value = (static_cast<uint32_t>(data[position]) << 24) |
                               (static_cast<uint32_t>(data[position + 1]) << 16) |
                               (static_cast<uint32_t>(data[position + 2]) << 8) | data[position + 3];
        position += 4;
        return value;
    }
    /// A variable-length quantity of at most 4 bytes; false when it is cut off or longer.
    bool vlq(uint32_t& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            if (!has(1)) {
                return false;
            }
            const uint8_t byte = u8();
            value = (value << 7) | (byte & 0x7f);
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }
};

struct OpenNote {
    uint64_t start;
    uint8_t velocity;
};

MidiReadResult fail(MidiReadError error, std::string message) {
    MidiReadResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

uint32_t toTicks960(uint64_t tick, uint16_t ppq) {
    return static_cast<uint32_t>((tick * kTicksPerQuarter + ppq / 2) / ppq);
}

} // namespace

MidiReadResult readMidiFile(const std::vector<uint8_t>& bytes) {
    return readMidiFile(bytes.data(), bytes.size());
}

MidiReadResult readMidiFile(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return fail(MidiReadError::Empty, "no data");
    }
    if (size > kMaxMidiFileBytes) {
        return fail(MidiReadError::TooBig, "the file is too big");
    }
    Reader in{data, size};
    if (!in.has(14) || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd') {
        return fail(MidiReadError::NotMidi, "no MThd header");
    }
    in.position = 4;
    const uint32_t headerLength = in.u32();
    if (headerLength < 6 || !in.has(headerLength)) {
        return fail(MidiReadError::NotMidi, "the header is cut off");
    }
    const uint16_t format = in.u16();
    const uint16_t trackCount = in.u16();
    const uint16_t division = in.u16();
    in.position = 8 + headerLength;
    if (format > 1) {
        return fail(MidiReadError::Unsupported, "only format 0 and 1");
    }
    if ((division & 0x8000) != 0 || division == 0) {
        return fail(MidiReadError::Unsupported, "no tick division in quarter notes");
    }
    if (trackCount > kMaxMidiTracks) {
        return fail(MidiReadError::TooBig, "too many tracks");
    }

    MidiReadResult result;
    MidiClip& clip = result.clip;
    clip.sourcePpq = division;
    uint64_t clipEnd = 0;
    bool anyTempo = false;

    for (size_t track = 0; track < trackCount && in.has(8); ++track) {
        const bool isTrack = data[in.position] == 'M' && data[in.position + 1] == 'T' && data[in.position + 2] == 'r' &&
                             data[in.position + 3] == 'k';
        in.position += 4;
        uint32_t length = in.u32();
        if (!isTrack) { // another chunk type: skip it
            in.position = std::min(size, in.position + length);
            --track;
            continue;
        }
        length = static_cast<uint32_t>(std::min<size_t>(length, size - in.position)); // tolerate a cut-off chunk
        Reader events{data + in.position, length};
        in.position += length;

        uint64_t tick = 0;
        uint8_t status = 0;
        uint64_t trackEnd = 0;
        std::map<std::pair<uint8_t, uint8_t>, std::deque<OpenNote>> open;
        const auto close = [&](uint8_t channel, uint8_t pitch, uint64_t end) {
            auto& queue = open[{channel, pitch}];
            if (queue.empty()) {
                return;
            }
            const OpenNote note = queue.front();
            queue.pop_front();
            MidiClipNote out;
            out.channel = static_cast<uint8_t>(channel + 1);
            out.pitch = pitch;
            out.velocity = note.velocity;
            out.startTick = toTicks960(note.start, division);
            const uint32_t endTick = toTicks960(end, division); // never before the start: the rounding is monotonic
            out.lengthTicks = std::max<uint32_t>(1, endTick - out.startTick);
            clip.notes.push_back(out);
        };

        while (events.has(1)) {
            uint32_t delta = 0;
            if (!events.vlq(delta)) {
                return fail(MidiReadError::Broken, "a delta time is cut off");
            }
            tick += delta;
            if (tick > kMaxMidiTicks) {
                return fail(MidiReadError::TooBig, "the clip is far too long");
            }
            if (!events.has(1)) {
                return fail(MidiReadError::Broken, "an event is missing");
            }
            uint8_t first = events.u8();
            if (first < 0x80) { // running status
                if (status == 0) {
                    return fail(MidiReadError::Broken, "data without a status byte");
                }
                --events.position;
                first = status;
            } else if (first < 0xf0) {
                status = first;
            }
            if (first >= 0x80 && first < 0xf0) {
                const uint8_t kind = first & 0xf0;
                const uint8_t channel = first & 0x0f;
                const size_t dataBytes = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
                if (!events.has(dataBytes)) {
                    return fail(MidiReadError::Broken, "a channel message is cut off");
                }
                const uint8_t d1 = events.u8();
                const uint8_t d2 = dataBytes == 2 ? events.u8() : 0;
                if (kind == 0x90 && d2 > 0) {
                    open[{channel, d1}].push_back({tick, d2});
                    if (clip.notes.size() + 1 > kMaxMidiNotes) {
                        return fail(MidiReadError::TooBig, "too many notes");
                    }
                } else if (kind == 0x80 || kind == 0x90) {
                    close(channel, d1, tick);
                }
            } else if (first == 0xff) {
                if (!events.has(1)) {
                    return fail(MidiReadError::Broken, "a meta event is cut off");
                }
                const uint8_t type = events.u8();
                uint32_t metaLength = 0;
                if (!events.vlq(metaLength) || !events.has(metaLength)) {
                    return fail(MidiReadError::Broken, "a meta event is cut off");
                }
                const uint8_t* body = events.data + events.position;
                if (type == 0x58 && metaLength >= 2) {
                    const uint32_t denominator = body[1] < 16 ? (1u << body[1]) : 0;
                    if (!(body[0] == 4 && denominator == 4)) {
                        clip.onlyFourFour = false;
                    }
                } else if (type == 0x51 && metaLength >= 3 && !anyTempo) {
                    const uint32_t micros = (static_cast<uint32_t>(body[0]) << 16) | (body[1] << 8) | body[2];
                    if (micros > 0) {
                        clip.bpm = 60000000.0 / micros;
                        anyTempo = true;
                    }
                }
                events.position += metaLength;
                if (type == 0x2f) {
                    break;
                }
            } else if (first == 0xf0 || first == 0xf7) {
                uint32_t sysexLength = 0;
                if (!events.vlq(sysexLength) || !events.has(sysexLength)) {
                    return fail(MidiReadError::Broken, "a system exclusive message is cut off");
                }
                events.position += sysexLength;
            } else {
                return fail(MidiReadError::Broken, "an unknown event");
            }
        }
        trackEnd = std::max(trackEnd, tick);
        for (auto& [key, queue] : open) { // a note without note off ends with its track
            while (!queue.empty()) {
                close(key.first, key.second, trackEnd);
            }
        }
        clipEnd = std::max(clipEnd, static_cast<uint64_t>(toTicks960(trackEnd, division)));
    }

    if (clip.notes.empty()) {
        result.error = MidiReadError::NoNotes;
        result.message = "the file has no notes";
        return result;
    }
    std::stable_sort(clip.notes.begin(), clip.notes.end(), [](const MidiClipNote& a, const MidiClipNote& b) {
        return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
    });
    uint64_t lastEnd = 0;
    for (const auto& note : clip.notes) {
        lastEnd = std::max<uint64_t>(lastEnd, static_cast<uint64_t>(note.startTick) + note.lengthTicks);
    }
    clip.lengthTicks = static_cast<uint32_t>(std::max(clipEnd, lastEnd));
    return result;
}

uint32_t roundUpPatternBars(uint32_t bars) {
    for (const uint32_t allowed : {1u, 2u, 4u, 8u, 16u}) {
        if (bars <= allowed) {
            return allowed;
        }
    }
    return kMaxImportBars;
}

ImportPlan planImport(const MidiClip& clip) {
    ImportPlan plan;
    if (clip.notes.empty()) {
        plan.status = ImportStatus::NoNotes;
        return plan;
    }
    if (!clip.onlyFourFour) {
        plan.status = ImportStatus::NotFourFour;
        return plan;
    }
    plan.status = ImportStatus::Ok;
    plan.sourceBars = std::max<uint32_t>(1, (clip.lengthTicks + kTicksPerBar - 1) / kTicksPerBar);
    plan.truncated = plan.sourceBars > kMaxImportBars;
    plan.lengthBars = roundUpPatternBars(plan.sourceBars);
    const uint32_t end = plan.lengthBars * kTicksPerBar;
    for (MidiClipNote note : clip.notes) {
        if (note.startTick >= end) {
            ++plan.droppedNotes;
            continue;
        }
        note.lengthTicks = std::min(note.lengthTicks, end - note.startTick);
        plan.notes.push_back(note);
    }
    return plan;
}

} // namespace mm::core
