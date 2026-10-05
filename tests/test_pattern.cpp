#include "core/Pattern.h"
#include "core/PatternJson.h"
#include "core/PatternValidation.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <vector>

using namespace mm::core;
using nlohmann::json;

namespace {

Phrase makePhrase(uint32_t startBar, uint32_t lengthBars, PhraseRole role, bool turnaround = false) {
    Phrase phrase;
    phrase.startBar = startBar;
    phrase.lengthBars = lengthBars;
    phrase.role = role;
    phrase.turnaround = turnaround;
    return phrase;
}

/// A pattern that sets every field to a non-default value.
Pattern fullPattern() {
    Pattern p = makeEmptyPattern(8, "melodic_techno");
    p.phrases = {{0, 4, PhraseRole::Main, std::nullopt, false, true},
                 {4, 4, PhraseRole::Build, std::string("halftime"), false, false}};
    p.phrases[1].turnaround = false;
    p.kickGridId = "broken_a";
    p.kickRoot = 5;
    p.polymeterPhase = PolymeterPhase::FreeRunning;
    RhythmReference ref;
    ref.kickSteps = 0x11111111u;
    ref.hatSteps = 0xaaaaaaaau;
    ref.accentSteps = 0x00010001u;
    for (size_t i = 0; i < 32; ++i) {
        ref.timingOffsetTicks[i] = static_cast<int16_t>(static_cast<int>(i) * 7 - 100);
        ref.velocity[i] = static_cast<uint8_t>(60 + i);
    }
    ref.bars = 2;
    p.rhythmRef = ref;
    p.refineHistory = {"fewer notes", "more syncopation \"please\"", "Ünï\ncode\\"};
    p.context.root = 4;
    p.context.scaleId = "phrygian_dominant";
    p.context.progression = {{{0, ChordQuality::Minor}, 0, 4},
                             {{8, ChordQuality::Major}, 4, 6},
                             {{1, ChordQuality::Diminished}, 10, 2},
                             {{5, ChordQuality::Sus2}, 12, 2},
                             {{3, ChordQuality::Sus4}, 14, 2}};
    p.voicing = {true, 50, 80};
    p.qualityScore = 87;
    p.version = 12345678901234ULL;
    p.info.source = "ai";
    p.info.seed = std::numeric_limits<uint64_t>::max();
    p.info.winnerSeed = 0x8000000000000001ULL;
    p.info.styleProfileVersion = 3;
    p.info.creativityPct = 70;
    p.info.energyPct = 90;
    p.info.prompt = "dark \"rolling\" bassline\twith slides\n(ünïcödé)";
    p.info.promptVersion = 2;
    p.info.providerId = "anthropic";
    p.info.modelId = "some-model";
    p.info.rawResponse = "{\"raw\": true}";
    p.info.referenceSetId = "set-1";
    p.info.createdUnixMs = -1234567890123LL;

    Track& bass = p.voices[0];
    bass.archetypeId = "rolling16";
    bass.archetypeAuto = false;
    bass.octaveOffset = -2;
    bass.groove = {0.52f, "swing16_62", 0.75f};
    bass.lock = {true, false, true};
    bass.muted = true;
    Note a;
    a.id = allocateNoteId(p);
    a.pitch = 33;
    a.startTick = 480;
    a.lengthTicks = 360;
    a.velocity = 124;
    a.accent = true;
    a.slide = true;
    a.lock = {false, true, false};
    Note b = a;
    b.id = allocateNoteId(p);
    b.startTick = 2400;
    b.slide = false;
    b.ratchet = 3;
    b.chance = 60;
    b.condA = 2;
    b.condB = 4;
    b.accent = false;
    bass.notes = {a, b};
    Track& melody = p.voices[1];
    melody.octaveOffset = 2;
    Note c;
    c.id = allocateNoteId(p);
    c.pitch = 69;
    c.startTick = 0;
    c.lengthTicks = 100;
    c.velocity = 1;
    melody.notes = {c};

    Track extra;
    extra.role = VoiceRole::Melody;
    extra.midiChannel = 5;
    p.voices.push_back(extra);
    return p;
}

Pattern smallPattern() {
    Pattern p = makeEmptyPattern(1, "peak_time");
    Note n;
    n.id = allocateNoteId(p);
    n.pitch = 33;
    n.startTick = 480;
    n.lengthTicks = 360;
    n.velocity = 100;
    p.voices[0].notes.push_back(n);
    return p;
}

LoadResult loadWith(const Pattern& pattern, const std::function<void(json&)>& mutate) {
    json document = patternToJson(pattern);
    mutate(document);
    return loadPatternJson(document);
}

bool hasIssue(const std::vector<std::string>& issues, const std::string& needle) {
    return std::any_of(issues.begin(), issues.end(),
                       [&](const std::string& issue) { return issue.find(needle) != std::string::npos; });
}

// ---------------------------------------------------------------------------------------------------------------
// Random valid patterns for the property test

Pattern randomPattern(Pcg32& rng) {
    static const uint32_t lengths[] = {1, 2, 4, 8, 16};
    static const std::vector<std::vector<uint32_t>> partitions8{{8}, {4, 4}};
    static const std::vector<std::vector<uint32_t>> partitions16{{16},      {8, 8},    {8, 4, 4},
                                                                 {4, 4, 8}, {4, 8, 4}, {4, 4, 4, 4}};
    static const char* texts[] = {
        "", "plain", "quote \" backslash \\", "line\nbreak\ttab", "Ünïcödé ♭VI", "{\"a\":[1,2]}"};
    auto text = [&]() { return std::string(texts[rng.bounded(6)]); };

    Pattern p = makeEmptyPattern(lengths[rng.bounded(5)], text());
    if (p.lengthBars == 8 || p.lengthBars == 16) {
        const auto& options = p.lengthBars == 8 ? partitions8 : partitions16;
        const auto& partition = options[rng.bounded(static_cast<uint32_t>(options.size()))];
        p.phrases.clear();
        uint32_t start = 0;
        for (const uint32_t length : partition) {
            Phrase phrase;
            phrase.startBar = start;
            phrase.lengthBars = length;
            phrase.role = static_cast<PhraseRole>(rng.bounded(5));
            phrase.turnaround = length >= 8 && rng.chance(50);
            phrase.locked = rng.chance(30);
            if (rng.chance(30)) {
                phrase.kickGridId = text();
            }
            p.phrases.push_back(phrase);
            start += length;
        }
    }
    p.kickGridId = text();
    if (rng.chance(50)) {
        p.kickRoot = static_cast<PitchClass>(rng.bounded(12));
    }
    p.polymeterPhase = rng.chance(50) ? PolymeterPhase::FreeRunning : PolymeterPhase::RestartAtPattern;
    if (rng.chance(50)) {
        RhythmReference ref;
        ref.bars = static_cast<uint8_t>(1 + rng.bounded(2));
        const uint32_t mask = ref.bars == 1 ? 0xffffu : 0xffffffffu;
        ref.kickSteps = rng.next() & mask;
        ref.hatSteps = rng.next() & mask;
        ref.accentSteps = rng.next() & mask;
        for (size_t i = 0; i < 32; ++i) {
            ref.timingOffsetTicks[i] = static_cast<int16_t>(rng.range(-32768, 32767));
            ref.velocity[i] = static_cast<uint8_t>(rng.bounded(256));
        }
        p.rhythmRef = ref;
    }
    for (uint32_t i = rng.bounded(6); i > 0; --i) {
        p.refineHistory.push_back(text());
    }
    p.context.root = static_cast<PitchClass>(rng.bounded(12));
    const auto scales = allScales();
    p.context.scaleId = std::string(scales[rng.bounded(static_cast<uint32_t>(scales.size()))].id);
    p.context.progression.clear();
    for (uint32_t pos = 0, total = p.lengthBars * 2; pos < total;) {
        ChordEvent event;
        event.chord = {static_cast<uint8_t>(rng.bounded(12)), static_cast<ChordQuality>(rng.bounded(5))};
        event.startHalfBar = pos;
        event.lengthHalfBars = 1 + rng.bounded(std::min<uint32_t>(4, total - pos));
        pos += event.lengthHalfBars;
        p.context.progression.push_back(event);
    }
    p.voicing = {rng.chance(50), static_cast<int>(rng.bounded(60)), 60 + static_cast<int>(rng.bounded(68))};
    p.qualityScore = static_cast<uint8_t>(rng.bounded(101));
    p.version = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
    p.info.source = text();
    p.info.seed = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
    p.info.winnerSeed = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
    p.info.styleProfileVersion = rng.next();
    p.info.creativityPct = static_cast<uint8_t>(rng.bounded(101));
    p.info.energyPct = static_cast<uint8_t>(rng.bounded(101));
    p.info.prompt = text();
    p.info.promptVersion = rng.next();
    p.info.providerId = text();
    p.info.modelId = text();
    p.info.rawResponse = text();
    p.info.referenceSetId = text();
    p.info.createdUnixMs = (static_cast<int64_t>(rng.next()) << 31) - (static_cast<int64_t>(rng.next()) << 20);

    p.voices.clear();
    std::vector<uint8_t> channels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    rng.shuffle(channels.begin(), channels.end());
    const uint32_t voiceCount = 1 + rng.bounded(kMaxVoices);
    const uint32_t endTick = p.lengthBars * kTicksPerBar;
    for (uint32_t v = 0; v < voiceCount; ++v) {
        Track track;
        track.role = rng.chance(50) ? VoiceRole::Bass : VoiceRole::Melody;
        track.midiChannel = channels[v];
        track.archetypeId = text();
        track.archetypeAuto = rng.chance(50);
        track.octaveOffset = static_cast<int8_t>(rng.range(-2, 2));
        track.groove = {0.5f + static_cast<float>(rng.bounded(251)) / 1000.0f, text(),
                        static_cast<float>(rng.bounded(1001)) / 1000.0f};
        track.lock = {rng.chance(50), rng.chance(50), rng.chance(50)};
        track.muted = rng.chance(20);
        for (uint32_t n = rng.bounded(30); n > 0; --n) {
            Note note;
            note.id = allocateNoteId(p);
            note.pitch = static_cast<uint8_t>(rng.bounded(128));
            note.startTick = rng.bounded(endTick - 1);
            note.lengthTicks = 1 + rng.bounded(std::min<uint32_t>(2000, endTick - note.startTick));
            note.velocity = static_cast<uint8_t>(1 + rng.bounded(127));
            note.slide = rng.chance(30);
            note.ratchet = note.slide ? 1 : static_cast<uint8_t>(1 + rng.bounded(4));
            note.chance = static_cast<uint8_t>(rng.bounded(101));
            note.condB = static_cast<uint8_t>(1 + rng.bounded(8));
            note.condA = static_cast<uint8_t>(1 + rng.bounded(note.condB));
            note.accent = rng.chance(30);
            note.lock = {rng.chance(50), rng.chance(50), rng.chance(50)};
            track.notes.push_back(note);
        }
        std::stable_sort(track.notes.begin(), track.notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        p.voices.push_back(std::move(track));
    }
    return p;
}

} // namespace

TEST_CASE("empty patterns are valid for every pattern length", "[core][pattern]") {
    for (const uint32_t bars : {1u, 2u, 4u, 8u, 16u}) {
        const Pattern p = makeEmptyPattern(bars, "peak_time");
        CHECK(validatePattern(p).empty());
        CHECK(p.voices.size() == 2);
        CHECK(p.voices[0].midiChannel == 1);
        CHECK(p.voices[1].midiChannel == 2);
        CHECK(p.context.root == 9);
        CHECK(p.context.scaleId == "natural_minor");
    }
    CHECK_FALSE(isValidPatternLength(0));
    CHECK_FALSE(isValidPatternLength(3));
    CHECK_FALSE(isValidPatternLength(32));
}

TEST_CASE("note ids only ever increase", "[core][pattern]") {
    Pattern p = makeEmptyPattern(1, "x");
    CHECK(allocateNoteId(p) == 1);
    CHECK(allocateNoteId(p) == 2);
    Pattern copy = p; // copying a slot keeps ids and counter
    CHECK(allocateNoteId(copy) == 3);
    CHECK(allocateNoteId(p) == 3);
}

TEST_CASE("a fully populated pattern survives a JSON round trip", "[core][pattern][json]") {
    const Pattern original = fullPattern();
    REQUIRE(validatePattern(original).empty());
    const std::string text = patternToString(original);

    const auto loaded = loadPattern(text);
    REQUIRE(loaded.ok());
    CHECK(loaded.error.empty());
    CHECK_FALSE(loaded.fromNewerVersion);
    CHECK(*loaded.pattern == original);
    CHECK(patternToString(*loaded.pattern) == text); // byte-identical
}

TEST_CASE("64-bit seeds and extreme values stay exact", "[core][pattern][json]") {
    Pattern p = smallPattern();
    p.info.seed = std::numeric_limits<uint64_t>::max();
    p.info.winnerSeed = (1ULL << 63) + 1;
    p.version = std::numeric_limits<uint64_t>::max();
    p.info.createdUnixMs = std::numeric_limits<int64_t>::min();
    const auto loaded = loadPattern(patternToString(p));
    REQUIRE(loaded.ok());
    CHECK(loaded.pattern->info.seed == std::numeric_limits<uint64_t>::max());
    CHECK(loaded.pattern->info.winnerSeed == (1ULL << 63) + 1);
    CHECK(loaded.pattern->version == std::numeric_limits<uint64_t>::max());
    CHECK(loaded.pattern->info.createdUnixMs == std::numeric_limits<int64_t>::min());
}

TEST_CASE("floats round trip exactly", "[core][pattern][json]") {
    Pattern p = smallPattern();
    for (const float swing : {0.5f, 0.52f, 0.6180339f, 0.75f}) {
        p.voices[0].groove.swing = swing;
        p.voices[0].groove.amount = 1.0f - swing;
        const auto loaded = loadPattern(patternToString(p));
        REQUIRE(loaded.ok());
        CHECK(loaded.pattern->voices[0].groove.swing == swing);
        CHECK(loaded.pattern->voices[0].groove.amount == 1.0f - swing);
    }
}

TEST_CASE("golden file: the JSON format is stable", "[core][pattern][json]") {
    const std::string expected =
        R"JSON({"context":{"progression":[{"lengthHalfBars":2,"quality":"minor","rootOffset":0,"startHalfBar":0}],"root":9,"scaleId":"natural_minor"},"info":{"createdUnixMs":0,"creativityPct":40,"energyPct":50,"modelId":"","prompt":"","promptVersion":0,"providerId":"","rawResponse":"","referenceSetId":"","seed":0,"source":"","styleProfileVersion":0,"winnerSeed":0},"kickGridId":"4otf","lengthBars":1,"nextNoteId":2,"phrases":[{"lengthBars":1,"locked":false,"role":"main","startBar":0,"turnaround":false}],"polymeterPhase":"restart","qualityScore":0,"refineHistory":[],"stateVersion":1,"styleId":"peak_time","timeSigDen":4,"timeSigNum":4,"version":0,"voices":[{"archetypeAuto":true,"archetypeId":"","groove":{"amount":1.0,"swing":0.5,"templateId":""},"lock":{"pitch":false,"rhythm":false,"velocity":false},"midiChannel":1,"muted":false,"notes":[{"accent":false,"chance":100,"condA":1,"condB":1,"id":1,"lengthTicks":360,"lock":{"pitch":false,"rhythm":false,"velocity":false},"pitch":33,"ratchet":1,"slide":false,"startTick":480,"velocity":100}],"octaveOffset":0,"role":"bass"},{"archetypeAuto":true,"archetypeId":"","groove":{"amount":1.0,"swing":0.5,"templateId":""},"lock":{"pitch":false,"rhythm":false,"velocity":false},"midiChannel":2,"muted":false,"notes":[],"octaveOffset":0,"role":"melody"}],"voicing":{"chordMemory":false,"highNote":79,"lowNote":55}})JSON";
    const std::string actual = patternToString(smallPattern());
    INFO(actual);
    CHECK(actual == expected);
}

TEST_CASE("random valid patterns round trip", "[core][pattern][json]") {
    for (uint64_t seed = 0; seed < 300; ++seed) {
        auto rng = Pcg32::fromSeed(seed);
        const Pattern original = randomPattern(rng);
        INFO("seed " << seed);
        REQUIRE(validatePattern(original).empty());
        const std::string text = patternToString(original);
        const auto loaded = loadPattern(text);
        REQUIRE(loaded.ok());
        REQUIRE(*loaded.pattern == original);
        REQUIRE(patternToString(*loaded.pattern) == text);
    }
}

TEST_CASE("unknown fields are ignored", "[core][pattern][json]") {
    const Pattern original = fullPattern();
    const auto loaded = loadWith(original, [](json& d) {
        d["futureField"] = {{"nested", 1}};
        d["voices"][0]["futureVoiceField"] = "x";
        d["voices"][0]["notes"][0]["futureNoteField"] = 7;
        d["context"]["futureContextField"] = true;
        d["info"]["futureInfoField"] = json::array({1, 2});
    });
    REQUIRE(loaded.ok());
    CHECK(*loaded.pattern == original);
}

TEST_CASE("missing optional fields get their defaults", "[core][pattern][json]") {
    const json minimal = json::parse(R"({
        "stateVersion": 1,
        "lengthBars": 1,
        "nextNoteId": 2,
        "context": {"root": 9, "scaleId": "natural_minor",
                    "progression": [{"rootOffset": 0, "quality": "minor", "startHalfBar": 0, "lengthHalfBars": 2}]},
        "phrases": [{"startBar": 0, "lengthBars": 1, "role": "main"}],
        "voices": [{"role": "bass", "midiChannel": 1,
                    "notes": [{"id": 1, "pitch": 33, "startTick": 0, "lengthTicks": 240, "velocity": 100}]}]
    })");
    const auto loaded = loadPatternJson(minimal);
    REQUIRE(loaded.ok());
    const Pattern& p = *loaded.pattern;
    CHECK(p.kickGridId == "4otf");
    CHECK(p.timeSigNum == 4);
    CHECK_FALSE(p.kickRoot.has_value());
    CHECK_FALSE(p.rhythmRef.has_value());
    CHECK(p.voices[0].groove == GrooveSettings{});
    CHECK(p.voices[0].archetypeAuto);
    const Note& n = p.voices[0].notes[0];
    CHECK(n.ratchet == 1);
    CHECK(n.chance == 100);
    CHECK(n.condA == 1);
    CHECK(n.condB == 1);
    CHECK_FALSE(n.slide);
    CHECK(p.voicing == VoicingSettings{});
}

