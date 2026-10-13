#include "PatternFixtures.h"
#include "core/Groove.h"
#include "core/GrooveLibrary.h"
#include "core/OutputStage.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;
using namespace mm::fixtures;

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::vector<std::string> shippedTexts() {
    std::vector<std::string> texts;
    for (const auto& entry : std::filesystem::directory_iterator(std::string(MIDIMAID_RESOURCE_DIR) + "/grooves")) {
        if (entry.path().extension() == ".json") {
            texts.push_back(slurp(entry.path()));
        }
    }
    return texts;
}

GrooveLibrary shipped() {
    const auto texts = shippedTexts();
    std::vector<std::string_view> views(texts.begin(), texts.end());
    return GrooveLibrary::fromTexts(views);
}

std::string valid(const std::string& tail = "") {
    std::string offsets = "[";
    std::string velocity = "[";
    for (int i = 0; i < 16; ++i) {
        offsets += (i ? "," : "") + std::string("0");
        velocity += (i ? "," : "") + std::string("1000");
    }
    return R"({"id": "test_groove", "nameDe": "Test", "bars": 1, "offsets": )" + offsets + "], \"velocity\": " + velocity + "]" +
           tail + "}";
}

Pattern bassLine() {
    Pattern pattern = makeEmptyPattern(1, "peak_time");
    pattern.info.seed = 99;
    for (uint32_t step = 0; step < 16; ++step) {
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = 40;
        note.startTick = step * 240;
        note.lengthTicks = 120;
        note.velocity = 100;
        pattern.voices[0].notes.push_back(note);
    }
    return pattern;
}

OutputSettings noKickWith(const GrooveLibrary* library) {
    OutputSettings settings;
    settings.ignoresKick = {true, true, true};
    settings.grooves = library;
    return settings;
}

std::vector<int32_t> starts(const OutputPattern& out) {
    std::vector<int32_t> result;
    for (const auto& note : out.voices[0].notes) {
        result.push_back(note.startTick);
    }
    return result;
}

} // namespace

TEST_CASE("a groove template is read and checked", "[groove-library]") {
    const auto ok = loadGrooveTemplate(valid());
    REQUIRE(ok.ok());
    CHECK(ok.groove->id == "test_groove");
    CHECK(ok.groove->steps() == 16);
    CHECK_FALSE(ok.groove->swing);
    CHECK(ok.groove->humanizeTicks == 0);
    const auto full = loadGrooveTemplate(valid(R"(, "swing": true, "humanize": {"ticks": 9, "velocity": 40})"));
    REQUIRE(full.ok());
    CHECK(full.groove->swing);
    CHECK(full.groove->humanizeTicks == 9);
    CHECK(full.groove->humanizeVelocityPermille == 40);
    for (const char* bad : {"", "[]", "{", "{\"id\": 3}"}) {
        INFO(bad);
        CHECK_FALSE(loadGrooveTemplate(bad).ok());
    }
    const auto replace = [&](const std::string& from, const std::string& to) {
        auto text = valid();
        text.replace(text.find(from), from.size(), to);
        return loadGrooveTemplate(text);
    };
    CHECK_FALSE(replace("test_groove", "Test Groove").ok());       // not a lower case id
    CHECK_FALSE(replace("test_groove", "").ok());
    CHECK_FALSE(replace("\"bars\": 1", "\"bars\": 3").ok());
    CHECK_FALSE(replace("\"bars\": 1", "\"bars\": 2").ok());       // 32 numbers expected
    {
        std::string offsets = "[";
        std::string velocity = "[";
        for (int i = 0; i < 48; ++i) { // 48 numbers fit 3 bars, which the format does not know
            offsets += (i ? "," : "") + std::string("0");
            velocity += (i ? "," : "") + std::string("1000");
        }
        CHECK_FALSE(loadGrooveTemplate(R"({"id": "three", "nameDe": "Drei", "bars": 3, "offsets": )" + offsets +
                                       "], \"velocity\": " + velocity + "]}").ok());
    }
    CHECK_FALSE(replace("\"offsets\": [0,", "\"offsets\": [241,").ok());
    CHECK_FALSE(replace("\"offsets\": [0,", "\"offsets\": [-241,").ok());
    CHECK(replace("\"offsets\": [0,", "\"offsets\": [-240,").ok());
    CHECK_FALSE(replace("\"velocity\": [1000,", "\"velocity\": [499,").ok());
    CHECK_FALSE(replace("\"velocity\": [1000,", "\"velocity\": [1501,").ok());
    CHECK_FALSE(replace("\"offsets\": [0,", "\"offsets\": [0.5,").ok());
    CHECK_FALSE(loadGrooveTemplate(valid(R"(, "swing": 1)")).ok());
    CHECK_FALSE(loadGrooveTemplate(valid(R"(, "humanize": {"ticks": 61})")).ok());
    CHECK_FALSE(loadGrooveTemplate(valid(R"(, "humanize": {"velocity": 201})")).ok());
    CHECK_FALSE(loadGrooveTemplate(valid(R"(, "humanize": 3)")).ok());
}

