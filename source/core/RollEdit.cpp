#include "core/RollEdit.h"

#include <algorithm>
#include <cmath>

namespace mm::core {

namespace {

constexpr double kMinNoteWidth = 3.0;
constexpr double kMinHitWidth = 8.0;

int64_t clampInt64(int64_t value, int64_t lo, int64_t hi) {
    return std::max(lo, std::min(value, hi));
}

} // namespace

RollViewport clampViewport(RollViewport viewport, uint32_t patternTicks) {
    const uint32_t total = std::max<uint32_t>(patternTicks, 1);
    const uint32_t minSpan = std::min(kMinRollSpanTicks, total);
    viewport.spanTicks = static_cast<uint32_t>(clampInt64(viewport.spanTicks, minSpan, total));
    viewport.startTick = static_cast<uint32_t>(clampInt64(viewport.startTick, 0, total - viewport.spanTicks));
    viewport.rows = static_cast<int>(clampInt64(viewport.rows, kMinRollRows, kMaxRollRows));
    viewport.lowPitch = static_cast<int>(clampInt64(viewport.lowPitch, 0, 128 - viewport.rows));
    return viewport;
}

RollViewport fitViewport(std::span<const RollNote> notes, uint32_t patternTicks, int rows) {
    RollViewport viewport;
    viewport.spanTicks = std::min(patternTicks, kDefaultRollSpanTicks);
    viewport.startTick = 0;
    viewport.rows = rows;
    int lowest = 60;
    int highest = 60;
    if (!notes.empty()) {
        lowest = highest = notes.front().pitch;
        for (const RollNote& note : notes) {
            lowest = std::min<int>(lowest, note.pitch);
            highest = std::max<int>(highest, note.pitch);
        }
    }
    const int centre = (lowest + highest) / 2;
    viewport.lowPitch = centre - rows / 2;
    return clampViewport(viewport, patternTicks);
}

RollViewport zoomViewport(RollViewport viewport, double factor, uint32_t anchorTick, uint32_t patternTicks) {
    viewport = clampViewport(viewport, patternTicks);
    if (!(factor > 0.0)) {
        return viewport;
    }
    const double anchor = std::clamp<double>(anchorTick, viewport.startTick, viewport.startTick + viewport.spanTicks);
    const double share = (anchor - viewport.startTick) / static_cast<double>(viewport.spanTicks);
    const auto span = static_cast<int64_t>(std::llround(static_cast<double>(viewport.spanTicks) * factor));
    const uint32_t total = std::max<uint32_t>(patternTicks, 1);
    const auto newSpan = static_cast<uint32_t>(clampInt64(span, std::min(kMinRollSpanTicks, total), total));
    viewport.startTick = static_cast<uint32_t>(
        clampInt64(std::llround(anchor - share * newSpan), 0, static_cast<int64_t>(total - newSpan)));
    viewport.spanTicks = newSpan;
    return clampViewport(viewport, patternTicks);
}

RollViewport scrollViewport(RollViewport viewport, int64_t deltaTicks, int deltaRows, uint32_t patternTicks) {
    viewport = clampViewport(viewport, patternTicks);
    const int64_t start = static_cast<int64_t>(viewport.startTick) + deltaTicks;
    viewport.startTick = static_cast<uint32_t>(std::max<int64_t>(start, 0));
    viewport.lowPitch += deltaRows;
    return clampViewport(viewport, patternTicks);
}

double RollGeometry::ticksPerPixel() const {
    return static_cast<double>(viewport.spanTicks) / std::max(width, 1.0);
}

double RollGeometry::rowHeight() const {
    return std::max(height, 1.0) / static_cast<double>(std::max(viewport.rows, 1));
}

double RollGeometry::tickToX(double tick) const {
    return (tick - static_cast<double>(viewport.startTick)) / ticksPerPixel();
}

uint32_t RollGeometry::xToTick(double x) const {
    const double tick = static_cast<double>(viewport.startTick) + x * ticksPerPixel();
    const double last = static_cast<double>(std::max<uint32_t>(patternTicks, 1) - 1);
    return static_cast<uint32_t>(std::floor(std::clamp(tick, 0.0, last)));
}

double RollGeometry::pitchToY(int pitch) const {
    return static_cast<double>(viewport.lowPitch + viewport.rows - 1 - pitch) * rowHeight();
}

int RollGeometry::yToPitch(double y) const {
    const int fromTop = static_cast<int>(std::floor(y / rowHeight()));
    return std::clamp(viewport.lowPitch + viewport.rows - 1 - fromTop, 0, 127);
}

RollBox noteBox(const RollGeometry& geometry, const RollNote& note) {
    RollBox box;
    box.x = geometry.tickToX(note.startTick);
    box.width =
        std::max(geometry.tickToX(static_cast<double>(note.startTick) + note.lengthTicks) - box.x, kMinNoteWidth);
    box.y = geometry.pitchToY(note.pitch);
    box.height = std::max(geometry.rowHeight() - 1.0, 1.0);
    return box;
}

RollHit hitTest(const RollGeometry& geometry, std::span<const RollNote> notes, double x, double y) {
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
        RollBox box = noteBox(geometry, *it);
        const double extra = std::max(kMinHitWidth - box.width, 0.0);
        box.width += extra;
        if (!box.contains(x, y)) {
            continue;
        }
        const double edge = std::clamp(box.width / 3.0, 3.0, 6.0);
        return {it->id, x >= box.x + box.width - edge ? RollZone::RightEdge : RollZone::Body};
    }
    return {};
}