TEST_CASE("a newer state version loads as far as understood and is flagged", "[core][pattern][json]") {
    const Pattern original = smallPattern();
    const auto loaded = loadWith(original, [](json& d) {
        d["stateVersion"] = kPatternStateVersion + 5;
        d["somethingNew"] = "ignored";
    });
    REQUIRE(loaded.ok());
    CHECK(loaded.fromNewerVersion);
    CHECK(*loaded.pattern == original);
}

TEST_CASE("unsupported state versions are rejected", "[core][pattern][json]") {
    for (const int version : {0, -1}) {
        const auto loaded = loadWith(smallPattern(), [&](json& d) { d["stateVersion"] = version; });
        CHECK_FALSE(loaded.ok());
        CHECK(loaded.error.find("stateVersion") != std::string::npos);
    }
    const auto missing = loadWith(smallPattern(), [](json& d) { d.erase("stateVersion"); });
    CHECK_FALSE(missing.ok());
    CHECK(missing.error.find("stateVersion") != std::string::npos);
}

TEST_CASE("malformed documents give an error with a path and never a pattern", "[core][pattern][json]") {
    struct Case {
        const char* what;
        const char* pathPart;
        std::function<void(json&)> mutate;
    };
    const std::vector<Case> cases = {
        {"pitch as string", "voices[0].notes[0].pitch", [](json& d) { d["voices"][0]["notes"][0]["pitch"] = "C"; }},
        {"pitch as float", "voices[0].notes[0].pitch", [](json& d) { d["voices"][0]["notes"][0]["pitch"] = 33.5; }},
        {"negative pitch", "voices[0].notes[0].pitch", [](json& d) { d["voices"][0]["notes"][0]["pitch"] = -1; }},
        {"pitch too large for its type", "voices[0].notes[0].pitch",
         [](json& d) { d["voices"][0]["notes"][0]["pitch"] = 300; }},
        {"pitch beyond 127", "voices[0].notes[0].pitch", [](json& d) { d["voices"][0]["notes"][0]["pitch"] = 130; }},
        {"velocity zero", "voices[0].notes[0].velocity", [](json& d) { d["voices"][0]["notes"][0]["velocity"] = 0; }},
        {"huge start tick", "voices[0].notes[0].startTick",
         [](json& d) { d["voices"][0]["notes"][0]["startTick"] = 1e30; }},
        {"start tick overflow", "voices[0].notes[0].startTick",
         [](json& d) { d["voices"][0]["notes"][0]["startTick"] = 4294967296ULL; }},
        {"missing note id", "voices[0].notes[0].id", [](json& d) { d["voices"][0]["notes"][0].erase("id"); }},
        {"note not an object", "voices[0].notes[0]", [](json& d) { d["voices"][0]["notes"][0] = 5; }},
        {"notes not an array", "voices[0].notes", [](json& d) { d["voices"][0]["notes"] = "none"; }},
        {"voice not an object", "voices[1]", [](json& d) { d["voices"][1] = nullptr; }},
        {"voices missing", "voices", [](json& d) { d.erase("voices"); }},
        {"voices not an array", "voices", [](json& d) { d["voices"] = json::object(); }},
        {"unknown voice role", "voices[0].role", [](json& d) { d["voices"][0]["role"] = "drums"; }},
        {"role not a string", "voices[0].role", [](json& d) { d["voices"][0]["role"] = 3; }},
        {"unknown chord quality", "context.progression[0].quality",
         [](json& d) { d["context"]["progression"][0]["quality"] = "lydian"; }},
        {"unknown phrase role", "phrases[0].role", [](json& d) { d["phrases"][0]["role"] = "chorus"; }},
        {"unknown polymeter phase", "polymeterPhase", [](json& d) { d["polymeterPhase"] = "sideways"; }},
        {"lengthBars missing", "lengthBars", [](json& d) { d.erase("lengthBars"); }},
        {"lengthBars wrong", "lengthBars", [](json& d) { d["lengthBars"] = 3; }},
        {"lengthBars as string", "lengthBars", [](json& d) { d["lengthBars"] = "1"; }},
        {"unknown scale", "context.scaleId", [](json& d) { d["context"]["scaleId"] = "lydian"; }},
        {"scale as number", "context.scaleId", [](json& d) { d["context"]["scaleId"] = 1; }},
        {"context missing", "context", [](json& d) { d.erase("context"); }},
        {"context not an object", "context", [](json& d) { d["context"] = json::array(); }},
        {"progression gap", "context.progression",
         [](json& d) { d["context"]["progression"][0]["lengthHalfBars"] = 1; }},
        {"phrases missing", "phrases", [](json& d) { d.erase("phrases"); }},
        {"phrases empty", "phrases", [](json& d) { d["phrases"] = json::array(); }},
        {"nextNoteId too small", "nextNoteId", [](json& d) { d["nextNoteId"] = 1; }},
        {"duplicate note id", "id",
         [](json& d) {
             auto note = d["voices"][0]["notes"][0];
             d["voices"][1]["notes"] = json::array({note});
         }},
        {"bool as number", "voices[0].muted", [](json& d) { d["voices"][0]["muted"] = 1; }},
        {"swing as string", "voices[0].groove.swing", [](json& d) { d["voices"][0]["groove"]["swing"] = "straight"; }},
        {"swing out of range", "voices[0].groove.swing", [](json& d) { d["voices"][0]["groove"]["swing"] = 0.9; }},
        {"duplicate channel", "midiChannel", [](json& d) { d["voices"][1]["midiChannel"] = 1; }},
        {"channel zero", "midiChannel", [](json& d) { d["voices"][0]["midiChannel"] = 0; }},
        {"rhythmRef wrong size", "rhythmRef.velocity",
         [](json& d) {
             Pattern p = smallPattern();
             p.rhythmRef = RhythmReference{};
             d = patternToJson(p);
             d["rhythmRef"]["velocity"] = json::array({1, 2, 3});
         }},
        {"rhythmRef entry wrong type", "rhythmRef.timingOffsetTicks[3]",
         [](json& d) {
             Pattern p = smallPattern();
             p.rhythmRef = RhythmReference{};
             d = patternToJson(p);
             d["rhythmRef"]["timingOffsetTicks"][3] = "x";
         }},
        {"refineHistory entry wrong type", "refineHistory[0]", [](json& d) { d["refineHistory"] = json::array({5}); }},
        {"info not an object", "info", [](json& d) { d["info"] = 7; }},
        {"seed negative", "info.seed", [](json& d) { d["info"]["seed"] = -5; }},
    };
    for (const auto& c : cases) {
        INFO(c.what);
        const auto loaded = loadWith(smallPattern(), c.mutate);
        CHECK_FALSE(loaded.ok());
        CHECK_FALSE(loaded.error.empty());
        CHECK(loaded.error.find(c.pathPart) != std::string::npos);
    }
}