TEST_CASE("the library keeps the first of a name, reports the rest and lists the templates in order", "[groove-library]") {
    const std::string a = valid();
    std::string b = valid();
    b.replace(b.find("test_groove"), 11, "a_groove");
    const std::vector<std::string_view> texts{a, "broken", a, b};
    const auto library = GrooveLibrary::fromTexts(texts);
    REQUIRE(library.size() == 2);
    CHECK(library.templates()[0].id == "a_groove"); // sorted by id
    CHECK(library.templates()[1].id == "test_groove");
    CHECK(library.problems().size() == 2); // the broken one and the duplicate
    CHECK(library.find("a_groove") != nullptr);
    CHECK(library.find("missing") == nullptr);
    CHECK(library.find("") == nullptr);
}

TEST_CASE("the shipped templates all load and are what their names say", "[groove-library]") {
    const auto library = shipped();
    CHECK(library.problems().empty());
    std::set<std::string> ids;
    for (const auto& groove : library.templates()) {
        ids.insert(groove.id);
    }
    CHECK(ids == std::set<std::string>{"humanize", "laid_back", "pushed", "shuffle", "swing_54", "swing_58", "swing_62",
                                       "swing_66"});
    for (const int pct : {54, 58, 62, 66}) {
        const auto* groove = library.find("swing_" + std::to_string(pct));
        REQUIRE(groove != nullptr);
        CHECK(groove->swing);
        // the odd 16ths are late by (swing - 50 %) of a pair of 16ths (960 ticks), the even ones are straight
        const int32_t delay = swingOffsetTicks(static_cast<float>(pct) / 100.0f, 1.0f);
        for (size_t i = 0; i < groove->steps(); ++i) {
            CHECK(groove->offsetTicks[i] == (i % 2 == 1 ? delay : 0));
        }
    }
    CHECK(library.find("shuffle")->swing);
    CHECK_FALSE(library.find("laid_back")->swing);
    CHECK_FALSE(library.find("pushed")->swing);
    CHECK(library.find("humanize")->humanizeTicks > 0);
    CHECK(library.find("laid_back")->offsetTicks[5] > 0);
    CHECK(library.find("pushed")->offsetTicks[5] < 0);
}

