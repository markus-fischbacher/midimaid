#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

namespace mm::core {

/// Sets the swing of every voice to the default of the style for its role (STYLES.md 1.15) with full amount and the
/// straight template. Called when a pattern is created or the style changes; generating a single voice again leaves
/// the controls of the user alone.
void applyStyleGroove(Pattern& pattern, const StyleProfile& style);

/// What the swing control of a voice shows (SPEC 3.9, no double swing). A drum reference brings its own swing: the
/// control is inactive and shows the swing of the template, the mean delay of the odd 16ths relative to a pair of
/// 16ths (480 ticks), scaled by the amount, as a value between 0.50 and 0.75 in whole percent. Without usable
/// reference the control works and shows the value of the voice.
struct EffectiveGroove {
    float swing = 0.5f;
    bool controlActive = true;
};

EffectiveGroove effectiveGroove(const Pattern& pattern, const Track& track);

/// True if the UI should warn that the swung bass may flam with the straight kick: bass with a swing above 56 %.
bool bassSwingMayFlam(VoiceRole role, float swing);

} // namespace mm::core
