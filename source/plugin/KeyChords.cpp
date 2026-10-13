#include "plugin/KeyChords.h"

#include <algorithm>
#include <array>

namespace mm::plugin {

namespace {

struct Named {
    int code;
    const char* name;
};

const std::array<Named, 12> kNamed{{{juce::KeyPress::leftKey, "left"},
                                    {juce::KeyPress::rightKey, "right"},
                                    {juce::KeyPress::upKey, "up"},
                                    {juce::KeyPress::downKey, "down"},
                                    {juce::KeyPress::homeKey, "home"},
                                    {juce::KeyPress::endKey, "end"},
                                    {juce::KeyPress::pageUpKey, "pageup"},
                                    {juce::KeyPress::pageDownKey, "pagedown"},
                                    {juce::KeyPress::deleteKey, "delete"},
                                    {juce::KeyPress::backspaceKey, "backspace"},
                                    {juce::KeyPress::returnKey, "return"},
                                    {juce::KeyPress::tabKey, "tab"}}};

} // namespace

std::optional<mm::core::KeyChord> chordOf(const juce::KeyPress& press) {
    if (!press.isValid()) {
        return std::nullopt;
    }
    mm::core::KeyChord chord;
    chord.cmd = press.getModifiers().isCommandDown();
    chord.shift = press.getModifiers().isShiftDown();
    chord.alt = press.getModifiers().isAltDown();
    const int code = press.getKeyCode();
    if (code == juce::KeyPress::escapeKey) {
        chord.key = "escape";
    } else if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F1Key + 19) {
        chord.key = "f" + std::to_string(code - juce::KeyPress::F1Key + 1);
    } else if (const auto it = std::find_if(kNamed.begin(), kNamed.end(), [&](const Named& n) { return n.code == code; });
               it != kNamed.end()) {
        chord.key = it->name;
    } else if (code > 32 && code < 127) {
        chord.key = std::string(1, static_cast<char>(std::tolower(code)));
    } else {
        return std::nullopt; // the space bar, modifier keys, the numeric pad and the like
    }
    // only what `core/Shortcuts` knows
    return mm::core::parseChord(mm::core::formatChord(chord));
}

juce::KeyPress keyPressOf(const mm::core::KeyChord& chord) {
    int modifiers = 0;
    modifiers |= chord.cmd ? juce::ModifierKeys::commandModifier : 0;
    modifiers |= chord.shift ? juce::ModifierKeys::shiftModifier : 0;
    modifiers |= chord.alt ? juce::ModifierKeys::altModifier : 0;
    int code = 0;
    if (chord.key == "escape") {
        code = juce::KeyPress::escapeKey;
    } else if (chord.key.size() >= 2 && chord.key[0] == 'f') {
        code = juce::KeyPress::F1Key + std::stoi(chord.key.substr(1)) - 1;
    } else if (const auto it = std::find_if(kNamed.begin(), kNamed.end(), [&](const Named& n) { return chord.key == n.name; });
               it != kNamed.end()) {
        code = it->code;
    } else if (chord.key.size() == 1) {
        code = std::toupper(static_cast<unsigned char>(chord.key[0]));
        if (!std::isalpha(code)) {
            code = static_cast<unsigned char>(chord.key[0]);
        }
    }
    return juce::KeyPress(code, juce::ModifierKeys(modifiers), 0);
}

juce::String chordText(const mm::core::KeyChord& chord) {
    juce::String text;
#if JUCE_MAC
    const juce::String command = "Cmd+";
#else
    const juce::String command = "Ctrl+";
#endif
    text += chord.cmd ? command : juce::String();
    text += chord.shift ? "Shift+" : "";
    text += chord.alt ? "Alt+" : "";
    juce::String key(chord.key);
    text += key.length() == 1 ? key.toUpperCase() : key.substring(0, 1).toUpperCase() + key.substring(1);
    return text;
}

} // namespace mm::plugin
