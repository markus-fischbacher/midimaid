#include "core/OutputStage.h"

#include "core/KickGrid.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace mm::core {

namespace {

constexpr int64_t kStepTicks = 240; // a 16th

int64_t clampTo(int64_t value, int64_t low, int64_t high) {
    return std::max(low, std::min(value, high));
}

int32_t toPermille(float value) {
    return static_cast<int32_t>(std::lround(value * 1000.0f));
}

/// value * factor / 1000, rounded half away from zero.
int64_t scaleByPermille(int64_t value, int64_t permille) {
    const int64_t magnitude = (std::abs(value) * permille + 500) / 1000;
    return value < 0 ? -magnitude : magnitude;
}

/// One note on its way through the stages.
struct Work {
    const Note* source = nullptr;
    int64_t start = 0;    ///< after the groove
    int64_t plainEnd = 0; ///< start + length, kept inside the pattern
    int64_t plainCut = 0; ///< plainEnd, cut where the next note of the line begins (the end without a slide)
    int64_t end = 0;      ///< plainCut, or the end of a slide
    int velocity = 100;
    bool wantsSlide = false; ///< slide flag still valid after the stages so far
    bool extended = false;   ///< the end reaches the next note (slide overlap, or a tie onto the same pitch)
    bool glides = false;     ///< the extension is a real slide: the next note has another pitch
    bool dropped = false;
    int64_t plainFinal = 0;  ///< the end without a slide, after the kick clearance (when the slide target stays out)
    uint32_t slideTargetId = 0;
};

struct GrooveTable {
    bool table = false;                  // a drum reference or a template: offsets and factors per 16th
    int32_t swingTicks = 0;              // straight, or a template without swing of its own: delay of every odd 16th
    std::vector<int64_t> offsets;        // per 16th of the reference or template
    std::vector<int64_t> velocityFactor; // permille, per 16th of the reference or template
    std::string templateId;              // a template with `humanize`: the deviation of every note
    int32_t humanizeTicks = 0;
    int32_t humanizeVelocity = 0;
};

GrooveTable buildGroove(const Pattern& pattern, const GrooveSettings& groove, bool includeGroove,
                        const GrooveLibrary* library) {
    GrooveTable result;
    if (!includeGroove) {
        return result; // straight: no swing, no profile
    }
    const int32_t amount = std::clamp(toPermille(groove.amount), 0, 1000);
    const bool wantsReference = std::string_view(groove.templateId) == kGrooveDrumReference;
    if (wantsReference && pattern.rhythmRef.has_value() && pattern.rhythmRef->bars >= 1) {
        const RhythmReference& ref = *pattern.rhythmRef;
        const size_t steps = std::min<size_t>(static_cast<size_t>(ref.bars) * 16, ref.velocity.size());
        int64_t sum = 0;
        int64_t count = 0;
        for (size_t i = 0; i < steps; ++i) {
            if (ref.velocity[i] > 0) {
                sum += ref.velocity[i];
                ++count;
            }
        }
        const int64_t mean = count > 0 ? std::max<int64_t>(sum / count, 1) : 0;
        result.table = true;
        for (size_t i = 0; i < steps; ++i) {
            result.offsets.push_back(scaleByPermille(ref.timingOffsetTicks[i], amount));
            int64_t factor = 1000;
            if (mean > 0 && ref.velocity[i] > 0) {
                factor = clampTo((static_cast<int64_t>(ref.velocity[i]) * 1000 + mean / 2) / mean, 500, 1500);
            }
            result.velocityFactor.push_back(1000 + scaleByPermille(factor - 1000, amount));
        }
    } else if (library != nullptr && !wantsReference && library->find(groove.templateId) != nullptr) {
        const GrooveTemplate& source = *library->find(groove.templateId);
        result.table = true;
        for (size_t i = 0; i < source.steps(); ++i) {
            result.offsets.push_back(scaleByPermille(source.offsetTicks[i], amount));
            result.velocityFactor.push_back(1000 + scaleByPermille(source.velocityPermille[i] - 1000, amount));
        }
        // a template without swing of its own takes the swing control on top (a template with swing replaces it)
        result.swingTicks = source.swing ? 0 : swingOffsetTicks(groove.swing, groove.amount);
        result.templateId = source.id;
        result.humanizeTicks = static_cast<int32_t>(scaleByPermille(source.humanizeTicks, amount));
        result.humanizeVelocity = static_cast<int32_t>(scaleByPermille(source.humanizeVelocityPermille, amount));
    } else {
        result.swingTicks = swingOffsetTicks(groove.swing, groove.amount);
    }
    return result;
}

/// Stage 2: timing offset and velocity profile. Only notes that start exactly on a 16th are touched (triplets and
/// freely placed notes stay, SPEC 3.9). Starts are kept inside [0, pattern end) (D-104).
void applyGroove(const Pattern& pattern, const Track& track, bool includeGroove, const GrooveLibrary* library,
                 std::vector<Work>& notes, int64_t patternEnd) {
    const GrooveTable groove = buildGroove(pattern, track.groove, includeGroove, library);
    for (Work& work : notes) {
        const Note& note = *work.source;
        int64_t offset = 0;
        int64_t factor = 1000;
        if (note.startTick % kStepTicks == 0) {
            const int64_t step = note.startTick / kStepTicks;
            if (groove.table && !groove.offsets.empty()) {
                const size_t index = static_cast<size_t>(step) % groove.offsets.size();
                offset = groove.offsets[index] + (step % 2 == 1 ? groove.swingTicks : 0);
                factor = groove.velocityFactor[index];
            } else if (step % 2 == 1) {
                offset = groove.swingTicks;
            }
            if (groove.humanizeTicks > 0) { // Humanize: a small, deterministic deviation of every note (SPEC 3.9)
                offset += humanizeOffset(pattern.info.seed, note.id, groove.templateId, groove.humanizeTicks);
            }
            if (groove.humanizeVelocity > 0) {
                factor = factor * (1000 + humanizeVelocity(pattern.info.seed, note.id, groove.templateId,
                                                           groove.humanizeVelocity)) /
                         1000;
            }
        }
        work.start = clampTo(static_cast<int64_t>(note.startTick) + offset, 0, patternEnd - 1);
        work.plainEnd = std::min(work.start + note.lengthTicks, patternEnd);
        work.end = work.plainEnd;
        work.velocity = static_cast<int>(clampTo((static_cast<int64_t>(note.velocity) * factor + 500) / 1000, 1, 127));
        work.wantsSlide = note.slide;
    }
}

/// The note that follows `index` in the pattern: the first note starting at or after this note's (unshifted) end,
/// or the first note of the next pass (`wraps`).
struct SlideTarget {
    size_t index = 0;
    bool wraps = false;
};

SlideTarget slideTargetOf(const std::vector<Work>& notes, size_t index) {
    const uint32_t end = notes[index].source->startTick + notes[index].source->lengthTicks;
    for (size_t j = 0; j < notes.size(); ++j) {
        if (j != index && notes[j].source->startTick >= end) {
            return {j, false};
        }
    }
    return {0, true};
}

/// Stage 3: slide overlaps relative to the shifted next note. Notes without a slide end at the latest where the next
/// note of the same line begins (the next note for the monophonic bass, the next equal pitch otherwise), so the groove
/// never creates an unintended overlap.
void applySlides(const Track& track, std::vector<Work>& notes, const OutputSettings& settings, int64_t patternEnd) {
    for (size_t i = 0; i < notes.size(); ++i) {
        Work& work = notes[i];
        int64_t limit = work.plainEnd;
        for (size_t j = 0; j < notes.size(); ++j) {
            if (j == i || notes[j].start < work.start) {
                continue;
            }
            const bool relevant = track.role == VoiceRole::Bass || notes[j].source->pitch == work.source->pitch;
            if (relevant && (notes[j].start > work.start || j > i)) {
                limit = std::min(limit, notes[j].start);
            }
        }
        work.plainCut = limit;
        work.plainFinal = limit;
        work.end = limit;
        work.extended = false;
        work.glides = false;
        if (!work.wantsSlide) {
            continue;
        }
        const SlideTarget target = slideTargetOf(notes, i);
        const int64_t targetStart = notes[target.index].start + (target.wraps ? patternEnd : 0);
        if (targetStart <= work.start) {
            work.wantsSlide = false; // the groove moved the target in front of this note
            continue;
        }
        work.slideTargetId = notes[target.index].source->id;
        const bool samePitch = notes[target.index].source->pitch == work.source->pitch;
        work.end = samePitch ? targetStart : targetStart + settings.slideOverlapTicks;
        work.extended = true;
        work.glides = !samePitch;
    }
}

/// Stage 4: the kick clearance after the groove (bass voices without `long_tied`). A slide that reaches over a kick
/// step loses its overlap; every other note is shortened to end the clearance before the next kick. Notes that keep
/// less than the minimum length are dropped, which in turn can take the slide of the note in front of them.
void applyKickClearance(std::vector<Work>& notes, const std::vector<uint32_t>& kicks, const OutputSettings& settings,
                        int64_t patternEnd) {
    if (kicks.empty()) {
        return;
    }
    auto nextKick = [&](int64_t tick) {
        const auto it = std::upper_bound(kicks.begin(), kicks.end(), tick);
        return it != kicks.end() ? static_cast<int64_t>(*it) : static_cast<int64_t>(kicks.front()) + patternEnd;
    };
    auto reachesKick = [&](const Work& work) {
        for (int pass = 0; pass < 2; ++pass) {
            for (const uint32_t kick : kicks) {
                const int64_t tick = static_cast<int64_t>(kick) + pass * patternEnd;
                if (tick > work.start && tick <= work.end) {
                    return true;
                }
            }
        }
        return false;
    };
    std::vector<int64_t> slideEnd(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        slideEnd[i] = notes[i].end;
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < notes.size(); ++i) {
            Work& work = notes[i];
            bool keepSlide = work.extended;
            if (keepSlide) {
                Work probe = work;
                probe.end = slideEnd[i];
                const SlideTarget target = slideTargetOf(notes, i);
                keepSlide = !reachesKick(probe) && !notes[target.index].dropped;
            }
            int64_t end = slideEnd[i];
            int64_t plain = work.plainCut;
            if (settings.kickClearanceTicks > 0) {
                plain = std::min(plain, nextKick(work.start) - static_cast<int64_t>(settings.kickClearanceTicks));
            }
            work.plainFinal = plain;
            if (!keepSlide) {
                end = plain; // a removed slide ends like a plain note
            }
            const bool dropped = end - work.start < static_cast<int64_t>(settings.minNoteTicks);
            if (keepSlide != work.extended || dropped != work.dropped || end != work.end) {
                work.extended = keepSlide;
                work.dropped = dropped;
                work.end = end;
                changed = true;
            }
        }
    }
}

} // namespace

