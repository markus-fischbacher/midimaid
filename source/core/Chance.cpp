#include "core/Chance.h"

#include "core/Random.h"

#include <algorithm>
#include <vector>

namespace mm::core {

namespace {

constexpr uint64_t kChanceStream = 0x63686e63ULL; // "chnc"

bool offSixteenth(const Note& note) {
    return note.startTick % 480 == 240;
}

PhraseRole roleAtBar(const Pattern& pattern, uint32_t bar) {
    for (const Phrase& phrase : pattern.phrases) {
        if (bar >= phrase.startBar && bar < phrase.startBar + phrase.lengthBars) {
            return phrase.role;
        }
    }
    return PhraseRole::Main;
}

bool plain(const Note& note) {
    return note.chance == 100 && note.condA == 1 && note.condB == 1 && !note.lock.rhythm;
}

} // namespace

int chanceBudgetPct(int energyPct) {
    return 4 + std::clamp(energyPct, 0, 100) / 10;
}

void applyChances(Pattern& pattern, size_t voice, const StyleProfile& style, int energyPct, uint64_t seed,
                  uint32_t fromBar, uint32_t toBar) {
    if (voice >= pattern.voices.size() || pattern.voices[voice].lock.rhythm) {
        return;
    }
    Track& track = pattern.voices[voice];
    const uint32_t from = fromBar * kTicksPerBar;
    const uint32_t to = toBar * kTicksPerBar;
    const bool bass = track.role == VoiceRole::Bass;
    const bool hard = style.id == "hard_industrial";
    const bool melodic = style.id == "melodic_techno";

    // the candidates and what they get
    struct Pick {
        int low = 100;
        int high = 100;
        uint8_t condA = 1;
        uint8_t condB = 1;
    };
    Pick pick;
    std::vector<size_t> candidates;
    size_t inWindow = 0;
    size_t already = 0;
    for (size_t i = 0; i < track.notes.size(); ++i) {
        const Note& note = track.notes[i];
        if (note.startTick < from || note.startTick >= to) {
            continue;
        }
        ++inWindow;
        if (!plain(note)) {
            already += note.lock.rhythm ? 0 : 1;
            continue;
        }
        bool wanted = false;
        if (melodic) {
            wanted = !bass && offSixteenth(note) && !note.accent && note.velocity < 100 &&
                     roleAtBar(pattern, note.startTick / kTicksPerBar) == PhraseRole::Answer;
        } else if (hard) {
            wanted = (note.ratchet > 1) || (!bass && track.archetypeId == "sparse_hits");
        } else {
            wanted = bass ? (offSixteenth(note) && note.velocity < 80) : (offSixteenth(note) && !note.accent && note.velocity < 100);
        }
        if (wanted) {
            candidates.push_back(i);
        }
    }
    if (melodic) {
        pick = {100, 100, 2, 2};
    } else if (hard) {
        pick = {40, 70, 1, 1};
    } else {
        pick = bass ? Pick{50, 75, 1, 1} : Pick{60, 80, 1, 1};
    }
    Pcg32 rng = Pcg32::fromSeed(deriveSeed(seed, kChanceStream + voice));
    // draws are made for every candidate, so that the list never shifts the values of the next ones
    std::vector<int> values;
    for (size_t i = 0; i < candidates.size(); ++i) {
        values.push_back(rng.range(pick.low, pick.high));
    }
    rng.shuffle(candidates.begin(), candidates.end());
    const size_t budget = inWindow * static_cast<size_t>(chanceBudgetPct(energyPct)) / 100;
    const size_t room = budget > already ? budget - already : 0;
    for (size_t k = 0; k < candidates.size() && k < room; ++k) {
        Note& note = track.notes[candidates[k]];
        note.chance = static_cast<uint8_t>(values[k]);
        note.condA = pick.condA;
        note.condB = pick.condB;
    }
}

} // namespace mm::core
