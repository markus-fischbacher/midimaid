#include "engine/PatternPlayer.h"

#include "core/Probability.h"

#include <algorithm>
#include <cmath>

namespace mm::engine {

namespace {

/// Sample offset within the block: `base` plus the rounded distance, kept inside [0, numSamples - 1].
int clampOffset(int base, double samples, int numSamples) {
    const auto offset = static_cast<long long>(base) + static_cast<long long>(std::llround(samples));
    return static_cast<int>(std::clamp<long long>(offset, 0, numSamples > 0 ? numSamples - 1 : 0));
}

/// First multiple of `gridPpq` not before `ppq`, rounded up to a bar line (4/4, SPEC 6.1a rule 3)
/// unless `offBar` allows a point inside the bar (the beat quantization, D-247).
double firstSwitchPoint(double ppq, double gridPpq, bool offBar) {
    constexpr double kBarPpq = 4.0;
    constexpr double kEpsilon = 1e-9;
    const double grid = gridPpq > 0.0 ? gridPpq : kBarPpq;
    double point = std::ceil((ppq - kEpsilon) / grid) * grid;
    if (!offBar && std::fmod(point, kBarPpq) > kEpsilon && kBarPpq - std::fmod(point, kBarPpq) > kEpsilon) {
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

PatternPlayer::PatternPlayer(PatternView pattern) : pattern_(pattern) {
    slots_[0].view = pattern;
}

void PatternPlayer::setTransposeMode(core::TransposeMode mode) {
    if (mode != transposeMode_) {
        transposeMode_ = mode;
        refreshTranspose();
    }
}

void PatternPlayer::setTranspose(int offset) {
    const int clamped = core::clampTranspose(offset);
    pendingTranspose_.has = false;
    if (clamped != transposeOffset_) {
        transposeOffset_ = clamped;
        refreshTranspose();
    }
}

void PatternPlayer::setTransposeAt(int offset, double atPpq) {
    pendingTranspose_ = {true, core::clampTranspose(offset), atPpq};
}

void PatternPlayer::refreshTranspose() {
    voiceShift_ = core::voiceOctaveShift(transposeOffset_, transposeMode_, pattern_.transpose);
}

int PatternPlayer::mapPitch(int pitch) const {
    return core::transposedPitch(pitch, transposeOffset_, transposeMode_, pattern_.transpose, voiceShift_);
}

PatternPlayer::~PatternPlayer() {
    // The audio thread no longer runs (host contract): the patterns in use are freed here.
    for (auto& entry : slots_) {
        dispose(entry.owner);
    }
    dispose(detachedOwner_);
}

void PatternPlayer::dispose(OwnedPattern* pattern) {
    std::unique_ptr<OwnedPattern> owner(pattern);
}

void PatternPlayer::retire(OwnedPattern* pattern) {
    if (pattern == nullptr) {
        return;
    }
    if (handover_ != nullptr) {
        handover_->giveBack(pattern); // the callers checked that there is room
    } else {
        dispose(pattern);
    }
}

void PatternPlayer::setSlot(int slot) {
    selected_ = std::clamp(slot, 1, static_cast<int>(kSlotCount)) - 1;
    if (switchPending() && !timing_.resolved) {
        timing_.gridPpq = slotGridPpq_;
        timing_.hasStamp = false;
    }
}

void PatternPlayer::setSlotAt(int slot, double stampPpq) {
    setSlot(slot);
    if (switchPending()) {
        timing_.stampPpq = stampPpq;
        timing_.hasStamp = true;
        timing_.resolved = false;
    }
}

double PatternPlayer::positionTolerance(double ppqPerSample) const {
    // The position may deviate by one sample. After a tempo change the previous block advanced at a rate between
    // the old and the new tempo, so it deviates by at most its length times the rate change as well.
    return ppqPerSample + 1e-6 + lastNumSamples_ * std::abs(ppqPerSample - lastPpqPerSample_);
}

bool PatternPlayer::startsOrJumps(const TransportInfo& transport, int numSamples, double sampleRate) const {
    if (!transport.hasPosition || !transport.isPlaying || transport.bpm <= 0.0 || sampleRate <= 0.0 ||
        numSamples <= 0) {
        return false;
    }
    if (!playing_ || pendingWrapRelease_) {
        return true;
    }
    const double ppqPerSample = transport.bpm / 60.0 / sampleRate;
    return std::abs(transport.ppq - expectedPpq_) > positionTolerance(ppqPerSample);
}

void PatternPlayer::setPattern(PatternView pattern) {
    retire(slots_[static_cast<size_t>(selected_)].owner);
    slots_[static_cast<size_t>(selected_)] = {pattern, nullptr};
    retire(detachedOwner_);
    detachedOwner_ = nullptr;
    pattern_ = pattern;
    refreshTranspose();
    playingSlot_ = selected_;
    timing_.resolved = false;
}

void PatternPlayer::requestSwitch(PatternView next, double gridPpq) {
    const auto slot = static_cast<size_t>(selected_);
    if (playingSlot_ == selected_) {
        detachedOwner_ = slots_[slot].owner;
        playingSlot_ = -1;
    } else {
        retire(slots_[slot].owner);
    }
    slots_[slot] = {next, nullptr};
    timing_.gridPpq = gridPpq;
    timing_.hasStamp = false;
}

void PatternPlayer::requestSwitchAt(PatternView next, double stampPpq, double gridPpq) {
    requestSwitch(next, gridPpq);
    timing_.stampPpq = stampPpq;
    timing_.hasStamp = true;
    timing_.resolved = false;
}

bool PatternPlayer::canRetire() const {
    return detachedOwner_ == nullptr || (handover_ != nullptr && handover_->freeReturnSlots() >= 1);
}

void PatternPlayer::reportActive() const {
    if (handover_ != nullptr) {
        const auto* owner = slots_[static_cast<size_t>(playingSlot_ >= 0 ? playingSlot_ : selected_)].owner;
        handover_->reportActive(owner != nullptr ? owner->version : 0, selected_ + 1);
    }
}

void PatternPlayer::commitSelected(double point, bool restart) {
    // A point inside the bar always continues the position: the pattern would start off the bar line otherwise (D-247).
    const bool onBar = std::fabs(point - std::round(point / 4.0) * 4.0) < 1e-6;
    phasePpq_ = restart ? 0.0 : (legato_ || !onBar ? phasePpq_ : point);
    retire(detachedOwner_);
    detachedOwner_ = nullptr;
    pattern_ = slots_[static_cast<size_t>(selected_)].view;
    refreshTranspose();
    playingSlot_ = selected_;
    timing_.resolved = false;
    reportActive();
}

bool PatternPlayer::canStoreResult(size_t slot) const {
    // The playing pattern is only detached (kept), anything else that gets replaced is handed back at once.
    return playingSlot_ == static_cast<int>(slot) || slots_[slot].owner == nullptr || handover_->freeReturnSlots() >= 1;
}

bool PatternPlayer::canStoreEdit(size_t slot) const {
    return slots_[slot].owner == nullptr || handover_->freeReturnSlots() >= 1;
}

void PatternPlayer::storeResult(size_t slot, OwnedPattern* result) {
    if (playingSlot_ == static_cast<int>(slot)) {
        detachedOwner_ = slots_[slot].owner;
        playingSlot_ = -1;
    } else {
        retire(slots_[slot].owner);
    }
    slots_[slot] = {result->view(), result};
    if (static_cast<int>(slot) == selected_) {
        timing_.gridPpq = result->gridPpq;
        timing_.stampPpq = result->stampPpq;
        timing_.hasStamp = result->hasStamp;
        if (result->hasStamp) {
            timing_.resolved = false;
        }
    }
}

void PatternPlayer::storeEdit(size_t slot, OwnedPattern* edit, MidiEventList& out) {
    const PatternView next = edit->view();
    if (playingSlot_ == static_cast<int>(slot)) {
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
                        kept = candidate.channel == channel + 1 && candidate.pitch == note.patternPitch &&
                               candidate.startTick == note.startTick && candidate.lengthTicks == note.lengthTicks &&
                               mapPitch(candidate.pitch) == static_cast<int>(pitch);
                    }
                    if (!kept) {
                        out.push({0, static_cast<uint8_t>(channel + 1), static_cast<uint8_t>(pitch), 0, false});
                        note.active = false;
                        --activeCount_;
                    }
                }
            }
        }
        pattern_ = next;
        refreshTranspose();
    }
    // An edit of a pattern that is not playing (a result still waiting for its point, another slot) only replaces
    // the entry.
    retire(slots_[slot].owner);
    slots_[slot] = {next, edit};
    if (playingSlot_ == static_cast<int>(slot)) {
        reportActive();
    }
}

