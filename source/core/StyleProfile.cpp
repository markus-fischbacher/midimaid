#include "core/StyleProfile.h"

#include "core/ChordSymbol.h"
#include "core/JsonReader.h"
#include "core/KickGrid.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace mm::core {

using detail::Reader;
using nlohmann::json;

namespace {

bool isIdentifier(const std::string& id) {
    if (id.empty() || !(id.front() >= 'a' && id.front() <= 'z')) {
        return false;
    }
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool isChordColorId(const std::string& id) {
    static const std::set<std::string> known{"min", "fifth", "min7", "min9", "sus", "cluster"};
    return known.count(id) > 0;
}

bool isHarmonyMode(const std::string& id) {
    return id == "static" || id == "two_chord" || id == "four_chord";
}

bool isChordLength(int bars) {
    return bars == 1 || bars == 2 || bars == 4 || bars == 8 || bars == 16;
}

int toPercent(double value) {
    return static_cast<int>(std::lround(value * 100.0));
}

struct ClearanceName {
    std::string_view name;
    uint32_t ticks;
};
constexpr ClearanceName kClearances[] = {{"off", 0}, {"1/64", 60}, {"1/32", 120}, {"1/16", 240}};

// ---------------------------------------------------------------------------------------------------------------
// Validation

class Issues {
public:
    void add(const std::string& path, const std::string& message) { list_.push_back(path + ": " + message); }
    std::vector<std::string> take() { return std::move(list_); }

private:
    std::vector<std::string> list_;
};

void checkWeighted(const std::vector<WeightedId>& list, const std::string& path, Issues& issues, int minWeight,
                   bool (*validId)(const std::string&)) {
    if (list.empty()) {
        issues.add(path, "needs at least one entry");
    }
    std::set<std::string> seen;
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string entry = path + "[" + std::to_string(i) + "]";
        if (!validId(list[i].id)) {
            issues.add(entry + ".id", "invalid id '" + list[i].id + "'");
        }
        if (!seen.insert(list[i].id).second) {
            issues.add(entry + ".id", "duplicate id '" + list[i].id + "'");
        }
        if (list[i].weight < minWeight) {
            issues.add(entry + ".w", "must be at least " + std::to_string(minWeight));
        }
    }
}

void checkRange(const Range& range, const Range& limit, const std::string& path, Issues& issues) {
    if (range.low < limit.low || range.high > limit.high) {
        issues.add(path, "a profile may only narrow the default range " + std::to_string(limit.low) + "-" +
                             std::to_string(limit.high));
    }
    if (range.width() < kMinRangeWidth) {
        issues.add(path, "needs at least " + std::to_string(kMinRangeWidth) + " pitches");
    }
}

} // namespace