TEST_CASE("the deviation of Humanize is deterministic, within its limit and different per note, seed and template",
          "[groove-library]") {
    std::set<int32_t> seen;
    for (uint32_t id = 1; id <= 200; ++id) {
        const int32_t offset = humanizeOffset(7, id, "humanize", 8);
        CHECK(offset >= -8);
        CHECK(offset <= 8);
        CHECK(offset == humanizeOffset(7, id, "humanize", 8)); // the same every time (a bounce is reproducible)
        const int32_t velocity = humanizeVelocity(7, id, "humanize", 60);
        CHECK(velocity >= -60);
        CHECK(velocity <= 60);
        seen.insert(offset);
    }
    CHECK(seen.size() >= 10); // a spread, not one value
    CHECK(seen.count(8) == 1); // both ends of the range are reached
    CHECK(seen.count(-8) == 1);
    CHECK(humanizeOffset(7, 3, "humanize", 0) == 0);
    int differing = 0;
    for (uint32_t id = 1; id <= 100; ++id) {
        differing += humanizeOffset(7, id, "humanize", 8) != humanizeOffset(8, id, "humanize", 8) ? 1 : 0;
        differing += humanizeOffset(7, id, "humanize", 8) != humanizeOffset(7, id, "other", 8) ? 1 : 0;
    }
    CHECK(differing > 100);
    CHECK(humanizeOffset(7, 3, "humanize", 5000) <= 60); // the limits of the format
    CHECK(humanizeVelocity(7, 3, "humanize", 5000) <= 200);
}

TEST_CASE("a swing template does what the swing control does, and replaces it", "[groove-library][output]") {
    const auto library = shipped();
    Pattern straight = bassLine();
    straight.voices[0].groove.swing = 0.62f;
    Pattern template62 = bassLine();
    template62.voices[0].groove.templateId = "swing_62";
    template62.voices[0].groove.swing = 0.54f; // the control is off for this template: no double swing
    const auto expected = starts(renderOutput(straight, noKickWith(&library)));
    CHECK(starts(renderOutput(template62, noKickWith(&library))) == expected);
    // the template of another strength
    template62.voices[0].groove.templateId = "swing_58";
    CHECK(starts(renderOutput(template62, noKickWith(&library))) != expected);
}

TEST_CASE("a template without swing takes the swing control on top", "[groove-library][output]") {
    const auto library = shipped();
    Pattern pattern = bassLine();
    pattern.voices[0].groove.templateId = "laid_back";
    pattern.voices[0].groove.swing = 0.5f;
    const auto plain = starts(renderOutput(pattern, noKickWith(&library)));
    for (size_t i = 0; i < plain.size(); ++i) {
        CHECK(plain[i] == static_cast<int32_t>(i * 240) + (i == 0 ? 0 : 14)); // every note but the first is late
    }
    pattern.voices[0].groove.swing = 0.58f;
    const auto swung = starts(renderOutput(pattern, noKickWith(&library)));
    const int32_t delay = swingOffsetTicks(0.58f, 1.0f);
    for (size_t i = 1; i < swung.size(); ++i) {
        CHECK(swung[i] == plain[i] + (i % 2 == 1 ? delay : 0));
    }
    // pushed is early, step 0 stays at the start of the pattern
    pattern.voices[0].groove.templateId = "pushed";
    pattern.voices[0].groove.swing = 0.5f;
    const auto pushed = starts(renderOutput(pattern, noKickWith(&library)));
    CHECK(pushed[0] == 0);
    CHECK(pushed[3] == 3 * 240 - 14);
}

TEST_CASE("the amount scales a template, 0 is straight, an unknown template is straight", "[groove-library][output]") {
    const auto library = shipped();
    Pattern pattern = bassLine();
    pattern.voices[0].groove.templateId = "swing_66";
    pattern.voices[0].groove.amount = 0.5f;
    const auto half = starts(renderOutput(pattern, noKickWith(&library)));
    const int32_t full = swingOffsetTicks(0.66f, 1.0f);
    CHECK(half[1] == 240 + (full + 1) / 2); // half of the delay, rounded half away from zero
    pattern.voices[0].groove.amount = 0.0f;
    for (size_t i = 0; i < 16; ++i) {
        CHECK(starts(renderOutput(pattern, noKickWith(&library)))[i] == static_cast<int32_t>(i * 240));
    }
    pattern.voices[0].groove.amount = 1.0f;
    pattern.voices[0].groove.templateId = "no_such_groove";
    pattern.voices[0].groove.swing = 0.5f;
    CHECK(starts(renderOutput(pattern, noKickWith(&library))) == starts(renderOutput(bassLine(), noKickWith(&library))));
    // without a library the template is not known either
    pattern.voices[0].groove.templateId = "swing_66";
    CHECK(starts(renderOutput(pattern, noKickWith(nullptr))) == starts(renderOutput(bassLine(), noKickWith(nullptr))));
    // and the export without groove
    OutputSettings plain = noKickWith(&library);
    plain.includeGroove = false;
    CHECK(starts(renderOutput(pattern, plain)) == starts(renderOutput(bassLine(), plain)));
}

