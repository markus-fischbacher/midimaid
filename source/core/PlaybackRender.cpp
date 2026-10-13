#include "core/PlaybackRender.h"

#include "core/Archetype.h"
#include "core/InstanceSettings.h"
#include "core/OutputStage.h"
#include "core/Register.h"

#include <algorithm>

namespace mm::core {

PlaybackNotes renderVoiceForPlayback(const Pattern& pattern, const StyleProfile& style, size_t voiceIndex,
                                     int octaveShift, const GrooveLibrary* grooves) {
    OutputSettings settings;
    settings.grooves = grooves;
    settings.octaveShift = clampOctave(octaveShift);
    settings.kickClearanceTicks = style.bass.kickClearanceTicks;
    for (const Track& track : pattern.voices) {
        settings.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
    }
    const OutputPattern out = renderOutput(pattern, settings);

    PlaybackNotes result;
    result.lengthTicks = out.lengthTicks;
    result.seed = pattern.info.winnerSeed ^ pattern.info.seed;
    if (voiceIndex >= out.voices.size()) {
        return result;
    }
    for (const OutputNote& note : out.voices[voiceIndex].notes) {
        const int32_t start = std::max<int32_t>(note.startTick, 0);
        const int32_t end = std::min<int32_t>(note.endTick, static_cast<int32_t>(out.lengthTicks));
        if (end > start) {
            PatternNote out{static_cast<uint32_t>(start), static_cast<uint32_t>(end - start), note.channel, note.pitch,
                            note.velocity};
            out.noteId = note.noteId;
            out.chance = note.chance;
            out.condA = note.condA;
            out.condB = note.condB;
            if (note.slideTargetId != 0) {
                out.slideTargetId = note.slideTargetId;
                const int32_t plainEnd = std::min<int32_t>(note.plainEndTick, end);
                out.plainLengthTicks = plainEnd > start ? static_cast<uint32_t>(plainEnd - start) : 0u;
            }
            result.notes.push_back(out);
        }
    }
    std::stable_sort(result.notes.begin(), result.notes.end(),
                     [](const PatternNote& a, const PatternNote& b) { return a.startTick < b.startTick; });

    // What the live transposition needs (SPEC 3.16): the key, the range of the voice and the extremes of its notes.
    TransposeInfo& info = result.transpose;
    info.root = static_cast<uint8_t>(pattern.context.root % 12);
    if (const Scale* scale = findScale(pattern.context.scaleId)) {
        for (const uint8_t interval : scale->intervals) {
            info.scaleMask = static_cast<uint16_t>(info.scaleMask | (1u << (interval % 12)));
        }
    }
    const ConstraintSettings constraints = constraintSettingsFor(pattern, style);
    if (voiceIndex < constraints.voices.size()) {
        info.rangeLow = static_cast<uint8_t>(std::clamp(constraints.voices[voiceIndex].rangeLow, 0, 127));
        info.rangeHigh = static_cast<uint8_t>(std::clamp(constraints.voices[voiceIndex].rangeHigh, 0, 127));
    }
    info.melody = voiceIndex < pattern.voices.size() && pattern.voices[voiceIndex].role == VoiceRole::Melody;
    if (!result.notes.empty()) {
        const auto [lowest, highest] = std::minmax_element(
            result.notes.begin(), result.notes.end(),
            [](const PatternNote& a, const PatternNote& b) { return a.pitch < b.pitch; });
        info.lowest = lowest->pitch;
        info.highest = highest->pitch;
    }
    return result;
}

} // namespace mm::core
