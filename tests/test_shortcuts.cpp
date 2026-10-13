#include "core/Shortcuts.h"

#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace mm::core;

TEST_CASE("the actions have unique ids and texts and cover the list of the spec", "[shortcuts]") {
    std::set<std::string_view> ids;
    std::set<std::string_view> texts;
    for (const ShortcutAction& action : shortcutActions()) {
        CHECK(ids.insert(action.id).second);
        CHECK(texts.insert(action.textKey).second);
        CHECK(findShortcutAction(action.id) == &action);
    }
    for (const char* id : {"undo", "redo", "duplicate", "select_all", "delete", "pitch_up", "pitch_down", "octave_up",
                           "octave_down", "move_left", "move_right", "longer", "shorter", "grid_finer", "grid_coarser",
                           "triplets", "snap_toggle", "fold_toggle", "focus_1", "focus_8", "focus_all", "generate",
                           "refine_send", "vary", "history_back", "history_forward"}) {
        CHECK(findShortcutAction(id) != nullptr);
    }
    CHECK(findShortcutAction("") == nullptr);
    CHECK(findShortcutAction("format_disk") == nullptr);
}

TEST_CASE("a chord is written and read in its normal form", "[shortcuts]") {
    KeyChord chord;
    chord.cmd = true;
    chord.shift = true;
    chord.key = "k";
    CHECK(formatChord(chord) == "cmd+shift+k");
    CHECK(parseChord("cmd+shift+k") == chord);
    CHECK(parseChord("Shift+CMD+K") == chord); // case and order do not matter
    CHECK(parseChord("ctrl+shift+k") == chord); // Ctrl is Cmd
    CHECK(formatChord(*parseChord("alt+left")) == "alt+left");
    CHECK(formatChord(*parseChord("f5")) == "f5");
    CHECK(formatChord(*parseChord("cmd+alt+shift+9")) == "cmd+shift+alt+9");
    CHECK(formatChord(*parseChord("cmd+[")) == "cmd+[");
    CHECK(formatChord(*parseChord("pageup")) == "pageup");
}

TEST_CASE("a chord without a usable key is refused, the space bar above all", "[shortcuts]") {
    for (const char* text : {"", "cmd+", "+", "space", "cmd+space", "alt+ ", "cmd+shift", "cmd+kk", "f21", "f0", "hyper+k", "cmd++",
                             "cmd+shift+", "k+cmd"}) {
        INFO(text);
        CHECK_FALSE(parseChord(text).has_value());
    }
    for (const char* text : {"a", "z", "0", "9", "f1", "f20", "escape", "return", "tab", "delete", "backspace", "home", "end",
                             "pagedown", "up", "down", "right", "-", "=", "[", "]", ";", "'", ",", ".", "/", "`", "\\"}) {
        INFO(text);
        CHECK(parseChord(text).has_value());
    }
}

TEST_CASE("a chord without a modifier is recognised", "[shortcuts]") {
    CHECK(hasNoModifier(*parseChord("f5")));
    CHECK(hasNoModifier(*parseChord("k")));
    CHECK_FALSE(hasNoModifier(*parseChord("cmd+k")));
    CHECK_FALSE(hasNoModifier(*parseChord("shift+k")));
    CHECK_FALSE(hasNoModifier(*parseChord("alt+k")));
}

TEST_CASE("the assignments of a file are put in order", "[shortcuts]") {
    const std::map<std::string, std::string> raw{{"generate", "CMD+G"},   {"vary", "cmd+g"},         // one chord twice
                                                 {"undo", "cmd+z"},       {"nonsense", "cmd+q"},     // unknown action
                                                 {"redo", "space"},       {"delete", ""},             // not bindable
                                                 {"duplicate", "cmd+d"}, {"select_all", "ctrl+a"}};
    const auto clean = sanitizeShortcuts(raw);
    CHECK(clean.size() == 4);
    CHECK(clean.at("generate") == "cmd+g"); // the first action by id keeps it
    CHECK(clean.count("vary") == 0);
    CHECK(clean.at("undo") == "cmd+z");
    CHECK(clean.at("duplicate") == "cmd+d");
    CHECK(clean.at("select_all") == "cmd+a"); // the normal form
    CHECK(sanitizeShortcuts(clean) == clean);
    CHECK(sanitizeShortcuts({}).empty());
}

TEST_CASE("the action of a chord is found", "[shortcuts]") {
    const std::map<std::string, std::string> map{{"generate", "cmd+g"}, {"undo", "f5"}};
    CHECK(actionForChord(map, *parseChord("cmd+g")) == "generate");
    CHECK(actionForChord(map, *parseChord("F5")) == "undo");
    CHECK(actionForChord(map, *parseChord("cmd+shift+g")).empty());
    CHECK(actionForChord({}, *parseChord("f5")).empty());
}

TEST_CASE("assigning a chord takes it from the action that had it", "[shortcuts]") {
    std::map<std::string, std::string> map{{"generate", "cmd+g"}};
    map = assignShortcut(map, "vary", parseChord("cmd+g"));
    CHECK(map.count("generate") == 0);
    CHECK(map.at("vary") == "cmd+g");
    map = assignShortcut(map, "vary", parseChord("f6")); // another key for the same action
    CHECK(map.at("vary") == "f6");
    CHECK(map.size() == 1);
    map = assignShortcut(map, "vary", std::nullopt); // removed
    CHECK(map.empty());
    map = assignShortcut(map, "vary", std::nullopt); // nothing to remove
    CHECK(map.empty());
    map = assignShortcut(map, "no_such_action", parseChord("f1"));
    CHECK(map.empty());
}