TEST_CASE("only notes on a 16th are touched by a template, triplets stay", "[groove-library][output]") {
    const auto library = shipped();
    Pattern pattern = makeEmptyPattern(1, "peak_time");
    for (const uint32_t start : {0u, 160u, 240u, 400u}) { // 160 and 400 are not on a 16th
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = 40;
        note.startTick = start;
        note.lengthTicks = 60;
        pattern.voices[0].notes.push_back(note);
    }
    pattern.voices[0].groove.templateId = "pushed";
    const auto out = starts(renderOutput(pattern, noKickWith(&library)));
    CHECK(out == std::vector<int32_t>{0, 160, 240 - 14, 400});
}

TEST_CASE("Humanize moves the notes a little, always the same way, and not outside the pattern", "[groove-library][output]") {
    const auto library = shipped();
    Pattern pattern = bassLine();
    pattern.voices[0].groove.templateId = "humanize";
    const auto first = renderOutput(pattern, noKickWith(&library));
    CHECK(first == renderOutput(pattern, noKickWith(&library))); // deterministic
    int moved = 0;
    int velocityChanged = 0;
    for (size_t i = 0; i < first.voices[0].notes.size(); ++i) {
        const auto& note = first.voices[0].notes[i];
        CHECK(std::abs(note.startTick - static_cast<int32_t>(i * 240)) <= 8);
        CHECK(note.startTick >= 0);
        moved += note.startTick != static_cast<int32_t>(i * 240) ? 1 : 0;
        velocityChanged += note.velocity != 100 ? 1 : 0;
        CHECK(note.velocity >= 94);
        CHECK(note.velocity <= 106); // 60 per mille of 100
    }
    CHECK(moved >= 8);
    CHECK(velocityChanged >= 8);
    pattern.info.seed = 100; // another seed, another deviation
    CHECK_FALSE(first == renderOutput(pattern, noKickWith(&library)));
    // the amount scales it away
    pattern.voices[0].groove.amount = 0.0f;
    for (size_t i = 0; i < 16; ++i) {
        CHECK(starts(renderOutput(pattern, noKickWith(&library)))[i] == static_cast<int32_t>(i * 240));
    }
}

TEST_CASE("a template of two bars repeats every two bars", "[groove-library][output]") {
    std::string offsets = "[";
    std::string velocity = "[";
    for (int i = 0; i < 32; ++i) {
        offsets += (i ? "," : "") + std::to_string(i == 17 ? 30 : 0);
        velocity += (i ? "," : "") + std::string("1000");
    }
    const std::string text = R"({"id": "two", "nameDe": "Zwei", "bars": 2, "offsets": )" + offsets + "], \"velocity\": " + velocity + "]}";
    const auto library = GrooveLibrary::fromTexts({text});
    REQUIRE(library.size() == 1);
    Pattern pattern = makeEmptyPattern(4, "peak_time");
    for (uint32_t step = 0; step < 64; ++step) { // four bars of 16ths
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = 40;
        note.startTick = step * 240;
        note.lengthTicks = 100;
        pattern.voices[0].notes.push_back(note);
    }
    pattern.voices[0].groove.templateId = "two";
    const auto out = starts(renderOutput(pattern, noKickWith(&library)));
    for (size_t step = 0; step < 64; ++step) {
        const bool late = step % 32 == 17; // steps 17 and 49
        CHECK(out[step] == static_cast<int32_t>(step * 240) + (late ? 30 : 0));
    }
}

