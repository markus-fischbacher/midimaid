#pragma once

#include "core/Shortcuts.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace mm::plugin {

/// The chord of a key press (SPEC 8.3, D-235): modifiers Cmd (Ctrl under Windows and Linux), Shift and Alt, and the
/// name of the key. Nullopt for the space bar, for a lone modifier key and for keys without a name in `core/Shortcuts`.
std::optional<mm::core::KeyChord> chordOf(const juce::KeyPress& press);

/// The key press that stands for a chord (for tests and for showing nothing but the text).
juce::KeyPress keyPressOf(const mm::core::KeyChord& chord);

/// How a chord is shown to the user: "Cmd+Shift+K" (Ctrl instead of Cmd under Windows and Linux).
juce::String chordText(const mm::core::KeyChord& chord);

} // namespace mm::plugin
