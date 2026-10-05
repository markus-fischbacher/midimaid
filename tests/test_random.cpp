#include "core/Random.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

using namespace mm::core;

// Golden values were produced with an independent Python implementation of PCG32 and SplitMix64 and match the
// published reference vectors. They must be identical on every platform and standard library (SPEC 4.3).

TEST_CASE("pcg32 matches the reference vectors", "[core][random]") {
    Pcg32 rng(42, 54); // pcg32-demo: pcg32_srandom_r(&rng, 42u, 54u)
    const uint32_t expected[] = {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e};
    for (const uint32_t value : expected) {
        CHECK(rng.next() == value);
    }
}

TEST_CASE("splitmix64 matches the reference vectors", "[core][random]") {
    uint64_t state = 0;
    CHECK(splitMix64(state) == 0xe220a8397b1dcdafULL);
    CHECK(splitMix64(state) == 0x6e789e6aa1b965f4ULL);
    CHECK(splitMix64(state) == 0x06c45d188009454fULL);
}

TEST_CASE("fromSeed is deterministic and seed dependent", "[core][random]") {
    auto a = Pcg32::fromSeed(12345);
    const uint32_t expected[] = {0x6ccdf691, 0x6d3fe79d, 0x3b35083d, 0x67b85189, 0xc5a0a639};
    for (const uint32_t value : expected) {
        CHECK(a.next() == value);
    }

    auto b = Pcg32::fromSeed(12345);
    auto c = Pcg32::fromSeed(12346);
    CHECK(b.next() == 0x6ccdf691);
    CHECK(c.next() != 0x6ccdf691);
}

TEST_CASE("a copy continues exactly like the original", "[core][random]") {
    auto rng = Pcg32::fromSeed(7);
    rng.next();
    auto copy = rng;
    for (int i = 0; i < 100; ++i) {
        CHECK(rng.next() == copy.next());
    }
}

TEST_CASE("deriveSeed golden values", "[core][random]") {
    CHECK(deriveSeed(2026, 0) == 0x78bc927ded35455dULL);
    CHECK(deriveSeed(2026, 1) == 0xaad71e75cde2b88eULL);
    CHECK(deriveSeed(2026, 2) == 0x6280938ad5a104f2ULL);
    CHECK(deriveSeed(2026, 3) == 0xcaa69c1e0798ff49ULL);
    CHECK(deriveSeed(0, 0) == 0x6e789e6aa1b965f4ULL);
}

TEST_CASE("bounded golden values", "[core][random]") {
    auto small = Pcg32::fromSeed(12345);
    const uint32_t expectedSmall[] = {2, 0, 6, 1, 6, 5, 5, 1, 5, 2};
    for (const uint32_t value : expectedSmall) {
        CHECK(small.bounded(7) == value);
    }
    auto large = Pcg32::fromSeed(12345); // bound close to 2^30: exercises the rejection threshold
    const uint32_t expectedLarge[] = {825437322, 832904598, 993331261, 740132738};
    for (const uint32_t value : expectedLarge) {
        CHECK(large.bounded(1000000007) == value);
    }
}

TEST_CASE("bounded rejects the biased part of the range", "[core][random]") {
    // Bound 3e9: about 30 % of the raw draws fall below the threshold and must be rejected (8 times in this
    // sequence). Without the rejection step these values change.
    auto rng = Pcg32::fromSeed(555);
    const uint32_t expected[] = {697218063,  1394970996, 2626610638, 2877761090, 1337891724, 2241307293,
                                 1608650744, 633797551,  1784777807, 1026101613, 2866159899, 1136768057};
    for (const uint32_t value : expected) {
        CHECK(rng.bounded(3000000000u) == value);
    }
}

TEST_CASE("bounded edge cases", "[core][random]") {
    auto rng = Pcg32::fromSeed(1);
    CHECK(rng.bounded(0) == 0);
    for (int i = 0; i < 100; ++i) {
        CHECK(rng.bounded(1) == 0);
    }
    for (int i = 0; i < 1000; ++i) {
        CHECK(rng.bounded(std::numeric_limits<uint32_t>::max()) < std::numeric_limits<uint32_t>::max());
    }
}

TEST_CASE("bounded is uniform for small bounds (no modulo bias)", "[core][random]") {
    for (const uint32_t bound : {3u, 6u, 7u, 10u}) {
        auto rng = Pcg32::fromSeed(bound);
        std::vector<int> counts(bound, 0);
        const int draws = 60000;
        for (int i = 0; i < draws; ++i) {
            ++counts[rng.bounded(bound)];
        }
        const double expected = static_cast<double>(draws) / bound;
        for (const int count : counts) {
            CHECK(count > expected * 0.94);
            CHECK(count < expected * 1.06);
        }
    }
}

