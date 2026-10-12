#include "core/Ratchet.h"

#include <algorithm>
#include <vector>

namespace mm::core {

namespace {

constexpr uint64_t kRatchetStream = 0x72617463ULL; // "ratc"
constexpr uint32_t kMinRatchetNoteTicks = 120;

bool allowsRatchets(const std::string& archetypeId) {
    return archetypeId == "acid_siren" || archetypeId == "roll16_aggressive";
}

} // namespace

int ratchetChancePct(const std::string& archetypeId, const std::string& styleId, int energyPct) {
    if (!allowsRatchets(archetypeId)) {
        return 0;
    }
    const int energy = std::clamp(energyPct, 0, 100);
    return styleId == "hard_industrial" ? 40 + energy * 3 / 10 : 20 + energy / 5;
}

int maxRatchetFor(uint32_t lengthTicks, uint32_t minNoteTicks) {
    const uint32_t each = std::max<uint32_t>(minNoteTicks, 1);
    return static_cast<int>(std::clamp<uint32_t>(lengthTicks / each, 1, 4));
}

void applyRatchets(Pattern& pattern, size_t voice, const std::string& styleId, int energyPct, uint64_t seed,
                   uint32_t fromBar, uint32_t toBar) {
    if (voice >= pattern.voices.size()) {
        return;
    }
    Track& track = pattern.voices[voice];
    const int chance = ratchetChancePct(track.archetypeId, styleId, energyPct);
    if (chance <= 0 || track.lock.rhythm) {
        return;
    }
    Pcg32 rng = Pcg32::fromSeed(deriveSeed(seed, kRatchetStream + voice));
    for (uint32_t bar = fromBar; bar < toBar; ++bar) {
        // every bar draws all three values, so that a bar without candidates never shifts the next one
        const bool hit = rng.chance(chance);
        const uint32_t pick = rng.next();
        const uint32_t repeatsDraw = rng.next();
        if (!hit) {
            continue;
        }
        const uint32_t from = bar * kTicksPerBar;
        const uint32_t to = from + kTicksPerBar;
        bool taken = false;
        std::vector<size_t> eligible;
        for (size_t i = 0; i < track.notes.size(); ++i) {
            const Note& note = track.notes[i];
            if (note.startTick < from || note.startTick >= to) {
                continue;
            }
            taken = taken || note.ratchet > 1;
            const bool single = (i == 0 || track.notes[i - 1].startTick != note.startTick) &&
                                (i + 1 >= track.notes.size() || track.notes[i + 1].startTick != note.startTick);
            const bool slideTarget = i > 0 && track.notes[i - 1].slide;
            if (single && !note.slide && !slideTarget && !note.lock.rhythm && note.lengthTicks >= kMinRatchetNoteTicks) {
                eligible.push_back(i);
            }
        }
        if (taken || eligible.empty()) {
            continue;
        }
        Note& note = track.notes[eligible[pick % eligible.size()]];
        const int most = maxRatchetFor(note.lengthTicks);
        if (most >= 2) {
            note.ratchet = static_cast<uint8_t>(2 + repeatsDraw % static_cast<uint32_t>(most - 1));
        }
    }
}

} // namespace mm::core
