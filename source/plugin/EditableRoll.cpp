#include "plugin/EditableRoll.h"

#include "core/PatternEdit.h"
#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

#include <algorithm>
#include <cmath>

namespace mm::plugin {

namespace {

using mm::core::RollHit;
using mm::core::RollZone;

constexpr double kDragThreshold = 3.0;

bool isBlackKey(int pitch) {
    switch (pitch % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

juce::Rectangle<float> toRect(const mm::core::RollBox& box, juce::Point<int> origin) {
    return {static_cast<float>(box.x) + static_cast<float>(origin.x),
            static_cast<float>(box.y) + static_cast<float>(origin.y), static_cast<float>(box.width),
            static_cast<float>(box.height)};
}

} // namespace

EditableRoll::EditableRoll(ProcessorBase& processor, int voice) : processor_(processor), voice_(voice) {
    setWantsKeyboardFocus(true);
    viewport_.rows = 24;
}

juce::Rectangle<int> EditableRoll::field() const {
    return getLocalBounds().withTrimmedLeft(kGutter);
}

int EditableRoll::rowsForHeight() const {
    const int height = field().getHeight();
    return height <= 0 ? 24 : std::max(mm::core::kMinRollRows, height / kRowPixels);
}

mm::core::RollGeometry EditableRoll::geometry() const {
    mm::core::RollGeometry geometry;
    geometry.viewport = viewport_;
    geometry.width = std::max(field().getWidth(), 1);
    geometry.height = std::max(field().getHeight(), 1);
    geometry.patternTicks = std::max<uint32_t>(patternTicks_, 1);
    return geometry;
}

mm::core::RollSnap EditableRoll::snap() const {
    mm::core::RollSnap snap;
    snap.grid = grid_;
    snap.pitch = pitchSnap_;
    snap.root = root_;
    snap.scale = mm::core::findScale(scaleId_);
    return snap;
}

const mm::core::RollNote* EditableRoll::find(uint32_t id) const {
    const auto it = std::find_if(source_.begin(), source_.end(), [&](const auto& n) { return n.id == id; });
    return it == source_.end() ? nullptr : &*it;
}

void EditableRoll::setEditSettings(mm::core::EditGrid grid, mm::core::PitchSnap snap) {
    grid_ = grid;
    pitchSnap_ = snap;
    repaint();
}

void EditableRoll::setSource(const ProcessorBase::VoiceView* view) {
    if (view == nullptr || !view->hasPattern) {
        endGesture();
        source_.clear();
        selection_.clear();
        slotIndex_ = -1;
        patternTicks_ = 0;
        haveView_ = false;
        repaint();
        return;
    }
    const int slot = view->slot - 1;
    const bool sameTarget = haveView_ && slot == slotIndex_ && view->lengthTicks == patternTicks_;
    if (!sameTarget) {
        endGesture();
        selection_.clear();
    }
    source_ = view->source;
    patternTicks_ = view->lengthTicks;
    root_ = view->root;
    scaleId_ = view->scaleId;
    slotIndex_ = slot;
    viewport_ = sameTarget ? mm::core::clampViewport(viewport_, patternTicks_)
                           : mm::core::fitViewport(source_, patternTicks_, rowsForHeight());
    haveView_ = true;
    for (auto it = selection_.begin(); it != selection_.end();) {
        it = find(*it) == nullptr ? selection_.erase(it) : std::next(it);
    }
    repaint();
}

void EditableRoll::endGesture() {
    mode_ = Mode::Idle;
    dragged_ = false;
    narrowOnRelease_ = false;
    expected_.reset();
}

void EditableRoll::press(juce::Point<double> point, juce::ModifierKeys mods) {
    endGesture();
    stepped_ = false;
    if (!haveView_) {
        return;
    }
    pressPoint_ = point;
    shiftHeld_ = mods.isShiftDown();
    const RollHit hit = mm::core::hitTest(geometry(), source_, point.x, point.y);
    if (hit.id != 0) {
        const bool selected = selection_.count(hit.id) != 0;
        if (shiftHeld_) {
            if (selected) {
                selection_.erase(hit.id); // Shift on a selected note takes it out again
                repaint();
                return;
            }
            selection_.insert(hit.id);
        } else if (!selected) {
            selection_ = {hit.id};
        } else {
            narrowOnRelease_ = true; // a click without a drag narrows the selection to this note
        }
        anchor_ = *find(hit.id);
        mode_ = hit.zone == RollZone::RightEdge ? Mode::Resizing : Mode::Moving;
    } else {
        rubberBase_ = shiftHeld_ ? selection_ : std::set<uint32_t>{};
        if (!shiftHeld_) {
            selection_.clear();
        }
        rubberEnd_ = point;
        mode_ = Mode::Rubber;
    }
    repaint();
}

void EditableRoll::drag(juce::Point<double> point) {
    if (mode_ == Mode::Idle) {
        return;
    }
    const double dx = point.x - pressPoint_.x;
    const double dy = point.y - pressPoint_.y;
    if (!dragged_) {
        if (std::hypot(dx, dy) < kDragThreshold) {
            return;
        }
        dragged_ = true;
        narrowOnRelease_ = false;
    }
    switch (mode_) {
    case Mode::Moving:
        stepMove(dx, dy);
        break;
    case Mode::Resizing:
        stepResize(dx);
        break;
    case Mode::Rubber: {
        rubberEnd_ = point;
        selection_ = rubberBase_;
        for (const uint32_t id :
             mm::core::notesInRect(geometry(), source_, pressPoint_.x, pressPoint_.y, point.x, point.y)) {
            selection_.insert(id);
        }
        repaint();
        break;
    }
    case Mode::Idle:
        break;
    }
}

void EditableRoll::release(juce::Point<double>) {
    if (mode_ != Mode::Idle && !dragged_ && narrowOnRelease_) {
        selection_ = {anchor_.id};
    }
    endGesture();
    repaint();
}

void EditableRoll::stepMove(double dx, double dy) {
    const std::vector<uint32_t> ids(selection_.begin(), selection_.end());
    const auto delta = mm::core::dragMoveDelta(geometry(), snap(), anchor_, dx, dy);
    const int64_t targetStart = static_cast<int64_t>(anchor_.startTick) + delta.ticks;
    const int targetPitch = static_cast<int>(anchor_.pitch) + delta.pitch;
    const size_t voice = static_cast<size_t>(voice_ - 1);
    bool mismatch = false;
    const bool ok = processor_.editNotes(
        static_cast<size_t>(slotIndex_),
        [&](mm::core::Pattern& pattern) -> size_t {
            if (voice >= pattern.voices.size()) {
                mismatch = true;
                return 0;
            }
            auto& notes = pattern.voices[voice].notes;
            const auto it = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == anchor_.id; });
            if (it == notes.end() ||
                (expected_ && (it->startTick != expected_->startTick || it->pitch != expected_->pitch))) {
                mismatch = true; // somebody else changed the note under the gesture
                return 0;
            }
            const int32_t dt = static_cast<int32_t>(targetStart - static_cast<int64_t>(it->startTick));
            const int dp = targetPitch - it->pitch;
            if (dt == 0 && dp == 0) {
                expected_ = mm::core::RollNote{it->id, it->pitch, it->startTick, it->lengthTicks};
                return 0;
            }
            const size_t changed = mm::core::moveNotes(pattern, voice, ids, dt, dp);
            const auto after =
                std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == anchor_.id; });
            expected_ = mm::core::RollNote{after->id, after->pitch, after->startTick, after->lengthTicks};
            return changed;
        },
        stepped_);
    stepped_ = stepped_ || ok;
    if (mismatch) {
        endGesture();
    }
}

