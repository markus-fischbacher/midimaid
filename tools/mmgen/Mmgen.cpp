#include "Mmgen.h"

#include "core/Archetype.h"
#include "core/ChordSymbol.h"
#include "core/KickGrid.h"
#include "core/MidiFile.h"
#include "core/OutputStage.h"
#include "core/Register.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifndef MIDIMAID_DEFAULT_STYLES_DIR
#define MIDIMAID_DEFAULT_STYLES_DIR "resources/styles"
#endif

namespace mm::tools {

using namespace mm::core;

namespace {

bool parseInt(const std::string& text, long long minValue, long long maxValue, long long& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed < minValue || parsed > maxValue) {
        return false;
    }
    value = parsed;
    return true;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string trim(const std::string& text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
        --last;
    }
    return text.substr(first, last - first);
}

std::string number(uint64_t value, int width) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%0*llu", width, static_cast<unsigned long long>(value));
    return buffer;
}

std::string percent(int permille) {
    return std::to_string(permille / 10) + "." + std::to_string(permille % 10) + " %";
}

const char* kPitchNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace

std::string usage() {
    return "mmgen - listening test tool of MidiMaid (SPEC 12)\n"
           "\n"
           "  mmgen one --style S [--bars N] [--seed N] [--energy 0-100] [--creativity 0-100]\n"
           "            [--bass ARCHETYPE] [--melody ARCHETYPE] [--bpm X] [--no-groove] [--out DIR]\n"
           "      writes <name>_bass.mid, <name>_melody.mid and <name>_all.mid (with the kick on channel 10)\n"
           "  mmgen series [--style S|all] [--archetype A] [--seeds 20] [--first-seed 1] [--energies 30,60,90]\n"
           "            [--bars N] [--creativity 0-100] [--bpm X] [--no-groove] [--out DIR]\n"
           "      one folder per style and archetype, plus manifest.csv with an empty `rating` column\n"
           "  mmgen summary MANIFEST.csv\n"
           "      evaluates the ratings: ok (usable at once), edit (needs rework), bad (unusable)\n"
           "\n"
           "  --styles DIR    folder of the style profiles (default: the one of the build)\n";
}

ParseResult parseArguments(const std::vector<std::string>& args) {
    ParseResult result;
    auto fail = [&](const std::string& message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (args.empty()) {
        return fail("no command (one, series, summary)");
    }
    Options& o = result.options;
    const std::string& command = args[0];
    if (command == "help" || command == "--help" || command == "-h") {
        o.command = Command::Help;
        result.ok = true;
        return result;
    }
    if (command == "one") {
        o.command = Command::One;
    } else if (command == "series") {
        o.command = Command::Series;
        o.style = "all";
    } else if (command == "summary") {
        o.command = Command::Summary;
    } else {
        return fail("unknown command '" + command + "'");
    }
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& name = args[i];
        if (name == "--no-groove") {
            o.groove = false;
            continue;
        }
        if (o.command == Command::Summary && name.rfind("--", 0) != 0) {
            o.manifest = name;
            continue;
        }
        if (name.rfind("--", 0) != 0) {
            return fail("unexpected argument '" + name + "'");
        }
        if (i + 1 >= args.size()) {
            return fail("missing value for " + name);
        }
        const std::string& value = args[++i];
        long long n = 0;
        if (name == "--style") {
            o.style = value;
        } else if (name == "--archetype") {
            o.archetype = value;
        } else if (name == "--bass") {
            o.bassArchetype = value;
        } else if (name == "--melody") {
            o.melodyArchetype = value;
        } else if (name == "--out") {
            o.out = value;
        } else if (name == "--styles") {
            o.stylesDir = value;
        } else if (name == "--manifest") {
            o.manifest = value;
        } else if (name == "--bars") {
            if (!parseInt(value, 1, 16, n) || !isValidPatternLength(static_cast<uint32_t>(n))) {
                return fail("--bars must be 1, 2, 4, 8 or 16");
            }
            o.bars = static_cast<uint32_t>(n);
        } else if (name == "--seed") {
            if (!parseInt(value, 0, 9000000000000000000LL, n)) {
                return fail("--seed must be a non-negative number");
            }
            o.seed = static_cast<uint64_t>(n);
        } else if (name == "--first-seed") {
            if (!parseInt(value, 0, 9000000000000000000LL, n)) {
                return fail("--first-seed must be a non-negative number");
            }
            o.firstSeed = static_cast<uint64_t>(n);
        } else if (name == "--seeds") {
            if (!parseInt(value, 1, 10000, n)) {
                return fail("--seeds must be 1-10000");
            }
            o.seeds = static_cast<int>(n);
        } else if (name == "--energy") {
            if (!parseInt(value, 0, 100, n)) {
                return fail("--energy must be 0-100");
            }
            o.energies = {static_cast<int>(n)};
        } else if (name == "--energies") {
            std::vector<int> energies;
            std::stringstream list(value);
            std::string item;
            while (std::getline(list, item, ',')) {
                if (!parseInt(item, 0, 100, n)) {
                    return fail("--energies must be a comma separated list of 0-100");
                }
                energies.push_back(static_cast<int>(n));
            }
            if (energies.empty()) {
                return fail("--energies must be a comma separated list of 0-100");
            }
            o.energies = energies;
        } else if (name == "--creativity") {
            if (!parseInt(value, 0, 100, n)) {
                return fail("--creativity must be 0-100");
            }
            o.creativity = static_cast<int>(n);
        } else if (name == "--bpm") {
            char* end = nullptr;
            const double bpm = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || bpm < 20.0 || bpm > 300.0) {
                return fail("--bpm must be 20-300");
            }
            o.bpm = bpm;
        } else {
            return fail("unknown argument '" + name + "'");
        }
    }
    if (o.command == Command::One && o.style.empty()) {
        return fail("one needs --style");
    }
    if (o.command == Command::Summary && o.manifest.empty()) {
        return fail("summary needs the path of the manifest");
    }
    result.ok = true;
    return result;
}

