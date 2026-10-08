#include "core/PlaybackRender.h"

#include "core/OutputStage.h"
#include "core/Register.h"

#include <algorithm>

namespace mm::core {

PlaybackNotes renderVoiceForPlayback(const Pattern& pattern, const StyleProfile& style, size_t voiceIndex) {
    OutputSettings settings;
    settings.kickClearanceTicks = style.bass.kickClearanceTicks;
    for (const Track& track : pattern.voices) {
        settings.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
    }
    const OutputPattern out = renderOutput(pattern, settings);

    PlaybackNotes result;
    result.lengthTicks = out.lengthTicks;
    if (voiceIndex >= out.voices.size()) {
        return result;
    }
    for (const OutputNote& note : out.voices[voiceIndex].notes) {
        const int32_t start = std::max<int32_t>(note.startTick, 0);
        const int32_t end = std::min<int32_t>(note.endTick, static_cast<int32_t>(out.lengthTicks));
        if (end > start) {
            result.notes.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(end - start), note.channel,
                                    note.pitch, note.velocity});
        }
    }
    std::stable_sort(result.notes.begin(), result.notes.end(),
                     [](const PatternNote& a, const PatternNote& b) { return a.startTick < b.startTick; });
    return result;
}

} // namespace mm::core