TEST_CASE("garbage input never crashes", "[core][pattern][json]") {
    for (const char* text : {"", " ", "not json", "{", "}", "[]", "null", "42", "\"text\"", "{}",
                             "{\"stateVersion\":1}", "[1,2,3", "{\"voices\": [[[[[[]]]]]]}", "\xff\xfe\x00"}) {
        const auto loaded = loadPattern(text);
        CHECK_FALSE(loaded.ok());
        CHECK_FALSE(loaded.error.empty());
    }
    const std::string deep = std::string(100000, '[') + std::string(100000, ']'); // deeply nested
    CHECK_FALSE(loadPattern(deep).ok());
    CHECK_FALSE(loadPattern(std::string(100000, '[')).ok());
}

TEST_CASE("migration steps run in order and set the version", "[core][pattern][json]") {
    const std::vector<Migration> steps = {
        [](json& d, std::string&) {
            d["step1"] = true;
            return true;
        },
        [](json& d, std::string&) {
            d["step2"] = d.contains("step1");
            return true;
        },
    };
    json doc = {{"stateVersion", 1}};
    std::string error;
    REQUIRE(migrateDocument(doc, 1, 3, steps, error));
    CHECK(doc["stateVersion"] == 3);
    CHECK(doc["step1"] == true);
    CHECK(doc["step2"] == true);

    json fromTwo = {{"stateVersion", 2}};
    REQUIRE(migrateDocument(fromTwo, 2, 3, steps, error));
    CHECK_FALSE(fromTwo.contains("step1"));
    CHECK(fromTwo["step2"] == false);

    json same = {{"stateVersion", 3}};
    CHECK(migrateDocument(same, 3, 3, steps, error));
}