TEST_CASE("the swing control follows the template: off with swing of its own, on without", "[groove-library][groove]") {
    const auto library = shipped();
    Pattern pattern = bassLine();
    auto& track = pattern.voices[0];
    track.groove.swing = 0.54f;
    CHECK(effectiveGroove(pattern, track, &library).controlActive); // straight
    CHECK(effectiveGroove(pattern, track, &library).swing == 0.54f);
    track.groove.templateId = "swing_62";
    const auto swung = effectiveGroove(pattern, track, &library);
    CHECK_FALSE(swung.controlActive);
    CHECK(swung.swing == 0.62f); // what the template does, not what the control said
    track.groove.templateId = "swing_66";
    CHECK(effectiveGroove(pattern, track, &library).swing == 0.66f);
    track.groove.amount = 0.5f;
    CHECK(effectiveGroove(pattern, track, &library).swing == 0.58f); // half the delay
    track.groove.amount = 1.0f;
    for (const char* id : {"laid_back", "pushed", "humanize", "no_such_groove"}) {
        track.groove.templateId = id;
        INFO(id);
        CHECK(effectiveGroove(pattern, track, &library).controlActive);
        CHECK(effectiveGroove(pattern, track, &library).swing == 0.54f);
    }
    track.groove.templateId = "swing_62";
    CHECK(effectiveGroove(pattern, track, nullptr).controlActive); // without a library no template is known
}

TEST_CASE("the velocity profile of a template scales the velocity, by the amount", "[groove-library][output]") {
    const auto library = shipped();
    Pattern pattern = bassLine();
    pattern.voices[0].groove.templateId = "shuffle"; // the odd 16ths at 90 %
    auto velocities = [&](const Pattern& p) {
        std::vector<int> result;
        const auto out = renderOutput(p, noKickWith(&library));
        for (const auto& note : out.voices[0].notes) {
            result.push_back(note.velocity);
        }
        return result;
    };
    auto full = velocities(pattern);
    for (size_t i = 0; i < full.size(); ++i) {
        CHECK(full[i] == (i % 2 == 1 ? 90 : 100));
    }
    pattern.voices[0].groove.amount = 0.5f;
    auto half = velocities(pattern);
    for (size_t i = 0; i < half.size(); ++i) {
        CHECK(half[i] == (i % 2 == 1 ? 95 : 100));
    }
}

TEST_CASE("a template may carry an English name", "[groove-library]") {
    auto plain = loadGrooveTemplate(valid());
    REQUIRE(plain.groove.has_value());
    CHECK(plain.groove->nameEn.empty());
    auto named = loadGrooveTemplate(valid(R"(, "nameEn": "Test EN")"));
    REQUIRE(named.groove.has_value());
    CHECK(named.groove->nameEn == "Test EN");
    auto wrong = loadGrooveTemplate(valid(R"(, "nameEn": 5)"));
    REQUIRE(wrong.groove.has_value());
    CHECK(wrong.groove->nameEn.empty());
}

// -- the groove of a voice as a template of its own (D-245) ---------------------------------------------------------

namespace {

/// The library with the captured template, and the output of the track with it against the track as it was.
void checkCapture(const Pattern& pattern, const GrooveLibrary& base, const char* what) {
    INFO(what);
    const auto captured = captureGroove(pattern, pattern.voices[0], &base, "user_mine", "Mein Groove");
    REQUIRE(captured.has_value());
    CHECK(captured->swing);
    const std::string text = grooveTemplateToJson(*captured);
    const auto library = GrooveLibrary::withTexts(base, {text});
    REQUIRE(library.find("user_mine") != nullptr);
    Pattern with = pattern;
    with.voices[0].groove.templateId = "user_mine";
    with.voices[0].groove.swing = 0.5f; // the control is off for it
    with.voices[0].groove.amount = 1.0f;
    CHECK(renderOutput(with, noKickWith(&library)) == renderOutput(pattern, noKickWith(&base)));
}

} // namespace

