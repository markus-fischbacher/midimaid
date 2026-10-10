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
    X(kStatusRenewed, "status.renewed")                                                                                \
    X(kStatusNothingToRenew, "status.nothingToRenew")                                                                  \
    X(kStatusRenewLocked, "status.renewLocked")                                                                        \
    X(kButtonMuteByHub, "button.muteByHub")                                                                            \
    X(kLabelEnergy, "label.energy")                                                                                    \
    X(kLabelCreativity, "label.creativity")                                                                            \
    X(kButtonExpert, "button.expert")                                                                                  \
    X(kButtonRenew, "button.renew")                                                                                    \
    X(kButtonVary, "button.vary")                                                                                      \
    X(kButtonVaryAll, "button.varyAll")                                                                                \
    X(kButtonDuplicate, "button.duplicate")                                                                            \
    X(kButtonUndo, "button.undo")                                                                                      \
    X(kButtonRedo, "button.redo")                                                                                      \
    X(kButtonHistoryBack, "button.historyBack")                                                                        \
    X(kButtonHistoryForward, "button.historyForward")                                                                  \
    X(kLabelStrength, "label.strength")                                                                                \
    X(kLabelHistory, "label.history")                                                                                  \
    X(kLabelHistoryNone, "label.historyNone")                                                                          \
    X(kOctaveItem, "octave.item")                                                                                      \
    X(kStatusGenerating, "status.generating")                                                                          \
    X(kStatusDone, "status.done")                                                                                      \
    X(kButtonCancelGeneration, "button.cancelGeneration")                                                              \
    X(kButtonRetry, "button.retry")                                                                                    \
    X(kButtonOffline, "button.offline")                                                                                \
    X(kToggleAutoOffline, "toggle.autoOffline")                                                                        \
    X(kPromptHint, "prompt.hint")                                                                                      \
    X(kRefineHint, "refine.hint")                                                                                      \
    X(kButtonRefine, "button.refine")                                                                                  \
    X(kRefineScopeAll, "refine.scopeAll")                                                                              \
    X(kRefineScopePhrase, "refine.scopePhrase")                                                                        \
    X(kStatusRefined, "status.refined")                                                                                \
    X(kStatusNothingToRefine, "status.nothingToRefine")                                                                \
    X(kStatusRefineAllLocked, "status.refineAllLocked")                                                                \
    X(kStatusRefineNeedsAi, "status.refineNeedsAi")                                                                    \
    X(kStatusGeneratingAi, "status.generatingAi")                                                                      \
    X(kStatusAiFailed, "status.aiFailed")                                                                              \
    X(kStatusCancelled, "status.cancelled")                                                                            \
    X(kStatusBelowQuality, "status.belowQuality")                                                                      \
    X(kAiReasonAuth, "ai.reason.auth")                                                                                 \
    X(kAiReasonModel, "ai.reason.model")                                                                               \
    X(kAiReasonRate, "ai.reason.rate")                                                                                 \
    X(kAiReasonServer, "ai.reason.server")                                                                             \
    X(kAiReasonBad, "ai.reason.bad")                                                                                   \
    X(kAiReasonNetwork, "ai.reason.network")                                                                           \
    X(kAiReasonTimeout, "ai.reason.timeout")                                                                           \
    X(kAiReasonInvalid, "ai.reason.invalid")                                                                           \
    X(kButtonSettings, "button.settings")                                                                              \
    X(kSettingsTitle, "settings.title")                                                                                \
    X(kLabelProvider, "settings.provider")                                                                             \
    X(kProviderOffline, "settings.providerOffline")                                                                    \
    X(kLabelKey, "settings.key")                                                                                       \
    X(kKeyHint, "settings.keyHint")                                                                                    \
    X(kButtonKeySave, "button.keySave")                                                                                \
    X(kButtonKeyRemove, "button.keyRemove")                                                                            \
    X(kKeyStored, "settings.keyStored")                                                                                \
    X(kKeyMissing, "settings.keyMissing")                                                                              \
    X(kKeyNotRequired, "settings.keyNotRequired")                                                                      \
    X(kKeyMemoryOnly, "settings.keyMemoryOnly")                                                                        \
    X(kKeySaved, "settings.keySaved")                                                                                  \
    X(kKeySaveFailed, "settings.keySaveFailed")                                                                        \
    X(kKeyRemoved, "settings.keyRemoved")                                                                              \
    X(kLabelBaseUrl, "settings.baseUrl")                                                                               \
    X(kLabelModel, "settings.model")                                                                                   \
    X(kButtonLoadModels, "button.loadModels")                                                                          \
    X(kModelsLoaded, "settings.modelsLoaded")                                                                          \
    X(kModelsNone, "settings.modelsNone")                                                                              \
    X(kLabelTimeout, "settings.timeout")                                                                               \
    X(kLabelMaxTokens, "settings.maxTokens")                                                                           \
    X(kLabelSchemaLevel, "settings.schemaLevel")                                                                       \
    X(kLevelDefault, "settings.levelDefault")                                                                          \
    X(kLevelEnforced, "settings.levelEnforced")                                                                        \
    X(kLevelJson, "settings.levelJson")                                                                                \
    X(kLevelPrompt, "settings.levelPrompt")                                                                            \
    X(kButtonTestConnection, "button.testConnection")                                                                  \
    X(kTestRunning, "settings.testRunning")                                                                            \
    X(kTestOk, "settings.testOk")                                                                                      \
    X(kTestOkLowered, "settings.testOkLowered")                                                                        \
    X(kTestFailed, "settings.testFailed")                                                                              \
    X(kLabelConsent, "settings.consent")                                                                               \
    X(kConsentToggle, "settings.consentToggle")                                                                        \
    X(kConsentLocal, "settings.consentLocal")                                                                          \
    X(kConsentList, "settings.consentList")                                                                            \
    X(kConsentNone, "settings.consentNone")                                                                            \
    X(kToggleLogPrompts, "settings.logPrompts")                                                                        \
    X(kLogHint, "settings.logHint")                                                                                    \
    X(kButtonClose, "button.close")                                                                                    \
    X(kStatusNoResult, "status.noResult")                                                                              \
    X(kStatusUseHub, "status.useHub")                                                                                  \
    X(kStatusForwarded, "status.forwarded")                                                                            \
    X(kStatusLate, "status.late")                                                                                      \
    X(kSlotEmpty, "slot.empty")                                                                                        \
    X(kSlotInfo, "slot.info")                                                                                          \
    X(kRollTitle, "roll.title")                                                                                        \
    X(kRollEmpty, "roll.empty")                                                                                        \
    X(kRollTriplet, "roll.triplet")                                                                                    \
    X(kRollFocus, "roll.focus")                                                                                        \
    X(kRollAccent, "roll.accent")                                                                                      \
    X(kRollSlide, "roll.slide")                                                                                        \
    X(kRollDelete, "roll.delete")                                                                                      \
    X(kRollSnapScale, "roll.snapScale")                                                                                \
    X(kRollSnapChromatic, "roll.snapChromatic")                                                                        \
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
