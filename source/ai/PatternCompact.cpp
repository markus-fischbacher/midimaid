#include "ai/PatternCompact.h"

#include "core/ChordSymbol.h"
#include "core/Register.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace mm::ai {

namespace {

using nlohmann::json;
using namespace mm::core;

constexpr uint32_t kTicksPerStep = kTicksPerBar / 16;

struct Spelling {
    int degree = 1;
    int alt = 0;
    int octave = 0;
};

int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

/// The degree, alteration and octave that `degreeToMidi` turns back into `pitch` (alteration 0 where the pitch is a
/// tone of the scale, else +1 above the nearest lower degree, or -1 below the next one).
std::optional<Spelling> spell(const Scale& scale, int base, int pitch) {
    const int relative = pitch - base;
    Spelling spelling;
    spelling.octave = floorDiv(relative, 12);
    const int inOctave = relative - 12 * spelling.octave;
    bool found = false;
    for (size_t i = 0; i < scale.size() && !found; ++i) {
        if (scale.intervals[i] == inOctave) {
            spelling.degree = static_cast<int>(i) + 1;
            found = true;
        }
    }
    for (size_t i = 0; i < scale.size() && !found; ++i) {
        if (inOctave - scale.intervals[i] == 1) {
            spelling.degree = static_cast<int>(i) + 1;
            spelling.alt = 1;
            found = true;
        }
    }
    for (size_t i = 0; i < scale.size() && !found; ++i) {
        if (inOctave - scale.intervals[i] == -1) {
            spelling.degree = static_cast<int>(i) + 1;
            spelling.alt = -1;
            found = true;
        }
    }
    if (!found || degreeToMidi(scale, base, spelling.degree, spelling.alt, spelling.octave) != pitch) {
        return std::nullopt;
    }
    return spelling;
}

const char* phraseRoleName(PhraseRole role) {
    switch (role) {
    case PhraseRole::Main:
        return "main";
    case PhraseRole::Variation:
        return "variation";
    case PhraseRole::Build:
        return "build";
    case PhraseRole::Breakdown:
        return "breakdown";
    case PhraseRole::Answer:
        return "answer";
    }
    return "main";
}

/// The chord that sounds at a half bar.
const ChordEvent* chordAt(const Pattern& pattern, uint32_t halfBar) {
    for (const auto& event : pattern.context.progression) {
        if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
            return &event;
        }
    }
    return nullptr;
}

} // namespace

std::string rootName(PitchClass root) {
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[root % 12];
}

std::optional<std::vector<std::string>> progressionSymbols(const Pattern& pattern) {
    std::vector<std::string> symbols;
    for (uint32_t bar = 0; bar < pattern.lengthBars; ++bar) {
        const ChordEvent* first = chordAt(pattern, bar * 2);
        const ChordEvent* second = chordAt(pattern, bar * 2 + 1);
        if (first == nullptr || second == nullptr) {
            return std::nullopt;
        }
        std::string symbol = formatChordSymbol(first->chord);
        if (!(first->chord == second->chord)) {
            symbol += "|" + formatChordSymbol(second->chord);
        }
        symbols.push_back(std::move(symbol));
    }
    return symbols;
}

std::optional<CompactPattern> patternToSchemaJson(const Pattern& pattern, const StyleProfile* style, bool includeIds) {
    const Scale* scale = findScale(pattern.context.scaleId);
    if (scale == nullptr) {
        return std::nullopt;
    }
    CompactPattern result;
    json root;
    root["schema_version"] = 1;
    root["context"]["root"] = rootName(pattern.context.root);
    root["context"]["scale"] = std::string(scale->id);
    const auto symbols = progressionSymbols(pattern);
    if (!symbols) {
        return std::nullopt;
    }
    json progression = json::array();
    for (const auto& symbol : *symbols) {
        progression.push_back(symbol);
    }
    root["context"]["progression"] = progression;
    json phrases = json::array();
    for (const auto& phrase : pattern.phrases) {
        json entry = {
            {"start_bar", phrase.startBar}, {"bars", phrase.lengthBars}, {"role", phraseRoleName(phrase.role)}};
        if (phrase.turnaround) {
            entry["turnaround"] = true;
        }
        phrases.push_back(entry);
    }
    root["phrases"] = phrases;

    const RegisterProfile registers = style != nullptr ? style->registerProfile() : RegisterProfile{};
    json voices = json::array();
    for (const auto& track : pattern.voices) {
        const auto range = effectiveRange(registers, track.role, track.archetypeId, pattern.voicing, 0);
        if (!range) {
            return std::nullopt;
        }
        const auto base = voiceBase(pattern.context.root, range->low, range->high);
        if (!base) {
            return std::nullopt;
        }
        json voice;
        voice["role"] = track.role == VoiceRole::Bass ? "bass" : "melody";
        if (!track.archetypeId.empty()) {
            voice["archetype"] = track.archetypeId;
        }
        json notes = json::array();
        for (const auto& note : track.notes) {
            const auto spelling = spell(*scale, *base, note.pitch);
            if (!spelling) {
                return std::nullopt;
            }
            const uint32_t step =
                std::min((note.startTick + kTicksPerStep / 2) / kTicksPerStep, pattern.lengthBars * 16 - 1);
            const uint32_t length = std::max<uint32_t>(1, (note.lengthTicks + kTicksPerStep / 2) / kTicksPerStep);
            if (note.startTick % kTicksPerStep != 0 || note.lengthTicks % kTicksPerStep != 0) {
                ++result.roundedNotes;
            }
            json entry;
            if (includeIds) {
                entry["id"] = note.id;
            }
            entry["step"] = step;
            entry["degree"] = spelling->degree;
            if (spelling->alt != 0) {
                entry["alt"] = spelling->alt;
            }
            entry["octave"] = spelling->octave;
            entry["len"] = length;
            entry["vel"] = note.velocity;
            if (note.accent) {
                entry["accent"] = true;
            }
            if (note.slide) {
                entry["slide"] = true;
            }
            notes.push_back(entry);
        }
        voice["notes"] = notes;
        voices.push_back(voice);
    }
    root["voices"] = voices;
    result.json = root.dump();
    return result;
}

} // namespace mm::ai
