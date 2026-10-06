#pragma once

#include "core/Pattern.h"
#include "core/PatternGenerator.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm::tools {

/// mmgen, the listening-test tool (SPEC 12): writes .mid files for styles, archetypes, energies and seeds, a manifest
/// with a column for the ratings, and evaluates the ratings. Built on `mm_core` only.

enum class Command { Help, One, Series, Summary };

struct Options {
    Command command = Command::Help;
    std::string style;                  ///< style id, or "all" (series)
    std::string archetype;              ///< empty = every archetype of the style (series) or automatic (one)
    std::string manifest;               ///< summary: path of the manifest
    std::string out = ".";              ///< output folder
    std::string stylesDir;              ///< folder of the style profiles; empty = the one of the build
    uint32_t bars = 4;                  ///< 1, 2, 4, 8 or 16
    uint64_t seed = 1;                  ///< one: the seed
    int seeds = 20;                     ///< series: seeds firstSeed ... firstSeed + seeds - 1
    uint64_t firstSeed = 1;             ///< series
    std::vector<int> energies{30, 60, 90}; ///< series (percent); one: the first value
    int creativity = 40;
    std::optional<double> bpm;          ///< default: the middle of the tempo range of the style
    bool groove = true;                 ///< false: export without groove (SPEC 4.2a)
    std::string bassArchetype;          ///< one: manual bass archetype
    std::string melodyArchetype;        ///< one: manual melody archetype
};

struct ParseResult {
    bool ok = false;
    std::string error;
    Options options;
};

/// Parses the arguments after the program name. Errors name the argument.
ParseResult parseArguments(const std::vector<std::string>& args);

/// The usage text.
std::string usage();

/// One pattern to generate.
struct Job {
    std::string styleId;
    std::string archetype; ///< "" = automatic for both voices
    mm::core::VoiceRole archetypeRole = mm::core::VoiceRole::Bass;
    uint32_t bars = 4;
    uint64_t seed = 1;
    int energy = 50;
    int creativity = 40;
    std::string bassArchetype;   ///< manual choice, "" = automatic
    std::string melodyArchetype; ///< manual choice, "" = automatic
};

/// The jobs of a series in the order style, archetype (bass archetypes of the profile first, then melody), energy,
/// seed. Ids without an implementation are skipped. Error for an unknown archetype.
bool planSeries(const Options& options, const std::vector<mm::core::StyleProfile>& styles, std::vector<Job>& jobs,
                std::string& error);

/// The single job of `mmgen one`.
Job planOne(const Options& options);

/// The folder (relative to the output folder) and base name of the files of a job, e.g.
/// `peak_time/rolling16` and `peak_time_rolling16_e030_s007`; "auto" stands for no archetype.
std::string jobFolder(const Job& job);
std::string jobBaseName(const Job& job);

enum class FileKind { Bass, Melody, All };

/// A Standard MIDI file of the pattern as the plugin would send it (output stage with groove, slides and kick
/// clearance). `Bass` holds the bass voices, `Melody` all other voices, `All` every voice plus the kick (channel 10,
/// note 36) on the kick grid of the pattern.
std::vector<uint8_t> midiBytes(const mm::core::Pattern& pattern, const mm::core::StyleProfile& style, FileKind kind,
                               double bpm, bool groove);

/// The tempo of a style: the middle of its range.
double defaultBpm(const mm::core::StyleProfile& style);

struct ManifestRow {
    std::string file; ///< the `_all` file, relative to the output folder; empty if nothing was generated
    std::string style;
    std::string archetype; ///< the archetype the series fixed, "auto" if none
    uint32_t bars = 0;
    uint64_t seed = 0;
    uint64_t winnerSeed = 0;
    int energy = 0;
    int creativity = 0;
    std::string bassArchetype;
    std::string melodyArchetype;
    std::string key;
    std::string scale;
    std::string chords;
    int score = 0;
    std::string status; ///< "ok" or "no_valid_candidate"
    std::string rating;
};

std::string manifestHeader();
std::string manifestLine(const ManifestRow& row);
std::string csvEscape(const std::string& field);
/// Splits one CSV line (quotes and doubled quotes as in `csvEscape`) at `delimiter` (spreadsheets of some locales
/// save with ';').
std::vector<std::string> splitCsvLine(const std::string& line, char delimiter = ',');

/// Generates the pattern of a job (`generatePattern` with the seed of the job). Nullopt without a valid candidate.
std::optional<mm::core::Pattern> generateJob(const Job& job, const mm::core::StyleProfile& style);

/// The manifest row of a generated pattern (or of a failed job with `pattern == nullptr`).
ManifestRow rowFor(const Job& job, const mm::core::Pattern* pattern, const std::string& file);

/// Ratings of the listening test: "ok" (usable at once), "edit" (needs rework), "bad" (unusable). German words and the
/// first letters are accepted; anything else gives an empty string.
std::string normalizeRating(const std::string& text);

struct SummaryLine {
    std::string style;
    std::string archetype; ///< bass archetype or melody archetype of the row, whichever was fixed
    int energy = 0;
    int ok = 0;
    int edit = 0;
    int bad = 0;
    int failed = 0;
    int unrated = 0;

    int rated() const { return ok + edit + bad; }
    int okPermille() const { return rated() == 0 ? 0 : ok * 1000 / rated(); }
};

struct Summary {
    std::vector<SummaryLine> lines; ///< per style, archetype, energy
    std::vector<SummaryLine> styles; ///< per style (archetype empty, energy 0)
    SummaryLine total;
    int unknownRatings = 0;
    std::string error;
};

constexpr int kTargetOkPermille = 800; ///< SPEC 12: at least 80 % usable at once per style

/// Evaluates a manifest (its text, delimiter "," or ";", columns found by their names).
Summary summarize(const std::string& manifestText);
std::string formatSummary(const Summary& summary);

/// The program: returns the exit code (0 ok, 1 usage error, 2 failure). Messages go to `out` and `err`.
int run(const std::vector<std::string>& args, std::string& out, std::string& err);

} // namespace mm::tools
