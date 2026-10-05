#include "core/Constraints.h"

#include <algorithm>
#include <array>

namespace mm::core {

namespace {

constexpr int kMaxPasses = 12;
constexpr uint32_t kTripletGridTicks = 160;
constexpr uint32_t kBeatTicks = 960;

enum class Dimension { Pitch, Rhythm, Velocity };

bool isLocked(const Track& track, const Note& note, Dimension dimension) {
    switch (dimension) {
    case Dimension::Pitch:
        return track.lock.pitch || note.lock.pitch;
    case Dimension::Rhythm:
        return track.lock.rhythm || note.lock.rhythm;
    case Dimension::Velocity:
        return track.lock.velocity || note.lock.velocity;
    }
    return false;
}

bool fullyLocked(const Track& track) {
    return track.lock.pitch && track.lock.rhythm && track.lock.velocity;
}

uint32_t endOf(const Note& note) {
    return note.startTick + note.lengthTicks;
}

/// Lowest-first order used for all passes: start, pitch, id.
bool noteLess(const Note& a, const Note& b) {
    if (a.startTick != b.startTick) {
        return a.startTick < b.startTick;
    }
    if (a.pitch != b.pitch) {
        return a.pitch < b.pitch;
    }
    return a.id < b.id;
}

std::optional<Chord> chordAt(const HarmonicContext& context, uint32_t tick) {
    const uint32_t halfBar = tick / (kTicksPerBar / 2);
    for (const ChordEvent& event : context.progression) {
        if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
            return event.chord;
        }
    }
    return std::nullopt;
}

/// The pitch with the same pitch class that lies in [low, high], nearest to `pitch`; nullopt if the range holds no
/// such pitch (very narrow ranges).
std::optional<int> foldIntoRange(int pitch, int low, int high) {
    if (pitch >= low && pitch <= high) {
        return pitch;
    }
    const int pitchClass = pitchClassOf(pitch);
    if (pitch < low) {
        const int candidate = low + ((pitchClass - low) % 12 + 12) % 12;
        return candidate <= high ? std::optional<int>(candidate) : std::nullopt;
    }
    const int candidate = high - ((high - pitchClass) % 12 + 12) % 12;
    return candidate >= low ? std::optional<int>(candidate) : std::nullopt;
}

class Pass {
public:
    Pass(Pattern& pattern, const ConstraintSettings& settings, bool firstPass, ConstraintReport& report)
        : pattern_(pattern), settings_(settings), countLocks_(firstPass), report_(report),
          endTick_(pattern.lengthBars * kTicksPerBar) {}

    void run() {
        reassignChannels();
        for (size_t v = 0; v < pattern_.voices.size(); ++v) {
            Track& track = pattern_.voices[v];
            if (fullyLocked(track)) {
                continue; // imported or locked voices stay exactly as they are
            }
            constrainVoice(track, voiceConstraints(v, track.role));
        }
    }

private:
    VoiceConstraints voiceConstraints(size_t index, VoiceRole role) const {
        if (index < settings_.voices.size()) {
            return settings_.voices[index];
        }
        return role == VoiceRole::Bass ? VoiceConstraints{28, 52} : VoiceConstraints{55, 88};
    }

    void skipped() {
        if (countLocks_) {
            ++report_.skippedByLock;
        }
    }

    void reassignChannels() {
        std::array<bool, 17> used{};
        for (Track& track : pattern_.voices) {
            const bool valid = track.midiChannel >= 1 && track.midiChannel <= 16;
            if (valid && !used[track.midiChannel]) {
                used[track.midiChannel] = true;
                continue;
            }
            for (uint8_t channel = 1; channel <= 16; ++channel) {
                if (!used[channel]) {
                    used[channel] = true;
                    track.midiChannel = channel;
                    ++report_.channelsReassigned;
                    break;
                }
            }
        }
    }