int32_t swingOffsetTicks(float swing, float amount) {
    const int64_t swingPermille = clampTo(toPermille(swing), 500, 750);
    const int64_t amountPermille = clampTo(toPermille(amount), 0, 1000);
    return static_cast<int32_t>(((swingPermille - 500) * 480 * amountPermille + 500000) / 1000000);
}

/// Stage 1: moves a pitch by whole octaves; a result outside 0 to 127 is folded back into the range.
static uint8_t shiftedPitch(uint8_t pitch, int octaveShift) {
    int value = static_cast<int>(pitch) + 12 * octaveShift;
    while (value > 127) {
        value -= 12;
    }
    while (value < 0) {
        value += 12;
    }
    return static_cast<uint8_t>(value);
}

/// Stage 5: a ratchet divides the sounding note into `repeats` equal parts; every part but the last gives a quarter of its
/// length back, so that the repeats do not touch (same pitch). The first part keeps the velocity, the others follow at
/// 85 % of it. When the parts would be shorter than the minimum note, the repeats are reduced until they fit; a note
/// that cannot be divided sounds as one. Slides never carry a ratchet.
static void resolveRatchet(const OutputNote& note, uint8_t ratchet, const OutputSettings& settings,
                           std::vector<OutputNote>& out) {
    const int64_t length = static_cast<int64_t>(note.endTick) - note.startTick;
    const int64_t minPart = static_cast<int64_t>(std::max<uint32_t>(settings.minNoteTicks, 1));
    int64_t repeats = note.slideIntoNext ? 1 : std::clamp<int64_t>(ratchet, 1, 4);
    while (repeats > 1 && length / repeats < minPart) {
        --repeats;
    }
    if (repeats <= 1) {
        out.push_back(note);
        return;
    }
    const int64_t part = length / repeats;
    for (int64_t i = 0; i < repeats; ++i) {
        OutputNote piece = note;
        piece.startTick = static_cast<int32_t>(note.startTick + i * part);
        piece.endTick = static_cast<int32_t>(i + 1 == repeats ? note.startTick + length : piece.startTick + part * 3 / 4);
        if (i > 0) {
            piece.velocity = static_cast<uint8_t>(std::max<int>(1, note.velocity * 85 / 100));
        }
        out.push_back(piece);
    }
}

