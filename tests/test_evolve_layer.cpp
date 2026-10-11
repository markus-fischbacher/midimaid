#include "core/EvolveLayer.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;

namespace {

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

Pattern sourcePattern(const StyleProfile& style, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    const auto result = generatePattern(style, request);
    REQUIRE(result.success);
    return result.pattern;
}

} // namespace

TEST_CASE("evolve settings are clamped", "[evolve]") {
    CHECK(clampEvolveSettings({0, false, 0}).everyPasses == kEvolveMinPasses);
    CHECK(clampEvolveSettings({99, false, 0}).everyPasses == kEvolveMaxPasses);
    CHECK(clampEvolveSettings({4, false, 0}).strengthPct == 1);
    CHECK(clampEvolveSettings({4, false, 500}).strengthPct == 100);
    const EvolveSettings fine{3, true, 40};
    CHECK(clampEvolveSettings(fine) == fine);
    CHECK(EvolveSettings{}.everyPasses == kDefaultEvolvePasses);
    CHECK(EvolveSettings{}.strengthPct == kDefaultEvolveStrength);
}

TEST_CASE("a started layer holds a copy of the slot as stage 0", "[evolve]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern base = sourcePattern(style, 1);
    EvolveLayer layer;
    CHECK_FALSE(layer.active());
    layer.start(3, base, 7);
    REQUIRE(layer.active());
    CHECK(layer.slot() == 3);
    CHECK(layer.stage() == 0);
    CHECK(layer.current() == base);
    CHECK(layer.base() == base);
    CHECK_FALSE(layer.changed());
    CHECK_FALSE(layer.canBack());
    layer.stop();
    CHECK_FALSE(layer.active());
}

TEST_CASE("every advance makes a new stage and the slot copy stays", "[evolve]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const Pattern base = sourcePattern(style, 2);
    EvolveLayer layer;
    layer.start(0, base, 11);
    const EvolveSettings settings{4, false, 30};
    Pattern before = layer.current();
    for (uint32_t n = 1; n <= 5; ++n) {
        REQUIRE(layer.advance(base, style, {}, settings));
        CHECK(layer.stage() == n);
        CHECK(layer.current() != before);
        CHECK(validatePattern(layer.current()).empty());
        before = layer.current();
    }
    CHECK(layer.base() == base);
    CHECK(layer.changed());
}

TEST_CASE("development varies the stage before, from original varies the slot", "[evolve]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern base = sourcePattern(style, 3);
    EvolveSettings develop{4, false, 25};
    EvolveSettings original{4, true, 25};
    EvolveLayer a;
    EvolveLayer b;
    a.start(0, base, 5);
    b.start(0, base, 5);
    REQUIRE(a.advance(base, style, {}, develop));
    REQUIRE(b.advance(base, style, {}, original));
    CHECK(a.current() == b.current()); // the first stage is the same in both modes
    for (int n = 0; n < 6; ++n) {
        REQUIRE(a.advance(base, style, {}, develop));
        REQUIRE(b.advance(base, style, {}, original));
    }
    // From the original every stage stays within one variation of the slot; development drifts further away.
    const auto distance = [&](const Pattern& p) {
        size_t d = 0;
        for (size_t v = 0; v < p.voices.size(); ++v) {
            const auto& x = base.voices[v].notes;
            const auto& y = p.voices[v].notes;
            for (const Note& n : x) {
                const auto it = std::find_if(y.begin(), y.end(), [&](const Note& m) { return m.id == n.id; });
                d += (it == y.end() || !(*it == n)) ? 1 : 0;
            }
        }
        return d;
    };
    CHECK(distance(a.current()) > distance(b.current()));
}

TEST_CASE("from the original every stage is a different variation of the slot", "[evolve]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern base = sourcePattern(style, 12);
    EvolveLayer layer;
    layer.start(0, base, 21);
    std::vector<Pattern> stages;
    for (int n = 0; n < 4; ++n) {
        REQUIRE(layer.advance(base, style, {}, {4, true, 40}));
        for (const Pattern& earlier : stages) {
            CHECK(layer.current() != earlier);
        }
        stages.push_back(layer.current());
    }
}

TEST_CASE("stages are deterministic for a seed and differ between seeds", "[evolve]") {
    const StyleProfile style = loadShipped("hard_industrial");
    const Pattern base = sourcePattern(style, 4);
    const EvolveSettings settings{4, false, 30};
    EvolveLayer a;
    EvolveLayer b;
    EvolveLayer c;
    a.start(0, base, 9);
    b.start(0, base, 9);
    c.start(0, base, 10);
    for (int n = 0; n < 3; ++n) {
        REQUIRE(a.advance(base, style, {}, settings));
        REQUIRE(b.advance(base, style, {}, settings));
        REQUIRE(c.advance(base, style, {}, settings));
    }
    CHECK(a.current() == b.current());
    CHECK(a.current() != c.current());
}

TEST_CASE("back returns through the last 8 stages and no further", "[evolve]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern base = sourcePattern(style, 5);
    const EvolveSettings settings{4, false, 30};
    EvolveLayer layer;
    layer.start(0, base, 1);
    std::vector<Pattern> seen{layer.current()};
    for (int n = 0; n < 12; ++n) {
        REQUIRE(layer.advance(base, style, {}, settings));
        seen.push_back(layer.current());
    }
    REQUIRE(layer.stage() == 12);
    for (size_t k = 1; k <= kEvolveBackStages; ++k) {
        REQUIRE(layer.canBack());
        REQUIRE(layer.back());
        CHECK(layer.current() == seen[seen.size() - 1 - k]);
    }
    CHECK_FALSE(layer.canBack());
    CHECK_FALSE(layer.back());
    CHECK(layer.stage() == 12); // the counter keeps counting
    // After going back, the next stage continues from the one that sounds.
    const Pattern shown = layer.current();
    REQUIRE(layer.advance(base, style, {}, settings));
    CHECK(layer.stage() == 13);
    CHECK(layer.current() != shown);
    CHECK(layer.canBack());
}

TEST_CASE("locked voices stay on every stage", "[evolve]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = sourcePattern(style, 6);
    REQUIRE(setVoiceLocked(base, 0, true) == 1);
    for (const bool fromOriginal : {false, true}) {
        EvolveLayer layer;
        layer.start(0, base, 3);
        for (int n = 0; n < 6; ++n) {
            REQUIRE(layer.advance(base, style, {}, {4, fromOriginal, 100}));
            CHECK(layer.current().voices[0].notes == base.voices[0].notes);
        }
    }
}

TEST_CASE("nothing to change gives no stage", "[evolve]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = sourcePattern(style, 7);
    for (size_t v = 0; v < base.voices.size(); ++v) {
        REQUIRE(setVoiceLocked(base, v, true) == 1);
    }
    EvolveLayer layer;
    layer.start(0, base, 1);
    CHECK_FALSE(layer.advance(base, style, {}, {}));
    CHECK(layer.stage() == 0);
    CHECK_FALSE(layer.changed());
    EvolveLayer idle;
    CHECK_FALSE(idle.advance(base, style, {}, {}));
}
