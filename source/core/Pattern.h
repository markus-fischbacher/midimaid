#pragma once

#include "core/Theory.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// Pattern data model of SPEC 5. Patterns are plain values; the engine receives them as immutable objects
/// (edits create a new version).

struct LockFlags {
    bool pitch = false;
    bool rhythm = false;
    bool velocity = false;

    bool operator==(const LockFlags&) const = default;
};

struct Note {
    uint32_t id = 0;        ///< stable within a slot (refine, locks, undo); never reused
    uint8_t pitch = 0;      ///< MIDI 0-127
    uint32_t startTick = 0; ///< 960 PPQ
    uint32_t lengthTicks = 0;
    uint8_t velocity = 100; ///< 1-127, base value without accent
    uint8_t ratchet = 1;    ///< [v1.1] 2-4 repeats within the note length (never with slide); field exists since v1.0
    uint8_t chance = 100;   ///< [v1.1] probability in percent (SPEC 3.19)
    uint8_t condA = 1;      ///< [v1.1] condition A:B, 1:1 = always, B <= 8
    uint8_t condB = 1;
    bool slide = false;
    bool accent = false;
    LockFlags lock;

    bool operator==(const Note&) const = default;
};

enum class VoiceRole { Bass, Melody /* later e.g. Stabs, Arp, Counter */ };

constexpr int kMaxVoices = 8; // architecture limit; v1 uses exactly 2

struct GrooveSettings {
    float swing = 0.5f; ///< 0.50-0.75
    std::string templateId;
    float amount = 1.0f; ///< 0-1

    bool operator==(const GrooveSettings&) const = default;
};

struct VoicingSettings {
    bool chordMemory = false;
    int lowNote = 55;
    int highNote = 79;

    bool operator==(const VoicingSettings&) const = default;
};

struct Track {
    VoiceRole role = VoiceRole::Bass;
    uint8_t midiChannel = 1; ///< 1-16
    std::string archetypeId; ///< chosen or automatically determined archetype
    bool archetypeAuto = true;
    int8_t octaveOffset = 0; ///< -2 ... +2
    GrooveSettings groove;
    LockFlags lock;          ///< locks for the whole voice
    std::vector<Note> notes; ///< sorted by startTick
    bool muted = false;

    bool operator==(const Track&) const = default;
};

struct ChordEvent {
    Chord chord;
    uint32_t startHalfBar = 0;   ///< position in half bars from the pattern start
    uint32_t lengthHalfBars = 2; ///< 1 = half bar, 2 = one bar ...

    bool operator==(const ChordEvent&) const = default;
};

struct HarmonicContext {
    PitchClass root = 9; ///< A
    std::string scaleId = "natural_minor";
    std::vector<ChordEvent> progression; ///< gapless over the pattern, exactly one chord at any time

    bool operator==(const HarmonicContext&) const = default;
};

enum class PhraseRole { Main, Variation, Build, Breakdown, Answer };

struct Phrase {
    uint32_t startBar = 0;
    uint32_t lengthBars = 4; ///< 4, 8 or 16; for 1/2/4-bar patterns the pattern length
    PhraseRole role = PhraseRole::Main;
    std::optional<std::string> kickGridId; ///< overrides Pattern::kickGridId
    bool turnaround = false;               ///< [v1.1] last bar as turnaround (8/16 only)
    bool locked = false;

    bool operator==(const Phrase&) const = default;
};

struct GenerationInfo {
    std::string source;      ///< "algorithm", "ai", "variation", "refine", "import", "edit"
    uint64_t seed = 0;       ///< initial seed
    uint64_t winnerSeed = 0; ///< seed of the chosen candidate (SPEC 4.3)
    uint32_t styleProfileVersion = 0;
    uint8_t creativityPct = 40;
    uint8_t energyPct = 50;
    std::string prompt;
    uint32_t promptVersion = 0; ///< AI only
    std::string providerId;
    std::string modelId;
    std::string rawResponse;    ///< AI only, unchanged
    std::string referenceSetId; ///< empty = no set
    int64_t createdUnixMs = 0;

    bool operator==(const GenerationInfo&) const = default;
};

enum class PolymeterPhase { RestartAtPattern, FreeRunning };

struct RhythmReference { ///< derived from drum MIDI, max. 2 bars = 32 steps
    std::bitset<32> kickSteps;
    std::bitset<32> hatSteps;
    std::bitset<32> accentSteps;
    std::array<int16_t, 32> timingOffsetTicks{};
    std::array<uint8_t, 32> velocity{};
    uint8_t bars = 1;

    bool operator==(const RhythmReference&) const = default;
};

struct Pattern {
    uint32_t lengthBars = 1; ///< 1, 2, 4, 8, 16
    std::vector<Phrase> phrases;
    uint8_t timeSigNum = 4;
    uint8_t timeSigDen = 4; ///< v1: 4/4 only
    std::string styleId;
    std::string kickGridId = "4otf";
    std::optional<PitchClass> kickRoot;
    PolymeterPhase polymeterPhase = PolymeterPhase::RestartAtPattern;
    std::optional<RhythmReference> rhythmRef;
    std::vector<std::string> refineHistory; ///< last 5 refinements (SPEC 3.15)
    HarmonicContext context;
    VoicingSettings voicing;
    std::vector<Track> voices; ///< v1: exactly 2 (bass, melody), never hard-wired
    uint8_t qualityScore = 0;  ///< 0-100
    uint32_t nextNoteId = 1;   ///< counter for note ids of this slot
    uint64_t version = 0;      ///< consecutive per instance, for handover and display
    GenerationInfo info;

    bool operator==(const Pattern&) const = default;
};

/// Ticks per bar in 4/4 at 960 PPQ.
constexpr uint32_t kTicksPerBar = 4 * 960;

/// The valid pattern lengths in bars.
bool isValidPatternLength(uint32_t bars);

/// A valid, empty pattern: A minor, bass (channel 1) and melody (channel 2), one main phrase over the whole length
/// and a single minor chord. Starting point for generators and tests.
Pattern makeEmptyPattern(uint32_t lengthBars, std::string styleId);

/// Hands out the next note id of the pattern (ids only ever increase).
uint32_t allocateNoteId(Pattern& pattern);

} // namespace mm::core
