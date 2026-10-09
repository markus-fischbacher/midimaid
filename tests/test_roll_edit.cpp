#include "core/RollEdit.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace mm::core;

namespace {

RollGeometry geometryOf(uint32_t patternTicks, uint32_t start, uint32_t span, int low, int rows, double w = 960.0,
                        double h = 240.0) {
    RollGeometry geometry;
    geometry.viewport = {start, span, low, rows};
    geometry.width = w;
    geometry.height = h;
    geometry.patternTicks = patternTicks;
    return geometry;
}

RollNote note(uint32_t id, int pitch, uint32_t start, uint32_t length) {
    RollNote n;
    n.id = id;
    n.pitch = static_cast<uint8_t>(pitch);
    n.startTick = start;
    n.lengthTicks = length;
    return n;
}

RollSnap snapOf(PitchSnap mode, uint32_t division = 16, bool triplet = false) {
    RollSnap snap;
    snap.grid = {division, triplet};
    snap.pitch = mode;
    snap.root = 9; // A
    snap.scale = findScale("natural_minor");
    return snap;
}

} // namespace

TEST_CASE("a viewport is clamped into the pattern and the pitch range", "[roll-edit]") {
    const uint32_t total = 8 * kTicksPerBar;
    auto v = clampViewport({100000, 100000, 200, 500}, total);
    CHECK(v.spanTicks == total);
    CHECK(v.startTick == 0);
    CHECK(v.rows == kMaxRollRows);
    CHECK(v.lowPitch == 0);

    v = clampViewport({total, 2 * kTicksPerBar, -5, 24}, total);
    CHECK(v.startTick == total - 2 * kTicksPerBar);
    CHECK(v.lowPitch == 0);

    v = clampViewport({0, 10, 120, 24}, total);
    CHECK(v.spanTicks == kMinRollSpanTicks);
    CHECK(v.lowPitch == 128 - 24);

    v = clampViewport({0, 100, 60, 2}, 500); // a pattern shorter than the minimum span
    CHECK(v.spanTicks == 500);
    CHECK(v.rows == kMinRollRows);
}

TEST_CASE("a fresh view shows up to four bars and centres the pitches on the notes", "[roll-edit]") {
    std::vector<RollNote> notes{note(1, 40, 0, 240), note(2, 52, 960, 240)};
    auto v = fitViewport(notes, 16 * kTicksPerBar, 24);
    CHECK(v.startTick == 0);
    CHECK(v.spanTicks == 4 * kTicksPerBar);
    CHECK(v.lowPitch + v.rows / 2 == 46);
    CHECK(fitViewport(notes, 2 * kTicksPerBar, 24).spanTicks == 2 * kTicksPerBar);
    v = fitViewport({}, kTicksPerBar, 24);
    CHECK(v.lowPitch + v.rows / 2 == 60);
    // notes at the edge of the keyboard keep the window inside 0-127
    std::vector<RollNote> high{note(1, 127, 0, 240)};
    v = fitViewport(high, kTicksPerBar, 24);
    CHECK(v.lowPitch + v.rows <= 128);
}

TEST_CASE("zooming keeps the anchor where it is", "[roll-edit]") {
    const uint32_t total = 16 * kTicksPerBar;
    RollViewport v{2 * kTicksPerBar, 4 * kTicksPerBar, 48, 24};
    const uint32_t anchor = 3 * kTicksPerBar;
    const auto zoomed = zoomViewport(v, 0.5, anchor, total);
    CHECK(zoomed.spanTicks == 2 * kTicksPerBar);
    // the anchor sits at the same share of the window
    const double before = static_cast<double>(anchor - v.startTick) / v.spanTicks;
    const double after = static_cast<double>(anchor - zoomed.startTick) / zoomed.spanTicks;
    CHECK(std::abs(before - after) < 1e-3);

    CHECK(zoomViewport(v, 100.0, anchor, total).spanTicks == total);              // zoomed out to everything
    CHECK(zoomViewport(v, 0.0001, anchor, total).spanTicks == kMinRollSpanTicks); // and in as far as allowed
    CHECK(zoomViewport(v, -1.0, anchor, total) == clampViewport(v, total));       // nonsense changes nothing
    CHECK(zoomViewport(v, 4.0, anchor, total).startTick + zoomViewport(v, 4.0, anchor, total).spanTicks <= total);
}

