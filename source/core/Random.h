#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace mm::core {

/// SplitMix64 step (public domain, Vigna). Used for seeding and seed derivation.
uint64_t splitMix64(uint64_t& state);

/// Deterministic seed derived from a base seed and an index (e.g. candidate seeds, further rounds; D-88).
uint64_t deriveSeed(uint64_t baseSeed, uint64_t index);

/// PCG32 (XSH-RR 64/32, O'Neill) plus our own distribution functions (SPEC 4.3).
///
/// Results are bit-identical on every platform and standard library: integer arithmetic only, no
/// `std::*_distribution`, no floating point. A change to any function here changes every generated pattern
/// and therefore needs a new decision and a state migration.
class Pcg32 {
public:
    /// Reference seeding (`pcg32_srandom_r`); mainly for test vectors.
    Pcg32(uint64_t initState, uint64_t initSeq);

    /// Seeds from a single 64-bit seed (the user-visible seed): state and stream come from SplitMix64.
    static Pcg32 fromSeed(uint64_t seed);

    /// Next 32 random bits.
    uint32_t next();

    /// Uniform in [0, bound) without modulo bias. Returns 0 for bound == 0.
    uint32_t bounded(uint32_t bound);

    /// Uniform in [lo, hi] (inclusive). Returns lo if hi < lo.
    int32_t range(int32_t lo, int32_t hi);

    /// True with the given probability in percent (<= 0 never, >= 100 always).
    bool chance(int percent);

    /// Index drawn with probability proportional to its weight; zero weights are never drawn.
    /// Returns weights.size() if the list is empty, all weights are zero or their sum exceeds 2^32 - 1.
    size_t weightedIndex(std::span<const uint32_t> weights);

    /// Fisher-Yates shuffle.
    template <typename RandomIt> void shuffle(RandomIt first, RandomIt last) {
        const auto count = static_cast<size_t>(last - first);
        for (size_t i = count; i > 1; --i) {
            const size_t j = bounded(static_cast<uint32_t>(i));
            using std::swap;
            swap(first[i - 1], first[j]);
        }
    }

private:
    Pcg32() = default;

    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

} // namespace mm::core
