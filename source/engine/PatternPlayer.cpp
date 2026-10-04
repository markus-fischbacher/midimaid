#include "engine/PatternPlayer.h"

#include <algorithm>
#include <cmath>

namespace mm::engine {

namespace {

/// Sample offset within the block: `base` plus the rounded distance, kept inside [0, numSamples - 1].
int clampOffset(int base, double samples, int numSamples) {
    const auto offset = static_cast<long long>(base) + static_cast<long long>(std::llround(samples));
    return static_cast<int>(std::clamp<long long>(offset, 0, numSamples > 0 ? numSamples - 1 : 0));
}

} // namespace

bool MidiEventList::push(const MidiEvent& event) {
    if (full()) {
        return false;
    }
    events_[size_++] = event;
    return true;
}

void MidiEventList::sort() {
    // Insertion sort: stable and allocation-free (std::stable_sort may allocate).
    for (size_t i = 1; i < size_; ++i) {
        const MidiEvent current = events_[i];
        size_t j = i;
        while (j > 0) {
            const MidiEvent& previous = events_[j - 1];
            const bool later = previous.sampleOffset > current.sampleOffset ||
                               (previous.sampleOffset == current.sampleOffset && previous.noteOn && !current.noteOn);
            if (!later) {
                break;
            }
            events_[j] = previous;
            --j;
        }
        events_[j] = current;
    }
}

PatternPlayer::PatternPlayer(PatternView pattern) : pattern_(pattern) {}

void PatternPlayer::setPattern(PatternView pattern) {
    pattern_ = pattern;
}

void PatternPlayer::invalidateTransport() {
    playing_ = false;
    pendingWrapRelease_ = false;
}

void PatternPlayer::releaseAll(MidiEventList& out, int sampleOffset) {
    if (activeCount_ == 0) {
        return;
    }
    for (size_t channel = 0; channel < active_.size(); ++channel) {
        for (size_t pitch = 0; pitch < active_[channel].size(); ++pitch) {
            auto& note = active_[channel][pitch];
            if (note.active) {
                out.push({sampleOffset, static_cast<uint8_t>(channel + 1), static_cast<uint8_t>(pitch), 0, false});
                note.active = false;
            }
        }
    }
    activeCount_ = 0;
}

void PatternPlayer::startNote(const PatternNote& note, double endPpq, int offset, MidiEventList& out) {
    auto& slot = active_[static_cast<size_t>(note.channel - 1)][note.pitch];
    if (slot.active) {
        // Same pitch still sounding: end it first so the new note-on is never swallowed.
        out.push({offset, note.channel, note.pitch, 0, false});
        slot.active = false;
        --activeCount_;
    }
    // Note-offs have priority: accept a note-on only if every sounding note (and this one) can still be ended.
    if (out.size() + activeCount_ + 2 > MidiEventList::kCapacity) {
        return;
    }
    out.push({offset, note.channel, note.pitch, note.velocity, true});
    slot.active = true;
    slot.endPpq = endPpq;
    slot.startedInCall = call_;
    slot.startOffset = offset;
    ++activeCount_;
}

void PatternPlayer::processSegment(double from, double to, int sampleBase, int numSamples, double ppqPerSample,
                                   MidiEventList& out) {
    if (pattern_.count > 0 && pattern_.lengthTicks > 0 && to > from) {
        const double ticksToPpq = 1.0 / kTicksPerQuarter;
        const double lengthPpq = pattern_.lengthTicks * ticksToPpq;
        const auto firstCycle = static_cast<long long>(std::floor(from / lengthPpq));
        const auto lastCycle = static_cast<long long>(std::floor(to / lengthPpq));
        for (long long cycle = firstCycle; cycle <= lastCycle; ++cycle) {
            for (size_t i = 0; i < pattern_.count; ++i) {
                const PatternNote& note = pattern_.notes[i];
                const double start = static_cast<double>(cycle) * lengthPpq + note.startTick * ticksToPpq;
                if (start >= from && start < to) {
                    const double end = start + note.lengthTicks * ticksToPpq;
                    startNote(note, end, clampOffset(sampleBase, (start - from) / ppqPerSample, numSamples), out);
                }
            }
        }
    }

    for (size_t channel = 0; channel < active_.size(); ++channel) {
        for (size_t pitch = 0; pitch < active_[channel].size(); ++pitch) {
            auto& note = active_[channel][pitch];
            if (!note.active || note.endPpq > to) {
                continue;
            }
            int offset = clampOffset(sampleBase, (note.endPpq - from) / ppqPerSample, numSamples);
            if (note.startedInCall == call_) {
                // Never let the note-off land on or before its own note-on.
                offset = std::max(offset, note.startOffset + 1);
                if (offset >= numSamples) {
                    continue; // ends in the next block
                }
            }
            out.push({offset, static_cast<uint8_t>(channel + 1), static_cast<uint8_t>(pitch), 0, false});
            note.active = false;
            --activeCount_;
        }
    }
}

void PatternPlayer::process(const TransportInfo& transport, int numSamples, double sampleRate, MidiEventList& out) {
    out.clear();
    ++call_;

    if (!transport.hasPosition || !transport.isPlaying || transport.bpm <= 0.0 || sampleRate <= 0.0 ||
        numSamples <= 0) {
        releaseAll(out, 0);
        playing_ = false;
        pendingWrapRelease_ = false;
        return;
    }

    const double ppqPerSample = transport.bpm / 60.0 / sampleRate;
    const double tolerance = ppqPerSample + 1e-6;

    if (!playing_) {
        releaseAll(out, 0);
        playing_ = true;
    } else if (pendingWrapRelease_ || std::abs(transport.ppq - expectedPpq_) > tolerance) {
        releaseAll(out, 0); // jump
    }
    pendingWrapRelease_ = false;

    const double blockEnd = transport.ppq + numSamples * ppqPerSample;
    const bool loops = transport.isLooping && transport.loopEndPpq > transport.loopStartPpq;
    constexpr double kEpsilon = 1e-9;

    if (loops && transport.ppq < transport.loopEndPpq && blockEnd > transport.loopEndPpq + kEpsilon) {
        // The loop wraps inside this block: handle both halves sample-accurately.
        const int wrapOffset = clampOffset(0, (transport.loopEndPpq - transport.ppq) / ppqPerSample, numSamples);
        processSegment(transport.ppq, transport.loopEndPpq, 0, numSamples, ppqPerSample, out);
        releaseAll(out, wrapOffset);
        const double secondEnd = transport.loopStartPpq + (blockEnd - transport.loopEndPpq);
        processSegment(transport.loopStartPpq, secondEnd, wrapOffset, numSamples, ppqPerSample, out);
        expectedPpq_ = secondEnd;
    } else {
        processSegment(transport.ppq, blockEnd, 0, numSamples, ppqPerSample, out);
        expectedPpq_ = blockEnd;
        if (loops && blockEnd >= transport.loopEndPpq - kEpsilon && transport.ppq < transport.loopEndPpq) {
            // The block ends exactly on the loop end: the wrap happens at the next block's first sample.
            expectedPpq_ = transport.loopStartPpq;
            pendingWrapRelease_ = true;
        }
    }

    out.sort();
}

} // namespace mm::engine
