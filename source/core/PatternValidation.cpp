#include "core/PatternValidation.h"

#include <algorithm>
#include <set>

namespace mm::core {

namespace {

class Issues {
public:
    void add(const std::string& path, const std::string& message) { list_.push_back(path + ": " + message); }
    std::vector<std::string> take() { return std::move(list_); }

private:
    std::vector<std::string> list_;
};

std::string index(const std::string& base, size_t i) {
    return base + "[" + std::to_string(i) + "]";
}

void checkNote(const Note& note, const std::string& path, uint32_t endTick, Issues& issues) {
    if (note.pitch > 127) {
        issues.add(path + ".pitch", "must be 0-127");
    }
    if (note.velocity < 1 || note.velocity > 127) {
        issues.add(path + ".velocity", "must be 1-127");
    }
    if (note.lengthTicks < 1) {
        issues.add(path + ".lengthTicks", "must be at least 1");
    }
    if (static_cast<uint64_t>(note.startTick) + note.lengthTicks > endTick) {
        issues.add(path, "note reaches beyond the pattern end");
    }
    if (note.ratchet < 1 || note.ratchet > 4) {
        issues.add(path + ".ratchet", "must be 1-4");
    }
    if (note.ratchet > 1 && note.slide) {
        issues.add(path, "a note cannot combine ratchet and slide");
    }
    if (note.chance > 100) {
        issues.add(path + ".chance", "must be 0-100");
    }
    if (note.condB < 1 || note.condB > 8) {
        issues.add(path + ".condB", "must be 1-8");
    }
    if (note.condA < 1 || note.condA > note.condB) {
        issues.add(path + ".condA", "must be between 1 and condB");
    }
}

void checkVoices(const Pattern& pattern, uint32_t endTick, Issues& issues) {
    if (pattern.voices.empty() || pattern.voices.size() > static_cast<size_t>(kMaxVoices)) {
        issues.add("voices", "must contain 1-" + std::to_string(kMaxVoices) + " voices");
    }
    std::set<uint8_t> channels;
    std::set<uint32_t> ids;
    uint32_t maxId = 0;
    for (size_t v = 0; v < pattern.voices.size(); ++v) {
        const Track& track = pattern.voices[v];
        const std::string path = index("voices", v);
        if (track.midiChannel < 1 || track.midiChannel > 16) {
            issues.add(path + ".midiChannel", "must be 1-16");
        } else if (!channels.insert(track.midiChannel).second) {
            issues.add(path + ".midiChannel", "channel is used by another voice");
        }
        if (track.octaveOffset < -2 || track.octaveOffset > 2) {
            issues.add(path + ".octaveOffset", "must be -2 to 2");
        }
        if (!(track.groove.swing >= 0.5f && track.groove.swing <= 0.75f)) {
            issues.add(path + ".groove.swing", "must be 0.50-0.75");
        }
        if (!(track.groove.amount >= 0.0f && track.groove.amount <= 1.0f)) {
            issues.add(path + ".groove.amount", "must be 0-1");
        }
        uint32_t previousStart = 0;
        for (size_t n = 0; n < track.notes.size(); ++n) {
            const Note& note = track.notes[n];
            const std::string notePath = index(path + ".notes", n);
            checkNote(note, notePath, endTick, issues);
            if (note.startTick < previousStart) {
                issues.add(notePath + ".startTick", "notes must be sorted by startTick");
            }
            previousStart = note.startTick;
            if (!ids.insert(note.id).second) {
                issues.add(notePath + ".id", "duplicate note id " + std::to_string(note.id));
            }
            maxId = std::max(maxId, note.id);
        }
    }
    if (pattern.nextNoteId < 1 || pattern.nextNoteId <= maxId) {
        issues.add("nextNoteId", "must be greater than every note id");
    }
}

void checkPhrases(const Pattern& pattern, Issues& issues) {
    if (pattern.phrases.empty()) {
        issues.add("phrases", "the pattern needs at least one phrase");
        return;
    }
    for (size_t i = 0; i < pattern.phrases.size(); ++i) {
        const Phrase& phrase = pattern.phrases[i];
        const std::string path = index("phrases", i);
        if (pattern.lengthBars <= 4) {
            if (pattern.phrases.size() != 1 || phrase.startBar != 0 || phrase.lengthBars != pattern.lengthBars ||
                phrase.role != PhraseRole::Main) {
                issues.add(path, "patterns of 1, 2 or 4 bars have exactly one main phrase of the pattern length");
            }
        } else if (phrase.lengthBars != 4 && phrase.lengthBars != 8 && phrase.lengthBars != 16) {
            issues.add(path + ".lengthBars", "must be 4, 8 or 16");
        }
        if (phrase.turnaround && phrase.lengthBars < 8) {
            issues.add(path + ".turnaround", "only for phrases of 8 or 16 bars");
        }
    }
    if (pattern.lengthBars > 4) {
        std::vector<const Phrase*> sorted;
        for (const Phrase& phrase : pattern.phrases) {
            sorted.push_back(&phrase);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const Phrase* a, const Phrase* b) { return a->startBar < b->startBar; });
        uint32_t expectedStart = 0;
        for (const Phrase* phrase : sorted) {
            if (phrase->startBar != expectedStart) {
                issues.add("phrases", "phrases must cover the pattern without gaps or overlaps");
                return;
            }
            expectedStart += phrase->lengthBars;
        }
        if (expectedStart != pattern.lengthBars) {
            issues.add("phrases", "phrases must cover the pattern without gaps or overlaps");
        }
    }
}

void checkContext(const Pattern& pattern, Issues& issues) {
    const HarmonicContext& context = pattern.context;
    if (context.root > 11) {
        issues.add("context.root", "must be 0-11");
    }
    if (findScale(context.scaleId) == nullptr) {
        issues.add("context.scaleId", "unknown scale '" + context.scaleId + "'");
    }
    if (context.progression.empty()) {
        issues.add("context.progression", "needs at least one chord");
        return;
    }
    uint64_t expectedStart = 0;
    for (size_t i = 0; i < context.progression.size(); ++i) {
        const ChordEvent& event = context.progression[i];
        const std::string path = index("context.progression", i);
        if (event.chord.rootOffset > 11) {
            issues.add(path + ".chord.rootOffset", "must be 0-11");
        }
        if (event.lengthHalfBars < 1) {
            issues.add(path + ".lengthHalfBars", "must be at least 1");
        }
        if (event.startHalfBar != expectedStart) {
            issues.add(path + ".startHalfBar", "the progression must be gapless and in order");
        }
        expectedStart = static_cast<uint64_t>(event.startHalfBar) + event.lengthHalfBars;
    }
    if (expectedStart != static_cast<uint64_t>(pattern.lengthBars) * 2) {
        issues.add("context.progression", "must cover the whole pattern");
    }
}

void checkRhythmReference(const RhythmReference& ref, Issues& issues) {
    if (ref.bars < 1 || ref.bars > 2) {
        issues.add("rhythmRef.bars", "must be 1 or 2");
        return;
    }
    const uint32_t allowed = ref.bars == 1 ? 0x0000ffffu : 0xffffffffu;
    for (const auto* steps : {&ref.kickSteps, &ref.hatSteps, &ref.accentSteps}) {
        if ((static_cast<uint32_t>(steps->to_ulong()) & ~allowed) != 0) {
            issues.add("rhythmRef", "steps beyond the reference length are set");
        }
    }
}

} // namespace