    void constrainVoice(Track& track, const VoiceConstraints& range) {
        clampVelocity(track);
        snapToGridAndClip(track);
        std::stable_sort(track.notes.begin(), track.notes.end(), noteLess);
        constrainPitch(track, range);
        mergeDuplicates(track);
        trimOverlaps(track);
        removeInvalidSlides(track);
        mergeTies(track);
        dropShortNotes(track);
        std::stable_sort(track.notes.begin(), track.notes.end(), noteLess);
    }

    // -- velocity ----------------------------------------------------------------------------------------------

    void clampVelocity(Track& track) {
        for (Note& note : track.notes) {
            const uint8_t clamped = static_cast<uint8_t>(std::clamp<int>(note.velocity, 1, 127));
            if (clamped == note.velocity) {
                continue;
            }
            if (isLocked(track, note, Dimension::Velocity)) {
                skipped();
                continue;
            }
            note.velocity = clamped;
            ++report_.velocityClamped;
        }
    }

    // -- grid, length and pattern end --------------------------------------------------------------------------

    bool onGrid(uint32_t start) const {
        if (settings_.gridTicks == 0) {
            return true;
        }
        return start % settings_.gridTicks == 0 || (settings_.acceptTriplets && start % kTripletGridTicks == 0);
    }

    void snapToGridAndClip(Track& track) {
        std::vector<Note> kept;
        kept.reserve(track.notes.size());
        for (Note note : track.notes) {
            if (isLocked(track, note, Dimension::Rhythm)) {
                if (!onGrid(note.startTick) || endOf(note) > endTick_ || note.startTick >= endTick_) {
                    skipped();
                }
                kept.push_back(note);
                continue;
            }
            if (note.startTick >= endTick_) {
                ++report_.droppedNotes;
                continue;
            }
            if (!onGrid(note.startTick)) {
                const uint32_t grid = settings_.gridTicks;
                const uint32_t lower = note.startTick - note.startTick % grid;
                const uint32_t upper = lower + grid;
                uint32_t snapped = (note.startTick - lower <= upper - note.startTick) ? lower : upper;
                if (snapped >= endTick_) {
                    snapped = lower;
                }
                note.startTick = snapped;
                ++report_.gridSnapped;
            }
            if (endOf(note) > endTick_) {
                note.lengthTicks = endTick_ - note.startTick;
                ++report_.clippedToEnd;
            }
            kept.push_back(note);
        }
        track.notes = std::move(kept);
    }

    // -- pitch -------------------------------------------------------------------------------------------------

    void constrainPitch(Track& track, const VoiceConstraints& range) {
        const Scale* scale = findScale(pattern_.context.scaleId);
        const PitchClass root = pattern_.context.root;
        const size_t budget = static_cast<size_t>(std::max(settings_.chromaticPercent, 0)) * track.notes.size() / 100;
        size_t chromaticUsed = 0;

        std::vector<Note> kept;
        kept.reserve(track.notes.size());
        for (Note note : track.notes) {
            const auto chord = chordAt(pattern_.context, note.startTick);
            const bool pitchLocked = isLocked(track, note, Dimension::Pitch);
            const bool inRange = note.pitch >= range.rangeLow && note.pitch <= range.rangeHigh;
            const bool allowed = scale == nullptr || isAllowed(*scale, root, chord, note.pitch);

            if (pitchLocked) {
                if (!inRange || !allowed) {
                    skipped();
                }
                if (!allowed) {
                    ++chromaticUsed;
                }
                kept.push_back(note);
                continue;
            }

            int pitch = note.pitch;
            if (const auto folded = foldIntoRange(pitch, range.rangeLow, range.rangeHigh)) {
                pitch = *folded;
            } else if (!isLocked(track, note, Dimension::Rhythm)) {
                ++report_.droppedNotes; // the range holds no pitch with this pitch class
                continue;
            } else {
                skipped();
                kept.push_back(note);
                continue;
            }

            if (scale != nullptr && !isAllowed(*scale, root, chord, pitch)) {
                const auto snapped = quantize(*scale, root, chord, pitch);
                const bool weakStep = note.startTick % kBeatTicks != 0;
                const bool adjacent = snapped.has_value() && std::abs(*snapped - pitch) == 1;
                if (weakStep && adjacent && chromaticUsed < budget) {
                    ++chromaticUsed; // a targeted chromatic passing tone within the share
                } else if (snapped.has_value()) {
                    pitch = *snapped;
                }
            }
            if (const auto folded = foldIntoRange(pitch, range.rangeLow, range.rangeHigh)) {
                pitch = *folded;
            }

            if (pitch != note.pitch) {
                if (pitch != foldIntoRange(note.pitch, range.rangeLow, range.rangeHigh).value_or(note.pitch)) {
                    ++report_.pitchSnapped;
                }
                if (note.pitch < range.rangeLow || note.pitch > range.rangeHigh) {
                    ++report_.registerFolded;
                }
                note.pitch = static_cast<uint8_t>(pitch);
            }
            kept.push_back(note);
        }
        track.notes = std::move(kept);
    }