void EditableRoll::stepResize(double dx) {
    const std::vector<uint32_t> ids(selection_.begin(), selection_.end());
    const uint32_t targetLength = mm::core::dragLength(geometry(), snap(), anchor_, dx);
    const size_t voice = static_cast<size_t>(voice_ - 1);
    bool mismatch = false;
    const bool ok = processor_.editNotes(
        static_cast<size_t>(slotIndex_),
        [&](mm::core::Pattern& pattern) -> size_t {
            if (voice >= pattern.voices.size()) {
                mismatch = true;
                return 0;
            }
            auto& notes = pattern.voices[voice].notes;
            const auto it = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == anchor_.id; });
            if (it == notes.end() || (expected_ && it->lengthTicks != expected_->lengthTicks)) {
                mismatch = true;
                return 0;
            }
            const int32_t delta = static_cast<int32_t>(static_cast<int64_t>(targetLength) - it->lengthTicks);
            if (delta == 0) {
                expected_ = mm::core::RollNote{it->id, it->pitch, it->startTick, it->lengthTicks};
                return 0;
            }
            const size_t changed = mm::core::resizeNotes(pattern, voice, ids, delta);
            const auto after =
                std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == anchor_.id; });
            expected_ = mm::core::RollNote{after->id, after->pitch, after->startTick, after->lengthTicks};
            return changed;
        },
        stepped_);
    stepped_ = stepped_ || ok;
    if (mismatch) {
        endGesture();
    }
}

