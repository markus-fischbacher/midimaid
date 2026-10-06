#include "Mmgen.h"
#include "core/KickGrid.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::tools;
using namespace mm::core;
namespace fs = std::filesystem;

namespace {

const std::string kStylesDir = std::string(MIDIMAID_RESOURCE_DIR) + "/styles";

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(kStylesDir + "/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

std::vector<StyleProfile> allStyles() {
    return {loadShipped("hard_industrial"), loadShipped("melodic_techno"), loadShipped("peak_time")};
}

ParseResult parse(std::initializer_list<const char*> args) {
    return parseArguments(std::vector<std::string>(args.begin(), args.end()));
}

/// A unique temporary folder, removed at the end of the test.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) {
        path = fs::temp_directory_path() / ("mmgen_test_" + name);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string str() const { return path.string(); }
};

std::vector<uint8_t> readBytes(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::string readText(const fs::path& file) {
    const auto bytes = readBytes(file);
    return std::string(bytes.begin(), bytes.end());
}

// -- a small Standard MIDI file reader ------------------------------------------------------------------------------

struct NoteOn {
    uint32_t tick;
    int channel;
    int pitch;
};

struct MidiInfo {
    bool valid = false;
    int format = -1;
    int division = 0;
    int tracks = 0;
    uint32_t endTick = 0;
    double bpm = 0;
    std::string trackName;
    std::vector<NoteOn> noteOns;
    int noteOffs = 0;
    uint32_t lastNoteOffTick = 0;
};

MidiInfo readMidi(const std::vector<uint8_t>& b) {
    MidiInfo info;
    auto u32 = [&](size_t i) {
        return (static_cast<uint32_t>(b[i]) << 24) | (static_cast<uint32_t>(b[i + 1]) << 16) |
               (static_cast<uint32_t>(b[i + 2]) << 8) | b[i + 3];
    };
    if (b.size() < 22 || std::string(b.begin(), b.begin() + 4) != "MThd" || u32(4) != 6) {
        return info;
    }
    info.format = (b[8] << 8) | b[9];
    info.tracks = (b[10] << 8) | b[11];
    info.division = (b[12] << 8) | b[13];
    size_t pos = 14;
    if (std::string(b.begin() + static_cast<long>(pos), b.begin() + static_cast<long>(pos) + 4) != "MTrk") {
        return info;
    }
    const size_t length = u32(pos + 4);
    pos += 8;
    const size_t end = pos + length;
    uint32_t tick = 0;
    uint8_t status = 0;
    while (pos < end && pos < b.size()) {
        uint32_t delta = 0;
        while (true) {
            const uint8_t byte = b[pos++];
            delta = (delta << 7) | (byte & 0x7f);
            if ((byte & 0x80) == 0) {
                break;
            }
        }
        tick += delta;
        if (b[pos] & 0x80) {
            status = b[pos++];
        }
        if (status == 0xff) {
            const uint8_t type = b[pos++];
            const size_t len = b[pos++];
            if (type == 0x51 && len == 3) {
                const uint32_t micros = (static_cast<uint32_t>(b[pos]) << 16) | (b[pos + 1] << 8) | b[pos + 2];
                info.bpm = 60000000.0 / micros;
            } else if (type == 0x03) {
                info.trackName.assign(b.begin() + static_cast<long>(pos), b.begin() + static_cast<long>(pos + len));
            } else if (type == 0x2f) {
                info.endTick = tick;
            }
            pos += len;
        } else if ((status & 0xf0) == 0x90) {
            if (b[pos + 1] > 0) {
                info.noteOns.push_back({tick, (status & 0x0f) + 1, b[pos]});
            } else {
                ++info.noteOffs;
                info.lastNoteOffTick = std::max(info.lastNoteOffTick, tick);
            }
            pos += 2;
        } else if ((status & 0xf0) == 0x80) {
            ++info.noteOffs;
            info.lastNoteOffTick = std::max(info.lastNoteOffTick, tick);
            pos += 2;
        } else {
            return info;
        }
    }
    info.valid = true;
    return info;
}

} // namespace

TEST_CASE("arguments: defaults of the commands", "[mmgen]") {
    const auto series = parse({"series"});
    REQUIRE(series.ok);
    CHECK(series.options.command == Command::Series);
    CHECK(series.options.style == "all");
    CHECK(series.options.seeds == 20);
    CHECK(series.options.firstSeed == 1);
    CHECK(series.options.energies == std::vector<int>{30, 60, 90});
    CHECK(series.options.bars == 4);
    CHECK(series.options.creativity == 40);
    CHECK(series.options.groove);
    CHECK_FALSE(series.options.bpm.has_value());
    CHECK(series.options.out == ".");
    const auto one = parse({"one", "--style", "peak_time"});
    REQUIRE(one.ok);
    CHECK(one.options.command == Command::One);
    CHECK(one.options.seed == 1);
    CHECK(parse({"help"}).options.command == Command::Help);
    CHECK(parse({"--help"}).options.command == Command::Help);
    CHECK(parse({"-h"}).ok);
}

TEST_CASE("arguments: all values are read and checked", "[mmgen]") {
    const auto r = parse({"series", "--style", "melodic_techno", "--archetype", "arp", "--seeds", "5", "--first-seed",
                          "10", "--energies", "0,50,100", "--bars", "16", "--creativity", "75", "--bpm", "126.5",
                          "--no-groove", "--out", "dir", "--styles", "sd"});
    REQUIRE(r.ok);
    const Options& o = r.options;
    CHECK(o.style == "melodic_techno");
    CHECK(o.archetype == "arp");
    CHECK(o.seeds == 5);
    CHECK(o.firstSeed == 10);
    CHECK(o.energies == std::vector<int>{0, 50, 100});
    CHECK(o.bars == 16);
    CHECK(o.creativity == 75);
    CHECK(o.bpm.value_or(0) == 126.5);
    CHECK_FALSE(o.groove);
    CHECK(o.out == "dir");
    CHECK(o.stylesDir == "sd");
    const auto one = parse({"one", "--style", "x", "--energy", "70", "--seed", "42", "--bass", "gallop", "--melody", "arp"});
    REQUIRE(one.ok);
    CHECK(one.options.energies == std::vector<int>{70});
    CHECK(one.options.seed == 42);
    CHECK(one.options.bassArchetype == "gallop");
    CHECK(one.options.melodyArchetype == "arp");
    const auto summary = parse({"summary", "m.csv"});
    REQUIRE(summary.ok);
    CHECK(summary.options.manifest == "m.csv");
    CHECK(parse({"summary", "--manifest", "n.csv"}).options.manifest == "n.csv");
}

TEST_CASE("arguments: errors name the argument", "[mmgen]") {
    auto errorOf = [](std::initializer_list<const char*> args) { return parse(args).error; };
    CHECK_FALSE(parseArguments({}).ok);
    CHECK(errorOf({"bogus"}).find("unknown command") != std::string::npos);
    CHECK(errorOf({"one"}).find("--style") != std::string::npos);
    CHECK(errorOf({"summary"}).find("manifest") != std::string::npos);
    CHECK(errorOf({"series", "--bars", "3"}).find("--bars") != std::string::npos);
    CHECK(errorOf({"series", "--bars", "x"}).find("--bars") != std::string::npos);
    CHECK(errorOf({"series", "--seeds", "0"}).find("--seeds") != std::string::npos);
    CHECK(errorOf({"series", "--seeds", "10001"}).find("--seeds") != std::string::npos);
    CHECK(errorOf({"series", "--energies", "10,200"}).find("--energies") != std::string::npos);
    CHECK(errorOf({"series", "--energies", ""}).find("--energies") != std::string::npos);
    CHECK(errorOf({"one", "--style", "x", "--energy", "101"}).find("--energy") != std::string::npos);
    CHECK(errorOf({"series", "--creativity", "-1"}).find("--creativity") != std::string::npos);
    CHECK(errorOf({"series", "--bpm", "10"}).find("--bpm") != std::string::npos);
    CHECK(errorOf({"series", "--bpm", "fast"}).find("--bpm") != std::string::npos);
    CHECK(errorOf({"series", "--seed", "-4"}).find("--seed") != std::string::npos);
    CHECK(errorOf({"series", "--first-seed", "-4"}).find("--first-seed") != std::string::npos);
    CHECK(errorOf({"series", "--out"}).find("missing value") != std::string::npos);
    CHECK(errorOf({"series", "--nonsense", "1"}).find("--nonsense") != std::string::npos);
    CHECK(errorOf({"series", "stray"}).find("stray") != std::string::npos);
    for (const char* bars : {"1", "2", "4", "8", "16"}) {
        CHECK(parse({"series", "--bars", bars}).ok);
    }
}

TEST_CASE("a series covers every archetype, energy and seed in a fixed order", "[mmgen]") {
    const auto styles = allStyles();
    Options options = parse({"series", "--style", "peak_time"}).options;
    std::vector<Job> jobs;
    std::string error;
    REQUIRE(planSeries(options, styles, jobs, error));
    // peak time: 3 bass + 4 melody archetypes, 3 energies, 20 seeds
    REQUIRE(jobs.size() == 7u * 3u * 20u);
    CHECK(jobs.front().styleId == "peak_time");
    CHECK(jobs.front().archetype == "rolling16");
    CHECK(jobs.front().archetypeRole == VoiceRole::Bass);
    CHECK(jobs.front().energy == 30);
    CHECK(jobs.front().seed == 1);
    CHECK(jobs.front().bassArchetype == "rolling16");
    CHECK(jobs.front().melodyArchetype.empty());
    CHECK(jobs[1].seed == 2);
    CHECK(jobs[20].energy == 60);
    CHECK(jobs[20].seed == 1);
    CHECK(jobs.back().archetype == "acid_siren");
    CHECK(jobs.back().archetypeRole == VoiceRole::Melody);
    CHECK(jobs.back().energy == 90);
    CHECK(jobs.back().seed == 20);
    CHECK(jobs.back().melodyArchetype == "acid_siren");
    CHECK(jobs.back().bassArchetype.empty());
    std::set<std::string> bass;
    std::set<std::string> melody;
    for (const Job& job : jobs) {
        (job.archetypeRole == VoiceRole::Bass ? bass : melody).insert(job.archetype);
    }
    CHECK(bass == std::set<std::string>{"rolling16", "offbeat", "gallop"});
    CHECK(melody == std::set<std::string>{"hypnotic_motif", "stabs", "arp", "acid_siren"});

    options.style = "all";
    REQUIRE(planSeries(options, styles, jobs, error));
    CHECK(jobs.size() == 21u * 3u * 20u);
    // the order of the styles is the one of the given profiles
    CHECK(jobs.front().styleId == "hard_industrial");
    CHECK(jobs.back().styleId == "peak_time");
}

TEST_CASE("a series can be narrowed to one archetype, seeds and energies", "[mmgen]") {
    const auto styles = allStyles();
    Options options = parse({"series", "--style", "all", "--archetype", "acid_siren", "--seeds", "3", "--first-seed", "7",
                             "--energies", "40", "--bars", "8", "--creativity", "10"})
                          .options;
    std::vector<Job> jobs;
    std::string error;
    REQUIRE(planSeries(options, styles, jobs, error));
    // acid_siren belongs to Peak Time and to Hard/Industrial
    REQUIRE(jobs.size() == 6);
    CHECK(jobs[0].styleId == "hard_industrial");
    CHECK(jobs[3].styleId == "peak_time");
    CHECK(jobs[0].seed == 7);
    CHECK(jobs[2].seed == 9);
    for (const Job& job : jobs) {
        CHECK(job.energy == 40);
        CHECK(job.bars == 8);
        CHECK(job.creativity == 10);
        CHECK(job.archetype == "acid_siren");
    }
}

TEST_CASE("series planning refuses unknown archetypes and styles", "[mmgen]") {
    const auto styles = allStyles();
    std::vector<Job> jobs;
    std::string error;
    Options options = parse({"series", "--archetype", "nonsense"}).options;
    CHECK_FALSE(planSeries(options, styles, jobs, error));
    CHECK(error.find("nonsense") != std::string::npos);
    options = parse({"series", "--style", "peak_time", "--archetype", "lead_phrase"}).options;
    CHECK_FALSE(planSeries(options, styles, jobs, error));
    CHECK(error.find("lead_phrase") != std::string::npos);
    options = parse({"series", "--style", "no_such_style"}).options;
    CHECK_FALSE(planSeries(options, styles, jobs, error));
    CHECK(jobs.empty());
}

TEST_CASE("file names carry style, archetype, energy and seed", "[mmgen]") {
    Job job;
    job.styleId = "peak_time";
    job.archetype = "rolling16";
    job.energy = 30;
    job.seed = 7;
    CHECK(jobFolder(job) == "peak_time/rolling16");
    CHECK(jobBaseName(job) == "peak_time_rolling16_e030_s007");
    job.energy = 100;
    job.seed = 1234;
    CHECK(jobBaseName(job) == "peak_time_rolling16_e100_s1234");
    job.archetype.clear();
    CHECK(jobFolder(job) == "peak_time/auto");
    CHECK(jobBaseName(job) == "peak_time_auto_e100_s1234");
    Options options = parse({"one", "--style", "peak_time", "--energy", "55", "--seed", "9", "--bars", "2",
                             "--creativity", "20", "--bass", "gallop"})
                          .options;
    const Job one = planOne(options);
    CHECK(one.styleId == "peak_time");
    CHECK(one.energy == 55);
    CHECK(one.seed == 9);
    CHECK(one.bars == 2);
    CHECK(one.creativity == 20);
    CHECK(one.bassArchetype == "gallop");
    CHECK(one.archetype.empty());
}

TEST_CASE("the MIDI files hold the voices, the kick and the tempo of the pattern", "[mmgen]") {
    const StyleProfile style = loadShipped("peak_time");
    Job job;
    job.styleId = "peak_time";
    job.seed = 3;
    job.bars = 2;
    const auto pattern = generateJob(job, style);
    REQUIRE(pattern.has_value());
    const double bpm = 128.0;
    const MidiInfo bass = readMidi(midiBytes(*pattern, style, FileKind::Bass, bpm, true));
    const MidiInfo melody = readMidi(midiBytes(*pattern, style, FileKind::Melody, bpm, true));
    const MidiInfo all = readMidi(midiBytes(*pattern, style, FileKind::All, bpm, true));
    for (const MidiInfo* info : {&bass, &melody, &all}) {
        REQUIRE(info->valid);
        CHECK(info->format == 0);
        CHECK(info->tracks == 1);
        CHECK(info->division == 960);
        CHECK(info->endTick == 2 * kTicksPerBar);
        CHECK(info->bpm > 127.9);
        CHECK(info->bpm < 128.1);
        CHECK(info->trackName == "MidiMaid peak_time");
        CHECK(info->noteOffs == static_cast<int>(info->noteOns.size()));
    }
    CHECK_FALSE(bass.noteOns.empty());
    CHECK_FALSE(melody.noteOns.empty());
    for (const NoteOn& n : bass.noteOns) {
        CHECK(n.channel == 1);
    }
    for (const NoteOn& n : melody.noteOns) {
        CHECK(n.channel == 2);
    }
    size_t kicks = 0;
    std::vector<uint32_t> kickPositions;
    for (const NoteOn& n : all.noteOns) {
        if (n.channel == 10) {
            CHECK(n.pitch == 36);
            kickPositions.push_back(n.tick);
            ++kicks;
        }
    }
    CHECK(kickPositions == kickTicks(*pattern));
    CHECK(all.noteOns.size() == bass.noteOns.size() + melody.noteOns.size() + kicks);
    CHECK(midiBytes(*pattern, style, FileKind::All, bpm, true) == midiBytes(*pattern, style, FileKind::All, bpm, true));
}

TEST_CASE("without groove the notes stay on the 16th grid, with groove the swing moves them", "[mmgen]") {
    const StyleProfile style = loadShipped("peak_time"); // swing 52 / 55 %
    Job job;
    job.styleId = "peak_time";
    job.seed = 2;
    job.bars = 4;
    const auto pattern = generateJob(job, style);
    REQUIRE(pattern.has_value());
    const MidiInfo straight = readMidi(midiBytes(*pattern, style, FileKind::All, 128.0, false));
    const MidiInfo swung = readMidi(midiBytes(*pattern, style, FileKind::All, 128.0, true));
    for (const NoteOn& n : straight.noteOns) {
        CHECK(n.tick % 240 == 0);
    }
    size_t off = 0;
    for (const NoteOn& n : swung.noteOns) {
        off += n.tick % 240 != 0 ? 1 : 0;
    }
    CHECK(off > 0);
    CHECK(straight.noteOns.size() == swung.noteOns.size());
}

TEST_CASE("the default tempo is the middle of the range of the style", "[mmgen]") {
    CHECK(defaultBpm(loadShipped("peak_time")) == (loadShipped("peak_time").tempoMin + loadShipped("peak_time").tempoMax) / 2.0);
    StyleProfile style;
    style.tempoMin = 120;
    style.tempoMax = 131;
    CHECK(defaultBpm(style) == 125.5);
}

TEST_CASE("csv fields are escaped and read back", "[mmgen]") {
    CHECK(csvEscape("plain") == "plain");
    CHECK(csvEscape("a,b") == "\"a,b\"");
    CHECK(csvEscape("a;b") == "\"a;b\"");
    CHECK(csvEscape("say \"hi\"") == "\"say \"\"hi\"\"\"");
    CHECK(csvEscape("two\nlines") == "\"two\nlines\"");
    CHECK(csvEscape("") == "");
    for (const std::string& text : {std::string("plain"), std::string("a,b"), std::string("say \"hi\""), std::string("")}) {
        CHECK(splitCsvLine(csvEscape(text) + "," + csvEscape("x"))[0] == text);
    }
    CHECK(splitCsvLine("a,b,,d") == std::vector<std::string>{"a", "b", "", "d"});
    CHECK(splitCsvLine("a;b;;d", ';') == std::vector<std::string>{"a", "b", "", "d"});
    CHECK(splitCsvLine("\"a;b\";c", ';') == std::vector<std::string>{"a;b", "c"});
    CHECK(splitCsvLine("") == std::vector<std::string>{""});
}

TEST_CASE("a manifest row describes the pattern, a failed job has no file", "[mmgen]") {
    const StyleProfile style = loadShipped("melodic_techno");
    Job job;
    job.styleId = "melodic_techno";
    job.archetype = "chord_arp";
    job.archetypeRole = VoiceRole::Melody;
    job.melodyArchetype = "chord_arp";
    job.energy = 60;
    job.seed = 4;
    job.bars = 4;
    const auto pattern = generateJob(job, style);
    REQUIRE(pattern.has_value());
    const ManifestRow row = rowFor(job, &*pattern, "x/y_all.mid");
    CHECK(row.file == "x/y_all.mid");
    CHECK(row.style == "melodic_techno");
    CHECK(row.archetype == "chord_arp");
    CHECK(row.bars == 4);
    CHECK(row.seed == 4);
    CHECK(row.winnerSeed == pattern->info.winnerSeed);
    CHECK(row.energy == 60);
    CHECK(row.creativity == 40);
    CHECK(row.melodyArchetype == "chord_arp");
    CHECK_FALSE(row.bassArchetype.empty());
    CHECK(row.bassArchetype != "auto");
    CHECK(row.scale == pattern->context.scaleId);
    CHECK(row.score == pattern->qualityScore);
    CHECK(row.status == "ok");
    CHECK(row.rating.empty());
    CHECK_FALSE(row.chords.empty());
    CHECK_FALSE(row.key.empty());
    const auto count = [](const std::string& text) {
        return splitCsvLine(text).size();
    };
    CHECK(count(manifestLine(row)) == count(manifestHeader()));
    const std::vector<std::string> columns = splitCsvLine(manifestLine(row));
    CHECK(columns.front() == "x/y_all.mid");
    CHECK(columns.back().empty());

    const ManifestRow failed = rowFor(job, nullptr, "ignored");
    CHECK(failed.file.empty());
    CHECK(failed.status == "no_valid_candidate");
    CHECK(failed.bassArchetype == "auto");
    CHECK(count(manifestLine(failed)) == count(manifestHeader()));
    Job automatic;
    automatic.styleId = "peak_time";
    CHECK(rowFor(automatic, nullptr, "").archetype == "auto");
}

TEST_CASE("ratings accept the English and German words", "[mmgen]") {
    for (const char* text : {"ok", "OK", " Ok ", "sofort", "Sofort nutzbar", "+", "1", "o"}) {
        CHECK(normalizeRating(text) == "ok");
    }
    for (const char* text : {"edit", "Edit", "nachbearbeiten", "~", "2", "e"}) {
        CHECK(normalizeRating(text) == "edit");
    }
    for (const char* text : {"bad", "unbrauchbar", "-", "x", "3", "b"}) {
        CHECK(normalizeRating(text) == "bad");
    }
    for (const char* text : {"", "maybe", "okay", "4", " "}) {
        CHECK(normalizeRating(text).empty());
    }
}

namespace {

std::string manifestOf(const std::vector<std::vector<std::string>>& rows, char delimiter = ',') {
    // columns: style, archetype, energy, status, rating
    std::string text;
    auto join = [&](const std::vector<std::string>& fields) {
        std::string line;
        for (size_t i = 0; i < fields.size(); ++i) {
            line += (i == 0 ? "" : std::string(1, delimiter)) + fields[i];
        }
        return line + "\n";
    };
    text += join({"file", "style", "archetype", "energy", "status", "rating"});
    for (const auto& r : rows) {
        text += join({"f.mid", r[0], r[1], r[2], r[3], r[4]});
    }
    return text;
}

} // namespace

TEST_CASE("the summary counts ratings per archetype, energy and style", "[mmgen]") {
    const std::string text = manifestOf({
        {"peak_time", "rolling16", "30", "ok", "ok"},
        {"peak_time", "rolling16", "30", "ok", "ok"},
        {"peak_time", "rolling16", "30", "ok", "edit"},
        {"peak_time", "rolling16", "30", "ok", "bad"},
        {"peak_time", "rolling16", "60", "ok", "sofort"},
        {"peak_time", "rolling16", "60", "no_valid_candidate", ""},
        {"peak_time", "arp", "30", "ok", ""},
        {"peak_time", "arp", "30", "ok", "maybe"},
        {"hard_industrial", "rumble", "30", "ok", "ok"},
        {"hard_industrial", "rumble", "30", "ok", "ok"},
    });
    const Summary s = summarize(text);
    REQUIRE(s.error.empty());
    REQUIRE(s.lines.size() == 4);
    CHECK(s.lines[0].style == "peak_time");
    CHECK(s.lines[0].archetype == "rolling16");
    CHECK(s.lines[0].energy == 30);
    CHECK(s.lines[0].ok == 2);
    CHECK(s.lines[0].edit == 1);
    CHECK(s.lines[0].bad == 1);
    CHECK(s.lines[0].rated() == 4);
    CHECK(s.lines[0].okPermille() == 500);
    CHECK(s.lines[1].energy == 60);
    CHECK(s.lines[1].ok == 1);
    CHECK(s.lines[1].failed == 1);
    CHECK(s.lines[1].okPermille() == 1000);
    CHECK(s.lines[2].archetype == "arp");
    CHECK(s.lines[2].unrated == 2);
    CHECK(s.lines[2].okPermille() == 0);
    REQUIRE(s.styles.size() == 2);
    CHECK(s.styles[0].style == "peak_time");
    CHECK(s.styles[0].ok == 3);
    CHECK(s.styles[0].rated() == 5);
    CHECK(s.styles[0].okPermille() == 600);
    CHECK(s.styles[0].failed == 1);
    CHECK(s.styles[0].unrated == 2);
    CHECK(s.styles[1].style == "hard_industrial");
    CHECK(s.styles[1].okPermille() == 1000);
    CHECK(s.total.ok == 5);
    CHECK(s.total.rated() == 7);
    CHECK(s.total.failed == 1);
    CHECK(s.total.unrated == 2);
    CHECK(s.unknownRatings == 1);
    const std::string report = formatSummary(s);
    CHECK(report.find("peak_time / rolling16 / e30") != std::string::npos);
    CHECK(report.find("BELOW TARGET") != std::string::npos); // peak time 60 % < 80 %
    CHECK(report.find("target reached") != std::string::npos); // hard/industrial 100 %
    CHECK(report.find("failed generations: 1 of 10") != std::string::npos);
    CHECK(report.find("50.0 %") != std::string::npos);
}

TEST_CASE("the target is 80 percent and counts exactly", "[mmgen]") {
    std::vector<std::vector<std::string>> rows;
    for (int i = 0; i < 4; ++i) {
        rows.push_back({"s", "a", "30", "ok", "ok"});
    }
    rows.push_back({"s", "a", "30", "ok", "edit"});
    Summary s = summarize(manifestOf(rows));
    CHECK(s.styles[0].okPermille() == 800);
    CHECK(formatSummary(s).find("target reached") != std::string::npos);
    rows.push_back({"s", "a", "30", "ok", "bad"});
    s = summarize(manifestOf(rows));
    CHECK(s.styles[0].okPermille() == 666);
    CHECK(formatSummary(s).find("BELOW TARGET") != std::string::npos);
    CHECK(kTargetOkPermille == 800);
}

TEST_CASE("the summary reads semicolons, windows line ends and reordered columns", "[mmgen]") {
    std::string text = manifestOf({{"s", "a", "30", "ok", "ok"}, {"s", "a", "30", "ok", "bad"}}, ';');
    CHECK(summarize(text).total.rated() == 2);
    std::string crlf;
    for (const char c : manifestOf({{"s", "a", "30", "ok", "ok"}})) {
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    }
    CHECK(summarize(crlf).total.ok == 1);
    const std::string reordered = "rating,status,energy,archetype,style\nok,ok,30,a,s\n\nedit,ok,30,a,s\n";
    const Summary s = summarize(reordered);
    CHECK(s.error.empty());
    CHECK(s.total.ok == 1);
    CHECK(s.total.edit == 1);
}

TEST_CASE("the summary reports a broken manifest", "[mmgen]") {
    CHECK_FALSE(summarize("").error.empty());
    const Summary missing = summarize("style,archetype,energy,status\ns,a,30,ok\n");
    CHECK(missing.error.find("rating") != std::string::npos);
    CHECK(formatSummary(missing).find("error:") == 0);
    const Summary onlyHeader = summarize(manifestOf({}));
    CHECK(onlyHeader.error.empty());
    CHECK(onlyHeader.total.rated() == 0);
    CHECK(formatSummary(onlyHeader).find("failed generations: 0 of 0") != std::string::npos);
    // a row with fewer fields than the header does not crash
    const Summary shortRow = summarize("style,archetype,energy,status,rating\ns,a\n");
    CHECK(shortRow.error.empty());
    CHECK(shortRow.total.unrated == 1);
}

TEST_CASE("run: one writes three files and prints the manifest row", "[mmgen]") {
    TempDir dir("one");
    std::string out;
    std::string err;
    const int code = run({"one", "--style", "peak_time", "--seed", "3", "--bars", "2", "--out", dir.str(), "--styles",
                          kStylesDir},
                         out, err);
    REQUIRE(code == 0);
    CHECK(err.empty());
    for (const char* suffix : {"_bass.mid", "_melody.mid", "_all.mid"}) {
        const fs::path file = dir.path / (std::string("peak_time_auto_e030_s003") + suffix);
        REQUIRE(fs::exists(file));
        CHECK(readMidi(readBytes(file)).valid);
    }
    CHECK(out.find(manifestHeader()) == 0);
    CHECK(out.find("peak_time_auto_e030_s003_all.mid") != std::string::npos);
    // the same call writes the same bytes
    TempDir again("one_again");
    REQUIRE(run({"one", "--style", "peak_time", "--seed", "3", "--bars", "2", "--out", again.str(), "--styles",
                 kStylesDir},
                out, err) == 0);
    for (const char* suffix : {"_bass.mid", "_melody.mid", "_all.mid"}) {
        const std::string name = std::string("peak_time_auto_e030_s003") + suffix;
        CHECK(readBytes(dir.path / name) == readBytes(again.path / name));
    }
}

TEST_CASE("run: a series writes folders per style and archetype and a manifest", "[mmgen]") {
    TempDir dir("series");
    std::string out;
    std::string err;
    REQUIRE(run({"series", "--style", "hard_industrial", "--seeds", "2", "--energies", "30,90", "--bars", "2", "--out",
                 dir.str(), "--styles", kStylesDir},
                out, err) == 0);
    CHECK(err.empty());
    CHECK(out.find("generated 28 patterns") != std::string::npos);
    // 7 archetypes x 2 energies x 2 seeds = 28 patterns, 3 files each
    size_t files = 0;
    std::set<std::string> folders;
    for (const auto& entry : fs::recursive_directory_iterator(dir.path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".mid") {
            ++files;
            folders.insert(entry.path().parent_path().filename().string());
        }
    }
    CHECK(files == 84);
    CHECK(folders == std::set<std::string>{"rumble", "hard_offbeat", "roll16_aggressive", "aggro_stabs", "atonal_motif",
                                           "acid_siren", "sparse_hits"});
    const std::string manifest = readText(dir.path / "manifest.csv");
    std::istringstream lines(manifest);
    std::string line;
    std::getline(lines, line);
    CHECK(line == manifestHeader());
    int rows = 0;
    while (std::getline(lines, line)) {
        ++rows;
        const auto fields = splitCsvLine(line);
        REQUIRE(fields.size() == splitCsvLine(manifestHeader()).size());
        CHECK(fs::exists(dir.path / fields[0]));
        CHECK(fields[1] == "hard_industrial");
        CHECK(fields.back().empty());
    }
    CHECK(rows == 28);
    // the empty manifest has nothing rated yet
    std::string summary;
    REQUIRE(run({"summary", (dir.path / "manifest.csv").string()}, summary, err) == 0);
    CHECK(summary.find("not rated yet") != std::string::npos);
    CHECK(summary.find("hard_industrial / rumble / e30") != std::string::npos);

    // the series is deterministic
    TempDir again("series_again");
    REQUIRE(run({"series", "--style", "hard_industrial", "--seeds", "2", "--energies", "30,90", "--bars", "2", "--out",
                 again.str(), "--styles", kStylesDir},
                out, err) == 0);
    CHECK(readText(again.path / "manifest.csv") == manifest);
    CHECK(readBytes(again.path / "hard_industrial/rumble/hard_industrial_rumble_e030_s001_all.mid") ==
          readBytes(dir.path / "hard_industrial/rumble/hard_industrial_rumble_e030_s001_all.mid"));
}

TEST_CASE("run: the ratings of a filled manifest are evaluated", "[mmgen]") {
    TempDir dir("rated");
    std::string out;
    std::string err;
    REQUIRE(run({"series", "--style", "peak_time", "--archetype", "gallop", "--seeds", "5", "--energies", "60", "--bars",
                 "1", "--out", dir.str(), "--styles", kStylesDir},
                out, err) == 0);
    // fill the rating column: 4 x ok, 1 x edit
    std::istringstream in(readText(dir.path / "manifest.csv"));
    std::string line;
    std::string filled;
    int row = 0;
    while (std::getline(in, line)) {
        if (row > 0) {
            line += row <= 4 ? "ok" : "edit";
        }
        filled += line + "\n";
        ++row;
    }
    {
        std::ofstream stream(dir.path / "filled.csv", std::ios::binary);
        stream << filled;
    }
    REQUIRE(run({"summary", (dir.path / "filled.csv").string()}, out, err) == 0);
    CHECK(out.find("peak_time / gallop / e60") != std::string::npos);
    CHECK(out.find("80.0 %") != std::string::npos);
    CHECK(out.find("target reached") != std::string::npos);
    CHECK(out.find("failed generations: 0 of 5") != std::string::npos);
}

TEST_CASE("run: errors give the exit codes and messages", "[mmgen]") {
    std::string out;
    std::string err;
    CHECK(run({}, out, err) == 1);
    CHECK(err.find("error:") == 0);
    CHECK(err.find("mmgen one") != std::string::npos);
    CHECK(run({"help"}, out, err) == 0);
    CHECK(out.find("mmgen series") != std::string::npos);
    CHECK(run({"series", "--bars", "3"}, out, err) == 1);
    TempDir dir("errors");
    CHECK(run({"one", "--style", "no_such_style", "--out", dir.str(), "--styles", kStylesDir}, out, err) == 2);
    CHECK(err.find("unknown style") != std::string::npos);
    CHECK(run({"one", "--style", "peak_time", "--bass", "nonsense", "--out", dir.str(), "--styles", kStylesDir}, out,
              err) == 1);
    CHECK(err.find("nonsense") != std::string::npos);
    CHECK(run({"series", "--archetype", "nonsense", "--out", dir.str(), "--styles", kStylesDir}, out, err) == 1);
    CHECK(run({"series", "--style", "all", "--styles", (dir.path / "missing").string()}, out, err) == 2);
    CHECK(run({"summary", (dir.path / "missing.csv").string()}, out, err) == 2);
    {
        std::ofstream stream(dir.path / "broken.csv");
        stream << "a,b\n1,2\n";
    }
    CHECK(run({"summary", (dir.path / "broken.csv").string()}, out, err) == 2);
    CHECK(err.find("error:") == 0);
    // the output folder is a file
    {
        std::ofstream stream(dir.path / "blocker");
        stream << "x";
    }
    CHECK(run({"one", "--style", "peak_time", "--out", (dir.path / "blocker").string(), "--styles", kStylesDir}, out,
              err) == 2);
    CHECK(err.find("error:") == 0);
    CHECK(run({"series", "--style", "peak_time", "--archetype", "gallop", "--seeds", "1", "--out",
               (dir.path / "blocker" / "sub").string(), "--styles", kStylesDir},
              out, err) == 2);
}

TEST_CASE("run: a broken style profile is reported with its file", "[mmgen]") {
    TempDir dir("styles");
    {
        std::ofstream stream(dir.path / "broken.json");
        stream << "{ not json";
    }
    std::string out;
    std::string err;
    CHECK(run({"one", "--style", "broken", "--styles", dir.str(), "--out", dir.str()}, out, err) == 2);
    CHECK(err.find("broken.json") != std::string::npos);
    CHECK(run({"series", "--style", "all", "--styles", dir.str(), "--out", dir.str()}, out, err) == 2);
    TempDir empty("styles_empty");
    CHECK(run({"series", "--style", "all", "--styles", empty.str(), "--out", dir.str()}, out, err) == 2);
}

TEST_CASE("a style that never yields a valid candidate gives exit code 2 and failed rows", "[mmgen]") {
    TempDir styles("never_styles");
    {
        std::string text = readText(kStylesDir + "/peak_time.json");
        const std::string from = "\"min_score\": 50";
        const size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), "\"min_score\": 100");
        std::ofstream stream(styles.path / "peak_time.json", std::ios::binary);
        stream << text;
    }
    TempDir dir("never_out");
    std::string out;
    std::string err;
    CHECK(run({"one", "--style", "peak_time", "--styles", styles.str(), "--out", dir.str()}, out, err) == 2);
    CHECK(err.find("no valid candidate") != std::string::npos);
    CHECK(fs::is_empty(dir.path));
    REQUIRE(run({"series", "--style", "peak_time", "--archetype", "gallop", "--seeds", "3", "--energies", "50", "--bars",
                 "1", "--styles", styles.str(), "--out", dir.str()},
                out, err) == 0);
    CHECK(out.find("generated 0 patterns (3 without a valid candidate)") != std::string::npos);
    std::istringstream lines(readText(dir.path / "manifest.csv"));
    std::string line;
    std::getline(lines, line);
    int rows = 0;
    while (std::getline(lines, line)) {
        const auto fields = splitCsvLine(line);
        CHECK(fields[0].empty());
        CHECK(fields[14] == "no_valid_candidate");
        ++rows;
    }
    CHECK(rows == 3);
    size_t midi = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir.path)) {
        midi += entry.path().extension() == ".mid" ? 1 : 0;
    }
    CHECK(midi == 0);
    REQUIRE(run({"summary", (dir.path / "manifest.csv").string()}, out, err) == 0);
    CHECK(out.find("failed generations: 3 of 3") != std::string::npos);
}

