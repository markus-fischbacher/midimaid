#pragma once

#include "core/Theory.h"

#include <optional>
#include <string>
#include <string_view>

namespace mm::core {

/// Parses a chord symbol of STYLES.md 1.17 in its JSON/ASCII spelling: roman numerals relative to the major scale on
/// the key's root (I = 0, II = 2, III = 4, IV = 5, V = 7, VI = 9, VII = 11 semitones), `b` or `#` (at most one in
/// front) shifts by a semitone. Upper case is major, lower case minor, a trailing `o` marks a diminished chord (lower
/// case numeral only), a trailing `sus2` or `sus4` a suspended chord (either case). Examples in A minor: `i` = Am, `iv`
/// = Dm, `V` = E, `bII` = Bb, `bIII` = C, `bVI` = F, `bVII` = G, `viio` = G#dim. Anything else, including surrounding
/// whitespace, gives nullopt.
std::optional<Chord> parseChordSymbol(std::string_view text);

/// The canonical spelling of a chord: flats in front of II, III, V, VI and VII (`bII`, `bIII`, `bV`, `bVI`, `bVII`),
/// minor in lower case, diminished as lower case plus `o`, suspended as upper case plus `sus2`/`sus4`. The root offset
/// is taken modulo 12. For every valid symbol `parseChordSymbol(formatChordSymbol(chord)) == chord`.
std::string formatChordSymbol(const Chord& chord);

} // namespace mm::core