void EditableRoll::removeIds(const std::vector<uint32_t>& ids) {
    if (ids.empty() || !haveView_) {
        return;
    }
    const size_t voice = static_cast<size_t>(voice_ - 1);
    processor_.editNotes(static_cast<size_t>(slotIndex_),
                         [&](mm::core::Pattern& pattern) { return mm::core::removeNotes(pattern, voice, ids); });
    for (const uint32_t id : ids) {
        selection_.erase(id);
    }
}

void EditableRoll::doubleClick(juce::Point<double> point) {
    endGesture();
    if (!haveView_) {
        return;
    }
    const RollHit hit = mm::core::hitTest(geometry(), source_, point.x, point.y);
    if (hit.id != 0) {
        removeIds({hit.id});
        return;
    }
    const auto note = mm::core::newNoteAt(geometry(), snap(), point.x, point.y);
    if (note.lengthTicks == 0) {
        return;
    }
    const size_t voice = static_cast<size_t>(voice_ - 1);
    uint32_t id = 0;
    processor_.editNotes(static_cast<size_t>(slotIndex_), [&](mm::core::Pattern& pattern) -> size_t {
        id = mm::core::addNote(pattern, voice, note.pitch, note.startTick, note.lengthTicks, 100);
        return id != 0 ? 1 : 0;
    });
    if (id != 0) {
        selection_ = {id};
    }
}

void EditableRoll::moveByKey(int64_t ticks, int pitchDirection, int pitchSteps) {
    if (selection_.empty() || !haveView_) {
        return;
    }
    const std::vector<uint32_t> ids(selection_.begin(), selection_.end());
    const size_t voice = static_cast<size_t>(voice_ - 1);
    const auto rules = snap();
    processor_.editNotes(static_cast<size_t>(slotIndex_), [&](mm::core::Pattern& pattern) -> size_t {
        if (voice >= pattern.voices.size()) {
            return 0;
        }
        int deltaPitch = 0;
        if (pitchDirection != 0) {
            // The first selected note decides the step, the rest follows rigidly.
            const auto& notes = pattern.voices[voice].notes;
            const auto anchor =
                std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == ids.front(); });
            if (anchor == notes.end()) {
                return 0;
            }
            deltaPitch = pitchSteps == 12 ? pitchDirection * 12
                                          : mm::core::stepPitch(anchor->pitch, pitchDirection, rules) - anchor->pitch;
        }
        return mm::core::moveNotes(pattern, voice, ids, static_cast<int32_t>(ticks), deltaPitch);
    });
}