TEST_CASE("scrolling moves the window and stops at the ends", "[roll-edit]") {
    const uint32_t total = 8 * kTicksPerBar;
    RollViewport v{kTicksPerBar, 2 * kTicksPerBar, 48, 24};
    auto s = scrollViewport(v, 960, 3, total);
    CHECK(s.startTick == kTicksPerBar + 960);
    CHECK(s.lowPitch == 51);
    s = scrollViewport(v, -100000, -100, total);
    CHECK(s.startTick == 0);
    CHECK(s.lowPitch == 0);
    s = scrollViewport(v, 100000, 100, total);
    CHECK(s.startTick == total - v.spanTicks);
    CHECK(s.lowPitch == 128 - 24);
}

TEST_CASE("pixels and ticks convert both ways", "[roll-edit]") {
    const auto g = geometryOf(4 * kTicksPerBar, 3840, 7680, 48, 24); // 960 px for 7680 ticks: 8 ticks per pixel
    CHECK(g.ticksPerPixel() == 8.0);
    CHECK(g.tickToX(3840) == 0.0);
    CHECK(g.tickToX(3840 + 800) == 100.0);
    CHECK(g.xToTick(0.0) == 3840);
    CHECK(g.xToTick(100.0) == 3840 + 800);
    CHECK(g.xToTick(-50.0) == 3840 - 400); // before the window, still inside the pattern
    CHECK(g.xToTick(-5000.0) == 0);
    CHECK(g.xToTick(100000.0) == 4 * kTicksPerBar - 1); // never past the pattern
    // rows: 24 rows in 240 px, the top row is pitch 71
    CHECK(g.rowHeight() == 10.0);
    CHECK(g.yToPitch(0.0) == 71);
    CHECK(g.yToPitch(9.9) == 71);
    CHECK(g.yToPitch(10.0) == 70);
    CHECK(g.yToPitch(239.0) == 48);
    CHECK(g.pitchToY(71) == 0.0);
    CHECK(g.pitchToY(48) == 230.0);
    for (int pitch = 48; pitch < 72; ++pitch) {
        CHECK(g.yToPitch(g.pitchToY(pitch) + 1.0) == pitch);
    }
}

TEST_CASE("a note box has a minimum width and leaves a gap between rows", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0); // 10 ticks per pixel
    const auto box = noteBox(g, note(1, 60, 960, 240));
    CHECK(box.x == 96.0);
    CHECK(box.width == 24.0);
    CHECK(box.height == 9.0);
    CHECK(box.y == g.pitchToY(60));
    CHECK(noteBox(g, note(2, 60, 960, 5)).width == 3.0);
}

TEST_CASE("the hit test finds the top note and the right edge", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0);
    std::vector<RollNote> notes{note(1, 60, 960, 960), note(2, 60, 1440, 480)}; // the second covers the first
    const auto box = noteBox(g, notes[1]);
    const double y = box.y + 3.0;

    auto hit = hitTest(g, notes, box.x + 5.0, y);
    CHECK(hit.id == 2);
    CHECK(hit.zone == RollZone::Body);
    hit = hitTest(g, notes, box.x + box.width - 1.0, y);
    CHECK(hit.id == 2);
    CHECK(hit.zone == RollZone::RightEdge);
    hit = hitTest(g, notes, box.x + box.width - 10.0, y); // the edge is a few pixels, not a third of a wide note
    CHECK(hit.id == 2);
    CHECK(hit.zone == RollZone::Body);
    hit = hitTest(g, notes, g.tickToX(1100.0), y); // only the first covers it
    CHECK(hit.id == 1);
    CHECK(hit.zone == RollZone::Body);
    CHECK(hitTest(g, notes, 3.0, y).id == 0);                                // empty: before the notes
    CHECK(hitTest(g, notes, box.x + 5.0, box.y - 2.0).id == 0);              // above the row
    CHECK(hitTest(g, notes, box.x + 5.0, box.y + box.height + 1.0).id == 0); // in the gap below the row
}