bool planSeries(const Options& options, const std::vector<StyleProfile>& styles, std::vector<Job>& jobs,
                std::string& error) {
    jobs.clear();
    if (!options.archetype.empty() && findArchetype(options.archetype) == nullptr) {
        error = "unknown archetype '" + options.archetype + "'";
        return false;
    }
    for (const StyleProfile& style : styles) {
        if (options.style != "all" && options.style != style.id) {
            continue;
        }
        std::vector<std::pair<std::string, VoiceRole>> archetypes;
        for (const WeightedId& id : style.bass.archetypes) {
            const Archetype* a = findArchetype(id.id);
            if (a != nullptr && a->role == VoiceRole::Bass) {
                archetypes.emplace_back(id.id, VoiceRole::Bass);
            }
        }
        for (const WeightedId& id : style.melody.archetypes) {
            const Archetype* a = findArchetype(id.id);
            if (a != nullptr && a->role == VoiceRole::Melody) {
                archetypes.emplace_back(id.id, VoiceRole::Melody);
            }
        }
        for (const auto& [id, role] : archetypes) {
            if (!options.archetype.empty() && options.archetype != id) {
                continue;
            }
            for (const int energy : options.energies) {
                for (int i = 0; i < options.seeds; ++i) {
                    Job job;
                    job.styleId = style.id;
                    job.archetype = id;
                    job.archetypeRole = role;
                    job.bars = options.bars;
                    job.seed = options.firstSeed + static_cast<uint64_t>(i);
                    job.energy = energy;
                    job.creativity = options.creativity;
                    (role == VoiceRole::Bass ? job.bassArchetype : job.melodyArchetype) = id;
                    jobs.push_back(job);
                }
            }
        }
    }
    if (jobs.empty()) {
        error = options.archetype.empty() ? "no style '" + options.style + "' with archetypes"
                                          : "archetype '" + options.archetype + "' is not used by the selected styles";
        return false;
    }
    return true;
}

Job planOne(const Options& options) {
    Job job;
    job.styleId = options.style;
    job.bars = options.bars;
    job.seed = options.seed;
    job.energy = options.energies.empty() ? 50 : options.energies.front();
    job.creativity = options.creativity;
    job.bassArchetype = options.bassArchetype;
    job.melodyArchetype = options.melodyArchetype;
    return job;
}