TEST_CASE("a captured groove sounds like the groove it was taken from", "[groove-library][output][capture]") {
    const auto base = shipped();
    Pattern pattern = bassLine();
    for (size_t i = 0; i < pattern.voices[0].notes.size(); ++i) {
        pattern.voices[0].notes[i].velocity = static_cast<uint8_t>(60 + (i * 7) % 60);
    }
    pattern.voices[0].groove.swing = 0.62f;
    checkCapture(pattern, base, "swing control");
    pattern.voices[0].groove.amount = 0.5f;
    checkCapture(pattern, base, "swing control, half amount");
    pattern.voices[0].groove.amount = 1.0f;
    pattern.voices[0].groove.templateId = "laid_back";
    pattern.voices[0].groove.swing = 0.58f;
    checkCapture(pattern, base, "template without swing and the control on top");
    pattern.voices[0].groove.templateId = "swing_66";
    checkCapture(pattern, base, "template with swing");
    pattern.voices[0].groove.templateId = "shuffle";
    pattern.voices[0].groove.amount = 0.6f;
    checkCapture(pattern, base, "shuffle with velocity profile, 60 %");
    pattern.voices[0].groove.templateId = kGrooveDrumReference;
    pattern.voices[0].groove.amount = 0.7f;
    RhythmReference ref;
    ref.bars = 2;
    for (size_t i = 0; i < 32; ++i) {
        ref.timingOffsetTicks[i] = static_cast<int16_t>((i % 5) * 9 - 12);
        ref.velocity[i] = static_cast<uint8_t>(i % 3 == 0 ? 0 : 70 + (i * 11) % 50);
    }
    pattern.rhythmRef = ref;
    checkCapture(pattern, base, "drum reference of two bars, 70 %");
}

TEST_CASE("a captured groove has the swing in the offsets and the velocity factors of the template", "[groove-library][capture]") {
    const auto base = shipped();
    Pattern pattern = bassLine();
    pattern.voices[0].groove.swing = 0.58f;
    const auto groove = captureGroove(pattern, pattern.voices[0], &base, "user_x", "X");
    REQUIRE(groove.has_value());
    CHECK(groove->bars == 1);
    CHECK(groove->id == "user_x");
    CHECK(groove->nameDe == "X");
    REQUIRE(groove->offsetTicks.size() == 16);
    for (size_t i = 0; i < 16; ++i) {
        CHECK(groove->offsetTicks[i] == (i % 2 == 1 ? swingOffsetTicks(0.58f, 1.0f) : 0));
        CHECK(groove->velocityPermille[i] == 1000);
    }
    // Humanize keeps its deviation
    pattern.voices[0].groove.templateId = "humanize";
    const auto human = captureGroove(pattern, pattern.voices[0], &base, "user_h", "H");
    REQUIRE(human.has_value());
    CHECK(human->humanizeTicks == base.find("humanize")->humanizeTicks);
    CHECK(human->humanizeVelocityPermille == base.find("humanize")->humanizeVelocityPermille);
}

TEST_CASE("nothing is captured from a groove that changes nothing", "[groove-library][capture]") {
    const auto base = shipped();
    Pattern pattern = bassLine();
    CHECK_FALSE(captureGroove(pattern, pattern.voices[0], &base, "user_x", "X").has_value()); // straight
    pattern.voices[0].groove.swing = 0.62f;
    pattern.voices[0].groove.amount = 0.0f;
    CHECK_FALSE(captureGroove(pattern, pattern.voices[0], &base, "user_x", "X").has_value()); // no amount
    pattern.voices[0].groove.amount = 1.0f;
    pattern.voices[0].groove.templateId = kGrooveDrumReference; // a missing reference is straight with the swing control
    pattern.voices[0].groove.amount = 1.0f;
    CHECK(captureGroove(pattern, pattern.voices[0], &base, "user_x", "X").has_value());
    RhythmReference odd;
    odd.bars = 1;
    pattern.rhythmRef = odd;
    // a one-bar reference with no offsets and one velocity everywhere changes nothing
    for (auto& v : odd.velocity) {
        v = 0;
    }
    pattern.rhythmRef = odd;
    CHECK_FALSE(captureGroove(pattern, pattern.voices[0], &base, "user_x", "X").has_value());
}

