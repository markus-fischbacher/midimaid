#pragma once

#include <array>
#include <string>
#include <string_view>

namespace mm::core::text {

// Every UI text the code asks for (SPEC 8.2). The tests check that each language file has exactly these keys.
// X(name, key)
#define MM_TEXT_KEYS(X)                                                                                                \
    X(kRoleSolo, "role.solo")                                                                                          \
    X(kRoleHub, "role.hub")                                                                                            \
    X(kRoleVoice, "role.voice")                                                                                        \
    X(kVoiceN, "voice.n")                                                                                              \
    X(kVoiceBass, "voice.bass")                                                                                        \
    X(kVoiceMelody, "voice.melody")                                                                                    \
    X(kFollowHub, "follow.hub")                                                                                        \
    X(kFollowOwn, "follow.own")                                                                                        \
    X(kOutputVoice, "output.voice")                                                                                    \
    X(kOutputNone, "output.none")                                                                                      \
    X(kKeyAuto, "key.auto")                                                                                            \
    X(kKeyNote, "key.note")                                                                                            \
    X(kScaleAuto, "scale.auto")                                                                                        \
    X(kScaleItem, "scale.item")                                                                                        \
    X(kBarsOne, "bars.one")                                                                                            \
    X(kBarsMany, "bars.many")                                                                                          \
    X(kSeedRandom, "seed.random")                                                                                      \
    X(kButtonRandom, "button.random")                                                                                  \
    X(kButtonTakeOver, "button.takeover")                                                                              \
    X(kButtonGenerate, "button.generate")                                                                              \
    X(kButtonOpenHub, "button.openHub")                                                                                \
    X(kButtonMute, "button.mute")                                                                                      \
    X(kButtonLock, "button.lock")                                                                                      \
    X(kVoiceLockedSuffix, "voice.lockedSuffix")                                                                        \
    X(kStatusDoneLocked, "status.doneLocked")                                                                          \
    X(kStatusAllLocked, "status.allLocked")                                                                            \
    X(kStatusVaried, "status.varied")                                                                                  \
    X(kStatusNothingToVary, "status.nothingToVary")                                                                    \
    X(kStatusVaryAllLocked, "status.varyAllLocked")                                                                    \
    X(kButtonMuteByHub, "button.muteByHub")                                                                            \
    X(kLabelEnergy, "label.energy")                                                                                    \
    X(kLabelCreativity, "label.creativity")                                                                            \
    X(kOctaveItem, "octave.item")                                                                                      \
    X(kStatusGenerating, "status.generating")                                                                          \
    X(kStatusDone, "status.done")                                                                                      \
    X(kStatusNoResult, "status.noResult")                                                                              \
    X(kStatusUseHub, "status.useHub")                                                                                  \
    X(kStatusForwarded, "status.forwarded")                                                                            \
    X(kStatusLate, "status.late")                                                                                      \
    X(kSlotEmpty, "slot.empty")                                                                                        \
    X(kSlotInfo, "slot.info")                                                                                          \
    X(kRollTitle, "roll.title")                                                                                        \
    X(kRollEmpty, "roll.empty")                                                                                        \
    X(kGroupHubOne, "group.hub.one")                                                                                   \
    X(kGroupHubMany, "group.hub.many")                                                                                 \
    X(kGroupConnected, "group.connected")                                                                              \
    X(kGroupMissing, "group.missing")                                                                                  \
    X(kGroupOffered, "group.offered")                                                                                  \
    X(kGroupRefused, "group.refused")                                                                                  \
    X(kGroupSandboxHint, "group.sandboxHint")

#define MM_TEXT_KEY_CONSTANT(name, key) inline constexpr std::string_view name = key;
MM_TEXT_KEYS(MM_TEXT_KEY_CONSTANT)
#undef MM_TEXT_KEY_CONSTANT

/// All keys above, for the tests.
inline constexpr auto kAllKeys = std::to_array<std::string_view>({
#define MM_TEXT_KEY_ENTRY(name, key) key,
    MM_TEXT_KEYS(MM_TEXT_KEY_ENTRY)
#undef MM_TEXT_KEY_ENTRY
});

/// The key of the name of a scale ("scale.natural_minor"); every id of `allScales()` has one.
inline std::string scaleKey(std::string_view scaleId) {
    return "scale." + std::string(scaleId);
}

} // namespace mm::core::text
