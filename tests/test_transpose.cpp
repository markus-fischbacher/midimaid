#include "core/Theory.h"
#include "core/Transpose.h"

#include <catch2/catch_test_macros.hpp>

using namespace mm::core;

namespace {

/// A minor natural (root 9, A) in the bass register: the voice goes from A1 (33) to G2 (43) in a range of 28 to 52.
TransposeInfo aMinor() {
    TransposeInfo info;
    info.root = 9;
    info.scaleMask = 0;
    const Scale* scale = findScale("natural_minor");
    for (const uint8_t interval : scale->intervals) {
        info.scaleMask = static_cast<uint16_t>(info.scaleMask | (1u << interval));
    }
    info.rangeLow = 28;
    info.rangeHigh = 52;
    info.lowest = 33;
    info.highest = 43;
    return info;
}

} // namespace

TEST_CASE("the transposition is limited to an octave either way", "[transpose]") {
    CHECK(clampTranspose(0) == 0);
    CHECK(clampTranspose(12) == 12);
    CHECK(clampTranspose(13) == 12);
    CHECK(clampTranspose(-12) == -12);
    CHECK(clampTranspose(-40) == -12);
}

TEST_CASE("the reference note is the key in the bass register unless it is set", "[transpose]") {
    TransposeSettings settings;
    CHECK(referenceNoteFor(settings, 0) == 36);  // C2
    CHECK(referenceNoteFor(settings, 9) == 45);  // A2
    CHECK(referenceNoteFor(settings, 11) == 47); // B2
    CHECK(referenceNoteFor(settings, 21) == 45); // a pitch class beyond 11 wraps
    settings.referenceNote = 60;
    CHECK(referenceNoteFor(settings, 9) == 60);
    settings.referenceNote = 200;
    CHECK(referenceNoteFor(settings, 9) == 127);
}

TEST_CASE("the defaults are those of the specification", "[transpose]") {
    const TransposeSettings settings;
    CHECK(settings.mode == TransposeMode::Chromatic);
    CHECK(settings.behavior == TransposeBehavior::Hold);
    CHECK(settings.timing == TransposeTiming::NextNote);
    CHECK(settings.target == TransposeTarget::Both);
    CHECK(settings.channel == 0);
    CHECK(settings.referenceNote < 0);
}

TEST_CASE("a key gives its distance to the reference in semitones", "[transpose]") {
    const TransposeInfo info = aMinor();
    CHECK(offsetForKey(45, 45, TransposeMode::Chromatic, info) == 0);
    CHECK(offsetForKey(46, 45, TransposeMode::Chromatic, info) == 1);
    CHECK(offsetForKey(43, 45, TransposeMode::Chromatic, info) == -2);
    CHECK(offsetForKey(60, 45, TransposeMode::Chromatic, info) == 12); // limited
    CHECK(offsetForKey(20, 45, TransposeMode::Chromatic, info) == -12);
}

TEST_CASE("in diatonic mode a key gives the number of scale degrees from the reference", "[transpose]") {
    const TransposeInfo info = aMinor(); // A B C D E F G
    CHECK(offsetForKey(45, 45, TransposeMode::Diatonic, info) == 0); // A -> A
    CHECK(offsetForKey(47, 45, TransposeMode::Diatonic, info) == 1); // B: one degree
    CHECK(offsetForKey(48, 45, TransposeMode::Diatonic, info) == 2); // C: two
    CHECK(offsetForKey(52, 45, TransposeMode::Diatonic, info) == 4); // E
    CHECK(offsetForKey(57, 45, TransposeMode::Diatonic, info) == 7); // A, an octave up: seven degrees
    CHECK(offsetForKey(43, 45, TransposeMode::Diatonic, info) == -1); // G below A
    CHECK(offsetForKey(46, 45, TransposeMode::Diatonic, info) == 0);  // A#: off the scale counts as the degree below
    CHECK(offsetForKey(33, 45, TransposeMode::Diatonic, info) == -7);
    CHECK(offsetForKey(100, 45, TransposeMode::Diatonic, info) == 12); // limited
    // without a scale, diatonic is chromatic
    TransposeInfo none = info;
    none.scaleMask = 0;
    CHECK(offsetForKey(48, 45, TransposeMode::Diatonic, none) == 3);
}

TEST_CASE("the target picks the voices", "[transpose]") {
    TransposeInfo bass = aMinor();
    TransposeInfo melody = aMinor();
    melody.melody = true;
    CHECK(targetsVoice(TransposeTarget::Both, bass));
    CHECK(targetsVoice(TransposeTarget::Both, melody));
    CHECK(targetsVoice(TransposeTarget::Bass, bass));
    CHECK_FALSE(targetsVoice(TransposeTarget::Bass, melody));
    CHECK_FALSE(targetsVoice(TransposeTarget::Melody, bass));
    CHECK(targetsVoice(TransposeTarget::Melody, melody));
}

TEST_CASE("a chromatic transposition moves every pitch by the offset", "[transpose]") {
    const TransposeInfo info = aMinor();
    const int shift = voiceOctaveShift(3, TransposeMode::Chromatic, info);
    CHECK(shift == 0);
    CHECK(transposedPitch(33, 3, TransposeMode::Chromatic, info, shift) == 36);
    CHECK(transposedPitch(43, 3, TransposeMode::Chromatic, info, shift) == 46);
    CHECK(transposedPitch(40, -5, TransposeMode::Chromatic, info, 0) == 35);
    CHECK(transposedPitch(40, 0, TransposeMode::Chromatic, info, 0) == 40);
}