std::vector<std::string> validateStyleProfile(const StyleProfile& profile) {
    Issues issues;
    if (!isIdentifier(profile.id)) {
        issues.add("id", "must be lower case letters, digits and underscores, starting with a letter");
    }
    if (profile.version < 1) {
        issues.add("version", "must be at least 1");
    }
    if (profile.nameDe.empty()) {
        issues.add("name.de", "must not be empty");
    }
    if (profile.tempoMin < 60 || profile.tempoMax > 220 || profile.tempoMin >= profile.tempoMax) {
        issues.add("tempo", "must be an ascending range within 60-220 BPM");
    }
    checkWeighted(profile.scales, "scales", issues, 1, [](const std::string& id) { return findScale(id) != nullptr; });
    if (profile.chromaticDefaultPercent < 0 || profile.chromaticDefaultPercent > 100) {
        issues.add("chromatic_default", "must be 0-1");
    }

    // harmony
    checkWeighted(profile.harmony.modes, "harmony.modes", issues, 0, isHarmonyMode);
    const int modeWeights = std::accumulate(profile.harmony.modes.begin(), profile.harmony.modes.end(), 0,
                                            [](int sum, const WeightedId& m) { return sum + m.weight; });
    if (modeWeights <= 0) {
        issues.add("harmony.modes", "at least one weight must be above 0");
    }
    bool wantsProgressions = false;
    for (const WeightedId& mode : profile.harmony.modes) {
        wantsProgressions = wantsProgressions || (mode.id != "static" && mode.weight > 0);
    }
    if (wantsProgressions && profile.harmony.progressions.empty()) {
        issues.add("harmony.progressions", "needed because a chord-changing mode has a weight above 0");
    }
    for (size_t i = 0; i < profile.harmony.progressions.size(); ++i) {
        const size_t size = profile.harmony.progressions[i].size();
        if (size < 2 || size > 4) {
            issues.add("harmony.progressions[" + std::to_string(i) + "]", "needs 2-4 chords");
        }
    }
    if (!isChordLength(profile.harmony.chordLengthBarsMin) || !isChordLength(profile.harmony.chordLengthBarsMax) ||
        profile.harmony.chordLengthBarsMin > profile.harmony.chordLengthBarsMax) {
        issues.add("harmony.chord_length_bars", "must be an ascending pair of 1, 2, 4, 8 or 16");
    }

    // kick
    if (findKickGrid(profile.kickDefault) == nullptr) {
        issues.add("kick_default", "unknown kick grid '" + profile.kickDefault + "'");
    }
    std::set<std::string> variants;
    for (size_t i = 0; i < profile.kickVariants.size(); ++i) {
        if (findKickGrid(profile.kickVariants[i]) == nullptr) {
            issues.add("kick_variants[" + std::to_string(i) + "]",
                       "unknown kick grid '" + profile.kickVariants[i] + "'");
        }
        if (!variants.insert(profile.kickVariants[i]).second) {
            issues.add("kick_variants[" + std::to_string(i) + "]", "duplicate kick grid");
        }
    }

    // bass
    checkRange(profile.bass.range, kDefaultBassRange, "bass.range", issues);
    if (!(profile.bass.swingDefault >= 0.5f && profile.bass.swingDefault <= 0.75f)) {
        issues.add("bass.swing_default", "must be 0.50-0.75");
    }
    const BassMovement& movement = profile.bass.movement;
    if (movement.rootPercent < 0 || movement.fifthOctavePercent < 0 || movement.passingPercent < 0 ||
        movement.rootPercent + movement.fifthOctavePercent + movement.passingPercent != 100) {
        issues.add("bass.movement", "root, fifth_octave and passing must be shares that add up to 1");
    }
    checkWeighted(profile.bass.archetypes, "bass.archetypes", issues, 1, isIdentifier);

    // melody
    checkRange(profile.melody.range, kDefaultMelodyRange, "melody.range", issues);
    checkRange(profile.melody.voicingRange, kDefaultStabRange, "melody.voicing.range", issues);
    if (!(profile.melody.swingDefault >= 0.5f && profile.melody.swingDefault <= 0.75f)) {
        issues.add("melody.swing_default", "must be 0.50-0.75");
    }
    checkWeighted(profile.melody.archetypes, "melody.archetypes", issues, 1, isIdentifier);
    checkWeighted(profile.melody.chordColors, "melody.chord_colors", issues, 1, isChordColorId);

    // quality
    const QualityProfile& q = profile.quality;
    if (q.minScore < 0 || q.minScore > 100) {
        issues.add("quality.min_score", "must be 0-100");
    }
    if (q.motif < 0 || q.range < 0 || q.density < 0 || q.kick < 0 || q.intervals < 0 || q.rhythmVariety < 0 ||
        q.motif + q.range + q.density + q.kick + q.intervals + q.rhythmVariety <= 0) {
        issues.add("quality.weights", "must not be negative and at least one must be above 0");
    }
    return issues.take();
}

// ---------------------------------------------------------------------------------------------------------------
// Loading

