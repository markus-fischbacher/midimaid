#include "ai/PhraseShaping.h"

#include "core/FormPlan.h"
#include "core/Variation.h"
#include "core/VelocityContour.h"

#include <algorithm>
#include <set>

namespace mm::ai {

namespace {

using namespace mm::core;

uint8_t clampVelocity(int value) {
    return static_cast<uint8_t>(std::clamp(value, 1, 127));
}

void varyPhrase(Pattern& pattern, const Phrase& phrase, size_t index, const StyleProfile& style,
                const ArchetypeSettings& settings, int strength, std::optional<size_t> voice) {
    Pattern sub = extractPhrase(pattern, phrase);
    const Pattern source = sub;
    VariationRequest request;
    request.seed =
        0x4d4f544946ull + index * 7919u + phrase.startBar; // fixed per phrase: the same motif, the same result
    request.strengthPct = strength;
    request.voice = voice;
    if (applyVariation(sub, style, settings, request) > 0) {
        if (voice) { // the constraint layer of the variation also reaches the other voices: they stay as they were
            for (size_t i = 0; i < sub.voices.size(); ++i) {
                if (i != *voice) {
                    sub.voices[i].notes = source.voices[i].notes;
                }
            }
        }
        replacePhraseNotes(pattern, phrase, sub);
    }
}

void build(Pattern& pattern, const Phrase& phrase) {
    const uint32_t from = phrase.startBar * kTicksPerBar;
    const uint32_t length = phrase.lengthBars * kTicksPerBar;
    const uint32_t lastBar = from + length - kTicksPerBar;
    for (auto& track : pattern.voices) {
        for (auto& note : track.notes) {
            if (note.startTick < from || note.startTick >= from + length) {
                continue;
            }
            const int rise =
                static_cast<int>((static_cast<uint64_t>(note.startTick - from) * kBuildVelocityRise) / length);
            note.velocity = clampVelocity(note.velocity + rise);
            const uint32_t step = (note.startTick % kTicksPerBar) / kTicksPerStep;
            if (note.startTick >= lastBar && step % 4 == 2) {
                note.accent = true;
            }
        }
    }
}

void breakdown(Pattern& pattern, const Phrase& phrase) {
    const uint32_t from = phrase.startBar * kTicksPerBar;
    const uint32_t to = from + phrase.lengthBars * kTicksPerBar;
    for (auto& track : pattern.voices) {
        std::vector<Note> kept;
        for (uint32_t bar = phrase.startBar; bar < phrase.startBar + phrase.lengthBars; ++bar) {
            std::set<uint32_t> steps; // the distinct starts of the bar, in order: chords count once
            for (const auto& note : track.notes) {
                if (note.startTick / kTicksPerBar == bar) {
                    steps.insert(note.startTick);
                }
            }
            std::set<uint32_t> dropped;
            size_t position = 0;
            for (const uint32_t start : steps) {
                if (position++ % 2 == 1) {
                    dropped.insert(start);
                }
            }
            for (auto note : track.notes) {
                if (note.startTick / kTicksPerBar != bar || dropped.count(note.startTick) != 0) {
                    continue;
                }
                note.velocity = clampVelocity(note.velocity - kBreakdownVelocityDrop);
                kept.push_back(note);
            }
        }
        std::vector<Note> outside;
        for (const auto& note : track.notes) {
            if (note.startTick < from || note.startTick >= to) {
                outside.push_back(note);
            }
        }
        outside.insert(outside.end(), kept.begin(), kept.end());
        std::stable_sort(outside.begin(), outside.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        track.notes = std::move(outside);
    }
}

} // namespace

void shapePhrasesByRole(Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings) {
    for (size_t index = 0; index < pattern.phrases.size(); ++index) {
        const Phrase phrase = pattern.phrases[index];
        switch (phrase.role) {
        case PhraseRole::Main:
            break;
        case PhraseRole::Variation:
            varyPhrase(pattern, phrase, index, style, settings, kVariationPhraseStrength, std::nullopt);
            break;
        case PhraseRole::Answer: {
            const auto melody = std::find_if(pattern.voices.begin(), pattern.voices.end(),
                                             [](const Track& track) { return track.role == VoiceRole::Melody; });
            if (melody != pattern.voices.end()) {
                varyPhrase(pattern, phrase, index, style, settings, kAnswerPhraseStrength,
                           static_cast<size_t>(melody - pattern.voices.begin()));
            }
            break;
        }
        case PhraseRole::Build:
            build(pattern, phrase);
            break;
        case PhraseRole::Breakdown:
            breakdown(pattern, phrase);
            break;
        }
    }
}

} // namespace mm::ai
