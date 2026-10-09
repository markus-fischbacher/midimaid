#include "core/Pattern.h"

#include <algorithm>
#include <utility>

namespace mm::core {

bool hasLockedVoice(const Pattern& pattern) {
    return std::any_of(pattern.voices.begin(), pattern.voices.end(), [](const Track& t) { return isVoiceLocked(t); });
}

bool isValidPatternLength(uint32_t bars) {
    return bars == 1 || bars == 2 || bars == 4 || bars == 8 || bars == 16;
}

Pattern makeEmptyPattern(uint32_t lengthBars, std::string styleId) {
    Pattern pattern;
    pattern.lengthBars = lengthBars;
    pattern.styleId = std::move(styleId);

    Phrase phrase;
    phrase.startBar = 0;
    phrase.lengthBars = lengthBars;
    phrase.role = PhraseRole::Main;
    pattern.phrases.push_back(phrase);

    pattern.context.root = 9;
    pattern.context.scaleId = "natural_minor";
    ChordEvent chord;
    chord.chord = Chord{0, ChordQuality::Minor};
    chord.startHalfBar = 0;
    chord.lengthHalfBars = lengthBars * 2;
    pattern.context.progression.push_back(chord);

    Track bass;
    bass.role = VoiceRole::Bass;
    bass.midiChannel = 1;
    Track melody;
    melody.role = VoiceRole::Melody;
    melody.midiChannel = 2;
    pattern.voices.push_back(std::move(bass));
    pattern.voices.push_back(std::move(melody));
    return pattern;
}

uint32_t allocateNoteId(Pattern& pattern) {
    return pattern.nextNoteId++;
}

} // namespace mm::core