    // -- duplicates and overlaps -------------------------------------------------------------------------------

    /// Notes with the same start and pitch collapse into one; the bass keeps a single note per start.
    void mergeDuplicates(Track& track) {
        std::vector<bool> remove(track.notes.size(), false);
        const bool bass = track.role == VoiceRole::Bass;
        for (size_t i = 0; i < track.notes.size(); ++i) {
            if (remove[i]) {
                continue;
            }
            for (size_t j = i + 1; j < track.notes.size() && track.notes[j].startTick == track.notes[i].startTick;
                 ++j) {
                if (remove[j] || !(bass || track.notes[j].pitch == track.notes[i].pitch)) {
                    continue;
                }
                if (isLocked(track, track.notes[j], Dimension::Rhythm)) {
                    skipped();
                    continue;
                }
                if (track.notes[j].pitch == track.notes[i].pitch &&
                    !isLocked(track, track.notes[i], Dimension::Rhythm)) {
                    track.notes[i].lengthTicks = std::max(track.notes[i].lengthTicks, track.notes[j].lengthTicks);
                }
                remove[j] = true;
                ++report_.droppedNotes;
            }
        }
        eraseMarked(track, remove);
    }

    void trimOverlaps(Track& track) {
        const bool bass = track.role == VoiceRole::Bass;
        for (size_t i = 0; i < track.notes.size(); ++i) {
            Note& note = track.notes[i];
            uint32_t limit = endOf(note);
            for (size_t j = i + 1; j < track.notes.size() && track.notes[j].startTick < limit; ++j) {
                if (bass || track.notes[j].pitch == note.pitch) {
                    limit = std::min(limit, track.notes[j].startTick);
                    break;
                }
            }
            if (limit < endOf(note)) {
                if (isLocked(track, note, Dimension::Rhythm)) {
                    skipped();
                    continue;
                }
                note.lengthTicks = limit - note.startTick;
                ++report_.trimmedOverlaps;
            }
        }
    }

    // -- slides ------------------------------------------------------------------------------------------------

    /// First note starting at or after `tick` and the number of notes starting at that tick; index == size if none.
    std::pair<size_t, size_t> targetAfter(const Track& track, uint32_t tick) const {
        size_t index = track.notes.size();
        for (size_t i = 0; i < track.notes.size(); ++i) {
            if (track.notes[i].startTick >= tick &&
                (index == track.notes.size() || track.notes[i].startTick < track.notes[index].startTick)) {
                index = i;
            }
        }
        size_t group = 0;
        if (index < track.notes.size()) {
            for (const Note& other : track.notes) {
                group += other.startTick == track.notes[index].startTick ? 1 : 0;
            }
        }
        return {index, group};
    }

    /// Slides only inside monophonic sections and never together with ratchets (SPEC 4.2).
    void removeInvalidSlides(Track& track) {
        for (size_t i = 0; i < track.notes.size(); ++i) {
            Note& note = track.notes[i];
            if (!note.slide) {
                continue;
            }
            bool invalid = note.ratchet > 1;
            for (size_t j = 0; j < track.notes.size() && !invalid; ++j) {
                invalid = j != i && track.notes[j].startTick < endOf(note) && endOf(track.notes[j]) > note.startTick;
            }
            if (!invalid) {
                const auto target = targetAfter(track, endOf(note));
                invalid = target.first < track.notes.size() && target.second > 1;
            }
            if (!invalid) {
                continue;
            }
            if (isLocked(track, note, Dimension::Rhythm)) {
                skipped();
                continue;
            }
            note.slide = false;
            ++report_.slidesRemoved;
        }
    }