std::vector<uint32_t> notesInRect(const RollGeometry& geometry, std::span<const RollNote> notes, double x0, double y0,
                                  double x1, double y1) {
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);
    const double top = std::min(y0, y1);
    const double bottom = std::max(y0, y1);
    std::vector<uint32_t> ids;
    for (const RollNote& note : notes) {
        const RollBox box = noteBox(geometry, note);
        if (box.x <= right && box.x + box.width >= left && box.y <= bottom && box.y + box.height >= top) {
            ids.push_back(note.id);
        }
    }
    return ids;
}

NewNote newNoteAt(const RollGeometry& geometry, const RollSnap& snap, double x, double y) {
    NewNote note;
    const uint32_t step = snap.grid.ticks();
    const uint32_t total = geometry.patternTicks;
    if (step == 0 || step > total) {
        return note;
    }
    const uint32_t tick = geometry.xToTick(x);
    note.lengthTicks = step;
    note.startTick = std::min((tick / step) * step, total - step);
    const int row = geometry.yToPitch(y);
    note.pitch = snap.scale != nullptr ? snapPitch(row, snap.pitch, snap.root, *snap.scale)
                                       : static_cast<uint8_t>(std::clamp(row, 0, 127));
    return note;
}

MoveDelta dragMoveDelta(const RollGeometry& geometry, const RollSnap& snap, const RollNote& anchor, double dx,
                        double dy) {
    MoveDelta delta;
    const double target = static_cast<double>(anchor.startTick) + dx * geometry.ticksPerPixel();
    const auto clamped = static_cast<uint32_t>(
        std::llround(std::clamp(target, 0.0, static_cast<double>(std::max<uint32_t>(geometry.patternTicks, 1)))));
    delta.ticks = static_cast<int32_t>(static_cast<int64_t>(snapTick(clamped, snap.grid)) - anchor.startTick);
    const int rows = -static_cast<int>(std::lround(dy / geometry.rowHeight()));
    const int wanted = std::clamp(static_cast<int>(anchor.pitch) + rows, 0, 127);
    const int snapped = snap.scale != nullptr ? snapPitch(wanted, snap.pitch, snap.root, *snap.scale) : wanted;
    delta.pitch = snapped - anchor.pitch;
    return delta;
}

uint32_t dragLength(const RollGeometry& geometry, const RollSnap& snap, const RollNote& anchor, double dx) {
    const uint32_t step = std::max<uint32_t>(snap.grid.ticks(), 1);
    const double end = static_cast<double>(anchor.startTick) + anchor.lengthTicks + dx * geometry.ticksPerPixel();
    const auto clamped = static_cast<uint32_t>(
        std::llround(std::clamp(end, 0.0, static_cast<double>(std::max<uint32_t>(geometry.patternTicks, 1)))));
    const int64_t snappedEnd = snapTick(clamped, snap.grid);
    return static_cast<uint32_t>(std::max<int64_t>(snappedEnd - anchor.startTick, step));
}

RollBox velocityBarBox(const RollGeometry& geometry, const RollNote& note, double laneHeight) {
    const RollBox noteRect = noteBox(geometry, note);
    RollBox box;
    box.x = noteRect.x;
    box.width = std::clamp(noteRect.width - 1.0, 3.0, 10.0);
    const double share = std::clamp(static_cast<double>(note.velocity), 1.0, 127.0) / 127.0;
    box.height = std::max(laneHeight * share, 1.0);
    box.y = laneHeight - box.height;
    return box;
}

uint8_t velocityAtY(double laneHeight, double y) {
    const double share = 1.0 - std::clamp(y / std::max(laneHeight, 1.0), 0.0, 1.0);
    return static_cast<uint8_t>(std::clamp<long>(std::lround(share * 127.0), 1, 127));
}

uint32_t hitVelocityBar(const RollGeometry& geometry, std::span<const RollNote> notes, double x) {
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
        const RollBox box = velocityBarBox(geometry, *it, 1.0);
        const double width = std::max(box.width, kMinHitWidth);
        if (x >= box.x && x < box.x + width) {
            return it->id;
        }
    }
    return 0;
}

std::vector<uint32_t> velocityBarsBetween(const RollGeometry& geometry, std::span<const RollNote> notes, double x0,
                                          double x1) {
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);
    std::vector<uint32_t> ids;
    for (const RollNote& note : notes) {
        const RollBox box = velocityBarBox(geometry, note, 1.0);
        const double middle = box.x + box.width / 2.0;
        if (middle >= left && middle <= right) {
            ids.push_back(note.id);
        }
    }
    return ids;
}

uint8_t shiftVelocity(uint8_t velocity, int delta) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(velocity) + delta, 1, 127));
}

int stepPitch(int pitch, int direction, const RollSnap& snap) {
    const int step = direction >= 0 ? 1 : -1;
    int candidate = pitch;
    for (int i = 0; i < 13; ++i) {
        candidate += step;
        if (candidate < 0 || candidate > 127) {
            return pitch;
        }
        const int snapped = snap.scale != nullptr && snap.pitch == PitchSnap::Scale
                                ? snapPitch(candidate, snap.pitch, snap.root, *snap.scale)
                                : candidate;
        if (snapped != pitch && (snapped - pitch) * step > 0) {
            return snapped;
        }
    }
    return pitch;
}

} // namespace mm::core
