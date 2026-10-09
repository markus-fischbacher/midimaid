#include "core/ChordSymbol.h"
#include "core/PatternGenerator.h"
#include "core/PatternJson.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif
#ifndef MIDIMAID_GOLDEN_DIR
#error "MIDIMAID_GOLDEN_DIR must point to tests/golden"
#endif

// Golden files (SPEC 4.3, 12): the generated patterns of fixed styles, lengths and seeds, bit for bit. A change to a
// generator, a style profile or the random functions changes them on purpose, so the files are renewed deliberately:
//   MIDIMAID_UPDATE_GOLDEN=1 ctest -R golden   (then check the diff and record the reason in DECISIONS.md)
// They have to be identical on macOS, Windows and Linux; the CI compares them on all three.

using namespace mm::core;

namespace {

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    INFO(name << ": " << result.error);
    REQUIRE(result.ok());
    return *result.profile;
}

bool updateRequested() {
#ifdef _WIN32
    char* value = nullptr;
    size_t size = 0;
    const bool set = _dupenv_s(&value, &size, "MIDIMAID_UPDATE_GOLDEN") == 0 && value != nullptr && value[0] == '1';
    std::free(value);
    return set;
#else
    const char* value = std::getenv("MIDIMAID_UPDATE_GOLDEN");
    return value != nullptr && value[0] == '1';
#endif
}

std::string readGolden(const std::string& file) {
    std::ifstream stream(std::string(MIDIMAID_GOLDEN_DIR) + "/" + file, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string text = buffer.str();
    std::string result;
    for (const char c : text) {
        if (c != '\r') {
            result += c;
        }
    }
    return result;
}

void writeGolden(const std::string& file, const std::string& text) {
    std::ofstream stream(std::string(MIDIMAID_GOLDEN_DIR) + "/" + file, std::ios::binary | std::ios::trunc);
    stream << text;
}

/// FNV-1a, 64 bit.
std::string fnv1a(const std::string& text) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::string roleName(PhraseRole role) {
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
    return "?";
}

/// A readable header plus the hash of the whole pattern JSON.
std::string describe(const Pattern& p, const GenerationRequest& request) {
    std::ostringstream out;
    out << "pattern " << p.styleId << " bars=" << p.lengthBars << " seed=" << request.seed
        << " winner=" << p.info.winnerSeed << " score=" << static_cast<int>(p.qualityScore) << "\n";
    out << "  key root=" << static_cast<int>(p.context.root) << " scale=" << p.context.scaleId
        << " kick=" << p.kickGridId << "\n";
    out << "  chords";
    for (const ChordEvent& event : p.context.progression) {
        out << " " << formatChordSymbol(event.chord) << "@" << event.startHalfBar << "+" << event.lengthHalfBars;
    }
    out << "\n  phrases";
    for (const Phrase& phrase : p.phrases) {
        out << " " << phrase.startBar << "+" << phrase.lengthBars << ":" << roleName(phrase.role);
        if (phrase.kickGridId.has_value()) {
            out << ":" << *phrase.kickGridId;
        }
    }
    out << "\n";
    for (const Track& track : p.voices) {
        uint64_t velocity = 0;
        for (const Note& note : track.notes) {
            velocity += note.velocity;
        }
        out << "  voice " << (track.role == VoiceRole::Bass ? "bass" : "melody") << " archetype=" << track.archetypeId
            << " notes=" << track.notes.size() << " velocitySum=" << velocity << "\n";
    }
    out << "  hash " << fnv1a(patternToString(p)) << "\n";
    return out.str();
}

std::string firstDifference(const std::string& expected, const std::string& actual) {
    std::istringstream a(expected);
    std::istringstream b(actual);
    std::string lineA;
    std::string lineB;
    int line = 1;
    while (true) {
        const bool okA = static_cast<bool>(std::getline(a, lineA));
        const bool okB = static_cast<bool>(std::getline(b, lineB));
        if (!okA && !okB) {
            return "no difference";
        }
        if (!okA || !okB || lineA != lineB) {
            return "line " + std::to_string(line) + "\n  golden: " + (okA ? lineA : "<end>") +
                   "\n  actual: " + (okB ? lineB : "<end>");
        }
        ++line;
    }
}

void compareWithGolden(const std::string& file, const std::string& actual) {
    if (updateRequested()) {
        writeGolden(file, actual);
        WARN("golden file renewed: " << file);
        return;
    }
    const std::string expected = readGolden(file);
    INFO(file << ": first difference at " << firstDifference(expected, actual));
    INFO("run with MIDIMAID_UPDATE_GOLDEN=1 to renew the file if the change is intended");
    CHECK(actual == expected);
}

const std::vector<std::string> kStyles{"peak_time", "melodic_techno", "hard_industrial"};

} // namespace

TEST_CASE("golden: generated patterns per style are bit-identical on every platform", "[golden]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        std::string text;
        for (const uint32_t bars : {1u, 2u, 4u, 8u, 16u}) {
            for (const uint64_t seed : {1ull, 2ull}) {
                GenerationRequest request;
                request.lengthBars = bars;
                request.seed = seed;
                const SelectionResult result = generatePattern(style, request);
                INFO(name << " bars " << bars << " seed " << seed);
                REQUIRE(result.success);
                text += describe(result.pattern, request);
            }
        }
        compareWithGolden(name + ".golden", text);
    }
}

TEST_CASE("golden: one complete pattern per style as JSON for review", "[golden]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        GenerationRequest request;
        request.lengthBars = 2;
        request.seed = 1;
        const SelectionResult result = generatePattern(style, request);
        REQUIRE(result.success);
        compareWithGolden(name + "_2bars.json", patternToString(result.pattern) + "\n");
    }
}

TEST_CASE("golden: the files describe what the generator produces now, not an older run", "[golden]") {
    // a second generation from the same request gives the same text (no hidden state between runs)
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request;
    request.lengthBars = 8;
    request.seed = 2;
    const SelectionResult a = generatePattern(style, request);
    const SelectionResult b = generatePattern(style, request);
    REQUIRE(a.success);
    REQUIRE(b.success);
    CHECK(describe(a.pattern, request) == describe(b.pattern, request));
    CHECK(fnv1a("") == "cbf29ce484222325");
    CHECK(fnv1a("a") == "af63dc4c8601ec8c");
}