    /// A slide onto the same pitch is a tie: both notes become one (SPEC 4.2); the first note keeps its id.
    void mergeTies(Track& track) {
        for (size_t i = 0; i < track.notes.size(); ++i) {
            while (track.notes[i].slide) {
                const auto target = targetAfter(track, endOf(track.notes[i]));
                if (target.first >= track.notes.size() || target.second != 1 ||
                    track.notes[target.first].pitch != track.notes[i].pitch || target.first == i) {
                    break;
                }
                if (isLocked(track, track.notes[i], Dimension::Rhythm) ||
                    isLocked(track, track.notes[target.first], Dimension::Rhythm)) {
                    skipped();
                    break;
                }
                Note& first = track.notes[i];
                const Note& second = track.notes[target.first];
                first.lengthTicks = endOf(second) - first.startTick;
                first.slide = second.slide;
                track.notes.erase(track.notes.begin() + static_cast<std::ptrdiff_t>(target.first));
                if (target.first < i) {
                    --i;
                }
                ++report_.tiesMerged;
            }
        }
    }

    void dropShortNotes(Track& track) {
        std::vector<bool> remove(track.notes.size(), false);
        for (size_t i = 0; i < track.notes.size(); ++i) {
            if (track.notes[i].lengthTicks >= settings_.minNoteTicks) {
                continue;
            }
            if (isLocked(track, track.notes[i], Dimension::Rhythm)) {
                skipped();
                continue;
            }
            remove[i] = true;
            ++report_.droppedNotes;
        }
        eraseMarked(track, remove);
    }

    static void eraseMarked(Track& track, const std::vector<bool>& remove) {
        std::vector<Note> kept;
        kept.reserve(track.notes.size());
        for (size_t i = 0; i < track.notes.size(); ++i) {
            if (!remove[i]) {
                kept.push_back(track.notes[i]);
            }
        }
        track.notes = std::move(kept);
    }

    Pattern& pattern_;
    const ConstraintSettings& settings_;
    bool countLocks_;
    ConstraintReport& report_;
    uint32_t endTick_;
};

void accumulate(ConstraintReport& total, const ConstraintReport& pass) {
    total.channelsReassigned += pass.channelsReassigned;
    total.velocityClamped += pass.velocityClamped;
    total.pitchSnapped += pass.pitchSnapped;
    total.registerFolded += pass.registerFolded;
    total.gridSnapped += pass.gridSnapped;
    total.clippedToEnd += pass.clippedToEnd;
    total.tiesMerged += pass.tiesMerged;
    total.trimmedOverlaps += pass.trimmedOverlaps;
    total.slidesRemoved += pass.slidesRemoved;
    total.droppedNotes += pass.droppedNotes;
    total.skippedByLock += pass.skippedByLock;
}

} // namespace

ConstraintSettings ConstraintSettings::defaultsFor(const Pattern& pattern) {
    ConstraintSettings settings;
    for (const Track& track : pattern.voices) {
        settings.voices.push_back(track.role == VoiceRole::Bass ? VoiceConstraints{28, 52} : VoiceConstraints{55, 88});
    }
    return settings;
}

size_t ConstraintReport::total() const {
    return channelsReassigned + velocityClamped + pitchSnapped + registerFolded + gridSnapped + clippedToEnd +
           tiesMerged + trimmedOverlaps + slidesRemoved + droppedNotes;
}

ConstraintReport applyConstraints(Pattern& pattern, const ConstraintSettings& settings) {
    ConstraintReport total;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        ConstraintReport current;
        Pass(pattern, settings, pass == 0, current).run();
        accumulate(total, current);
        ++total.passes;
        if (current.total() == 0) {
            total.converged = true;
            return total;
        }
    }
    total.converged = false;
    return total;
}

} // namespace mm::core
