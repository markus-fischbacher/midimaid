#pragma once

#include "core/EditGrid.h"
#include "core/Pattern.h"
#include "core/Theory.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mm::core {

/// Geometry and gesture arithmetic of the piano roll (SPEC 3.5, D-157). Everything here is a pure function of numbers,
/// so the roll component only translates mouse events into these calls. Pixels are `double` for the geometry and `int`
/// for positions; ticks are 960 PPQ like everywhere.

/// A note as the roll draws and edits it: the source note of the pattern, not the rendered one.
struct RollNote {
    uint32_t id = 0;
    uint8_t pitch = 0;
    uint32_t startTick = 0;
    uint32_t lengthTicks = 0;
    uint8_t velocity = 100;
    bool accent = false;
    bool slide = false;

    bool operator==(const RollNote&) const = default;
};

/// The part of the pattern that is visible: a tick window and a window of pitch rows.
struct RollViewport {
    uint32_t startTick = 0;
    uint32_t spanTicks = 4 * kTicksPerBar;
    int lowPitch = 48; ///< lowest visible pitch row
    int rows = 24;     ///< number of visible pitch rows

    bool operator==(const RollViewport&) const = default;
};

/// Shortest tick window the roll zooms to (one 1/16 note per 16 ticks would be useless; a quarter note at most).
constexpr uint32_t kMinRollSpanTicks = 960;
constexpr int kMinRollRows = 8;
constexpr int kMaxRollRows = 128;
/// The window a fresh view shows horizontally at most (four bars).
constexpr uint32_t kDefaultRollSpanTicks = 4 * kTicksPerBar;

/// Brings a viewport into range: the span between `kMinRollSpanTicks` and the pattern length, the window inside the
/// pattern, 8 to 128 rows, and the rows inside 0 to 127.
RollViewport clampViewport(RollViewport viewport, uint32_t patternTicks);

/// The view of a freshly shown voice: the whole pattern up to four bars from the start, the pitch rows centred on the
/// notes (around middle C without notes).
RollViewport fitViewport(std::span<const RollNote> notes, uint32_t patternTicks, int rows);

/// Zooms the tick window by `factor` (> 1 shows more) so that `anchorTick` stays where it is on screen. The result is
/// clamped.
RollViewport zoomViewport(RollViewport viewport, double factor, uint32_t anchorTick, uint32_t patternTicks);

/// Moves the window by ticks and rows (positive: later, higher). The result is clamped.
RollViewport scrollViewport(RollViewport viewport, int64_t deltaTicks, int deltaRows, uint32_t patternTicks);

/// A viewport and the pixel area of the note field: converts between pixels and ticks or pitches.
struct RollGeometry {
    RollViewport viewport;
    double width = 1.0;  ///< pixels of the note field
    double height = 1.0; ///< pixels of the note field
    uint32_t patternTicks = kTicksPerBar;

    double ticksPerPixel() const;
    double rowHeight() const;
    double tickToX(double tick) const;
    /// The tick under pixel `x` (clamped to the pattern, rounded down to a whole tick).
    uint32_t xToTick(double x) const;
    /// Top edge of the row of `pitch`.
    double pitchToY(int pitch) const;
    /// The pitch of the row under pixel `y` (clamped to 0-127).
    int yToPitch(double y) const;
};

struct RollBox {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool contains(double px, double py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};

/// The rectangle of a note, at least 3 pixels wide and one pixel shorter than its row (the gap between rows).
RollBox noteBox(const RollGeometry& geometry, const RollNote& note);

enum class RollZone { None, Body, RightEdge };

struct RollHit {
    uint32_t id = 0; ///< 0: nothing was hit
    RollZone zone = RollZone::None;
};

/// The topmost note under the pixel (a later note covers an earlier one). The right edge of a note (a third of its
/// width, 3 to 6 pixels) resizes; a note narrower than 8 pixels is still 8 pixels wide for the hit, so short notes
/// stay reachable.
RollHit hitTest(const RollGeometry& geometry, std::span<const RollNote> notes, double x, double y);

/// The ids of the notes that touch the rectangle between two corners (any order).
std::vector<uint32_t> notesInRect(const RollGeometry& geometry, std::span<const RollNote> notes, double x0, double y0,
                                  double x1, double y1);

/// What a roll edits with: the grid and the pitch snapping with the key and scale of the pattern.
struct RollSnap {
    EditGrid grid;
    PitchSnap pitch = PitchSnap::Scale;
    PitchClass root = 9;
    const Scale* scale = nullptr; ///< null: no scale known, chromatic
};

/// The note a double click at pixel (x, y) adds: the start rounded *down* to the grid (the cell that was clicked), the
/// pitch snapped, the length one grid step, the start moved back so that the note fits into the pattern.
/// `length == 0` in the result means nothing fits (a pattern shorter than one step).
struct NewNote {
    uint32_t startTick = 0;
    uint32_t lengthTicks = 0;
    uint8_t pitch = 60;
};
NewNote newNoteAt(const RollGeometry& geometry, const RollSnap& snap, double x, double y);

/// How far a block moves when the grabbed note `anchor` is dragged by (`dx`, `dy`) pixels: the anchor snaps to the
/// grid in time and (by the pitch snapping) in pitch, the rest follows rigidly. Positive ticks: later, positive pitch:
/// higher.
struct MoveDelta {
    int32_t ticks = 0;
    int pitch = 0;
};
MoveDelta dragMoveDelta(const RollGeometry& geometry, const RollSnap& snap, const RollNote& anchor, double dx,
                        double dy);

/// The length the grabbed note gets when its right edge is dragged by `dx` pixels: the end snaps to the grid, at least
/// one grid step.
uint32_t dragLength(const RollGeometry& geometry, const RollSnap& snap, const RollNote& anchor, double dx);

/// The velocity lane under the notes (SPEC 3.5): one bar per note at the start of the note, as high as its velocity.
/// `laneHeight` is the pixel height of the lane, positions are field x and lane y (0 at the top of the lane).

/// The bar of a note: 3 to 10 pixels wide (the note width less a pixel in between), from the height of the velocity
/// down to the bottom of the lane.
RollBox velocityBarBox(const RollGeometry& geometry, const RollNote& note, double laneHeight);

/// The velocity that a mouse height stands for: 127 at the top of the lane or above, 1 at the bottom or below, linear
/// in between and rounded.
uint8_t velocityAtY(double laneHeight, double y);

/// The topmost bar under field position `x` (a later note covers an earlier one); a bar narrower than 8 pixels is still
/// 8 pixels wide for the hit. 0 when there is none.
uint32_t hitVelocityBar(const RollGeometry& geometry, std::span<const RollNote> notes, double x);

/// The notes whose bar lies between the field positions `x0` and `x1` (any order, both ends included; by the middle of
/// the bar): what a freehand stroke across the lane touches.
std::vector<uint32_t> velocityBarsBetween(const RollGeometry& geometry, std::span<const RollNote> notes, double x0,
                                          double x1);

/// A velocity moved by `delta`, cut to 1-127.
uint8_t shiftVelocity(uint8_t velocity, int delta);

/// The next pitch above (`direction` > 0) or below the pitch that the snapping allows; an arrow key steps by this.
/// Chromatic steps by one semitone, scale by one scale tone. Stays inside 0-127 (the pitch itself at the ends).
int stepPitch(int pitch, int direction, const RollSnap& snap);

} // namespace mm::core