std::vector<std::string> validatePattern(const Pattern& pattern) {
    Issues issues;
    if (!isValidPatternLength(pattern.lengthBars)) {
        issues.add("lengthBars", "must be 1, 2, 4, 8 or 16");
    }
    if (pattern.timeSigNum != 4 || pattern.timeSigDen != 4) {
        issues.add("timeSig", "only 4/4 is supported");
    }
    if (pattern.kickRoot.has_value() && *pattern.kickRoot > 11) {
        issues.add("kickRoot", "must be 0-11");
    }
    if (pattern.refineHistory.size() > 5) {
        issues.add("refineHistory", "holds at most the last 5 refinements");
    }
    if (pattern.qualityScore > 100) {
        issues.add("qualityScore", "must be 0-100");
    }
    if (pattern.info.creativityPct > 100) {
        issues.add("info.creativityPct", "must be 0-100");
    }
    if (pattern.info.energyPct > 100) {
        issues.add("info.energyPct", "must be 0-100");
    }
    if (pattern.voicing.lowNote < 0 || pattern.voicing.highNote > 127 ||
        pattern.voicing.lowNote > pattern.voicing.highNote) {
        issues.add("voicing", "lowNote and highNote must be ordered and within 0-127");
    }
    if (pattern.rhythmRef.has_value()) {
        checkRhythmReference(*pattern.rhythmRef, issues);
    }

    const uint32_t endTick = pattern.lengthBars * kTicksPerBar; // only meaningful with a valid length
    checkVoices(pattern, endTick, issues);
    checkPhrases(pattern, issues);
    checkContext(pattern, issues);
    return issues.take();
}

} // namespace mm::core
