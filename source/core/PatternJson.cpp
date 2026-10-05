#include "core/PatternJson.h"

#include "core/JsonReader.h"
#include "core/PatternValidation.h"

#include <array>
#include <limits>
#include <utility>

namespace mm::core {

using nlohmann::json;

namespace {

using detail::EnumName;
using detail::Reader;

// ---------------------------------------------------------------------------------------------------------------
// Enum tables (strings keep the format stable and readable)

constexpr std::array<EnumName<VoiceRole>, 2> kVoiceRoles{{{VoiceRole::Bass, "bass"}, {VoiceRole::Melody, "melody"}}};
constexpr std::array<EnumName<PhraseRole>, 5> kPhraseRoles{{{PhraseRole::Main, "main"},
                                                            {PhraseRole::Variation, "variation"},
                                                            {PhraseRole::Build, "build"},
                                                            {PhraseRole::Breakdown, "breakdown"},
                                                            {PhraseRole::Answer, "answer"}}};
constexpr std::array<EnumName<ChordQuality>, 5> kChordQualities{{{ChordQuality::Major, "major"},
                                                                 {ChordQuality::Minor, "minor"},
                                                                 {ChordQuality::Diminished, "diminished"},
                                                                 {ChordQuality::Sus2, "sus2"},
                                                                 {ChordQuality::Sus4, "sus4"}}};
constexpr std::array<EnumName<PolymeterPhase>, 2> kPolymeterPhases{
    {{PolymeterPhase::RestartAtPattern, "restart"}, {PolymeterPhase::FreeRunning, "free"}}};

template <typename E, size_t N> std::string_view nameOf(const std::array<EnumName<E>, N>& table, E value) {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return table[0].name;
}

// ---------------------------------------------------------------------------------------------------------------
// Writing

json noteToJson(const Note& n) {
    return json{{"id", n.id},
                {"pitch", n.pitch},
                {"startTick", n.startTick},
                {"lengthTicks", n.lengthTicks},
                {"velocity", n.velocity},
                {"ratchet", n.ratchet},
                {"chance", n.chance},
                {"condA", n.condA},
                {"condB", n.condB},
                {"slide", n.slide},
                {"accent", n.accent},
                {"lock", json{{"pitch", n.lock.pitch}, {"rhythm", n.lock.rhythm}, {"velocity", n.lock.velocity}}}};
}

json trackToJson(const Track& t) {
    json notes = json::array();
    for (const Note& n : t.notes) {
        notes.push_back(noteToJson(n));
    }
    return json{{"role", nameOf(kVoiceRoles, t.role)},
                {"midiChannel", t.midiChannel},
                {"archetypeId", t.archetypeId},
                {"archetypeAuto", t.archetypeAuto},
                {"octaveOffset", t.octaveOffset},
                {"groove", json{{"swing", static_cast<double>(t.groove.swing)},
                                {"templateId", t.groove.templateId},
                                {"amount", static_cast<double>(t.groove.amount)}}},
                {"lock", json{{"pitch", t.lock.pitch}, {"rhythm", t.lock.rhythm}, {"velocity", t.lock.velocity}}},
                {"muted", t.muted},
                {"notes", std::move(notes)}};
}

json phraseToJson(const Phrase& p) {
    json j{{"startBar", p.startBar},
           {"lengthBars", p.lengthBars},
           {"role", nameOf(kPhraseRoles, p.role)},
           {"turnaround", p.turnaround},
           {"locked", p.locked}};
    if (p.kickGridId.has_value()) {
        j["kickGridId"] = *p.kickGridId;
    }
    return j;
}

json maskToJson(const std::bitset<32>& bits) {
    return static_cast<uint32_t>(bits.to_ulong());
}

json rhythmReferenceToJson(const RhythmReference& r) {
    json offsets = json::array();
    for (const int16_t value : r.timingOffsetTicks) {
        offsets.push_back(value);
    }
    json velocities = json::array();
    for (const uint8_t value : r.velocity) {
        velocities.push_back(value);
    }
    return json{{"kickSteps", maskToJson(r.kickSteps)},     {"hatSteps", maskToJson(r.hatSteps)},
                {"accentSteps", maskToJson(r.accentSteps)}, {"timingOffsetTicks", std::move(offsets)},
                {"velocity", std::move(velocities)},        {"bars", r.bars}};
}

json infoToJson(const GenerationInfo& i) {
    return json{{"source", i.source},
                {"seed", i.seed},
                {"winnerSeed", i.winnerSeed},
                {"styleProfileVersion", i.styleProfileVersion},
                {"creativityPct", i.creativityPct},
                {"energyPct", i.energyPct},
                {"prompt", i.prompt},
                {"promptVersion", i.promptVersion},
                {"providerId", i.providerId},
                {"modelId", i.modelId},
                {"rawResponse", i.rawResponse},
                {"referenceSetId", i.referenceSetId},
                {"createdUnixMs", i.createdUnixMs}};
}

// ---------------------------------------------------------------------------------------------------------------
// Reading: type and fit checks only (value rules live in validatePattern). The first error wins.

void readLock(Reader& r, const json& parent, const std::string& path, LockFlags& lock) {
    if (const json* object = r.objectMember(parent, "lock", path, false)) {
        const std::string lockPath = Reader::child(path, "lock");
        r.boolValue(*object, "pitch", lockPath, false, lock.pitch);
        r.boolValue(*object, "rhythm", lockPath, false, lock.rhythm);
        r.boolValue(*object, "velocity", lockPath, false, lock.velocity);
    }
}

void readNote(Reader& r, const json& object, const std::string& path, Note& note) {
    r.uintValue(object, "id", path, true, note.id);
    r.uintValue(object, "pitch", path, true, note.pitch);
    r.uintValue(object, "startTick", path, true, note.startTick);
    r.uintValue(object, "lengthTicks", path, true, note.lengthTicks);
    r.uintValue(object, "velocity", path, true, note.velocity);
    r.uintValue(object, "ratchet", path, false, note.ratchet);
    r.uintValue(object, "chance", path, false, note.chance);
    r.uintValue(object, "condA", path, false, note.condA);
    r.uintValue(object, "condB", path, false, note.condB);
    r.boolValue(object, "slide", path, false, note.slide);
    r.boolValue(object, "accent", path, false, note.accent);
    readLock(r, object, path, note.lock);
}

void readTrack(Reader& r, const json& object, const std::string& path, Track& track) {
    r.enumValue(object, "role", path, true, kVoiceRoles, track.role);
    r.uintValue(object, "midiChannel", path, true, track.midiChannel);
    r.stringValue(object, "archetypeId", path, false, track.archetypeId);
    r.boolValue(object, "archetypeAuto", path, false, track.archetypeAuto);
    r.intValue(object, "octaveOffset", path, false, track.octaveOffset);
    r.boolValue(object, "muted", path, false, track.muted);
    if (const json* groove = r.objectMember(object, "groove", path, false)) {
        const std::string groovePath = Reader::child(path, "groove");
        r.floatValue(*groove, "swing", groovePath, false, track.groove.swing);
        r.stringValue(*groove, "templateId", groovePath, false, track.groove.templateId);
        r.floatValue(*groove, "amount", groovePath, false, track.groove.amount);
    }
    readLock(r, object, path, track.lock);
    if (const json* notes = r.arrayMember(object, "notes", path, true)) {
        const std::string notesPath = Reader::child(path, "notes");
        for (size_t i = 0; i < notes->size() && !r.failed(); ++i) {
            const json& entry = (*notes)[i];
            const std::string notePath = Reader::element(notesPath, i);
            if (!entry.is_object()) {
                r.fail(notePath, "expected an object");
                break;
            }
            Note note;
            readNote(r, entry, notePath, note);
            track.notes.push_back(note);
        }
    }
}

void readMask(Reader& r, const json& object, std::string_view key, const std::string& path, std::bitset<32>& out) {
    uint32_t raw = 0;
    r.uintValue(object, key, path, true, raw);
    out = std::bitset<32>(raw);
}

template <typename T>
void readArray32(Reader& r, const json& object, std::string_view key, const std::string& path, std::array<T, 32>& out,
                 bool isSigned) {
    const json* array = r.arrayMember(object, key, path, true);
    if (array == nullptr) {
        return;
    }
    const std::string arrayPath = Reader::child(path, key);
    if (array->size() != out.size()) {
        r.fail(arrayPath, "expected 32 entries");
        return;
    }
    for (size_t i = 0; i < out.size() && !r.failed(); ++i) {
        json wrapper{{"v", (*array)[i]}};
        T value{};
        if (isSigned) {
            r.intValue(wrapper, "v", Reader::element(arrayPath, i), true, value);
        } else {
            r.uintValue(wrapper, "v", Reader::element(arrayPath, i), true, value);
        }
        out[i] = value;
    }
}

void readRhythmReference(Reader& r, const json& object, const std::string& path, RhythmReference& ref) {
    readMask(r, object, "kickSteps", path, ref.kickSteps);
    readMask(r, object, "hatSteps", path, ref.hatSteps);
    readMask(r, object, "accentSteps", path, ref.accentSteps);
    readArray32(r, object, "timingOffsetTicks", path, ref.timingOffsetTicks, true);
    readArray32(r, object, "velocity", path, ref.velocity, false);
    r.uintValue(object, "bars", path, true, ref.bars);
}

void readInfo(Reader& r, const json& object, const std::string& path, GenerationInfo& info) {
    r.stringValue(object, "source", path, false, info.source);
    r.uintValue(object, "seed", path, false, info.seed);
    r.uintValue(object, "winnerSeed", path, false, info.winnerSeed);
    r.uintValue(object, "styleProfileVersion", path, false, info.styleProfileVersion);
    r.uintValue(object, "creativityPct", path, false, info.creativityPct);
    r.uintValue(object, "energyPct", path, false, info.energyPct);
    r.stringValue(object, "prompt", path, false, info.prompt);
    r.uintValue(object, "promptVersion", path, false, info.promptVersion);
    r.stringValue(object, "providerId", path, false, info.providerId);
    r.stringValue(object, "modelId", path, false, info.modelId);
    r.stringValue(object, "rawResponse", path, false, info.rawResponse);
    r.stringValue(object, "referenceSetId", path, false, info.referenceSetId);
    r.intValue(object, "createdUnixMs", path, false, info.createdUnixMs);
}

void readPhrases(Reader& r, const json& root, Pattern& pattern) {
    const json* phrases = r.arrayMember(root, "phrases", "", true);
    if (phrases == nullptr) {
        return;
    }
    for (size_t i = 0; i < phrases->size() && !r.failed(); ++i) {
        const json& entry = (*phrases)[i];
        const std::string path = Reader::element("phrases", i);
        if (!entry.is_object()) {
            r.fail(path, "expected an object");
            return;
        }
        Phrase phrase;
        r.uintValue(entry, "startBar", path, true, phrase.startBar);
        r.uintValue(entry, "lengthBars", path, true, phrase.lengthBars);
        r.enumValue(entry, "role", path, true, kPhraseRoles, phrase.role);
        r.boolValue(entry, "turnaround", path, false, phrase.turnaround);
        r.boolValue(entry, "locked", path, false, phrase.locked);
        if (entry.contains("kickGridId")) {
            std::string kickGrid;
            r.stringValue(entry, "kickGridId", path, true, kickGrid);
            phrase.kickGridId = kickGrid;
        }
        pattern.phrases.push_back(std::move(phrase));
    }
}

void readContext(Reader& r, const json& root, Pattern& pattern) {
    const json* context = r.objectMember(root, "context", "", true);
    if (context == nullptr) {
        return;
    }
    r.uintValue(*context, "root", "context", true, pattern.context.root);
    r.stringValue(*context, "scaleId", "context", true, pattern.context.scaleId);
    const json* progression = r.arrayMember(*context, "progression", "context", true);
    if (progression == nullptr) {
        return;
    }
    for (size_t i = 0; i < progression->size() && !r.failed(); ++i) {
        const json& entry = (*progression)[i];
        const std::string path = Reader::element("context.progression", i);
        if (!entry.is_object()) {
            r.fail(path, "expected an object");
            return;
        }
        ChordEvent event;
        r.uintValue(entry, "rootOffset", path, true, event.chord.rootOffset);
        r.enumValue(entry, "quality", path, true, kChordQualities, event.chord.quality);
        r.uintValue(entry, "startHalfBar", path, true, event.startHalfBar);
        r.uintValue(entry, "lengthHalfBars", path, true, event.lengthHalfBars);
        pattern.context.progression.push_back(event);
    }
}

void readVoices(Reader& r, const json& root, Pattern& pattern) {
    const json* voices = r.arrayMember(root, "voices", "", true);
    if (voices == nullptr) {
        return;
    }
    for (size_t i = 0; i < voices->size() && !r.failed(); ++i) {
        const json& entry = (*voices)[i];
        const std::string path = Reader::element("voices", i);
        if (!entry.is_object()) {
            r.fail(path, "expected an object");
            return;
        }
        Track track;
        readTrack(r, entry, path, track);
        pattern.voices.push_back(std::move(track));
    }
}

bool readPattern(const json& root, Pattern& pattern, std::string& error) {
    Reader r;
    r.uintValue(root, "lengthBars", "", true, pattern.lengthBars);
    r.uintValue(root, "timeSigNum", "", false, pattern.timeSigNum);
    r.uintValue(root, "timeSigDen", "", false, pattern.timeSigDen);
    r.stringValue(root, "styleId", "", false, pattern.styleId);
    r.stringValue(root, "kickGridId", "", false, pattern.kickGridId);
    if (root.contains("kickRoot")) {
        PitchClass kickRoot = 0;
        r.uintValue(root, "kickRoot", "", true, kickRoot);
        pattern.kickRoot = kickRoot;
    }
    r.enumValue(root, "polymeterPhase", "", false, kPolymeterPhases, pattern.polymeterPhase);
    if (const json* ref = r.objectMember(root, "rhythmRef", "", false)) {
        RhythmReference reference;
        readRhythmReference(r, *ref, "rhythmRef", reference);
        pattern.rhythmRef = reference;
    }
    if (const json* history = r.arrayMember(root, "refineHistory", "", false)) {
        for (size_t i = 0; i < history->size() && !r.failed(); ++i) {
            if (!(*history)[i].is_string()) {
                r.fail(Reader::element("refineHistory", i), "expected a string");
                break;
            }
            pattern.refineHistory.push_back((*history)[i].get<std::string>());
        }
    }
    if (const json* voicing = r.objectMember(root, "voicing", "", false)) {
        r.boolValue(*voicing, "chordMemory", "voicing", false, pattern.voicing.chordMemory);
        r.intValue(*voicing, "lowNote", "voicing", false, pattern.voicing.lowNote);
        r.intValue(*voicing, "highNote", "voicing", false, pattern.voicing.highNote);
    }
    r.uintValue(root, "qualityScore", "", false, pattern.qualityScore);
    r.uintValue(root, "nextNoteId", "", true, pattern.nextNoteId);
    r.uintValue(root, "version", "", false, pattern.version);
    if (const json* info = r.objectMember(root, "info", "", false)) {
        readInfo(r, *info, "info", pattern.info);
    }
    readContext(r, root, pattern);
    readPhrases(r, root, pattern);
    readVoices(r, root, pattern);
    if (r.failed()) {
        error = r.error();
        return false;
    }
    return true;
}

} // namespace

json patternToJson(const Pattern& p) {
    json phrases = json::array();
    for (const Phrase& phrase : p.phrases) {
        phrases.push_back(phraseToJson(phrase));
    }
    json voices = json::array();
    for (const Track& track : p.voices) {
        voices.push_back(trackToJson(track));
    }
    json progression = json::array();
    for (const ChordEvent& event : p.context.progression) {
        progression.push_back(json{{"rootOffset", event.chord.rootOffset},
                                   {"quality", nameOf(kChordQualities, event.chord.quality)},
                                   {"startHalfBar", event.startHalfBar},
                                   {"lengthHalfBars", event.lengthHalfBars}});
    }
    json history = json::array();
    for (const std::string& entry : p.refineHistory) {
        history.push_back(entry);
    }

    json root{{"stateVersion", kPatternStateVersion},
              {"lengthBars", p.lengthBars},
              {"phrases", std::move(phrases)},
              {"timeSigNum", p.timeSigNum},
              {"timeSigDen", p.timeSigDen},
              {"styleId", p.styleId},
              {"kickGridId", p.kickGridId},
              {"polymeterPhase", nameOf(kPolymeterPhases, p.polymeterPhase)},
              {"refineHistory", std::move(history)},
              {"context",
               json{{"root", p.context.root}, {"scaleId", p.context.scaleId}, {"progression", std::move(progression)}}},
              {"voicing", json{{"chordMemory", p.voicing.chordMemory},
                               {"lowNote", p.voicing.lowNote},
                               {"highNote", p.voicing.highNote}}},
              {"voices", std::move(voices)},
              {"qualityScore", p.qualityScore},
              {"nextNoteId", p.nextNoteId},
              {"version", p.version},
              {"info", infoToJson(p.info)}};
    if (p.kickRoot.has_value()) {
        root["kickRoot"] = *p.kickRoot;
    }
    if (p.rhythmRef.has_value()) {
        root["rhythmRef"] = rhythmReferenceToJson(*p.rhythmRef);
    }
    return root;
}

std::string patternToString(const Pattern& pattern) {
    return patternToJson(pattern).dump();
}

bool migrateDocument(json& document, int fromVersion, int toVersion, std::span<const Migration> steps,
                     std::string& error) {
    if (fromVersion < 1) {
        error = "stateVersion: unsupported version " + std::to_string(fromVersion);
        return false;
    }
    if (toVersion < fromVersion) {
        error = "stateVersion: cannot migrate backwards";
        return false;
    }
    // Upgrading version v to v + 1 uses steps[v - 1], so steps[fromVersion - 1 .. toVersion - 2] must exist.
    if (toVersion > fromVersion && steps.size() < static_cast<size_t>(toVersion - 1)) {
        error = "stateVersion: no migration from version " + std::to_string(fromVersion);
        return false;
    }
    for (int version = fromVersion; version < toVersion; ++version) {
        const Migration& step = steps[static_cast<size_t>(version - 1)];
        if (!step || !step(document, error)) {
            if (error.empty()) {
                error = "stateVersion: migration from version " + std::to_string(version) + " failed";
            }
            return false;
        }
        document["stateVersion"] = version + 1;
    }
    return true;
}

LoadResult loadPatternJson(const json& document) {
    LoadResult result;
    if (!document.is_object()) {
        result.error = "document: expected an object";
        return result;
    }
    json working = document;

    Reader versionReader;
    int version = 0;
    versionReader.intValue(working, "stateVersion", "", true, version);
    if (versionReader.failed()) {
        result.error = versionReader.error();
        return result;
    }
    if (version > kPatternStateVersion) {
        result.fromNewerVersion = true; // best effort: unknown fields are ignored
    } else if (version < kPatternStateVersion) {
        static const std::vector<Migration> kMigrations; // empty until the format changes
        std::string error;
        if (!migrateDocument(working, version, kPatternStateVersion, kMigrations, error)) {
            result.error = error;
            return result;
        }
    }

    Pattern pattern;
    std::string error;
    if (!readPattern(working, pattern, error)) {
        result.error = error;
        return result;
    }
    const auto issues = validatePattern(pattern);
    if (!issues.empty()) {
        result.error = issues.front();
        return result;
    }
    result.pattern = std::move(pattern);
    return result;
}

LoadResult loadPattern(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded()) {
        LoadResult result;
        result.error = "document: not valid JSON";
        return result;
    }
    return loadPatternJson(document);
}

} // namespace mm::core
