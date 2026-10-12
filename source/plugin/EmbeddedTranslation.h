#pragma once

#include "core/Translation.h"

#include <juce_core/juce_core.h>
#include <string>

namespace mm::plugin {

/// The UI texts compiled into the plugin (`resources/i18n/de.json`, `en.json`) in the language of the UI. The tables are
/// built once, on first use; the language can change (`setUiLanguage`), the tables never do, so the reference stays
/// valid. A text is looked up when a control is built: a window opened after a change shows the new language.
const mm::core::Translation& embeddedTranslation();

/// Chooses the language: "auto" (the system language), "de" or "en" (SPEC 8.2). Any thread.
void setUiLanguage(const std::string& setting);
/// "de" or "en": the language in use.
std::string uiLanguage();
/// The language the first use starts with: the setting of the settings file (read at once, the store reads in the
/// background), "de" when there is no file (tests, a disabled store) so that the texts do not depend on the computer.
std::string initialUiSetting();

/// `german` or `english` after the language in use; an empty English text gives the German one.
const std::string& localized(const std::string& german, const std::string& english);

/// The UI text for `key` (see `core/TextKeys.h`), with `{name}` placeholders filled from `args`.
juce::String tr(std::string_view key);
juce::String tr(std::string_view key, mm::core::Translation::Args args);

} // namespace mm::plugin
