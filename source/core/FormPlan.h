#pragma once

#include "core/Pattern.h"
#include "core/Random.h"
#include "core/StyleProfile.h"

#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// The form plan of a pattern (SPEC 3.7, D-121): the phrases with their roles. Patterns of 1, 2 and 4 bars have one
/// main phrase of the pattern length and need no draw. Patterns of 8 and 16 bars draw a layout (8: `8`, `4+4`; 16:
/// `16`, `8+8`, `8+4+4`, `4+4+8`, `4+8+4`, `4+4+4+4`) by the weights of the style and take the roles from the table of
/// the style: the first phrase is always the main phrase, later ones are variation or answer, build or (Melodic Techno
/// only) breakdown. A breakdown gets the `halftime` kick grid. The tables are starting values for the listening test.
std::vector<Phrase> generateFormPlan(const StyleProfile& style, uint32_t lengthBars, Pcg32& rng);

/// True if `phrases` is a valid form plan for a pattern of `lengthBars` (the rules of `validatePattern`).
bool isValidFormPlan(uint32_t lengthBars, const std::vector<Phrase>& phrases);

/// What a role does to the generation of its phrase: energy and creativity are moved by these values (and limited to
/// 0-100 by the caller).
int phraseEnergyDelta(PhraseRole role);
int phraseCreativityDelta(PhraseRole role);

/// A pattern of the length of `phrase` with the harmony (the slice of the progression), kick grid, settings and voices
/// of `pattern`, without notes.
Pattern phraseSkeleton(const Pattern& pattern, const Phrase& phrase);

/// The skeleton with the notes of the phrase, moved so that the phrase starts at 0, and the id counter of `pattern`:
/// notes an operator adds get ids that no note of `pattern` has. For operators that work on a whole pattern.
Pattern extractPhrase(const Pattern& pattern, const Phrase& phrase);

/// Puts the notes of `edited` (an extracted phrase after editing) back: the notes of the phrase window of `pattern`
/// are replaced, the id counter follows. Notes outside the window stay.
void replacePhraseNotes(Pattern& pattern, const Phrase& phrase, const Pattern& edited);

/// The kick grid a role forces (breakdown: `halftime`), else nullopt.
std::optional<std::string> phraseKickGrid(PhraseRole role);

} // namespace mm::core