std::string jobFolder(const Job& job) {
    return job.styleId + "/" + (job.archetype.empty() ? "auto" : job.archetype);
}

std::string jobBaseName(const Job& job) {
    return job.styleId + "_" + (job.archetype.empty() ? "auto" : job.archetype) + "_e" +
           number(static_cast<uint64_t>(job.energy), 3) + "_s" + number(job.seed, 3);
}

double defaultBpm(const StyleProfile& style) {
    return (style.tempoMin + style.tempoMax) / 2.0;
}

std::vector<uint8_t> midiBytes(const Pattern& pattern, const StyleProfile& style, FileKind kind, double bpm,
                               bool groove) {
    OutputSettings settings;
    settings.kickClearanceTicks = style.bass.kickClearanceTicks;
    settings.includeGroove = groove;
    for (const Track& track : pattern.voices) {
        settings.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
    }
    const OutputPattern out = renderOutput(pattern, settings);
    std::vector<PatternNote> notes;
    for (const OutputVoice& voice : out.voices) {
        const bool bass = voice.role == VoiceRole::Bass;
        if ((kind == FileKind::Bass && !bass) || (kind == FileKind::Melody && bass)) {
            continue;
        }
        for (const OutputNote& note : voice.notes) {
            const int32_t start = std::max<int32_t>(note.startTick, 0);
            const int32_t end = std::min<int32_t>(note.endTick, static_cast<int32_t>(out.lengthTicks));
            if (end > start) {
                notes.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(end - start), note.channel,
                                 note.pitch, note.velocity});
            }
        }
    }
    if (kind == FileKind::All) {
        for (const uint32_t tick : kickTicks(pattern)) {
            notes.push_back({tick, 120, 10, 36, 110});
        }
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const PatternNote& a, const PatternNote& b) { return a.startTick < b.startTick; });
    MidiFileOptions options;
    options.bpm = bpm;
    options.trackName = "MidiMaid " + style.id;
    const PatternView view{notes.data(), notes.size(), out.lengthTicks};
    return writeMidiFile(view, options);
}

std::string csvEscape(const std::string& field) {
    if (field.find_first_of(",;\"\r\n") == std::string::npos) {
        return field;
    }
    std::string result = "\"";
    for (const char c : field) {
        result += c;
        if (c == '"') {
            result += '"';
        }
    }
    return result + "\"";
}

std::vector<std::string> splitCsvLine(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == delimiter) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

std::string manifestHeader() {
    return "file,style,archetype,bars,seed,winner_seed,energy,creativity,bass_archetype,melody_archetype,key,scale,"
           "chords,score,status,rating";
}

std::string manifestLine(const ManifestRow& r) {
    const std::vector<std::string> fields{csvEscape(r.file),
                                          csvEscape(r.style),
                                          csvEscape(r.archetype),
                                          std::to_string(r.bars),
                                          std::to_string(r.seed),
                                          std::to_string(r.winnerSeed),
                                          std::to_string(r.energy),
                                          std::to_string(r.creativity),
                                          csvEscape(r.bassArchetype),
                                          csvEscape(r.melodyArchetype),
                                          csvEscape(r.key),
                                          csvEscape(r.scale),
                                          csvEscape(r.chords),
                                          std::to_string(r.score),
                                          csvEscape(r.status),
                                          csvEscape(r.rating)};
    std::string line;
    for (size_t i = 0; i < fields.size(); ++i) {
        line += (i == 0 ? "" : ",") + fields[i];
    }
    return line;
}

std::optional<Pattern> generateJob(const Job& job, const StyleProfile& style) {
    GenerationRequest request;
    request.lengthBars = job.bars;
    request.seed = job.seed;
    request.settings.energyPct = job.energy;
    request.settings.creativityPct = job.creativity;
    if (!job.bassArchetype.empty()) {
        request.bassArchetype = job.bassArchetype;
    }
    if (!job.melodyArchetype.empty()) {
        request.melodyArchetype = job.melodyArchetype;
    }
    SelectionResult result = generatePattern(style, request);
    if (!result.success) {
        return std::nullopt;
    }
    return std::move(result.pattern);
}