void PatternPlayer::pollHandover(MidiEventList& out) {
    if (handover_ == nullptr) {
        return;
    }
    // A request is only taken when what it displaces can be handed back; otherwise it stays in the mailbox (and may
    // be replaced there) until the message thread has emptied the return queue.
    for (size_t mailbox = 0; mailbox <= kSlotCount; ++mailbox) {
        const size_t slot = mailbox == kActiveSlot ? static_cast<size_t>(selected_) : mailbox;
        if (handover_->hasResult(mailbox) && canStoreResult(slot)) {
            storeResult(slot, handover_->takeResult(mailbox));
        }
    }
    for (size_t mailbox = 0; mailbox <= kSlotCount; ++mailbox) {
        const size_t slot = mailbox == kActiveSlot ? static_cast<size_t>(selected_) : mailbox;
        if (handover_->hasEdit(mailbox) && canStoreEdit(slot)) {
            storeEdit(slot, handover_->takeEdit(mailbox), out);
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
    if (muted_) {
        return;
    }
    // The table is indexed by the pitch that is sent, so a transposition while notes sound leaves no hanging note.
    const auto sent = static_cast<uint8_t>(mapPitch(note.pitch));
    auto& slot = active_[static_cast<size_t>(note.channel - 1)][sent];
    if (slot.active) {
        // Same pitch still sounding: end it first so the new note-on is never swallowed.
        out.push({offset, note.channel, sent, 0, false});
        slot.active = false;
        --activeCount_;
    }
    // Note-offs have priority: accept a note-on only if every sounding note (and this one) can still be ended.
    if (out.size() + activeCount_ + 2 > MidiEventList::kCapacity) {
        return;
    }
    out.push({offset, note.channel, sent, note.velocity, true});
    slot.active = true;
    slot.patternPitch = note.pitch;
    slot.endPpq = endPpq;
    slot.startedInCall = call_;
    slot.startOffset = offset;
    slot.startTick = note.startTick;
    slot.lengthTicks = note.lengthTicks;
    ++activeCount_;
}

bool PatternPlayer::targetSounds(uint32_t noteId, uint64_t pass) const {
    for (size_t i = 0; i < pattern_.count; ++i) {
        if (pattern_.notes[i].noteId == noteId) {
            return core::soundsInPass(pattern_.notes[i], pattern_.seed, pass);
        }
    }
    return false;
}

void PatternPlayer::processSegment(double scanFrom, double origin, double to, int sampleBase, int numSamples,
                                   double ppqPerSample, MidiEventList& out) {
    // Hosts report negative positions during a count-in: stay silent there (O-24). A block that crosses zero
    // starts its notes from zero on.
    scanFrom = std::max(scanFrom, 0.0);
    if (pattern_.count > 0 && pattern_.lengthTicks > 0 && to > scanFrom) {
        const double ticksToPpq = 1.0 / kTicksPerQuarter;
        const double lengthPpq = pattern_.lengthTicks * ticksToPpq;
        // position 0 of the pattern lies on `phasePpq_`: the passes are counted from there (a restart at a switch
        // starts at pass 0 again); before it (legato running over a negative phase) the passes count downwards
        const auto firstCycle = static_cast<long long>(std::floor((scanFrom - phasePpq_) / lengthPpq));
        const auto lastCycle = static_cast<long long>(std::floor((to - phasePpq_) / lengthPpq));
        for (long long cycle = std::max<long long>(firstCycle, 0); cycle <= lastCycle; ++cycle) {
            for (size_t i = 0; i < pattern_.count; ++i) {
                const PatternNote& note = pattern_.notes[i];
                const double start = phasePpq_ + static_cast<double>(cycle) * lengthPpq + note.startTick * ticksToPpq;
                if (start >= scanFrom && start < to) {
                    // probability and conditions (SPEC 3.19): the same decision for the same pass wherever it is played
                    if (!core::soundsInPass(note, pattern_.seed, static_cast<uint64_t>(cycle))) {
                        continue;
                    }
                    uint32_t lengthTicks = note.lengthTicks;
                    if (note.slideTargetId != 0 && note.plainLengthTicks != 0 &&
                        !targetSounds(note.slideTargetId, static_cast<uint64_t>(cycle))) {
                        lengthTicks = note.plainLengthTicks; // the note it glides into stays out: it ends as a plain note
                    }
                    const double end = start + lengthTicks * ticksToPpq;
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

void PatternPlayer::playRangeSwitch(double scanFrom, double origin, double to, int sampleBase, int numSamples,
                                    double ppqPerSample, MidiEventList& out) {
    if (switchPending() && timing_.resolved && timing_.pointPpq < to && !canRetire()) {
        // The return queue is full: the old pattern cannot be handed back, so the switch waits for the next grid
        // point (SPEC 6.3). Until then the old pattern keeps playing.
        timing_.pointPpq = firstSwitchPoint(std::max(timing_.pointPpq, scanFrom) + 1e-6, timing_.gridPpq, offBarSwitch_);
    }
    if (switchPending() && timing_.resolved && timing_.pointPpq < to) {
        const double point = std::max(timing_.pointPpq, scanFrom);
        processSegment(scanFrom, origin, point, sampleBase, numSamples, ppqPerSample, out);
        releaseAt(out, clampOffset(sampleBase, (point - origin) / ppqPerSample, numSamples), true);
        commitSelected(point, false);
        processSegment(point, origin, to, sampleBase, numSamples, ppqPerSample, out);
    } else {
        processSegment(scanFrom, origin, to, sampleBase, numSamples, ppqPerSample, out);
    }
}

void PatternPlayer::playRange(double scanFrom, double origin, double to, int sampleBase, int numSamples,
                              double ppqPerSample, MidiEventList& out) {
    // A planned transposition inside the range cuts it: what comes before is played as it was (SPEC 3.16).
    if (pendingTranspose_.has && pendingTranspose_.atPpq > scanFrom && pendingTranspose_.atPpq < to) {
        const double point = pendingTranspose_.atPpq;
        playRangeSwitch(scanFrom, origin, point, sampleBase, numSamples, ppqPerSample, out);
        setTranspose(pendingTranspose_.offset);
        playRangeSwitch(point, origin, to, sampleBase, numSamples, ppqPerSample, out);
        return;
    }
    playRangeSwitch(scanFrom, origin, to, sampleBase, numSamples, ppqPerSample, out);
}

void PatternPlayer::process(const TransportInfo& transport, int numSamples, double sampleRate, MidiEventList& out) {
    out.clear();
    ++call_;
    pollHandover(out);
    if (muted_) {
        releaseAt(out, 0, false); // nothing starts while muted, so this only ends what sounded before
    }

    if (!transport.hasPosition || !transport.isPlaying || transport.bpm <= 0.0 || sampleRate <= 0.0 ||
        numSamples <= 0) {
        releaseAll(out, 0);
        playing_ = false;
        pendingWrapRelease_ = false;
        return;
    }

    const double ppqPerSample = transport.bpm / 60.0 / sampleRate;
    const double tolerance = positionTolerance(ppqPerSample);

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
    if (restart) {
        phasePpq_ = 0.0; // after a start or a jump the song grid decides (SPEC 6.1)
    }
    lastPpqPerSample_ = ppqPerSample;
    lastNumSamples_ = numSamples;

    if (pendingTranspose_.has && (restart || pendingTranspose_.atPpq <= transport.ppq + 1e-9)) {
        setTranspose(pendingTranspose_.offset); // its point is behind us (or the transport jumped): take it now
    }

    if (switchPending()) {
        if (restart && canRetire()) {
            commitSelected(0.0, true);
        } else if (!timing_.resolved) {
            // Fixed once, in the block that first sees the change (rule 2): never in the past.
            timing_.pointPpq = timing_.hasStamp && timing_.stampPpq >= transport.ppq - 1e-9
                                   ? timing_.stampPpq
                                   : firstSwitchPoint(transport.ppq, timing_.gridPpq, offBarSwitch_);
            timing_.resolved = true;
        }
    } else {
        timing_.resolved = false;
    }

    const double blockEnd = transport.ppq + numSamples * ppqPerSample;
    const bool loops = transport.isLooping && transport.loopEndPpq > transport.loopStartPpq;
    constexpr double kEpsilon = 1e-9;

    if (loops && transport.ppq < transport.loopEndPpq && blockEnd > transport.loopEndPpq + kEpsilon) {
        // The loop wraps inside this block: handle both halves sample-accurately.
        const int wrapOffset = clampOffset(0, (transport.loopEndPpq - transport.ppq) / ppqPerSample, numSamples);
        playRange(scanFrom, transport.ppq, transport.loopEndPpq, 0, numSamples, ppqPerSample, out);
        releaseAt(out, wrapOffset, true);
        if (switchPending()) {
            // The loop wrap is a jump: a change whose point was not reached before it applies here (T4).
            if (canRetire()) {
                commitSelected(0.0, true);
            } else {
                timing_.resolved = false; // queue full: choose a new point in the next block
            }
        }
        phasePpq_ = 0.0; // the wrap is a jump: the song grid decides
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