TEST_CASE("a very short note can still be hit and still has an edge", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0);
    std::vector<RollNote> notes{note(1, 60, 960, 20)}; // 2 px wide
    const auto box = noteBox(g, notes[0]);
    CHECK(box.width == 3.0);
    CHECK(hitTest(g, notes, box.x + 6.0, box.y + 2.0).id == 1); // outside the drawn box, inside the 8 px hit area
    CHECK(hitTest(g, notes, box.x + 9.0, box.y + 2.0).id == 0);
    CHECK(hitTest(g, notes, box.x + 7.0, box.y + 2.0).zone == RollZone::RightEdge);
    CHECK(hitTest(g, notes, box.x + 1.0, box.y + 2.0).zone == RollZone::Body);
}

TEST_CASE("the rubber band takes every note it touches", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0);
    std::vector<RollNote> notes{note(1, 60, 0, 240), note(2, 62, 960, 240), note(3, 70, 1920, 240),
                                note(4, 60, 2880, 240)};
    const auto ids = notesInRect(g, notes, g.tickToX(900.0), g.pitchToY(63), g.tickToX(1300.0), g.pitchToY(59));
    CHECK(ids == std::vector<uint32_t>{2});
    // corners in any order
    CHECK(notesInRect(g, notes, g.tickToX(1300.0), g.pitchToY(59), g.tickToX(900.0), g.pitchToY(63)) ==
          std::vector<uint32_t>{2});
    // touching is enough
    const auto wide = notesInRect(g, notes, 0.0, 0.0, 384.0, 240.0);
    CHECK(wide.size() == 4);
    CHECK(notesInRect(g, notes, 0.0, 0.0, 1.0, 1.0).empty());
    // touching a note is enough, it does not have to lie inside the band
    CHECK(notesInRect(g, notes, g.tickToX(1050.0), g.pitchToY(63), g.tickToX(1100.0), g.pitchToY(59)) ==
          std::vector<uint32_t>{2});
}

TEST_CASE("a new note lands in the clicked cell with one grid step", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0); // 10 ticks per pixel
    auto n = newNoteAt(g, snapOf(PitchSnap::Chromatic), 100.0, g.pitchToY(61) + 1.0);
    CHECK(n.startTick == 960); // tick 1000 rounds down to the 16th at 960
    CHECK(n.lengthTicks == 240);
    CHECK(n.pitch == 61);

    n = newNoteAt(g, snapOf(PitchSnap::Chromatic, 8), 100.0, g.pitchToY(61) + 1.0);
    CHECK(n.startTick == 960);
    CHECK(n.lengthTicks == 480);
    n = newNoteAt(g, snapOf(PitchSnap::Chromatic, 4), 150.0, g.pitchToY(61) + 1.0);
    CHECK(n.startTick == 960); // the cell of tick 1500, a quarter note
    CHECK(n.lengthTicks == 960);
    n = newNoteAt(g, snapOf(PitchSnap::Chromatic, 16, true), 100.0, g.pitchToY(61) + 1.0);
    CHECK(n.startTick == 960); // 160-tick triplet grid: 1000 / 160 = 6.25 -> 960
    CHECK(n.lengthTicks == 160);

    // scale snapping: C#4 (61) is not in A minor, it snaps down to C
    n = newNoteAt(g, snapOf(PitchSnap::Scale), 100.0, g.pitchToY(61) + 1.0);
    CHECK(n.pitch == 60);
    n = newNoteAt(g, snapOf(PitchSnap::Scale), 100.0, g.pitchToY(62) + 1.0);
    CHECK(n.pitch == 62);

    // the last cell of the pattern and a click past it keep the note inside
    n = newNoteAt(g, snapOf(PitchSnap::Chromatic), 383.0, 5.0);
    CHECK(n.startTick + n.lengthTicks <= kTicksPerBar);
    CHECK(n.startTick == kTicksPerBar - 240);
    n = newNoteAt(g, snapOf(PitchSnap::Chromatic), 5000.0, 5.0);
    CHECK(n.startTick + n.lengthTicks <= kTicksPerBar);
}

TEST_CASE("a pattern too short for the grid step gets no note", "[roll-edit]") {
    const auto g = geometryOf(100, 0, 100, 48, 24, 384.0, 240.0);
    CHECK(newNoteAt(g, snapOf(PitchSnap::Chromatic, 4), 10.0, 10.0).lengthTicks == 0);
}