ManifestRow rowFor(const Job& job, const Pattern* pattern, const std::string& file) {
    ManifestRow row;
    row.style = job.styleId;
    row.archetype = job.archetype.empty() ? "auto" : job.archetype;
    row.bars = job.bars;
    row.seed = job.seed;
    row.energy = job.energy;
    row.creativity = job.creativity;
    row.bassArchetype = job.bassArchetype.empty() ? "auto" : job.bassArchetype;
    row.melodyArchetype = job.melodyArchetype.empty() ? "auto" : job.melodyArchetype;
    if (pattern == nullptr) {
        row.status = "no_valid_candidate";
        return row;
    }
    row.file = file;
    row.winnerSeed = pattern->info.winnerSeed;
    row.score = pattern->qualityScore;
    row.status = "ok";
    bool seenBass = false;
    bool seenMelody = false;
    for (const Track& track : pattern->voices) {
        const bool bass = track.role == VoiceRole::Bass;
        bool& seen = bass ? seenBass : seenMelody;
        if (!seen) {
            (bass ? row.bassArchetype : row.melodyArchetype) = track.archetypeId;
            seen = true;
        }
    }
    row.key = kPitchNames[pattern->context.root % 12];
    row.scale = pattern->context.scaleId;
    for (const ChordEvent& event : pattern->context.progression) {
        row.chords += (row.chords.empty() ? "" : " ") + formatChordSymbol(event.chord) + "@" +
                      std::to_string(event.startHalfBar) + "+" + std::to_string(event.lengthHalfBars);
    }
    return row;
}

std::string normalizeRating(const std::string& text) {
    const std::string t = lower(trim(text));
    if (t == "ok" || t == "o" || t == "sofort" || t == "sofort nutzbar" || t == "+" || t == "1") {
        return "ok";
    }
    if (t == "edit" || t == "e" || t == "nachbearbeiten" || t == "~" || t == "2") {
        return "edit";
    }
    if (t == "bad" || t == "b" || t == "unbrauchbar" || t == "-" || t == "x" || t == "3") {
        return "bad";
    }
    return "";
}

namespace {

void add(SummaryLine& line, const std::string& status, const std::string& rating, bool& unknown) {
    if (status == "no_valid_candidate") {
        ++line.failed;
        return;
    }
    const std::string normalized = normalizeRating(rating);
    if (normalized == "ok") {
        ++line.ok;
    } else if (normalized == "edit") {
        ++line.edit;
    } else if (normalized == "bad") {
        ++line.bad;
    } else {
        ++line.unrated;
        unknown = unknown || !trim(rating).empty();
    }
}

} // namespace

Summary summarize(const std::string& manifestText) {
    Summary summary;
    std::istringstream stream(manifestText);
    std::string headerLine;
    if (!std::getline(stream, headerLine)) {
        summary.error = "the manifest is empty";
        return summary;
    }
    if (!headerLine.empty() && headerLine.back() == '\r') {
        headerLine.pop_back();
    }
    const char delimiter = headerLine.find(',') == std::string::npos && headerLine.find(';') != std::string::npos
                               ? ';'
                               : ',';
    std::map<std::string, size_t> column;
    const std::vector<std::string> header = splitCsvLine(headerLine, delimiter);
    for (size_t i = 0; i < header.size(); ++i) {
        column[lower(trim(header[i]))] = i;
    }
    for (const char* needed : {"style", "archetype", "energy", "status", "rating"}) {
        if (column.count(needed) == 0) {
            summary.error = std::string("the manifest has no column '") + needed + "'";
            return summary;
        }
    }
    std::map<std::pair<std::string, std::string>, std::map<int, size_t>> index; // style, archetype -> energy -> line
    std::map<std::string, size_t> styleIndex;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> fields = splitCsvLine(line, delimiter);
        auto field = [&](const char* name) {
            const size_t i = column[name];
            return i < fields.size() ? fields[i] : std::string();
        };
        long long energy = 0;
        parseInt(trim(field("energy")), 0, 100, energy);
        const std::string style = field("style");
        const std::string archetype = field("archetype");
        auto& byEnergy = index[{style, archetype}];
        if (byEnergy.count(static_cast<int>(energy)) == 0) {
            SummaryLine fresh;
            fresh.style = style;
            fresh.archetype = archetype;
            fresh.energy = static_cast<int>(energy);
            byEnergy[static_cast<int>(energy)] = summary.lines.size();
            summary.lines.push_back(fresh);
        }
        if (styleIndex.count(style) == 0) {
            SummaryLine fresh;
            fresh.style = style;
            styleIndex[style] = summary.styles.size();
            summary.styles.push_back(fresh);
        }
        bool unknown = false;
        const std::string status = field("status");
        const std::string rating = field("rating");
        add(summary.lines[byEnergy[static_cast<int>(energy)]], status, rating, unknown);
        add(summary.styles[styleIndex[style]], status, rating, unknown);
        bool ignored = false;
        add(summary.total, status, rating, ignored);
        summary.unknownRatings += unknown ? 1 : 0;
    }
    return summary;
}