bool EditableRoll::handleKey(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (mods.isCommandDown() && (code == 'Z' || code == 'z')) {
        if (mods.isShiftDown()) {
            processor_.redo();
        } else {
            processor_.undo();
        }
        return true;
    }
    if (mods.isCommandDown() && (code == 'A' || code == 'a')) {
        selection_.clear();
        for (const auto& note : source_) {
            selection_.insert(note.id);
        }
        repaint();
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        removeIds(std::vector<uint32_t>(selection_.begin(), selection_.end()));
        return true;
    }
    const int64_t step = static_cast<int64_t>(grid_.ticks()) * (mods.isShiftDown() ? 4 : 1);
    if (code == juce::KeyPress::leftKey) {
        moveByKey(-step, 0, 0);
        return true;
    }
    if (code == juce::KeyPress::rightKey) {
        moveByKey(step, 0, 0);
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        moveByKey(0, code == juce::KeyPress::upKey ? 1 : -1, mods.isShiftDown() ? 12 : 1);
        return true;
    }
    return false;
}

void EditableRoll::zoomAt(double x, double factor) {
    if (!haveView_) {
        return;
    }
    const auto anchor = geometry().xToTick(x);
    viewport_ = mm::core::zoomViewport(viewport_, factor, anchor, patternTicks_);
    repaint();
}

void EditableRoll::scrollBy(int64_t deltaTicks, int deltaRows) {
    if (!haveView_) {
        return;
    }
    viewport_ = mm::core::scrollViewport(viewport_, deltaTicks, deltaRows, patternTicks_);
    repaint();
}

void EditableRoll::resized() {
    const int rows = rowsForHeight();
    if (haveView_) {
        const int centre = viewport_.lowPitch + viewport_.rows / 2;
        viewport_.rows = rows;
        viewport_.lowPitch = centre - rows / 2;
        viewport_ = mm::core::clampViewport(viewport_, patternTicks_);
    } else {
        viewport_.rows = rows;
    }
}

void EditableRoll::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    const auto point = (event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble();
    if (event.getNumberOfClicks() >= 2) {
        doubleClick(point);
        return;
    }
    press(point, event.mods);
}

