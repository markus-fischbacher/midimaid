#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mm::ai {

/// The response schema version 1 (SPEC 7.3). v1.0 sends and reads the fields below; `ratchet`, `chance` and `cond` of
/// notes come with v1.1, the parser ignores them (and every other unknown field) without an error.
constexpr int kSchemaVersion = 1;

/// Limits against answers that are too big or too deep (a hostile or broken backend): the parser refuses them.
constexpr size_t kMaxResponseBytes = size_t{1} << 20;
constexpr int kMaxJsonDepth = 24;
constexpr size_t kMaxNotesPerVoice = 4096;
constexpr size_t kMaxProgressionEntries = 64;
constexpr size_t kMaxPhrases = 16;

/// The JSON schema of the answer (v1.0 fields only) as text, for the providers and for the prompts.
std::string schemaV1Json();

struct AiNoteDraft {
    std::optional<uint32_t> id; ///< only for existing notes (refine); new notes have none
    int step = 0;               ///< 16th-note position
    int degree = 1;             ///< scale degree, 1 = root
    int alt = 0;
    int octave = 0;
    int len = 1; ///< in 16ths
    int vel = 100;
    bool accent = false;
    bool slide = false;
};

struct AiVoiceDraft {
    std::string role; ///< "bass", "melody" (others are ignored by the builder)
    std::string archetype;
    std::optional<uint32_t> motifBars; ///< patterns over 8 bars: the notes are a motif of this many bars
    std::vector<AiNoteDraft> notes;
};

struct AiPhraseDraft {
    int startBar = 0;
    int bars = 0;
    std::string role; ///< "main", "variation", "build", "breakdown", "answer"
    bool turnaround = false;
};

struct AiIntent {
    std::optional<double> energy;
    std::optional<double> density;
    std::string contour;
    std::string motifIdea;
    std::string groove;
};

/// What an answer says, checked for structure but not yet for musical sense (that is the builder's job).
struct AiDraft {
    int schemaVersion = kSchemaVersion;
    AiIntent intent;
    std::string root;
    std::string scale;
    std::vector<std::string> progression;
    std::vector<AiPhraseDraft> phrases; ///< empty when the answer has none or they were unusable
    std::vector<AiVoiceDraft> voices;
};

struct ParseResult {
    bool ok = false;
    std::string error; ///< path and reason of the first structural problem, for the repair request and the log
    AiDraft draft;
    size_t droppedNotes = 0; ///< notes that were not objects or lacked `step` or `degree`
    size_t droppedVoices = 0;
};

/// Reads the text of an answer. Never throws and never crashes, whatever the text is. The JSON may stand inside a code
/// fence or between sentences (the first `{` to the last `}` is tried when the whole text is no JSON). Structural
/// problems (no JSON, wrong types, a missing `context`, no voice, over the limits above) give `ok == false` with the
/// path of the problem; single bad notes are dropped and counted. A schema version above 1 is accepted: fields of
/// later versions are ignored.
ParseResult parseAiResponse(std::string_view text);

struct BuildContext {
    uint32_t lengthBars = 4;
    std::string styleId;
    const mm::core::StyleProfile* style = nullptr; ///< ranges and kick grid; the register defaults when null
    int energyPct = 50;
    int creativityPct = 40;
    std::string prompt;
    uint32_t promptVersion = 1;
    std::string providerId;
    std::string modelId;
    std::string rawResponse; ///< stored unchanged in `GenerationInfo`
    int64_t createdUnixMs = 0;
    /// Note ids that exist (refine): an `id` of the answer that is in this set and used once keeps its note; any other
    /// id gives a new note (SPEC 3.15, 5). Null: every note is new.
    const std::set<uint32_t>* knownIds = nullptr;
    /// The first id new notes get at least (refine: the counter of the pattern that is refined).
    uint32_t firstNewId = 1;
};

struct BuildResult {
    bool ok = false;
    std::string error; ///< path and reason, for the repair request and the log
    mm::core::Pattern pattern;
    size_t droppedNotes = 0;  ///< notes outside the pattern, with a degree that gives no pitch, or without a degree
    size_t clampedValues = 0; ///< `alt`, `octave`, `len`, `vel` that were cut into their range
    size_t ignoredVoices = 0; ///< voices of the answer with a role that is not used
};

/// Turns an answer into a pattern (SPEC 7.3): key and scale, one chord per bar or half bar (the progression repeats
/// when it is shorter than the pattern and must divide the number of bars), the notes from step, degree, alteration and
/// octave (relative to the voice base, 7.3) with values cut into their ranges and notes with an invalid position
/// dropped. The pattern has bass and melody; an answer without one of them is refused. `info` gets source "ai" and the
/// prompt data. The constraint layer and the quality rating run afterwards (the caller's job), as for every pattern.
BuildResult buildPattern(const AiDraft& draft, const BuildContext& context);

} // namespace mm::ai