std::string formatSummary(const Summary& summary) {
    if (!summary.error.empty()) {
        return "error: " + summary.error + "\n";
    }
    std::ostringstream out;
    auto row = [&](const SummaryLine& l, const std::string& label) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%-44s ok %4d  edit %4d  bad %4d  unrated %4d  failed %3d  usable %s\n",
                      label.c_str(), l.ok, l.edit, l.bad, l.unrated, l.failed,
                      l.rated() == 0 ? "-" : percent(l.okPermille()).c_str());
        out << buffer;
    };
    out << "per style, archetype and energy\n";
    for (const SummaryLine& l : summary.lines) {
        row(l, l.style + " / " + l.archetype + " / e" + std::to_string(l.energy));
    }
    out << "\nper style (target: at least " << percent(kTargetOkPermille) << " usable at once)\n";
    for (const SummaryLine& l : summary.styles) {
        row(l, l.style);
        if (l.rated() == 0) {
            out << "    -> not rated yet\n";
        } else {
            out << (l.okPermille() >= kTargetOkPermille ? "    -> target reached\n" : "    -> BELOW TARGET\n");
        }
    }
    out << "\n";
    row(summary.total, "total");
    const int generated = summary.total.rated() + summary.total.unrated + summary.total.failed;
    out << "failed generations: " << summary.total.failed << " of " << generated << "\n";
    if (summary.total.unrated > 0) {
        out << "rows without a usable rating: " << summary.total.unrated << " (unknown values: "
            << summary.unknownRatings << ")\n";
    }
    return out.str();
}

namespace {

namespace fs = std::filesystem;

bool loadStyles(const Options& options, std::vector<StyleProfile>& styles, std::string& error) {
    const fs::path dir = options.stylesDir.empty() ? fs::path(MIDIMAID_DEFAULT_STYLES_DIR) : fs::path(options.stylesDir);
    std::vector<fs::path> files;
    if (options.style == "all") {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        if (ec) {
            error = "cannot read the style folder '" + dir.string() + "'";
            return false;
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(dir / (options.style + ".json"));
    }
    for (const fs::path& file : files) {
        std::ifstream stream(file, std::ios::binary);
        if (!stream.good()) {
            error = "unknown style '" + options.style + "' (cannot read " + file.string() + ")";
            return false;
        }
        std::stringstream buffer;
        buffer << stream.rdbuf();
        const StyleLoadResult loaded = loadStyleProfile(buffer.str());
        if (!loaded.ok()) {
            error = file.string() + ": " + loaded.error;
            return false;
        }
        styles.push_back(*loaded.profile);
    }
    if (styles.empty()) {
        error = "no style profiles in '" + dir.string() + "'";
        return false;
    }
    return true;
}

bool writeFile(const fs::path& path, const std::string& data, std::string& error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!stream.good()) {
        error = "cannot write '" + path.string() + "'";
        return false;
    }
    return true;
}

bool writeMidi(const fs::path& path, const std::vector<uint8_t>& bytes, std::string& error) {
    return writeFile(path, std::string(bytes.begin(), bytes.end()), error);
}

bool writeJobFiles(const Pattern& pattern, const StyleProfile& style, const Options& options, const fs::path& folder,
                   const std::string& base, std::string& error) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        error = "cannot create '" + folder.string() + "'";
        return false;
    }
    const double bpm = options.bpm.value_or(defaultBpm(style));
    for (const auto& [kind, suffix] : {std::pair{FileKind::Bass, "_bass.mid"}, std::pair{FileKind::Melody, "_melody.mid"},
                                       std::pair{FileKind::All, "_all.mid"}}) {
        if (!writeMidi(folder / (base + suffix), midiBytes(pattern, style, kind, bpm, options.groove), error)) {
            return false;
        }
    }
    return true;
}

} // namespace