OutputPattern renderOutput(const Pattern& pattern, const OutputSettings& settings) {
    OutputPattern output;
    const int64_t patternEnd = static_cast<int64_t>(pattern.lengthBars) * kTicksPerBar;
    output.lengthTicks = static_cast<uint32_t>(patternEnd);
    const std::vector<uint32_t> kicks = kickTicks(pattern);

    for (size_t v = 0; v < pattern.voices.size(); ++v) {
        const Track& track = pattern.voices[v];
        OutputVoice voice;
        voice.role = track.role;
        voice.channel = track.midiChannel;
        voice.muted = track.muted;

        std::vector<Work> notes(track.notes.size());
        for (size_t i = 0; i < notes.size(); ++i) {
            notes[i].source = &track.notes[i];
        }
        applyGroove(pattern, track, settings.includeGroove, settings.grooves, notes, patternEnd); // stage 2
        applySlides(track, notes, settings, patternEnd);                        // stage 3
        const bool ignoresKick = v < settings.ignoresKick.size() && settings.ignoresKick[v];
        if (track.role == VoiceRole::Bass && !ignoresKick) {
            applyKickClearance(notes, kicks, settings, patternEnd); // stage 4
        }
        for (const Work& work : notes) {
            if (work.dropped ||
                work.end - work.start < static_cast<int64_t>(std::max<uint32_t>(settings.minNoteTicks, 1))) {
                continue;
            }
            OutputNote note;
            note.noteId = work.source->id;
            note.channel = track.midiChannel;
            note.pitch = shiftedPitch(work.source->pitch, settings.octaveShift); // stage 1
            // stage 5: accent velocity
            note.velocity = work.source->accent ? settings.accentVelocity : static_cast<uint8_t>(work.velocity);
            note.startTick = static_cast<int32_t>(work.start);
            note.endTick = static_cast<int32_t>(work.end);
            note.slideIntoNext = work.extended && work.glides;
            note.chance = work.source->chance;
            note.condA = work.source->condA;
            note.condB = work.source->condB;
            if (work.extended && work.slideTargetId != 0) {
                note.slideTargetId = work.slideTargetId;
                note.plainEndTick = static_cast<int32_t>(std::max<int64_t>(work.plainFinal, work.start));
            }
            resolveRatchet(note, work.source->slide ? uint8_t{1} : work.source->ratchet, settings, voice.notes);
        }
        std::stable_sort(voice.notes.begin(), voice.notes.end(), [](const OutputNote& a, const OutputNote& b) {
            if (a.startTick != b.startTick) {
                return a.startTick < b.startTick;
            }
            if (a.pitch != b.pitch) {
                return a.pitch < b.pitch;
            }
            return a.noteId < b.noteId;
        });
        output.voices.push_back(std::move(voice));
    }
    return output;
}

} // namespace mm::core
