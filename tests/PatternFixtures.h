#pragma once

#include "core/Pattern.h"
#include "core/Random.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mm::fixtures {

using namespace mm::core;

inline Phrase makePhrase(uint32_t startBar, uint32_t lengthBars, PhraseRole role, bool turnaround = false) {
    Phrase phrase;
    phrase.startBar = startBar;
    phrase.lengthBars = lengthBars;
    phrase.role = role;
    phrase.turnaround = turnaround;
    return phrase;
}

// ---------------------------------------------------------------------------------------------------------------
// Random valid patterns for the property test

inline Pattern randomPattern(Pcg32& rng) {
    static const uint32_t lengths[] = {1, 2, 4, 8, 16};
    static const std::vector<std::vector<uint32_t>> partitions8{{8}, {4, 4}};
    static const std::vector<std::vector<uint32_t>> partitions16{{16},      {8, 8},    {8, 4, 4},
                                                                 {4, 4, 8}, {4, 8, 4}, {4, 4, 4, 4}};
    static const char* texts[] = {
        "", "plain", "quote \" backslash \\", "line\nbreak\ttab", "Ünïcödé ♭VI", "{\"a\":[1,2]}"};
    auto text = [&]() { return std::string(texts[rng.bounded(6)]); };

    Pattern p = makeEmptyPattern(lengths[rng.bounded(5)], text());
    if (p.lengthBars == 8 || p.lengthBars == 16) {
        const auto& options = p.lengthBars == 8 ? partitions8 : partitions16;
        const auto& partition = options[rng.bounded(static_cast<uint32_t>(options.size()))];
        p.phrases.clear();
        uint32_t start = 0;
        for (const uint32_t length : partition) {
            Phrase phrase;
            phrase.startBar = start;
            phrase.lengthBars = length;
            phrase.role = static_cast<PhraseRole>(rng.bounded(5));
            phrase.turnaround = length >= 8 && rng.chance(50);
            phrase.locked = rng.chance(30);
            if (rng.chance(30)) {
                phrase.kickGridId = text();
            }
            p.phrases.push_back(phrase);
            start += length;
        }
    }
    p.kickGridId = text();
    if (rng.chance(50)) {
        p.kickRoot = static_cast<PitchClass>(rng.bounded(12));
    }
    p.polymeterPhase = rng.chance(50) ? PolymeterPhase::FreeRunning : PolymeterPhase::RestartAtPattern;
    if (rng.chance(50)) {
        RhythmReference ref;
        ref.bars = static_cast<uint8_t>(1 + rng.bounded(2));
        const uint32_t mask = ref.bars == 1 ? 0xffffu : 0xffffffffu;
        ref.kickSteps = rng.next() & mask;
        ref.hatSteps = rng.next() & mask;
        ref.accentSteps = rng.next() & mask;
        for (size_t i = 0; i < 32; ++i) {
            ref.timingOffsetTicks[i] = static_cast<int16_t>(rng.range(-32768, 32767));
            ref.velocity[i] = static_cast<uint8_t>(rng.bounded(256));
        }
        p.rhythmRef = ref;
    }
    for (uint32_t i = rng.bounded(6); i > 0; --i) {
        p.refineHistory.push_back(text());
    }
    p.context.root = static_cast<PitchClass>(rng.bounded(12));
    const auto scales = allScales();
    p.context.scaleId = std::string(scales[rng.bounded(static_cast<uint32_t>(scales.size()))].id);
    p.context.progression.clear();
    for (uint32_t pos = 0, total = p.lengthBars * 2; pos < total;) {
        ChordEvent event;
        event.chord = {static_cast<uint8_t>(rng.bounded(12)), static_cast<ChordQuality>(rng.bounded(5))};
        event.startHalfBar = pos;
        event.lengthHalfBars = 1 + rng.bounded(std::min<uint32_t>(4, total - pos));
        pos += event.lengthHalfBars;
        p.context.progression.push_back(event);
    }
    p.voicing = {rng.chance(50), static_cast<int>(rng.bounded(60)), 60 + static_cast<int>(rng.bounded(68))};
    p.qualityScore = static_cast<uint8_t>(rng.bounded(101));
    p.version = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
    p.info.source = text();
    p.info.seed = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
    p.info.winnerSeed = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
    p.info.styleProfileVersion = rng.next();
    p.info.creativityPct = static_cast<uint8_t>(rng.bounded(101));
    p.info.energyPct = static_cast<uint8_t>(rng.bounded(101));
    p.info.prompt = text();
    p.info.promptVersion = rng.next();
    p.info.providerId = text();
    p.info.modelId = text();
    p.info.rawResponse = text();
    p.info.referenceSetId = text();
    p.info.createdUnixMs = (static_cast<int64_t>(rng.next()) << 31) - (static_cast<int64_t>(rng.next()) << 20);

    p.voices.clear();
    std::vector<uint8_t> channels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    rng.shuffle(channels.begin(), channels.end());
    const uint32_t voiceCount = 1 + rng.bounded(kMaxVoices);
    const uint32_t endTick = p.lengthBars * kTicksPerBar;
    for (uint32_t v = 0; v < voiceCount; ++v) {
        Track track;
        track.role = rng.chance(50) ? VoiceRole::Bass : VoiceRole::Melody;
        track.midiChannel = channels[v];
        track.archetypeId = text();
        track.archetypeAuto = rng.chance(50);
        track.octaveOffset = static_cast<int8_t>(rng.range(-2, 2));
        track.groove = {0.5f + static_cast<float>(rng.bounded(251)) / 1000.0f, text(),
                        static_cast<float>(rng.bounded(1001)) / 1000.0f};
        track.lock = {rng.chance(50), rng.chance(50), rng.chance(50)};
        track.muted = rng.chance(20);
        for (uint32_t n = rng.bounded(30); n > 0; --n) {
            Note note;
            note.id = allocateNoteId(p);
            note.pitch = static_cast<uint8_t>(rng.bounded(128));
            note.startTick = rng.bounded(endTick - 1);
            note.lengthTicks = 1 + rng.bounded(std::min<uint32_t>(2000, endTick - note.startTick));
            note.velocity = static_cast<uint8_t>(1 + rng.bounded(127));
            note.slide = rng.chance(30);
            note.ratchet = note.slide ? 1 : static_cast<uint8_t>(1 + rng.bounded(4));
            note.chance = static_cast<uint8_t>(rng.bounded(101));
            note.condB = static_cast<uint8_t>(1 + rng.bounded(8));
            note.condA = static_cast<uint8_t>(1 + rng.bounded(note.condB));
            note.accent = rng.chance(30);
            note.lock = {rng.chance(50), rng.chance(50), rng.chance(50)};
            track.notes.push_back(note);
        }
        std::stable_sort(track.notes.begin(), track.notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        p.voices.push_back(std::move(track));
    }
    return p;
}

} // namespace mm::fixtures