int run(const std::vector<std::string>& args, std::string& out, std::string& err) {
    const ParseResult parsed = parseArguments(args);
    if (!parsed.ok) {
        err = "error: " + parsed.error + "\n\n" + usage();
        return 1;
    }
    const Options& options = parsed.options;
    if (options.command == Command::Help) {
        out = usage();
        return 0;
    }
    if (options.command == Command::Summary) {
        std::ifstream stream(options.manifest, std::ios::binary);
        if (!stream.good()) {
            err = "error: cannot read '" + options.manifest + "'\n";
            return 2;
        }
        std::stringstream buffer;
        buffer << stream.rdbuf();
        const Summary summary = summarize(buffer.str());
        if (!summary.error.empty()) {
            err = formatSummary(summary);
            return 2;
        }
        out = formatSummary(summary);
        return 0;
    }

    std::vector<StyleProfile> styles;
    std::string error;
    if (!loadStyles(options, styles, error)) {
        err = "error: " + error + "\n";
        return 2;
    }
    std::vector<Job> jobs;
    if (options.command == Command::One) {
        const StyleProfile& style = styles.front();
        for (const std::string& id : {options.bassArchetype, options.melodyArchetype}) {
            if (!id.empty() && findArchetype(id) == nullptr) {
                err = "error: unknown archetype '" + id + "'\n";
                return 1;
            }
        }
        const Job job = planOne(options);
        const auto pattern = generateJob(job, style);
        if (!pattern.has_value()) {
            err = "error: no valid candidate for this seed; try another seed\n";
            return 2;
        }
        const std::string base = jobBaseName(job);
        if (!writeJobFiles(*pattern, style, options, fs::path(options.out), base, error)) {
            err = "error: " + error + "\n";
            return 2;
        }
        out = manifestHeader() + "\n" + manifestLine(rowFor(job, &*pattern, base + "_all.mid")) + "\n";
        return 0;
    }

    if (!planSeries(options, styles, jobs, error)) {
        err = "error: " + error + "\n";
        return 1;
    }
    std::map<std::string, const StyleProfile*> byId;
    for (const StyleProfile& style : styles) {
        byId[style.id] = &style;
    }
    std::string manifest = manifestHeader() + "\n";
    int failed = 0;
    for (const Job& job : jobs) {
        const StyleProfile& style = *byId[job.styleId];
        const auto pattern = generateJob(job, style);
        std::string file;
        if (pattern.has_value()) {
            const std::string base = jobBaseName(job);
            if (!writeJobFiles(*pattern, style, options, fs::path(options.out) / jobFolder(job), base, error)) {
                err = "error: " + error + "\n";
                return 2;
            }
            file = jobFolder(job) + "/" + base + "_all.mid";
        } else {
            ++failed;
        }
        manifest += manifestLine(rowFor(job, pattern.has_value() ? &*pattern : nullptr, file)) + "\n";
    }
    std::error_code ec;
    fs::create_directories(fs::path(options.out), ec);
    if (!writeFile(fs::path(options.out) / "manifest.csv", manifest, error)) {
        err = "error: " + error + "\n";
        return 2;
    }
    out = "generated " + std::to_string(jobs.size() - static_cast<size_t>(failed)) + " patterns (" +
          std::to_string(failed) + " without a valid candidate) in '" + options.out + "'; manifest.csv written\n";
    return 0;
}

} // namespace mm::tools