TEST_CASE("dragging a note moves the block by the snapped anchor", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0); // 10 ticks/px, 10 px/row
    const auto anchor = note(1, 60, 960, 240);
    auto d = dragMoveDelta(g, snapOf(PitchSnap::Chromatic), anchor, 0.0, 0.0);
    CHECK(d.ticks == 0);
    CHECK(d.pitch == 0);
    d = dragMoveDelta(g, snapOf(PitchSnap::Chromatic), anchor, 30.0, -20.0); // +300 ticks -> 1260 -> grid 1200
    CHECK(d.ticks == 240);
    CHECK(d.pitch == 2);
    d = dragMoveDelta(g, snapOf(PitchSnap::Chromatic), anchor, -4.0, 10.0); // -40 ticks -> 920 -> 960
    CHECK(d.ticks == 0);
    CHECK(d.pitch == -1);
    d = dragMoveDelta(g, snapOf(PitchSnap::Chromatic), anchor, -5000.0, 5000.0); // far outside
    CHECK(d.ticks == -960);
    CHECK(d.pitch == -60);
    d = dragMoveDelta(g, snapOf(PitchSnap::Chromatic), anchor, 5000.0, -5000.0);
    CHECK(d.ticks >= 0);
    CHECK(60 + d.pitch == 127);
    // the scale snapping decides the pitch of the anchor: 62 is D, 61 snaps down to C
    d = dragMoveDelta(g, snapOf(PitchSnap::Scale), anchor, 0.0, -10.0); // one row up: 61
    CHECK(d.pitch == 0);
    d = dragMoveDelta(g, snapOf(PitchSnap::Scale), anchor, 0.0, -20.0); // 62
    CHECK(d.pitch == 2);
    // the triplet grid
    d = dragMoveDelta(g, snapOf(PitchSnap::Chromatic, 16, true), anchor, 20.0, 0.0); // 960 + 200 = 1160 -> 1120
    CHECK(d.ticks == 160);
}

TEST_CASE("dragging the right edge sets the length from the snapped end", "[roll-edit]") {
    const auto g = geometryOf(kTicksPerBar, 0, kTicksPerBar, 48, 24, 384.0, 240.0);
    const auto anchor = note(1, 60, 960, 240); // ends at 1200
    CHECK(dragLength(g, snapOf(PitchSnap::Chromatic), anchor, 0.0) == 240);
    CHECK(dragLength(g, snapOf(PitchSnap::Chromatic), anchor, 30.0) == 480);  // end 1500 -> 1440
    CHECK(dragLength(g, snapOf(PitchSnap::Chromatic), anchor, -30.0) == 240); // end 900 -> 960 -> below one step
    CHECK(dragLength(g, snapOf(PitchSnap::Chromatic), anchor, -5000.0) == 240);
    CHECK(dragLength(g, snapOf(PitchSnap::Chromatic), anchor, 5000.0) == kTicksPerBar - 960);
    CHECK(dragLength(g, snapOf(PitchSnap::Chromatic, 8), anchor, 0.0) == 480); // the end snaps to 1440
}

TEST_CASE("an arrow key steps to the next allowed pitch", "[roll-edit]") {
    const auto chromatic = snapOf(PitchSnap::Chromatic);
    CHECK(stepPitch(60, 1, chromatic) == 61);
    CHECK(stepPitch(60, -1, chromatic) == 59);
    const auto scale = snapOf(PitchSnap::Scale); // A natural minor: A B C D E F G
    CHECK(stepPitch(60, 1, scale) == 62);        // C -> D
    CHECK(stepPitch(60, -1, scale) == 59);       // C -> B
    CHECK(stepPitch(64, 1, scale) == 65);        // E -> F
    CHECK(stepPitch(62, -1, scale) == 60);       // D -> C
    CHECK(stepPitch(61, 1, scale) == 62);        // from outside the scale
    CHECK(stepPitch(127, 1, chromatic) == 127);
    CHECK(stepPitch(0, -1, chromatic) == 0);
    RollSnap noScale = scale;
    noScale.scale = nullptr;
    CHECK(stepPitch(60, 1, noScale) == 61); // without a scale it is chromatic
}
