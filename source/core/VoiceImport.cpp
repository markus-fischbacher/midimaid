#include "core/VoiceImport.h"

#include "core/Analysis.h"
#include "core/FormPlan.h"
#include "core/Groove.h"
#include "core/Random.h"

#include <algorithm>

namespace mm::core {

namespace {

constexpr uint64_t kImportFormSeed = 0x494d504f5254ULL; ///< the form plan of an imported pattern is always the same

std::vector<MidiClipNote> clipNotesOf(const Track& track) {
    std::vector<MidiClipNote> notes;
    for (const Note& note : track.notes) {
        MidiClipNote clip;
        clip.pitch = note.pitch;
        clip.velocity = note.velocity;
        clip.startTick = note.startTick;
        clip.lengthTicks = note.lengthTicks;
        notes.push_back(clip);
    }
    return notes;
}

AnalysisSettings analysisSettingsOf(const StyleProfile& style) {
    AnalysisSettings settings;
    for (const WeightedId& scale : style.scales) {
        settings.candidateScales.push_back(scale.id);
        settings.scaleWeights.push_back(scale.weight);
    }
    return settings;
}

void setProgression(Pattern& pattern, const std::vector<ChordGuess>& guesses) {
    pattern.context.progression.clear();
    for (const ChordGuess& guess : guesses) {
        ChordEvent event;
        event.chord = guess.chord;
        event.startHalfBar = guess.startHalfBar;
        event.lengthHalfBars = guess.lengthHalfBars;
        pattern.context.progression.push_back(event);
    }
}

} // namespace

std::optional<Pattern> importVoice(const StyleProfile& style, const std::optional<Pattern>& current, size_t voice,
                                   const ImportPlan& plan) {
    if (plan.status != ImportStatus::Ok || plan.notes.empty() || !isValidPatternLength(plan.lengthBars)) {
        return std::nullopt;
    }
    const bool sameLength = current && current->lengthBars == plan.lengthBars;
    Pattern pattern;
    if (sameLength) {
        pattern = *current;
    } else {
        pattern = makeEmptyPattern(plan.lengthBars, style.id);
        if (current) {
            pattern.voices = current->voices;
            for (Track& track : pattern.voices) {
                track.lock = {};
            }
            pattern.nextNoteId = current->nextNoteId;
            pattern.styleId = current->styleId;
            pattern.voicing = current->voicing;
        }
        pattern.kickGridId = style.kickDefault;
        applyStyleGroove(pattern, style);
        Pcg32 rng = Pcg32::fromSeed(kImportFormSeed);
        pattern.phrases = generateFormPlan(style, plan.lengthBars, rng);
    }
    if (voice >= pattern.voices.size()) {
        return std::nullopt;
    }
    // The other voices do not fit the new harmony: they are generated again around the imported ones (the voice that
    // is imported is cleared below).
    for (size_t i = 0; i < pattern.voices.size(); ++i) {
        if (!isVoiceLocked(pattern.voices[i])) {
            pattern.voices[i].notes.clear();
        }
    }

    Track& track = pattern.voices[voice];
    track.notes.clear();
    for (const MidiClipNote& source : plan.notes) {
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = std::min<uint8_t>(source.pitch, 127);
        note.velocity = std::clamp<uint8_t>(source.velocity, 1, 127);
        note.startTick = source.startTick;
        note.lengthTicks = std::max<uint32_t>(source.lengthTicks, 1);
        track.notes.push_back(note);
    }
    track.archetypeId.clear();
    track.archetypeAuto = true;
    track.octaveOffset = 0;
    track.groove = GrooveSettings{0.5f, "", 0.0f}; // the groove of the producer is in the ticks
    track.lock.pitch = track.lock.rhythm = track.lock.velocity = true;

    std::vector<std::vector<MidiClipNote>> tracks;
    for (const Track& each : pattern.voices) {
        if (isVoiceLocked(each) && !each.notes.empty()) {
            tracks.push_back(clipNotesOf(each));
        }
    }
    const AnalysisSettings settings = analysisSettingsOf(style);
    const KeyEstimate key = estimateKeyOf(tracks, settings);
    pattern.context.root = key.root;
    pattern.context.scaleId = key.scaleId;
    setProgression(pattern, estimateProgressionOf(tracks, pattern.lengthBars, key));

    pattern.qualityScore = 0;
    pattern.info.source = "import";
    pattern.info.prompt.clear();
    pattern.info.rawResponse.clear();
    return pattern;
}

bool correctImportedKey(Pattern& pattern, PitchClass root, const std::string& scaleId) {
    if (findScale(scaleId) == nullptr || root > 11) {
        return false;
    }
    std::vector<std::vector<MidiClipNote>> tracks;
    for (const Track& track : pattern.voices) {
        if (isVoiceLocked(track) && !track.notes.empty()) {
            tracks.push_back(clipNotesOf(track));
        }
    }
    if (tracks.empty()) {
        return false;
    }
    KeyEstimate key;
    key.root = root;
    key.scaleId = scaleId;
    key.confidence = 1;
    pattern.context.root = root;
    pattern.context.scaleId = scaleId;
    setProgression(pattern, estimateProgressionOf(tracks, pattern.lengthBars, key));
    return true;
}

} // namespace mm::core