void EditableRoll::mouseDrag(const juce::MouseEvent& event) {
    drag((event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble());
}

void EditableRoll::mouseUp(const juce::MouseEvent& event) {
    release((event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble());
}

void EditableRoll::mouseMove(const juce::MouseEvent& event) {
    const auto point = (event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble();
    const bool edge = haveView_ && mm::core::hitTest(geometry(), source_, point.x, point.y).zone == RollZone::RightEdge;
    setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void EditableRoll::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (!haveView_) {
        return;
    }
    const double x = static_cast<double>(event.position.x) - kGutter;
    if (event.mods.isCommandDown()) {
        zoomAt(x, std::exp(-static_cast<double>(wheel.deltaY) * 2.0));
        return;
    }
    const double ticksPerPixel = geometry().ticksPerPixel();
    if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        const double amount = event.mods.isShiftDown() ? wheel.deltaY : -wheel.deltaX;
        scrollBy(static_cast<int64_t>(std::llround(-amount * 300.0 * ticksPerPixel)), 0);
        return;
    }
    scrollBy(0, static_cast<int>(std::lround(wheel.deltaY * 8.0)));
}

bool EditableRoll::keyPressed(const juce::KeyPress& key) {
    return handleKey(key);
}

void EditableRoll::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff25252b));
    g.fillRoundedRectangle(bounds, 6.0f);

    const auto area = field();
    const auto origin = area.getPosition();
    const auto rules = snap();
    const auto geo = geometry();
    const float alpha = dimmed_ ? 0.25f : 1.0f;

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(area);
        // Pitch rows: the black keys a little darker, the C rows with a line.
        for (int pitch = viewport_.lowPitch; pitch < viewport_.lowPitch + viewport_.rows; ++pitch) {
            const auto y = static_cast<float>(geo.pitchToY(pitch)) + static_cast<float>(origin.y);
            const auto row =
                juce::Rectangle<float>(static_cast<float>(origin.x), y, static_cast<float>(area.getWidth()),
                                       static_cast<float>(geo.rowHeight()));
            if (isBlackKey(pitch)) {
                g.setColour(juce::Colours::black.withAlpha(0.18f));
                g.fillRect(row);
            }
            if (pitch % 12 == 0) {
                g.setColour(juce::Colours::white.withAlpha(0.10f));
                g.drawHorizontalLine(juce::roundToInt(row.getBottom()), row.getX(), row.getRight());
            }
        }
        // Grid, beat and bar lines inside the visible window.
        const uint32_t step = std::max<uint32_t>(rules.grid.ticks(), 1);
        const uint32_t first = (viewport_.startTick / step) * step;
        for (uint32_t tick = first; tick <= viewport_.startTick + viewport_.spanTicks; tick += step) {
            const auto x = static_cast<float>(geo.tickToX(tick)) + static_cast<float>(origin.x);
            const bool bar = tick % mm::core::kTicksPerBar == 0;
            const bool beat = tick % mm::core::kTicksPerQuarter == 0;
            if (!bar && !beat && step / geo.ticksPerPixel() < 5.0) {
                continue; // too dense to draw
            }
            g.setColour(juce::Colours::white.withAlpha(bar ? 0.22f : (beat ? 0.10f : 0.04f)));
            g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(origin.y), bounds.getBottom());
        }
        // Notes.
        for (const auto& note : source_) {
            const auto box = toRect(mm::core::noteBox(geo, note), origin);
            if (!box.intersects(area.toFloat())) {
                continue;
            }
            const bool selected = selection_.count(note.id) != 0;
            const float velocity = 0.45f + 0.55f * static_cast<float>(note.velocity) / 127.0f;
            g.setColour((selected ? accent_.brighter(0.5f) : accent_).withAlpha(alpha * (selected ? 1.0f : velocity)));
            g.fillRoundedRectangle(box, 2.0f);
            if (selected) {
                g.setColour(juce::Colours::white.withAlpha(alpha * 0.9f));
                g.drawRoundedRectangle(box.reduced(0.5f), 2.0f, 1.2f);
            }
        }
        if (mode_ == Mode::Rubber && dragged_) {
            const auto band = juce::Rectangle<float>(
                static_cast<float>(pressPoint_.x) + static_cast<float>(origin.x), static_cast<float>(pressPoint_.y),
                static_cast<float>(rubberEnd_.x - pressPoint_.x), static_cast<float>(rubberEnd_.y - pressPoint_.y));
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillRect(band);
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawRect(band, 1.0f);
        }
    }

    // Key labels: the C of every octave, in the naming of Ableton and Logic (C3 = 60).
    g.setFont(juce::FontOptions(10.0f));
    for (int pitch = viewport_.lowPitch; pitch < viewport_.lowPitch + viewport_.rows; ++pitch) {
        const auto y = static_cast<float>(geo.pitchToY(pitch));
        if (isBlackKey(pitch)) {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillRect(juce::Rectangle<float>(2.0f, y, static_cast<float>(kGutter) - 6.0f,
                                              static_cast<float>(geo.rowHeight())));
        }
        if (pitch % 12 == 0) {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawText("C" + juce::String(pitch / 12 - 2), 2, juce::roundToInt(y) - 4, kGutter - 6, 10,
                       juce::Justification::centredRight);
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(empty_ ? tr(mm::core::text::kRollEmpty) : title_, area.reduced(8, 4), juce::Justification::topLeft);
}

} // namespace mm::plugin