TEST_CASE("the bpm option sets the tempo of the files", "[mmgen]") {
    TempDir dir("bpm");
    std::string out;
    std::string err;
    REQUIRE(run({"one", "--style", "peak_time", "--bars", "1", "--bpm", "140", "--out", dir.str(), "--styles", kStylesDir},
                out, err) == 0);
    const MidiInfo info = readMidi(readBytes(dir.path / "peak_time_auto_e030_s001_all.mid"));
    REQUIRE(info.valid);
    CHECK(info.bpm > 139.9);
    CHECK(info.bpm < 140.1);
    TempDir other("bpm_default");
    REQUIRE(run({"one", "--style", "peak_time", "--bars", "1", "--out", other.str(), "--styles", kStylesDir}, out, err) ==
            0);
    const MidiInfo standard = readMidi(readBytes(other.path / "peak_time_auto_e030_s001_all.mid"));
    const StyleProfile style = loadShipped("peak_time");
    CHECK(standard.bpm > defaultBpm(style) - 0.1);
    CHECK(standard.bpm < defaultBpm(style) + 0.1);
}

TEST_CASE("a slide on the last note does not reach beyond the end of the file", "[mmgen]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makeEmptyPattern(1, "peak_time");
    p.voices[0].archetypeId = "offbeat";
    Note note;
    note.id = 1;
    note.pitch = 33;
    note.startTick = 3600; // the last 16th
    note.lengthTicks = 240;
    note.velocity = 100;
    note.slide = true;
    p.voices[0].notes.push_back(note);
    const MidiInfo info = readMidi(midiBytes(p, style, FileKind::Bass, 128.0, false));
    REQUIRE(info.valid);
    CHECK(info.noteOns.size() == 1);
    CHECK(info.endTick == kTicksPerBar);
    CHECK(info.lastNoteOffTick <= kTicksPerBar);
}

TEST_CASE("a series uses only the bass archetypes of the bass list and the melody archetypes of the melody list",
          "[mmgen]") {
    StyleProfile style = loadShipped("peak_time");
    style.bass.archetypes.push_back({"arp", 5});            // a melody archetype in the bass list
    style.melody.archetypes.push_back({"rolling16", 5});    // a bass archetype in the melody list
    style.bass.archetypes.push_back({"no_such_archetype", 5});
    std::vector<Job> jobs;
    std::string error;
    Options options = parse({"series", "--style", "peak_time", "--seeds", "1", "--energies", "50"}).options;
    REQUIRE(planSeries(options, {style}, jobs, error));
    std::map<std::string, int> count;
    for (const Job& job : jobs) {
        ++count[job.archetype];
    }
    CHECK(count["arp"] == 1); // only from the melody list
    CHECK(count["rolling16"] == 1); // only from the bass list
    CHECK(count.count("no_such_archetype") == 0);
    CHECK(jobs.size() == 7);
}
