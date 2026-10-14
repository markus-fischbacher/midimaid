#pragma once

#include "core/Pattern.h"

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
    X(kVoiceStabs, "voice.stabs")                                                                                      \
    X(kVoiceArp, "voice.arp")                                                                                          \
    X(kVoiceCounter, "voice.counter")                                                                                  \
    X(kButtonAddVoice, "button.addVoice")                                                                              \
    X(kButtonRemoveVoice, "button.removeVoice")                                                                        \
    X(kVoiceBarEmpty, "voicebar.empty")                                                                                \
    X(kStatusVoiceAdded, "status.voiceAdded")                                                                          \
    X(kStatusVoiceRemoved, "status.voiceRemoved")                                                                      \
    X(kStatusVoiceLimit, "status.voiceLimit")                                                                          \
    X(kStatusVoiceNoRemove, "status.voiceNoRemove")                                                                    \
    X(kStatusVoiceRemoveLocked, "status.voiceRemoveLocked")                                                            \
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
    X(kLabelChromatic, "label.chromatic")                                                                              \
    X(kLabelPersonal, "label.personal")                                                                                \
    X(kStartRestart, "start.restart")                                                                                  \
    X(kSectionGenerator, "section.generator")                                                                          \
    X(kSectionWishes, "section.wishes")                                                                                \
    X(kSectionSwitch, "section.switch")                                                                                \
    X(kSectionEvolve, "section.evolve")                                                                                \
    X(kInspectorInstance, "inspector.instance")                                                                       \
    X(kInspectorEdit, "inspector.edit")                                                                               \
    X(kButtonInspector, "button.inspector")                                                                           \
    X(kModeNew, "dock.new")                                                                                           \
    X(kModeRefine, "dock.refine")                                                                                     \
    X(kStartLegato, "start.legato")                                                                                    \
    X(kQuantBar, "quant.bar")                                                                                          \
    X(kQuantBeat, "quant.beat")                                                                                        \
    X(kQuantBars2, "quant.bars2")                                                                                      \
    X(kQuantBars4, "quant.bars4")                                                                                      \
    X(kQuantBars8, "quant.bars8")                                                                                      \
    X(kQuantBars16, "quant.bars16")                                                                                    \
    X(kLabelHatMode, "label.hatMode")                                                                                  \
    X(kHatModeFree, "hatMode.free")                                                                                    \
    X(kHatModeWith, "hatMode.with")                                                                                    \
    X(kHatModeAgainst, "hatMode.against")                                                                              \
    X(kChromaticStyle, "label.chromaticStyle")                                                                         \
    X(kButtonExpert, "button.expert")                                                                                  \
    X(kButtonRenew, "button.renew")                                                                                    \
    X(kButtonVary, "button.vary")                                                                                      \
    X(kButtonVaryAll, "button.varyAll")                                                                                \
    X(kButtonDuplicate, "button.duplicate")                                                                            \
    X(kButtonPreview, "button.preview")                                                                                \
    X(kButtonPreviewStop, "button.previewStop")                                                                        \
    X(kButtonUndo, "button.undo")                                                                                      \
    X(kButtonRedo, "button.redo")                                                                                      \
    X(kButtonDrumsRemove, "button.drumsRemove")                                                                        \
    X(kStatusDrumApplied, "status.drumApplied")                                                                        \
    X(kStatusDrumStored, "status.drumStored")                                                                          \
    X(kStatusDrumNoKickHat, "status.drumNoKickHat")                                                                    \
    X(kStatusDrumRemoved, "status.drumRemoved")                                                                        \
    X(kButtonHistoryBack, "button.historyBack")                                                                        \
    X(kButtonHistoryForward, "button.historyForward")                                                                  \
    X(kLabelStrength, "label.strength")                                                                                \
    X(kLabelHistory, "label.history")                                                                                  \
    X(kLabelHistoryNone, "label.historyNone")                                                                          \
    X(kOctaveItem, "octave.item")                                                                                      \
    X(kZoomItem, "zoom.item")                                                                                          \
    X(kButtonHelp, "button.help")                                                                                      \
    X(kButtonFollow, "button.follow")                                                                                  \
    X(kGroovePickStraight, "groove.straight")                                                                              \
    X(kGroovePickDrumReference, "groove.drumReference")                                                                    \
    X(kGroovePickSave, "groove.saveAs")                                                                                  \
    X(kGrooveSaveTitle, "groove.saveTitle")                                                                              \
    X(kGrooveSaveHint, "groove.saveHint")                                                                                \
    X(kGrooveSaveButton, "groove.saveButton")                                                                            \
    X(kSwingSuffix, "groove.swingSuffix")                                                                              \
    X(kSwingFlamHint, "groove.flamHint")                                                                               \
    X(kSlotTransposed, "slot.transposed")                                                                              \
    X(kButtonEvolve, "button.evolve")                                                                                  \
    X(kButtonEvolveKeep, "button.evolveKeep")                                                                          \
    X(kButtonEvolveFromOriginal, "button.evolveFromOriginal")                                                          \
    X(kLabelEvolveStage, "label.evolveStage")                                                                          \
    X(kLabelEvolveStageOff, "label.evolveStageOff")                                                                    \
    X(kLabelEvolveEvery, "label.evolveEvery")                                                                          \
    X(kLabelEvolveStrength, "label.evolveStrength")                                                                    \
    X(kStatusEvolveKept, "status.evolveKept")                                                                          \
    X(kStatusEvolveNothingToKeep, "status.evolveNothingToKeep")                                                        \
    X(kDragLabel, "drag.label")                                                                                        \
    X(kHelpTitle, "help.title")                                                                                        \
    X(kHelpTabGeneral, "help.tab.general")                                                                             \
    X(kHelpTabLive, "help.tab.live")                                                                                   \
    X(kHelpTabLogic, "help.tab.logic")                                                                                 \
    X(kHelpTabOther, "help.tab.other")                                                                                 \
    X(kHelpTextGeneral, "help.text.general")                                                                           \
    X(kHelpTextLive, "help.text.live")                                                                                 \
    X(kHelpTextLogic, "help.text.logic")                                                                               \
    X(kHelpTextOther, "help.text.other")                                                                               \
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
    X(kStatusImportRead, "status.importRead")                                                                          \
    X(kStatusImportCut, "status.importCut")                                                                            \
    X(kStatusImportNotFourFour, "status.importNotFourFour")                                                            \
    X(kStatusImportNoNotes, "status.importNoNotes")                                                                    \
    X(kStatusImportUnreadable, "status.importUnreadable")                                                              \
    X(kStatusImportApplied, "status.importApplied")                                                                    \
    X(kStatusDoneImportCut, "status.doneImportCut")                                                                    \
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
    X(kLabelDrumMap, "settings.drumMap")                                                                               \
    X(kLabelNoteNaming, "settings.noteNaming")                                                                         \
    X(kNoteNamingC3, "settings.noteNaming.c3")                                                                         \
    X(kNoteNamingC4, "settings.noteNaming.c4")                                                                         \
    X(kLabelTranspose, "settings.transpose")                                                                           \
    X(kTransposeHint, "settings.transposeHint")                                                                        \
    X(kTransposeModeChromatic, "settings.transpose.mode.chromatic")                                                    \
    X(kTransposeModeDiatonic, "settings.transpose.mode.diatonic")                                                      \
    X(kTransposeBehaviorHold, "settings.transpose.behavior.hold")                                                      \
    X(kTransposeBehaviorMomentary, "settings.transpose.behavior.momentary")                                            \
    X(kTransposeTimingNote, "settings.transpose.timing.note")                                                          \
    X(kTransposeTimingBeat, "settings.transpose.timing.beat")                                                          \
    X(kTransposeTimingBar, "settings.transpose.timing.bar")                                                            \
    X(kTransposeTargetBoth, "settings.transpose.target.both")                                                          \
    X(kTransposeTargetBass, "settings.transpose.target.bass")                                                          \
    X(kTransposeTargetMelody, "settings.transpose.target.melody")                                                      \
    X(kTransposeReferenceAuto, "settings.transpose.reference.auto")                                                    \
    X(kTransposeReferenceNote, "settings.transpose.reference.note")                                                    \
    X(kTransposeChannelAll, "settings.transpose.channel.all")                                                          \
    X(kTransposeChannelN, "settings.transpose.channel.n")                                                              \
    X(kToggleTransposeExport, "settings.transpose.export")                                                             \
    X(kToggleExportAllNotes, "settings.exportAllNotes")                                                                \
    X(kToggleExportGroove, "settings.exportGroove")                                                                    \
    X(kToggleFavoritesToActiveSet, "settings.favoritesToActiveSet")                                                    \
    X(kLabelRollView, "settings.rollView")                                                                             \
    X(kToggleRollFold, "settings.rollFold")                                                                            \
    X(kToggleRollGuides, "settings.rollGuides")                                                                        \
    X(kToggleRollMarkers, "settings.rollMarkers")                                                                      \
    X(kToggleMidiThru, "settings.transpose.thru")                                                                      \
    X(kLabelLanguage, "settings.language")                                                                             \
    X(kLanguageAuto, "settings.language.auto")                                                                         \
    X(kLanguageDe, "settings.language.de")                                                                             \
    X(kLanguageEn, "settings.language.en")                                                                             \
    X(kLanguageHint, "settings.languageHint")                                                                          \
    X(kLabelLog, "settings.log")                                                                                       \
    X(kDrumMapHint, "settings.drumMapHint")                                                                            \
    X(kButtonReferences, "button.references")                                                                      \
    X(kReferencesTitle, "references.title")                                                                        \
    X(kReferencesSummary, "references.summary")                                                                    \
    X(kReferencesEmpty, "references.empty")                                                                        \
    X(kReferencesInfo, "references.info")                                                                          \
    X(kReferencesInfoDrums, "references.infoDrums")                                                                \
    X(kReferencesAddFiles, "references.addFiles")                                                                  \
    X(kReferencesAddFolder, "references.addFolder")                                                                \
    X(kReferencesCancel, "references.cancel")                                                                      \
    X(kReferencesRemove, "references.remove")                                                                      \
    X(kReferencesActive, "references.active")                                                                      \
    X(kReferencesProgress, "references.progress")                                                                  \
    X(kReferencesReport, "references.report")                                                                      \
    X(kReferencesReportMore, "references.reportMore")                                                              \
    X(kReferencesDuplicates, "references.duplicates")                                                              \
    X(kReferencesNotFourFour, "references.notFourFour")                                                            \
    X(kReferencesUnreadable, "references.unreadable")                                                              \
    X(kReferencesTooSmall, "references.tooSmall")                                                                  \
    X(kReferencesOverLimit, "references.overLimit")                                                                \
    X(kReferencesTruncated, "references.truncated")                                                                \
    X(kReferencesRoleDrums, "references.role.drums")                                                               \
    X(kReferencesRoleChords, "references.role.chords")                                                             \
    X(kReferencesRoleBass, "references.role.bass")                                                                 \
    X(kReferencesRoleMelody, "references.role.melody")                                                             \
    X(kReferencesWeight, "references.weight")                                                                      \
    X(kReferencesSetFavorites, "references.set.favorites")                                                         \
    X(kReferencesRoleUnsure, "references.roleUnsure")                                                              \
    X(kReferencesSummaryUnsure, "references.summaryUnsure")                                                        \
    X(kReferencesProfile, "references.profile")                                                                    \
    X(kReferencesProfileTooSmall, "references.profileTooSmall")                                                    \
    X(kReferencesProfileDensity, "references.profileDensity")                                                      \
    X(kReferencesSetNew, "references.set.new")                                                                     \
    X(kReferencesSetRename, "references.set.rename")                                                               \
    X(kReferencesSetDelete, "references.set.delete")                                                               \
    X(kReferencesSetNameHint, "references.set.nameHint")                                                           \
    X(kReferencesFromPool, "references.fromPool")                                                                  \
    X(kReferencesFromPoolNone, "references.fromPoolNone")                                                          \
    X(kReferencesTagsHint, "references.tagsHint")                                                                  \
    X(kButtonLibrary, "button.library")                                                                            \
    X(kLibraryTitle, "library.title")                                                                              \
    X(kLibrarySaveSlot, "library.saveSlot")                                                                        \
    X(kLibrarySaveSet, "library.saveSet")                                                                          \
    X(kLibraryLoad, "library.load")                                                                                \
    X(kLibrarySearch, "library.search")                                                                                \
    X(kLibraryAllStars, "library.allStars")                                                                            \
    X(kLibraryAllTags, "library.allTags")                                                                              \
    X(kLibraryAllStyles, "library.allStyles")                                                                          \
    X(kLibraryTagsHint, "library.tagsHint")                                                                            \
    X(kLibraryBpmFilter, "library.bpmFilter")                                                                          \
    X(kLibraryBpmLow, "library.bpmLow")                                                                                \
    X(kLibraryBpmHigh, "library.bpmHigh")                                                                              \
    X(kLibraryApply, "library.apply")                                                                                  \
    X(kLibraryNoRating, "library.noRating")                                                                            \
    X(kLibraryRename, "library.rename")                                                                            \
    X(kLibraryRemove, "library.remove")                                                                            \
    X(kLibraryRemoveConfirm, "library.removeConfirm")                                                              \
    X(kLibraryExport, "library.export")                                                                            \
    X(kLibraryImport, "library.import")                                                                            \
    X(kLibraryNameHint, "library.nameHint")                                                                        \
    X(kLibraryEmpty, "library.empty")                                                                              \
    X(kLibraryInfoPattern, "library.infoPattern")                                                                  \
    X(kLibraryInfoSet, "library.infoSet")                                                                          \
    X(kLibrarySummary, "library.summary")                                                                          \
    X(kStatusLibrarySaved, "status.librarySaved")                                                                  \
    X(kStatusLibrarySetSaved, "status.librarySetSaved")                                                            \
    X(kStatusLibraryNothing, "status.libraryNothing")                                                              \
    X(kStatusLibraryFull, "status.libraryFull")                                                                    \
    X(kStatusLibraryLoaded, "status.libraryLoaded")                                                                \
    X(kStatusLibrarySetLoaded, "status.librarySetLoaded")                                                          \
    X(kStatusLibraryLoadFailed, "status.libraryLoadFailed")                                                        \
    X(kStatusLibraryImported, "status.libraryImported")                                                            \
    X(kStatusLibraryExported, "status.libraryExported")                                                            \
    X(kStatusLibraryExportFailed, "status.libraryExportFailed")                                                    \
    X(kStatusSlotCopied, "status.slotCopied")                                                                          \
    X(kStatusSlotPasted, "status.slotPasted")                                                                          \
    X(kStatusSlotNothingToCopy, "status.slotNothingToCopy")                                                            \
    X(kStatusSlotCleared, "status.slotCleared")                                                                        \
    X(kStatusSlotsSwapped, "status.slotsSwapped")                                                                      \
    X(kSlotMenuCopy, "slot.menu.copy")                                                                                 \
    X(kSlotMenuPaste, "slot.menu.paste")                                                                               \
    X(kSlotMenuClear, "slot.menu.clear")                                                                               \
    X(kSlotMenuSwap, "slot.menu.swap")                                                                                 \
    X(kSlotMenuSwapItem, "slot.menu.swapItem")                                                                         \
    X(kButtonClose, "button.close")                                                                                    \
    X(kStatusNoResult, "status.noResult")                                                                              \
    X(kStatusUseHub, "status.useHub")                                                                                  \
    X(kStatusHubClosed, "status.hubClosed")                                                                            \
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
    X(kRollRatchet, "roll.ratchet")                                                                                    \
    X(kLockMenu, "lock.menu")                                                                                          \
    X(kLockPitch, "lock.pitch")                                                                                        \
    X(kLockRhythm, "lock.rhythm")                                                                                      \
    X(kLockVelocity, "lock.velocity")                                                                                  \
    X(kLockAll, "lock.all")                                                                                            \
    X(kVoicePartialLockedSuffix, "voice.partialLockedSuffix")                                                          \
    X(kShortcutUndo, "shortcut.undo")                                                                              \
    X(kShortcutRedo, "shortcut.redo")                                                                              \
    X(kShortcutDuplicate, "shortcut.duplicate")                                                                    \
    X(kShortcutSelectAll, "shortcut.selectAll")                                                                    \
    X(kShortcutDelete, "shortcut.delete")                                                                          \
    X(kShortcutPitchUp, "shortcut.pitchUp")                                                                        \
    X(kShortcutPitchDown, "shortcut.pitchDown")                                                                    \
    X(kShortcutOctaveUp, "shortcut.octaveUp")                                                                      \
    X(kShortcutOctaveDown, "shortcut.octaveDown")                                                                  \
    X(kShortcutMoveLeft, "shortcut.moveLeft")                                                                      \
    X(kShortcutMoveRight, "shortcut.moveRight")                                                                    \
    X(kShortcutLonger, "shortcut.longer")                                                                          \
    X(kShortcutShorter, "shortcut.shorter")                                                                        \
    X(kShortcutGridFiner, "shortcut.gridFiner")                                                                    \
    X(kShortcutGridCoarser, "shortcut.gridCoarser")                                                                \
    X(kShortcutTriplets, "shortcut.triplets")                                                                      \
    X(kShortcutSnapToggle, "shortcut.snapToggle")                                                                  \
    X(kShortcutFoldToggle, "shortcut.foldToggle")                                                                  \
    X(kShortcutFocus1, "shortcut.focus1")                                                                          \
    X(kShortcutFocus2, "shortcut.focus2")                                                                          \
    X(kShortcutFocus3, "shortcut.focus3")                                                                          \
    X(kShortcutFocus4, "shortcut.focus4")                                                                          \
    X(kShortcutFocus5, "shortcut.focus5")                                                                          \
    X(kShortcutFocus6, "shortcut.focus6")                                                                          \
    X(kShortcutFocus7, "shortcut.focus7")                                                                          \
    X(kShortcutFocus8, "shortcut.focus8")                                                                          \
    X(kShortcutFocusAll, "shortcut.focusAll")                                                                      \
    X(kShortcutGenerate, "shortcut.generate")                                                                      \
    X(kShortcutRefineSend, "shortcut.refineSend")                                                                  \
    X(kShortcutVary, "shortcut.vary")                                                                              \
    X(kShortcutHistoryBack, "shortcut.historyBack")                                                                \
    X(kShortcutHistoryForward, "shortcut.historyForward")                                                          \
    X(kShortcutAccent, "shortcut.accent")                                                                          \
    X(kShortcutSlide, "shortcut.slide")                                                                            \
    X(kShortcutRatchetOff, "shortcut.ratchetOff")                                                                  \
    X(kShortcutPalette, "shortcut.palette")                                                                        \
    X(kButtonCommands, "button.commands")                                                                          \
    X(kPaletteSearch, "palette.search")                                                                            \
    X(kPaletteEmpty, "palette.empty")                                                                              \
    X(kLabelShortcuts, "settings.shortcuts")                                                                       \
    X(kShortcutAction, "settings.shortcutAction")                                                                  \
    X(kShortcutAssign, "settings.shortcutAssign")                                                                  \
    X(kShortcutPress, "settings.shortcutPress")                                                                    \
    X(kShortcutRemove, "settings.shortcutRemove")                                                                  \
    X(kShortcutReset, "settings.shortcutReset")                                                                    \
    X(kShortcutNone, "settings.shortcutNone")                                                                      \
    X(kShortcutHint, "settings.shortcutHint")                                                                      \
    X(kShortcutSpace, "settings.shortcutSpace")                                                                    \
    X(kRollChance, "roll.chance")                                                                                      \
    X(kRollCondition, "roll.condition")                                                                                \
    X(kRollConditionAlways, "roll.conditionAlways")                                                                    \
    X(kRollRatchetOff, "roll.ratchetOff")                                                                              \
    X(kRollRatchet2, "roll.ratchet2")                                                                                  \
    X(kRollRatchet3, "roll.ratchet3")                                                                                  \
    X(kRollRatchet4, "roll.ratchet4")                                                                                  \
    X(kRollDelete, "roll.delete")                                                                                      \
    X(kRollSnapScale, "roll.snapScale")                                                                                \
    X(kRollSnapChromatic, "roll.snapChromatic")                                                                        \
    X(kGroupHubOne, "group.hub.one")                                                                                   \
    X(kGroupHubMany, "group.hub.many")                                                                                 \
    X(kGroupConnected, "group.connected")                                                                              \
    X(kGroupMissing, "group.missing")                                                                                  \
    X(kGroupOffered, "group.offered")                                                                                  \
    X(kGroupRefused, "group.refused")                                                                                  \
    X(kUsageTokens, "status.usageTokens")                                                                              \
    X(kUsageCost, "status.usageCost")                                                                                  \
    X(kGroupMixedFormats, "group.mixedFormats")                                                                        \
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

/// The key of the name of a voice role ("voice.stabs"): the row names and the menus of the hub.
inline std::string_view roleTextKey(VoiceRole role) {
    switch (role) {
    case VoiceRole::Bass:
        return kVoiceBass;
    case VoiceRole::Melody:
        return kVoiceMelody;
    case VoiceRole::Stabs:
        return kVoiceStabs;
    case VoiceRole::Arp:
        return kVoiceArp;
    case VoiceRole::Counter:
        return kVoiceCounter;
    }
    return kVoiceMelody;
}

/// The key of the name of a scale ("scale.natural_minor"); every id of `allScales()` has one.
inline std::string scaleKey(std::string_view scaleId) {
    return "scale." + std::string(scaleId);
}

} // namespace mm::core::text
