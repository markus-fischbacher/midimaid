#include "core/Shortcuts.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace mm::core {

namespace {

constexpr std::array<ShortcutAction, 36> kActions{{
    {"undo", "shortcut.undo"},
    {"redo", "shortcut.redo"},
    {"duplicate", "shortcut.duplicate"},
    {"select_all", "shortcut.selectAll"},
    {"delete", "shortcut.delete"},
    {"pitch_up", "shortcut.pitchUp"},
    {"pitch_down", "shortcut.pitchDown"},
    {"octave_up", "shortcut.octaveUp"},
    {"octave_down", "shortcut.octaveDown"},
    {"move_left", "shortcut.moveLeft"},
    {"move_right", "shortcut.moveRight"},
    {"longer", "shortcut.longer"},
    {"shorter", "shortcut.shorter"},
    {"grid_finer", "shortcut.gridFiner"},
    {"grid_coarser", "shortcut.gridCoarser"},
    {"triplets", "shortcut.triplets"},
    {"snap_toggle", "shortcut.snapToggle"},
    {"fold_toggle", "shortcut.foldToggle"},
    {"focus_1", "shortcut.focus1"},
    {"focus_2", "shortcut.focus2"},
    {"focus_3", "shortcut.focus3"},
    {"focus_4", "shortcut.focus4"},
    {"focus_5", "shortcut.focus5"},
    {"focus_6", "shortcut.focus6"},
    {"focus_7", "shortcut.focus7"},
    {"focus_8", "shortcut.focus8"},
    {"focus_all", "shortcut.focusAll"},
    {"generate", "shortcut.generate"},
    {"refine_send", "shortcut.refineSend"},
    {"vary", "shortcut.vary"},
    {"history_back", "shortcut.historyBack"},
    {"history_forward", "shortcut.historyForward"},
    {"accent", "shortcut.accent"},
    {"slide", "shortcut.slide"},
    {"ratchet_off", "shortcut.ratchetOff"},
    {"palette", "shortcut.palette"},
}};

bool isNamedKey(const std::string& key) {
    static const std::array<std::string_view, 12> names{"left", "right", "up",  "down",      "home",   "end",
                                                        "pageup", "pagedown", "delete", "backspace", "return", "tab"};
    if (std::find(names.begin(), names.end(), key) != names.end() || key == "escape") {
        return true;
    }
    if (key.size() >= 2 && key[0] == 'f') { // f1 to f20
        const bool digits = std::all_of(key.begin() + 1, key.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        if (digits) {
            const int number = std::stoi(key.substr(1));
            return number >= 1 && number <= 20;
        }
    }
    if (key.size() == 1) {
        const char c = key[0];
        return (std::isalnum(static_cast<unsigned char>(c)) != 0) || std::string_view("-=[];',./`\\").find(c) != std::string_view::npos;
    }
    return false;
}

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

std::span<const ShortcutAction> shortcutActions() {
    return kActions;
}

const ShortcutAction* findShortcutAction(std::string_view id) {
    const auto it = std::find_if(kActions.begin(), kActions.end(), [&](const ShortcutAction& action) { return action.id == id; });
    return it == kActions.end() ? nullptr : &*it;
}

std::string formatChord(const KeyChord& chord) {
    std::string text;
    if (chord.cmd) {
        text += "cmd+";
    }
    if (chord.shift) {
        text += "shift+";
    }
    if (chord.alt) {
        text += "alt+";
    }
    return text + chord.key;
}

std::optional<KeyChord> parseChord(std::string_view text) {
    KeyChord chord;
    std::vector<std::string> parts;
    std::string current;
    const std::string all = lower(text);
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == '+' && i + 1 < all.size() && !current.empty()) {
            parts.push_back(current);
            current.clear();
        } else {
            current += all[i];
        }
    }
    if (current.empty()) {
        return std::nullopt;
    }
    parts.push_back(current);
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part == "cmd" || part == "ctrl") {
            chord.cmd = true;
        } else if (part == "shift") {
            chord.shift = true;
        } else if (part == "alt") {
            chord.alt = true;
        } else {
            return std::nullopt;
        }
    }
    chord.key = parts.back();
    if (chord.key == "space" || chord.key == " " || !isNamedKey(chord.key)) {
        return std::nullopt;
    }
    return chord;
}

bool hasNoModifier(const KeyChord& chord) {
    return !chord.cmd && !chord.shift && !chord.alt;
}

std::map<std::string, std::string> sanitizeShortcuts(const std::map<std::string, std::string>& shortcuts) {
    std::map<std::string, std::string> result;
    std::map<std::string, std::string> owner; // normal form of the chord -> action
    for (const auto& [action, text] : shortcuts) { // by id: the first of two actions with one chord keeps it
        if (findShortcutAction(action) == nullptr) {
            continue;
        }
        const auto chord = parseChord(text);
        if (!chord.has_value()) {
            continue;
        }
        const std::string normal = formatChord(*chord);
        if (owner.count(normal) != 0) {
            continue;
        }
        owner[normal] = action;
        result[action] = normal;
    }
    return result;
}

std::string actionForChord(const std::map<std::string, std::string>& shortcuts, const KeyChord& chord) {
    const std::string normal = formatChord(chord);
    for (const auto& [action, text] : shortcuts) {
        if (text == normal) {
            return action;
        }
    }
    return {};
}

std::map<std::string, std::string> assignShortcut(std::map<std::string, std::string> shortcuts, std::string_view action,
                                                  const std::optional<KeyChord>& chord) {
    if (findShortcutAction(action) == nullptr) {
        return shortcuts;
    }
    if (!chord.has_value()) {
        shortcuts.erase(std::string(action));
        return shortcuts;
    }
    const std::string normal = formatChord(*chord);
    for (auto it = shortcuts.begin(); it != shortcuts.end();) {
        it = it->second == normal ? shortcuts.erase(it) : std::next(it);
    }
    shortcuts[std::string(action)] = normal;
    return shortcuts;
}

} // namespace mm::core