namespace {

void readWeighted(Reader& r, const json& parent, const std::string& key, const std::string& path,
                  std::vector<WeightedId>& out) {
    const json* array = r.arrayMember(parent, key, path, true);
    if (array == nullptr) {
        return;
    }
    const std::string arrayPath = Reader::child(path, key);
    for (size_t i = 0; i < array->size() && !r.failed(); ++i) {
        const std::string entryPath = Reader::element(arrayPath, i);
        const json& entry = (*array)[i];
        if (!entry.is_object()) {
            r.fail(entryPath, "expected an object");
            return;
        }
        WeightedId weighted;
        r.stringValue(entry, "id", entryPath, true, weighted.id);
        r.intValue(entry, "w", entryPath, true, weighted.weight);
        out.push_back(std::move(weighted));
    }
}

template <typename T>
void readPair(Reader& r, const json& parent, const std::string& key, const std::string& path, T& first, T& second) {
    const json* array = r.arrayMember(parent, key, path, true);
    if (array == nullptr) {
        return;
    }
    const std::string arrayPath = Reader::child(path, key);
    if (array->size() != 2) {
        r.fail(arrayPath, "expected two numbers");
        return;
    }
    T values[2]{};
    for (size_t i = 0; i < 2; ++i) {
        json wrapper{{"v", (*array)[i]}};
        r.intValue(wrapper, "v", Reader::element(arrayPath, i), true, values[i]);
    }
    first = values[0];
    second = values[1];
}

void readRange(Reader& r, const json& parent, const std::string& path, Range& range) {
    readPair(r, parent, "range", path, range.low, range.high);
}

void readClearance(Reader& r, const json& parent, const std::string& path, uint32_t& ticks) {
    std::string text;
    r.stringValue(parent, "kick_clearance", path, true, text);
    if (r.failed()) {
        return;
    }
    for (const ClearanceName& entry : kClearances) {
        if (entry.name == text) {
            ticks = entry.ticks;
            return;
        }
    }
    r.fail(Reader::child(path, "kick_clearance"), "expected off, 1/64, 1/32 or 1/16");
}

void readHarmony(Reader& r, const json& root, HarmonyProfile& harmony) {
    const json* object = r.objectMember(root, "harmony", "", true);
    if (object == nullptr) {
        return;
    }
    readWeighted(r, *object, "modes", "harmony", harmony.modes);
    if (const json* progressions = r.arrayMember(*object, "progressions", "harmony", true)) {
        for (size_t i = 0; i < progressions->size() && !r.failed(); ++i) {
            const std::string path = Reader::element("harmony.progressions", i);
            const json& entry = (*progressions)[i];
            if (!entry.is_array()) {
                r.fail(path, "expected an array of chord symbols");
                return;
            }
            std::vector<Chord> chords;
            for (size_t c = 0; c < entry.size() && !r.failed(); ++c) {
                std::string symbol;
                r.stringElement(entry[c], Reader::element(path, c), symbol);
                if (r.failed()) {
                    return;
                }
                const auto chord = parseChordSymbol(symbol);
                if (!chord.has_value()) {
                    r.fail(Reader::element(path, c), "invalid chord symbol '" + symbol + "'");
                    return;
                }
                chords.push_back(*chord);
            }
            harmony.progressions.push_back(std::move(chords));
        }
    }
    readPair(r, *object, "chord_length_bars", "harmony", harmony.chordLengthBarsMin, harmony.chordLengthBarsMax);
}

void readBass(Reader& r, const json& root, BassProfile& bass) {
    const json* object = r.objectMember(root, "bass", "", true);
    if (object == nullptr) {
        return;
    }
    readRange(r, *object, "bass", bass.range);
    readClearance(r, *object, "bass", bass.kickClearanceTicks);
    double swing = 0.5;
    r.doubleValue(*object, "swing_default", "bass", true, swing);
    bass.swingDefault = static_cast<float>(swing);
    if (const json* movement = r.objectMember(*object, "movement", "bass", true)) {
        double rootShare = 0.0, fifthShare = 0.0, passingShare = 0.0;
        r.doubleValue(*movement, "root", "bass.movement", true, rootShare);
        r.doubleValue(*movement, "fifth_octave", "bass.movement", true, fifthShare);
        r.doubleValue(*movement, "passing", "bass.movement", true, passingShare);
        r.boolValue(*movement, "root_on_chord_change", "bass.movement", false, bass.movement.rootOnChordChange);
        bass.movement.rootPercent = toPercent(rootShare);
        bass.movement.fifthOctavePercent = toPercent(fifthShare);
        bass.movement.passingPercent = toPercent(passingShare);
    }
    readWeighted(r, *object, "archetypes", "bass", bass.archetypes);
}

void readMelody(Reader& r, const json& root, MelodyProfile& melody) {
    const json* object = r.objectMember(root, "melody", "", true);
    if (object == nullptr) {
        return;
    }
    readRange(r, *object, "melody", melody.range);
    if (const json* voicing = r.objectMember(*object, "voicing", "melody", true)) {
        readRange(r, *voicing, "melody.voicing", melody.voicingRange);
        r.boolValue(*voicing, "chord_memory", "melody.voicing", false, melody.chordMemory);
    }
    double swing = 0.5;
    r.doubleValue(*object, "swing_default", "melody", true, swing);
    melody.swingDefault = static_cast<float>(swing);
    readWeighted(r, *object, "archetypes", "melody", melody.archetypes);
    readWeighted(r, *object, "chord_colors", "melody", melody.chordColors);
}

void readQuality(Reader& r, const json& root, QualityProfile& quality) {
    const json* object = r.objectMember(root, "quality", "", true);
    if (object == nullptr) {
        return;
    }
    r.intValue(*object, "min_score", "quality", true, quality.minScore);
    if (const json* weights = r.objectMember(*object, "weights", "quality", true)) {
        r.intValue(*weights, "motif", "quality.weights", true, quality.motif);
        r.intValue(*weights, "range", "quality.weights", true, quality.range);
        r.intValue(*weights, "density", "quality.weights", true, quality.density);
        r.intValue(*weights, "kick", "quality.weights", true, quality.kick);
        r.intValue(*weights, "intervals", "quality.weights", true, quality.intervals);
        r.intValue(*weights, "rhythm_variety", "quality.weights", true, quality.rhythmVariety);
    }
}

} // namespace