TEST_CASE("a diatonic transposition keeps the key", "[transpose]") {
    TransposeInfo info = aMinor();
    info.rangeLow = 0; // the range is not what is tested here
    info.rangeHigh = 127;
    // A, C, E (45, 48, 52) two degrees up: C, E, G
    CHECK(transposedPitch(45, 2, TransposeMode::Diatonic, info, 0) == 48);
    CHECK(transposedPitch(48, 2, TransposeMode::Diatonic, info, 0) == 52);
    CHECK(transposedPitch(52, 2, TransposeMode::Diatonic, info, 0) == 55);
    // G (43) one degree up is A (45), across the root
    CHECK(transposedPitch(43, 1, TransposeMode::Diatonic, info, 0) == 45);
    // a whole octave of degrees is an octave
    CHECK(transposedPitch(40, 7, TransposeMode::Diatonic, info, 0) == 52);
    CHECK(transposedPitch(40, -7, TransposeMode::Diatonic, info, 0) == 28);
    // every result of every offset stays in the scale for notes that are in it
    for (int pitch = 33; pitch <= 43; ++pitch) {
        const int rel = ((pitch - info.root) % 12 + 12) % 12;
        if (!((info.scaleMask >> rel) & 1)) {
            continue;
        }
        for (int offset = -12; offset <= 12; ++offset) {
            const int out = transposedPitch(pitch, offset, TransposeMode::Diatonic, info, 0);
            const int outRel = ((out - info.root) % 12 + 12) % 12;
            CHECK(((info.scaleMask >> outRel) & 1) == 1);
        }
    }
    // a chromatic note keeps its distance above its degree
    CHECK(transposedPitch(46, 2, TransposeMode::Diatonic, info, 0) == 49); // A# is a semitone above A: two degrees up it stays a semitone above C
}

TEST_CASE("a transposition out of the range moves the whole voice an octave, to keep its contour", "[transpose]") {
    TransposeInfo info = aMinor();
    info.rangeLow = 20;
    info.rangeHigh = 45; // the voice from 33 to 43: +5 would put the top at 48
    const int up = voiceOctaveShift(5, TransposeMode::Chromatic, info);
    CHECK(up == -12);
    // all notes move by 5 - 12 = -7: the contour is the same, only an octave lower than a plain +5
    CHECK(transposedPitch(33, 5, TransposeMode::Chromatic, info, up) == 26);
    CHECK(transposedPitch(43, 5, TransposeMode::Chromatic, info, up) == 36);
    CHECK(transposedPitch(40, 5, TransposeMode::Chromatic, info, up) - transposedPitch(33, 5, TransposeMode::Chromatic, info, up) == 7);

    TransposeInfo low = aMinor();
    low.rangeLow = 30; // -5 would put the bottom at 28
    const int down = voiceOctaveShift(-5, TransposeMode::Chromatic, low);
    CHECK(down == 12);
    CHECK(transposedPitch(33, -5, TransposeMode::Chromatic, low, down) == 40);

    // inside the range nothing moves an octave
    CHECK(voiceOctaveShift(2, TransposeMode::Chromatic, aMinor()) == 0);
    CHECK(voiceOctaveShift(0, TransposeMode::Chromatic, info) == 0);
}

TEST_CASE("a note that is still outside the range after the voice moved goes in by octaves", "[transpose]") {
    TransposeInfo info = aMinor();
    info.rangeLow = 30;
    info.rangeHigh = 44;
    info.lowest = 31;
    info.highest = 44;
    // +12 puts every note an octave too high: the voice moves down an octave, the notes land where they were
    const int shift = voiceOctaveShift(12, TransposeMode::Chromatic, info);
    CHECK(shift == -12);
    CHECK(transposedPitch(40, 12, TransposeMode::Chromatic, info, shift) == 40);
    // a single note that is outside the range while the voice is inside comes in alone
    CHECK(transposedPitch(50, 3, TransposeMode::Chromatic, info, 0) == 41); // 53 -> 41
    CHECK(transposedPitch(20, -1, TransposeMode::Chromatic, info, 0) == 31); // 19 -> 31
}

TEST_CASE("the result is always a MIDI note", "[transpose]") {
    TransposeInfo wide;
    wide.rangeLow = 0;
    wide.rangeHigh = 127;
    for (const auto mode : {TransposeMode::Chromatic, TransposeMode::Diatonic}) {
        for (int pitch = 0; pitch <= 127; pitch += 7) {
            for (int offset = -12; offset <= 12; ++offset) {
                const int shift = voiceOctaveShift(offset, mode, wide);
                const int out = transposedPitch(pitch, offset, mode, wide, shift);
                CHECK(out >= 0);
                CHECK(out <= 127);
            }
        }
    }
    CHECK(transposedPitch(127, 12, TransposeMode::Chromatic, wide, 0) == 127);
    CHECK(transposedPitch(0, -12, TransposeMode::Chromatic, wide, 0) == 0);
}
