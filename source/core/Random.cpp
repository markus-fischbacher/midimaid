#include "core/Random.h"

namespace mm::core {

namespace {

constexpr uint64_t kPcgMultiplier = 6364136223846793005ULL;
constexpr uint64_t kGoldenGamma = 0x9e3779b97f4a7c15ULL;

} // namespace

uint64_t splitMix64(uint64_t& state) {
    state += kGoldenGamma;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t deriveSeed(uint64_t baseSeed, uint64_t index) {
    uint64_t state = baseSeed + (index + 1) * kGoldenGamma;
    return splitMix64(state);
}

Pcg32::Pcg32(uint64_t initState, uint64_t initSeq) {
    state_ = 0;
    inc_ = (initSeq << 1u) | 1u;
    next();
    state_ += initState;
    next();
}

Pcg32 Pcg32::fromSeed(uint64_t seed) {
    uint64_t sm = seed;
    const uint64_t initState = splitMix64(sm);
    const uint64_t initSeq = splitMix64(sm);
    return Pcg32(initState, initSeq);
}

uint32_t Pcg32::next() {
    const uint64_t old = state_;
    state_ = old * kPcgMultiplier + inc_;
    const auto xorShifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<uint32_t>(old >> 59u);
    return (xorShifted >> rot) | (xorShifted << ((0u - rot) & 31u));
}

uint32_t Pcg32::bounded(uint32_t bound) {
    if (bound == 0) {
        return 0;
    }
    // Reject the biased low part of the 32-bit range (PCG reference, pcg32_boundedrand_r).
    const uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const uint32_t r = next();
        if (r >= threshold) {
            return r % bound;
        }
    }
}

int32_t Pcg32::range(int32_t lo, int32_t hi) {
    if (hi < lo) {
        return lo;
    }
    const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(hi) - static_cast<int64_t>(lo)) + 1u;
    if (span > 0xffffffffULL) { // the full int32 range
        return static_cast<int32_t>(static_cast<uint32_t>(next()) + static_cast<uint32_t>(lo));
    }
    return static_cast<int32_t>(static_cast<int64_t>(lo) + static_cast<int64_t>(bounded(static_cast<uint32_t>(span))));
}

bool Pcg32::chance(int percent) {
    if (percent <= 0) {
        return false;
    }
    if (percent >= 100) {
        return true;
    }
    return bounded(100) < static_cast<uint32_t>(percent);
}

size_t Pcg32::weightedIndex(std::span<const uint32_t> weights) {
    uint64_t total = 0;
    for (const uint32_t weight : weights) {
        total += weight;
    }
    if (total == 0 || total > 0xffffffffULL) {
        return weights.size();
    }
    uint32_t pick = bounded(static_cast<uint32_t>(total));
    for (size_t i = 0; i < weights.size(); ++i) {
        if (pick < weights[i]) {
            return i;
        }
        pick -= weights[i];
    }
    return weights.size(); // unreachable
}

} // namespace mm::core
