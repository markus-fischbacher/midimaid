#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mm::core {

/// Keyboard shortcuts assigned by the user (SPEC 8.3, v1.1, D-126, D-235). MidiMaid binds no key in advance: the actions
/// below exist, and the user gives them keys in the settings. No JUCE here; the plug-in translates key presses into the
/// text form of `KeyChord`.

/// What a shortcut can do. The ids are what the settings file stores.
struct ShortcutAction {
    std::string_view id;
    std::string_view textKey; ///< key in the translation table
};

std::span<const ShortcutAction> shortcutActions();
/// The action with this id, or null.
const ShortcutAction* findShortcutAction(std::string_view id);

/// A key with its modifiers. `cmd` is Cmd on macOS and Ctrl on Windows and Linux. `key` is the lower case name of the
/// key: a letter, a digit, a punctuation character, `f1`-`f20`, `left`, `right`, `up`, `down`, `home`, `end`, `pageup`,
/// `pagedown`, `delete`, `backspace`, `return`, `tab`, `escape`. The space bar is not a key a chord can have: it stays
/// the transport of the DAW.
struct KeyChord {
    bool cmd = false;
    bool shift = false;
    bool alt = false;
    std::string key;

    bool operator==(const KeyChord&) const = default;
};

/// "cmd+shift+k", "alt+left", "f5": modifiers in the order cmd, shift, alt, then the key.
std::string formatChord(const KeyChord& chord);
/// The chord of a text; nullopt for an unknown key, an unknown modifier, a space bar or an empty text. Case and the
/// order of the parts do not matter.
std::optional<KeyChord> parseChord(std::string_view text);
/// True if the chord has no modifier (it must not fire while a text field has the focus).
bool hasNoModifier(const KeyChord& chord);

/// The assignments of a settings file: action id -> chord text. Brings them into order: unknown actions and chords that
/// do not parse are dropped, every chord is written in its normal form, and a chord that two actions share stays with
/// the first of them (by id).
std::map<std::string, std::string> sanitizeShortcuts(const std::map<std::string, std::string>& shortcuts);

/// The action a chord is assigned to, empty for none.
std::string actionForChord(const std::map<std::string, std::string>& shortcuts, const KeyChord& chord);

/// The assignments with `chord` given to `action` (taken from another action if it had it), or with the assignment of
/// `action` removed when `chord` is empty.
std::map<std::string, std::string> assignShortcut(std::map<std::string, std::string> shortcuts, std::string_view action,
                                                  const std::optional<KeyChord>& chord);

} // namespace mm::core