TEST_CASE("a template is written as JSON that reads back the same", "[groove-library][capture]") {
    GrooveTemplate groove;
    groove.id = "user_round";
    groove.nameDe = "Rund \"herum\"";
    groove.nameEn = "Round";
    groove.bars = 2;
    for (int i = 0; i < 32; ++i) {
        groove.offsetTicks.push_back(i % 7 - 3);
        groove.velocityPermille.push_back(900 + i);
    }
    groove.swing = true;
    groove.humanizeTicks = 12;
    groove.humanizeVelocityPermille = 33;
    const auto back = loadGrooveTemplate(grooveTemplateToJson(groove));
    REQUIRE(back.ok());
    CHECK(*back.groove == groove);
    groove.humanizeTicks = 0;
    groove.humanizeVelocityPermille = 0;
    groove.nameEn.clear();
    groove.swing = false;
    const auto plain = loadGrooveTemplate(grooveTemplateToJson(groove));
    REQUIRE(plain.ok());
    CHECK(*plain.groove == groove);
}

TEST_CASE("the id of an own template comes from its name and is free", "[groove-library][capture]") {
    const auto base = shipped();
    CHECK(grooveIdFromName("Mein Groove 2", base) == "user_mein_groove_2");
    CHECK(grooveIdFromName("  Laid-Back!! ", base) == "user_laid_back");
    CHECK(grooveIdFromName("Schöne Größe über Äpfel", base) == "user_schoene_groesse_ueber_aepfel");
    CHECK(grooveIdFromName("!!!", base) == "user_groove");
    CHECK(grooveIdFromName("日本", base) == "user_groove");
    CHECK(grooveIdFromName(std::string(100, 'a'), base).size() == 5 + 30);
    CHECK(loadGrooveTemplate(valid()).ok());
    // taken ids get a number
    GrooveTemplate groove = *loadGrooveTemplate(valid()).groove;
    groove.id = "user_mein_groove";
    const auto library = GrooveLibrary::withTexts(base, {grooveTemplateToJson(groove)});
    CHECK(grooveIdFromName("Mein Groove", library) == "user_mein_groove_2");
    groove.id = "user_mein_groove_2";
    const auto more = GrooveLibrary::withTexts(library, {grooveTemplateToJson(groove)});
    CHECK(grooveIdFromName("Mein Groove", more) == "user_mein_groove_3");
}

TEST_CASE("own templates come after the shipped ones, bad and doubled ones are reported", "[groove-library][capture]") {
    const auto base = shipped();
    GrooveTemplate a = *loadGrooveTemplate(valid()).groove;
    a.id = "user_b";
    GrooveTemplate b = a;
    b.id = "user_a";
    GrooveTemplate shippedTwin = a;
    shippedTwin.id = "laid_back";
    const std::string bad = "{ nonsense";
    const auto library = GrooveLibrary::withTexts(
        base, {grooveTemplateToJson(a), grooveTemplateToJson(b), bad, grooveTemplateToJson(shippedTwin), grooveTemplateToJson(a)});
    REQUIRE(library.size() == base.size() + 2);
    for (size_t i = 0; i < base.size(); ++i) {
        CHECK(library.templates()[i] == base.templates()[i]);
    }
    CHECK(library.templates()[base.size()].id == "user_a"); // in the order of the ids
    CHECK(library.templates()[base.size() + 1].id == "user_b");
    CHECK(library.problems().size() == base.problems().size() + 3);
    CHECK(base.find("user_a") == nullptr); // the base is not changed
}