StyleLoadResult loadStyleProfileJson(const json& document) {
    StyleLoadResult result;
    if (!document.is_object()) {
        result.error = "document: expected an object";
        return result;
    }
    StyleProfile profile;
    Reader r;
    r.stringValue(document, "id", "", true, profile.id);
    r.uintValue(document, "version", "", true, profile.version);
    if (const json* name = r.objectMember(document, "name", "", true)) {
        r.stringValue(*name, "de", "name", true, profile.nameDe);
        r.stringValue(*name, "en", "name", false, profile.nameEn);
    }
    readPair(r, document, "tempo", "", profile.tempoMin, profile.tempoMax);
    readWeighted(r, document, "scales", "", profile.scales);
    double chromatic = 0.0;
    r.doubleValue(document, "chromatic_default", "", true, chromatic);
    profile.chromaticDefaultPercent = toPercent(chromatic);
    readHarmony(r, document, profile.harmony);
    r.stringValue(document, "kick_default", "", true, profile.kickDefault);
    if (const json* variants = r.arrayMember(document, "kick_variants", "", false)) {
        for (size_t i = 0; i < variants->size() && !r.failed(); ++i) {
            std::string variant;
            r.stringElement((*variants)[i], Reader::element("kick_variants", i), variant);
            profile.kickVariants.push_back(std::move(variant));
        }
    }
    readBass(r, document, profile.bass);
    readMelody(r, document, profile.melody);
    readQuality(r, document, profile.quality);
    if (profile.nameEn.empty()) {
        profile.nameEn = profile.nameDe;
    }
    if (r.failed()) {
        result.error = r.error();
        return result;
    }
    const auto issues = validateStyleProfile(profile);
    if (!issues.empty()) {
        result.error = issues.front();
        return result;
    }
    result.profile = std::move(profile);
    return result;
}

StyleLoadResult loadStyleProfile(std::string_view text) {
    const json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded()) {
        StyleLoadResult result;
        result.error = "document: not valid JSON";
        return result;
    }
    return loadStyleProfileJson(document);
}

} // namespace mm::core