TEST_CASE("migration failures are reported", "[core][pattern][json]") {
    const std::vector<Migration> steps = {[](json&, std::string& error) {
        error = "boom";
        return false;
    }};
    json doc = {{"stateVersion", 1}};
    std::string error;
    CHECK_FALSE(migrateDocument(doc, 1, 2, steps, error));
    CHECK(error == "boom");

    error.clear();
    CHECK_FALSE(migrateDocument(doc, 1, 3, steps, error)); // second step missing
    CHECK(error.find("no migration") != std::string::npos);

    error.clear();
    CHECK_FALSE(migrateDocument(doc, 0, 1, steps, error));
    CHECK(error.find("unsupported") != std::string::npos);

    error.clear();
    CHECK_FALSE(migrateDocument(doc, 3, 2, steps, error));
    CHECK(error.find("backwards") != std::string::npos);
}

TEST_CASE("validation: every rule has a violating case", "[core][pattern][validation]") {
    struct Case {
        const char* path;
        std::function<void(Pattern&)> mutate;
    };
    const std::vector<Case> cases = {
        {"lengthBars", [](Pattern& p) { p.lengthBars = 3; }},
        {"timeSig", [](Pattern& p) { p.timeSigNum = 3; }},
        {"kickRoot", [](Pattern& p) { p.kickRoot = 12; }},
        {"refineHistory", [](Pattern& p) { p.refineHistory.assign(6, "x"); }},
        {"qualityScore", [](Pattern& p) { p.qualityScore = 101; }},
        {"info.creativityPct", [](Pattern& p) { p.info.creativityPct = 101; }},
        {"info.energyPct", [](Pattern& p) { p.info.energyPct = 101; }},
        {"voicing", [](Pattern& p) { p.voicing.lowNote = 90; }},
        {"voicing", [](Pattern& p) { p.voicing.highNote = 128; }},
        {"voices", [](Pattern& p) { p.voices.clear(); }},
        {"voices",
         [](Pattern& p) {
             p.voices.assign(9, p.voices[0]);
             for (size_t i = 0; i < p.voices.size(); ++i) {
                 p.voices[i].midiChannel = static_cast<uint8_t>(i + 1);
             }
         }},
        {"voices[0].midiChannel", [](Pattern& p) { p.voices[0].midiChannel = 17; }},
        {"voices[1].midiChannel", [](Pattern& p) { p.voices[1].midiChannel = 1; }},
        {"voices[0].octaveOffset", [](Pattern& p) { p.voices[0].octaveOffset = 3; }},
        {"voices[0].groove.swing", [](Pattern& p) { p.voices[0].groove.swing = 0.49f; }},
        {"voices[0].groove.amount", [](Pattern& p) { p.voices[0].groove.amount = 1.5f; }},
        {"voices[0].notes[0].pitch", [](Pattern& p) { p.voices[0].notes[0].pitch = 128; }},
        {"voices[0].notes[0].velocity", [](Pattern& p) { p.voices[0].notes[0].velocity = 128; }},
        {"voices[0].notes[0].lengthTicks", [](Pattern& p) { p.voices[0].notes[0].lengthTicks = 0; }},
        {"voices[0].notes[0]", [](Pattern& p) { p.voices[0].notes[0].lengthTicks = 5000; }},
        {"voices[0].notes[0].ratchet", [](Pattern& p) { p.voices[0].notes[0].ratchet = 5; }},
        {"voices[0].notes[0]",
         [](Pattern& p) {
             p.voices[0].notes[0].ratchet = 2;
             p.voices[0].notes[0].slide = true;
         }},
        {"voices[0].notes[0].chance", [](Pattern& p) { p.voices[0].notes[0].chance = 101; }},
        {"voices[0].notes[0].condB", [](Pattern& p) { p.voices[0].notes[0].condB = 9; }},
        {"voices[0].notes[0].condA", [](Pattern& p) { p.voices[0].notes[0].condA = 2; }},
        {"voices[0].notes[1].startTick",
         [](Pattern& p) {
             Note earlier = p.voices[0].notes[0];
             earlier.id = allocateNoteId(p);
             earlier.startTick = 0;
             p.voices[0].notes.push_back(earlier);
         }},
        {"nextNoteId", [](Pattern& p) { p.nextNoteId = 1; }},
        {"phrases", [](Pattern& p) { p.phrases.clear(); }},
        {"phrases[0]", [](Pattern& p) { p.phrases[0].role = PhraseRole::Build; }},
        {"phrases[0].turnaround", [](Pattern& p) { p.phrases[0].turnaround = true; }},
        {"context.root", [](Pattern& p) { p.context.root = 12; }},
        {"context.scaleId", [](Pattern& p) { p.context.scaleId = "lydian"; }},
        {"context.progression", [](Pattern& p) { p.context.progression.clear(); }},
        {"context.progression[0].chord.rootOffset", [](Pattern& p) { p.context.progression[0].chord.rootOffset = 12; }},
        {"context.progression", [](Pattern& p) { p.context.progression[0].lengthHalfBars = 1; }},
        {"rhythmRef.bars",
         [](Pattern& p) {
             p.rhythmRef = RhythmReference{};
             p.rhythmRef->bars = 3;
         }},
        {"rhythmRef",
         [](Pattern& p) {
             p.rhythmRef = RhythmReference{};
             p.rhythmRef->kickSteps = 0x10000u; // step 16 with a one-bar reference
         }},
    };
    for (const auto& c : cases) {
        Pattern p = smallPattern();
        c.mutate(p);
        INFO(c.path);
        const auto issues = validatePattern(p);
        CHECK(hasIssue(issues, c.path));
    }
    CHECK(validatePattern(smallPattern()).empty());
}

