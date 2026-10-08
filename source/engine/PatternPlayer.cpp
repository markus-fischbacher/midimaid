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

/// First multiple of `gridPpq` not before `ppq`, rounded up to a bar line (4/4, SPEC 6.1a rule 3).
double firstSwitchPoint(double ppq, double gridPpq) {
    constexpr double kBarPpq = 4.0;
    constexpr double kEpsilon = 1e-9;
    const double grid = gridPpq > 0.0 ? gridPpq : kBarPpq;
    double point = std::ceil((ppq - kEpsilon) / grid) * grid;
    if (std::fmod(point, kBarPpq) > kEpsilon && kBarPpq - std::fmod(point, kBarPpq) > kEpsilon) {
        point = std::ceil((point - kEpsilon) / kBarPpq) * kBarPpq;
    }
    return point;
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

PatternPlayer::~PatternPlayer() {
    // The audio thread no longer runs (host contract): the patterns in use are freed here.
    dispose(activeOwner_);
    dispose(pending_.owner);
}

void PatternPlayer::dispose(OwnedPattern* pattern) {
    std::unique_ptr<OwnedPattern> owner(pattern);
}

void PatternPlayer::setPattern(PatternView pattern) {
    dispose(activeOwner_);
    activeOwner_ = nullptr;
    pattern_ = pattern;
}

void PatternPlayer::requestSwitch(PatternView next, double gridPpq) {
    dispose(pending_.owner);
    pending_ = {next, gridPpq, 0.0, false, true, false, 0.0, nullptr};
}

void PatternPlayer::requestSwitchAt(PatternView next, double stampPpq, double gridPpq) {
    dispose(pending_.owner);
    pending_ = {next, gridPpq, stampPpq, true, true, false, 0.0, nullptr};
}

bool PatternPlayer::canRetire() const {
    return activeOwner_ == nullptr || (handover_ != nullptr && handover_->freeReturnSlots() >= 1);
}

void PatternPlayer::commitPending() {
    if (activeOwner_ != nullptr) {
        handover_->giveBack(activeOwner_); // canRetire() guaranteed the room
    }
    pattern_ = pending_.pattern;
    activeOwner_ = pending_.owner;
    pending_.owner = nullptr;
    pending_.active = false;
    if (handover_ != nullptr && activeOwner_ != nullptr) {
        handover_->reportActiveVersion(activeOwner_->version);
    }
}

void PatternPlayer::applyEdit(OwnedPattern* edit, MidiEventList& out) {
    const PatternView next = edit->view();
    if (next.lengthTicks != pattern_.lengthTicks) {
        releaseAt(out, 0, false); // positions shift: nothing sounding can be kept
    } else {
        // Notes that are not in the new pattern exactly as they sound now end at once (SPEC 6.3).
        for (size_t channel = 0; channel < active_.size(); ++channel) {
            for (size_t pitch = 0; pitch < active_[channel].size(); ++pitch) {
                auto& note = active_[channel][pitch];
                if (!note.active) {
                    continue;
                }
                bool kept = false;
                for (size_t i = 0; i < next.count && !kept; ++i) {
                    const PatternNote& candidate = next.notes[i];
                    kept = candidate.channel == channel + 1 && candidate.pitch == pitch &&
                           candidate.startTick == note.startTick && candidate.lengthTicks == note.lengthTicks;
                }
                if (!kept) {
                    out.push({0, static_cast<uint8_t>(channel + 1), static_cast<uint8_t>(pitch), 0, false});
                    note.active = false;
                    --activeCount_;
                }
            }
        }
    }
    if (activeOwner_ != nullptr) {
        handover_->giveBack(activeOwner_);
    }
    pattern_ = next;
    activeOwner_ = edit;
    handover_->reportActiveVersion(edit->version);
}

void PatternPlayer::pollHandover(MidiEventList& out) {
    if (handover_ == nullptr) {
        return;
    }
    // A request is only taken when the pattern it displaces can be handed back; otherwise it stays in the mailbox
    // (and may be replaced there) until the message thread has emptied the return queue.
    if (pending_.owner == nullptr || handover_->freeReturnSlots() >= 1) {
        if (OwnedPattern* request = handover_->takeSwitch()) {
            if (pending_.owner != nullptr) {
                handover_->giveBack(pending_.owner);
            }
            pending_ = {request->view(), request->gridPpq, request->stampPpq, request->hasStamp, true, false, 0.0,
                        request};
        }
    }
    if (canRetire()) {
        if (OwnedPattern* edit = handover_->takeEdit()) {
            applyEdit(edit, out);
        }
    }
}

void PatternPlayer::invalidateTransport() {
    playing_ = false;
    pendingWrapRelease_ = false;
    lastNumSamples_ = 0;
}

void PatternPlayer::releaseAll(MidiEventList& out, int sampleOffset) {
    releaseAt(out, sampleOffset, false);
}

void PatternPlayer::releaseAt(MidiEventList& out, int sampleOffset, bool guardOwnStarts) {
    if (activeCount_ == 0) {
        return;
    }
    for (size_t channel = 0; channel < active_.size(); ++channel) {
        for (size_t pitch = 0; pitch < active_[channel].size(); ++pitch) {
            auto& note = active_[channel][pitch];
            if (!note.active) {
                continue;
            }
            int offset = sampleOffset;
            if (guardOwnStarts && note.startedInCall == call_ && note.startOffset >= offset) {
                // A note-off on or before its own note-on would sort before it and the note would hang: end it
                // one sample later, or in the next block's first sample when the block is used up.
                offset = note.startOffset + 1;
                if (offset >= lastNumSamples_) {
                    note.endPpq = 0.0;
                    continue;
                }
            }
            out.push({offset, static_cast<uint8_t>(channel + 1), static_cast<uint8_t>(pitch), 0, false});
            note.active = false;
            --activeCount_;
        }
    }
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
    slot.startTick = note.startTick;
    slot.lengthTicks = note.lengthTicks;
    ++activeCount_;
}

void PatternPlayer::processSegment(double scanFrom, double origin, double to, int sampleBase, int numSamples,
                                   double ppqPerSample, MidiEventList& out) {
    // Hosts report negative positions during a count-in: stay silent there (O-24). A block that crosses zero
    // starts its notes from zero on.
    scanFrom = std::max(scanFrom, 0.0);
    if (pattern_.count > 0 && pattern_.lengthTicks > 0 && to > scanFrom) {
        const double ticksToPpq = 1.0 / kTicksPerQuarter;
        const double lengthPpq = pattern_.lengthTicks * ticksToPpq;
        const auto firstCycle = static_cast<long long>(std::floor(scanFrom / lengthPpq));
        const auto lastCycle = static_cast<long long>(std::floor(to / lengthPpq));
        for (long long cycle = firstCycle; cycle <= lastCycle; ++cycle) {
            for (size_t i = 0; i < pattern_.count; ++i) {
                const PatternNote& note = pattern_.notes[i];
                const double start = static_cast<double>(cycle) * lengthPpq + note.startTick * ticksToPpq;
                if (start >= scanFrom && start < to) {
                    const double end = start + note.lengthTicks * ticksToPpq;
                    startNote(note, end, clampOffset(sampleBase, (start - origin) / ppqPerSample, numSamples), out);
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
            int offset = clampOffset(sampleBase, (note.endPpq - origin) / ppqPerSample, numSamples);
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

void PatternPlayer::playRange(double scanFrom, double origin, double to, int sampleBase, int numSamples,
                              double ppqPerSample, MidiEventList& out) {
    if (pending_.active && pending_.resolved && pending_.pointPpq < to && !canRetire()) {
        // The return queue is full: the old pattern cannot be handed back, so the switch waits for the next grid
        // point (SPEC 6.3). Until then the old pattern keeps playing.
        pending_.pointPpq = firstSwitchPoint(std::max(pending_.pointPpq, scanFrom) + 1e-6, pending_.gridPpq);
    }
    if (pending_.active && pending_.resolved && pending_.pointPpq < to) {
        const double point = std::max(pending_.pointPpq, scanFrom);
        processSegment(scanFrom, origin, point, sampleBase, numSamples, ppqPerSample, out);
        releaseAt(out, clampOffset(sampleBase, (point - origin) / ppqPerSample, numSamples), true);
        commitPending();
        processSegment(point, origin, to, sampleBase, numSamples, ppqPerSample, out);
    } else {
        processSegment(scanFrom, origin, to, sampleBase, numSamples, ppqPerSample, out);
    }
}

void PatternPlayer::process(const TransportInfo& transport, int numSamples, double sampleRate, MidiEventList& out) {
    out.clear();
    ++call_;
    pollHandover(out);

    if (!transport.hasPosition || !transport.isPlaying || transport.bpm <= 0.0 || sampleRate <= 0.0 ||
        numSamples <= 0) {
        releaseAll(out, 0);
        playing_ = false;
        pendingWrapRelease_ = false;
        return;
    }

    const double ppqPerSample = transport.bpm / 60.0 / sampleRate;
    // The position may deviate by one sample. After a tempo change the previous block advanced at a rate between
    // the old and the new tempo, so it deviates by at most its length times the rate change as well.
    const double tolerance = ppqPerSample + 1e-6 + lastNumSamples_ * std::abs(ppqPerSample - lastPpqPerSample_);

    // Without a jump the scan continues exactly where the previous block ended: no gap (swallowed note-on) and
    // no overlap (double note-on), whatever the host reports within the tolerance.
    double scanFrom = transport.ppq;
    bool restart = false; // start or jump: a pending switch applies at once (SPEC 6.1a, T1 and T4)
    if (!playing_) {
        releaseAll(out, 0);
        playing_ = true;
        restart = true;
    } else if (pendingWrapRelease_ || std::abs(transport.ppq - expectedPpq_) > tolerance) {
        releaseAll(out, 0); // jump
        restart = true;
    } else {
        scanFrom = expectedPpq_;
    }
    pendingWrapRelease_ = false;
    lastPpqPerSample_ = ppqPerSample;
    lastNumSamples_ = numSamples;

    if (pending_.active) {
        if (restart && canRetire()) {
            commitPending();
        } else if (!pending_.resolved) {
            // Fixed once, in the block that first sees the request (rule 2): never in the past.
            const double point = pending_.hasStamp && pending_.stampPpq >= transport.ppq - 1e-9
                                     ? pending_.stampPpq
                                     : firstSwitchPoint(transport.ppq, pending_.gridPpq);
            pending_.pointPpq = point;
            pending_.resolved = true;
        }
    }

    const double blockEnd = transport.ppq + numSamples * ppqPerSample;
    const bool loops = transport.isLooping && transport.loopEndPpq > transport.loopStartPpq;
    constexpr double kEpsilon = 1e-9;

    if (loops && transport.ppq < transport.loopEndPpq && blockEnd > transport.loopEndPpq + kEpsilon) {
        // The loop wraps inside this block: handle both halves sample-accurately.
        const int wrapOffset = clampOffset(0, (transport.loopEndPpq - transport.ppq) / ppqPerSample, numSamples);
        playRange(scanFrom, transport.ppq, transport.loopEndPpq, 0, numSamples, ppqPerSample, out);
        releaseAt(out, wrapOffset, true);
        if (pending_.active) {
            // The loop wrap is a jump: a switch whose point was not reached before it applies here (T4).
            if (canRetire()) {
                commitPending();
            } else {
                pending_.resolved = false; // queue full: choose a new point in the next block
            }
        }
        const double secondEnd = transport.loopStartPpq + (blockEnd - transport.loopEndPpq);
        processSegment(transport.loopStartPpq, transport.loopStartPpq, secondEnd, wrapOffset, numSamples, ppqPerSample,
                       out);
        expectedPpq_ = secondEnd;
    } else {
        playRange(scanFrom, transport.ppq, blockEnd, 0, numSamples, ppqPerSample, out);
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