TEST_CASE("range golden values and bounds", "[core][random]") {
    auto rng = Pcg32::fromSeed(777);
    const int32_t expected[] = {0, 1, -1, -4, 5, 3, 1, -5, -3, 3, -3, -2};
    for (const int32_t value : expected) {
        CHECK(rng.range(-5, 5) == value);
    }

    auto other = Pcg32::fromSeed(5);
    for (int i = 0; i < 1000; ++i) {
        const int32_t value = other.range(10, 12);
        CHECK(value >= 10);
        CHECK(value <= 12);
    }
    CHECK(other.range(4, 4) == 4);
    CHECK(other.range(9, 3) == 9); // hi < lo returns lo
}

TEST_CASE("range over the full int32 range", "[core][random]") {
    auto a = Pcg32::fromSeed(31);
    auto b = Pcg32::fromSeed(31);
    for (int i = 0; i < 100; ++i) {
        const int32_t value = a.range(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
        CHECK(value == static_cast<int32_t>(b.next() + 0x80000000u));
    }
}

TEST_CASE("chance golden values and edges", "[core][random]") {
    auto rng = Pcg32::fromSeed(777);
    const int expected[] = {0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1};
    for (const int value : expected) {
        CHECK(static_cast<int>(rng.chance(30)) == value);
    }
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(rng.chance(0));
        CHECK_FALSE(rng.chance(-5));
        CHECK(rng.chance(100));
        CHECK(rng.chance(250));
    }
}

TEST_CASE("chance frequency follows the percentage", "[core][random]") {
    auto rng = Pcg32::fromSeed(4);
    int hits = 0;
    for (int i = 0; i < 100000; ++i) {
        hits += rng.chance(25) ? 1 : 0;
    }
    CHECK(hits > 24000);
    CHECK(hits < 26000);
}

TEST_CASE("weightedIndex golden values", "[core][random]") {
    auto rng = Pcg32::fromSeed(99);
    const std::array<uint32_t, 5> weights{4, 0, 3, 3, 1};
    const size_t expected[] = {3, 3, 2, 4, 3, 2, 3, 0, 2, 3, 0, 2, 4, 2, 0, 2};
    for (const size_t value : expected) {
        CHECK(rng.weightedIndex(weights) == value);
    }
}

TEST_CASE("weightedIndex follows the weights and never picks zero weights", "[core][random]") {
    auto rng = Pcg32::fromSeed(8);
    const std::array<uint32_t, 4> weights{1, 0, 2, 7};
    std::array<int, 4> counts{};
    const int draws = 100000;
    for (int i = 0; i < draws; ++i) {
        ++counts[rng.weightedIndex(weights)];
    }
    CHECK(counts[1] == 0);
    CHECK(counts[0] > 9000);
    CHECK(counts[0] < 11000);
    CHECK(counts[2] > 19000);
    CHECK(counts[2] < 21000);
    CHECK(counts[3] > 69000);
    CHECK(counts[3] < 71000);
}

TEST_CASE("weightedIndex edge cases", "[core][random]") {
    auto rng = Pcg32::fromSeed(2);
    const std::array<uint32_t, 1> single{5};
    CHECK(rng.weightedIndex(single) == 0);

    const std::array<uint32_t, 3> zeros{0, 0, 0};
    CHECK(rng.weightedIndex(zeros) == 3);
    CHECK(rng.weightedIndex(std::span<const uint32_t>{}) == 0);

    const std::array<uint32_t, 2> overflow{0xffffffffu, 1u};
    CHECK(rng.weightedIndex(overflow) == 2);

    const std::array<uint32_t, 3> lastOnly{0, 0, 9};
    for (int i = 0; i < 100; ++i) {
        CHECK(rng.weightedIndex(lastOnly) == 2);
    }
}

TEST_CASE("shuffle golden value and permutation property", "[core][random]") {
    std::vector<int> values(10);
    std::iota(values.begin(), values.end(), 0);
    auto rng = Pcg32::fromSeed(2026);
    rng.shuffle(values.begin(), values.end());
    CHECK(values == std::vector<int>{9, 6, 4, 0, 1, 8, 3, 7, 5, 2});

    for (uint64_t seed = 0; seed < 200; ++seed) {
        std::vector<int> items(17);
        std::iota(items.begin(), items.end(), 100);
        auto r = Pcg32::fromSeed(seed);
        r.shuffle(items.begin(), items.end());
        std::sort(items.begin(), items.end());
        std::vector<int> sorted(17);
        std::iota(sorted.begin(), sorted.end(), 100);
        CHECK(items == sorted);
    }
}

TEST_CASE("shuffle edge cases", "[core][random]") {
    auto rng = Pcg32::fromSeed(1);
    std::vector<int> empty;
    rng.shuffle(empty.begin(), empty.end());
    CHECK(empty.empty());

    std::vector<int> one{42};
    rng.shuffle(one.begin(), one.end());
    CHECK(one == std::vector<int>{42});
}