TEST_CASE("validation: multi-bar phrase rules", "[core][pattern][validation]") {
    Pattern p = makeEmptyPattern(16, "x");
    p.phrases = {makePhrase(0, 8, PhraseRole::Main, true), makePhrase(8, 4, PhraseRole::Variation),
                 makePhrase(12, 4, PhraseRole::Answer)};
    CHECK(validatePattern(p).empty());

    Pattern gap = p;
    gap.phrases[1].startBar = 9;
    CHECK(hasIssue(validatePattern(gap), "phrases"));

    Pattern overlap = p;
    overlap.phrases[2].startBar = 11;
    CHECK(hasIssue(validatePattern(overlap), "phrases"));

    Pattern shortCover = p;
    shortCover.phrases.pop_back();
    CHECK(hasIssue(validatePattern(shortCover), "phrases"));

    Pattern badLength = p;
    badLength.phrases = {makePhrase(0, 6, PhraseRole::Main), makePhrase(6, 10, PhraseRole::Main)};
    CHECK(hasIssue(validatePattern(badLength), "lengthBars"));

    Pattern unordered = p;
    std::swap(unordered.phrases[0], unordered.phrases[2]);
    CHECK(validatePattern(unordered).empty()); // order in the list does not matter, coverage does
}

TEST_CASE("validation: values at the limits are valid", "[core][pattern][validation]") {
    Pattern p = makeEmptyPattern(16, "x");
    Note last;
    last.id = allocateNoteId(p);
    last.pitch = 127;
    last.velocity = 127;
    last.startTick = 16 * kTicksPerBar - 1;
    last.lengthTicks = 1;
    last.ratchet = 4;
    last.chance = 0;
    last.condA = 8;
    last.condB = 8;
    p.voices[0].notes.push_back(last);
    p.voices[0].octaveOffset = 2;
    p.voices[0].groove.swing = 0.75f;
    p.voices[0].groove.amount = 0.0f;
    p.voicing = {false, 0, 127};
    p.qualityScore = 100;
    CHECK(validatePattern(p).empty());

    p.voices.clear();
    for (int v = 0; v < kMaxVoices; ++v) {
        Track track;
        track.midiChannel = static_cast<uint8_t>(16 - v);
        p.voices.push_back(track);
    }
    p.nextNoteId = 1;
    CHECK(validatePattern(p).empty());
}
