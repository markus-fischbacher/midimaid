#pragma once

#include "core/KickGrid.h"
#include "core/Pattern.h"
#include "core/Register.h"
#include "core/StyleProfile.h"
#include "core/Theory.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mm::series {

using namespace mm::core;

/// The rules of SPEC 12 (STYLES.md 1.2, 1.6-1.8, 1.7) written against the data model only. They do not call the
/// functions the generators and the constraint layer use for the same rules, so a shared mistake cannot hide.
inline const Chord& chordAtTick(const Pattern& p, uint32_t tick) {
    const uint32_t half = tick / (kTicksPerBar / 2);
    for (const ChordEvent& event : p.context.progression) {
        if (half >= event.startHalfBar && half < event.startHalfBar + event.lengthHalfBars) {
            return event.chord;
        }
    }
    return p.context.progression.back().chord;
}

inline bool isChordTone(const Pattern& p, uint32_t tick, int pitch) {
    const Chord& chord = chordAtTick(p, tick);
    for (const PitchClass pc : chordPitchClasses(p.context.root, chord)) {
        if (pitch % 12 == pc) {
            return true;
        }
    }
    return false;
}

inline bool inKeyScale(const Pattern& p, int pitch) {
    const Scale* scale = findScale(p.context.scaleId);
    return scale != nullptr && inScale(*scale, p.context.root, pitch);
}

inline bool sounds(const Note& n, uint32_t tick) {
    return n.startTick <= tick && tick < n.startTick + n.lengthTicks;
}

inline bool tenseInterval(int a, int b) {
    const int semis = ((a - b) % 12 + 12) % 12;
    return semis == 1 || semis == 6 || semis == 11;
}

/// Violations of the rules, one message each; empty = the pattern keeps all of them.
inline std::vector<std::string> violations(const Pattern& p, const StyleProfile& style) {
    std::vector<std::string> issues;
    auto add = [&](size_t voice, const std::string& text) {
        issues.push_back("voice " + std::to_string(voice) + " (" + p.voices[voice].archetypeId + "): " + text);
    };
    const uint32_t end = p.lengthBars * kTicksPerBar;
    const std::vector<uint32_t> kicks = kickTicks(p);
    const bool tension = style.chromaticDefaultPercent >= 30 || style.id == "hard_industrial";

    for (size_t v = 0; v < p.voices.size(); ++v) {
        const Track& track = p.voices[v];
        const bool ignoresKick = ignoresKickArchetype(track.archetypeId);
        const auto range =
            effectiveRange(style.registerProfile(), track.role, track.archetypeId, p.voicing, track.octaveOffset);
        if (!range.has_value()) {
            add(v, "no range");
            continue;
        }
        size_t outsideScale = 0;
        for (const Note& n : track.notes) {
            if (n.startTick % 240 != 0) {
                add(v, "start off the 16th grid");
            }
            if (n.lengthTicks < 60 || n.startTick + n.lengthTicks > end) {
                add(v, "length outside the pattern");
            }
            if (n.pitch < range->low || n.pitch > range->high) {
                add(v, "pitch " + std::to_string(n.pitch) + " outside the range");
            }
            if (!inKeyScale(p, n.pitch) && !isChordTone(p, n.startTick, n.pitch)) {
                ++outsideScale;
            }
            if (track.role == VoiceRole::Bass && !ignoresKick &&
                std::binary_search(kicks.begin(), kicks.end(), n.startTick)) {
                add(v, "bass on a kick");
            }
            if (track.role == VoiceRole::Bass && !ignoresKick) {
                const auto next = std::upper_bound(kicks.begin(), kicks.end(), n.startTick);
                if (next != kicks.end() &&
                    static_cast<int64_t>(n.startTick) + n.lengthTicks >
                        static_cast<int64_t>(*next) - static_cast<int64_t>(style.bass.kickClearanceTicks)) {
                    add(v, "bass reaches into the kick clearance");
                }
            }
        }
        if (outsideScale > static_cast<size_t>(style.chromaticDefaultPercent) * track.notes.size() / 100) {
            add(v, "more notes outside the scale than the chromatic share allows");
        }
        for (size_t i = 0; i < track.notes.size(); ++i) {
            for (size_t j = i + 1; j < track.notes.size(); ++j) {
                const Note& a = track.notes[i];
                const Note& b = track.notes[j];
                if (b.startTick >= a.startTick + a.lengthTicks) {
                    break; // sorted by start: nothing later can overlap a
                }
                if (a.pitch == b.pitch) {
                    add(v, "overlapping notes of equal pitch");
                }
                if (track.role == VoiceRole::Bass && !ignoresKick) {
                    add(v, "bass is not monophonic");
                }
            }
        }
    }

    // The first bass voice against every other voice on the strong steps (STYLES.md 1.6, 1.8). The rules know one
    // bass; a second bass voice is not part of v1.0 (D-122).
    for (size_t b = 0; b < p.voices.size(); ++b) {
        if (p.voices[b].role != VoiceRole::Bass) {
            continue;
        }
        bool firstBass = true;
        for (size_t earlier = 0; earlier < b; ++earlier) {
            firstBass = firstBass && p.voices[earlier].role != VoiceRole::Bass;
        }
        if (!firstBass) {
            continue;
        }
        for (size_t o = 0; o < p.voices.size(); ++o) {
            if (p.voices[o].role == VoiceRole::Bass) {
                continue;
            }
            for (const Note& bass : p.voices[b].notes) {
                for (const Note& other : p.voices[o].notes) {
                    if (other.startTick == bass.startTick && other.pitch < bass.pitch + 12) {
                        add(o, "less than an octave above a simultaneous bass attack");
                    }
                    const uint32_t strong = std::max(bass.startTick, other.startTick);
                    const bool attackOnStrong = (bass.startTick == strong || other.startTick == strong) &&
                                                strong % (kTicksPerBar / 4) == 0;
                    if (attackOnStrong && sounds(bass, strong) && sounds(other, strong) && !tension &&
                        tenseInterval(other.pitch, bass.pitch)) {
                        add(o, "tense interval against the bass on a strong step");
                    }
                }
            }
        }
    }
    return issues;
}

} // namespace mm::series
