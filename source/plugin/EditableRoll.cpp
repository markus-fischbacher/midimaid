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
    setWantsKeyboardFocus(false); // D-126, D-159: the roll takes no keys; actions come as buttons and menus
    viewport_.rows = 24;
}

int EditableRoll::laneHeight() const {
    return std::min(kLaneHeight, getHeight() / 3);
}

juce::Rectangle<int> EditableRoll::field() const {
    return getLocalBounds().withTrimmedLeft(kGutter).withTrimmedBottom(laneHeight());
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
    velocityStart_.clear();
    hintValue_ = 0;
    hintId_ = 0;
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
        if (std::hypot(dx, dy) < kDragThreshold) { // a stroke starts dragged: it needs no threshold
            return;
        }
        dragged_ = true;
        narrowOnRelease_ = false;
    }
    switch (mode_) {
    case Mode::Velocity:
        stepVelocity(point.y);
        break;
    case Mode::Draw:
        stepDraw(point);
        break;
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

void EditableRoll::pressLane(juce::Point<double> point, juce::ModifierKeys mods) {
    endGesture();
    stepped_ = false;
    if (!haveView_) {
        return;
    }
    pressPoint_ = point;
    lastLanePoint_ = point;
    const uint32_t id = mm::core::hitVelocityBar(geometry(), source_, point.x);
    if (id != 0) {
        // A bar of a note outside the selection selects that note, like a click in the note field.
        if (selection_.count(id) == 0) {
            selection_ = {id};
        }
        anchor_ = *find(id);
        velocityStart_.clear();
        for (const uint32_t selected : selection_) {
            if (const auto* note = find(selected)) {
                velocityStart_[selected] = note->velocity;
            }
        }
        mode_ = Mode::Velocity;
    } else if (mods.isAltDown()) {
        mode_ = Mode::Draw;
        dragged_ = true;
        stepDraw(point);
    }
    repaint();
}

void EditableRoll::stepVelocity(double y) {
    const int target = mm::core::velocityAtY(laneHeight(), y);
    const int delta = target - velocityStart_[anchor_.id];
    const size_t voice = static_cast<size_t>(voice_ - 1);
    std::vector<mm::core::VelocityChange> changes;
    for (const auto& [id, velocity] : velocityStart_) {
        changes.push_back({id, mm::core::shiftVelocity(velocity, delta)});
    }
    bool mismatch = false;
    const bool ok = processor_.editNotes(
        static_cast<size_t>(slotIndex_),
        [&](mm::core::Pattern& pattern) -> size_t {
            if (voice >= pattern.voices.size()) {
                mismatch = true;
                return 0;
            }
            const auto& notes = pattern.voices[voice].notes;
            const auto it = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.id == anchor_.id; });
            if (it == notes.end() || (expected_ && it->velocity != expected_->velocity)) {
                mismatch = true; // somebody else changed the velocity under the gesture
                return 0;
            }
            const size_t changed = mm::core::setVelocities(pattern, voice, changes);
            expected_ =
                mm::core::RollNote{anchor_.id, 0, 0, 0, mm::core::shiftVelocity(velocityStart_[anchor_.id], delta)};
            return changed;
        },
        stepped_);
    stepped_ = stepped_ || ok;
    hintId_ = anchor_.id;
    hintValue_ = mm::core::shiftVelocity(velocityStart_[anchor_.id], delta);
    if (mismatch) {
        endGesture();
    }
    repaint();
}

void EditableRoll::stepDraw(juce::Point<double> point) {
    const auto ids = mm::core::velocityBarsBetween(geometry(), source_, lastLanePoint_.x, point.x);
    const double dx = point.x - lastLanePoint_.x;
    std::vector<mm::core::VelocityChange> changes;
    for (const uint32_t id : ids) {
        const auto* note = find(id);
        if (note == nullptr) {
            continue;
        }
        // The height of the stroke where it crosses the bar: a straight line between the last and this position.
        const double middle = mm::core::velocityBarBox(geometry(), *note, 1.0).x + 2.0;
        const double share = std::abs(dx) < 1e-9 ? 1.0 : std::clamp((middle - lastLanePoint_.x) / dx, 0.0, 1.0);
        const double y = lastLanePoint_.y + (point.y - lastLanePoint_.y) * share;
        changes.push_back({id, mm::core::velocityAtY(laneHeight(), y)});
    }
    lastLanePoint_ = point;
    if (changes.empty()) {
        return;
    }
    const size_t voice = static_cast<size_t>(voice_ - 1);
    const bool ok = processor_.editNotes(
        static_cast<size_t>(slotIndex_),
        [&](mm::core::Pattern& pattern) { return mm::core::setVelocities(pattern, voice, changes); }, stepped_);
    stepped_ = stepped_ || ok;
    repaint();
}

void EditableRoll::flipFlag(bool accent) {
    if (selection_.empty() || !haveView_) {
        return;
    }
    const std::vector<uint32_t> ids(selection_.begin(), selection_.end());
    const size_t voice = static_cast<size_t>(voice_ - 1);
    processor_.editNotes(static_cast<size_t>(slotIndex_), [&](mm::core::Pattern& pattern) -> size_t {
        if (voice >= pattern.voices.size()) {
            return 0;
        }
        bool all = true;
        for (const auto& note : pattern.voices[voice].notes) {
            if (std::find(ids.begin(), ids.end(), note.id) != ids.end() && !(accent ? note.accent : note.slide)) {
                all = false;
            }
        }
        return accent ? mm::core::setAccent(pattern, voice, ids, !all) : mm::core::setSlide(pattern, voice, ids, !all);
    });
}

void EditableRoll::toggleAccent() {
    flipFlag(true);
}

void EditableRoll::toggleSlide() {
    flipFlag(false);
}

void EditableRoll::changeVelocity(int delta) {
    if (selection_.empty() || !haveView_) {
        return;
    }
    const std::vector<uint32_t> ids(selection_.begin(), selection_.end());
    const size_t voice = static_cast<size_t>(voice_ - 1);
    processor_.editNotes(static_cast<size_t>(slotIndex_), [&](mm::core::Pattern& pattern) -> size_t {
        if (voice >= pattern.voices.size()) {
            return 0;
        }
        std::vector<mm::core::VelocityChange> changes;
        for (const auto& note : pattern.voices[voice].notes) {
            if (std::find(ids.begin(), ids.end(), note.id) != ids.end()) {
                changes.push_back({note.id, mm::core::shiftVelocity(note.velocity, delta)});
            }
        }
        return mm::core::setVelocities(pattern, voice, changes);
    });
}

juce::PopupMenu EditableRoll::contextMenu() const {
    bool allAccent = !selection_.empty();
    bool allSlide = !selection_.empty();
    for (const uint32_t id : selection_) {
        if (const auto* note = find(id)) {
            allAccent = allAccent && note->accent;
            allSlide = allSlide && note->slide;
        }
    }
    juce::PopupMenu menu;
    menu.addItem(kMenuAccent, tr(mm::core::text::kRollAccent), !selection_.empty(), allAccent);
    menu.addItem(kMenuSlide, tr(mm::core::text::kRollSlide), !selection_.empty(), allSlide);
    menu.addSeparator();
    menu.addItem(kMenuDelete, tr(mm::core::text::kRollDelete), !selection_.empty());
    return menu;
}

void EditableRoll::runMenuAction(int id) {
    if (id == kMenuAccent) {
        toggleAccent();
    } else if (id == kMenuSlide) {
        toggleSlide();
    } else if (id == kMenuDelete) {
        deleteSelection();
    }
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

void EditableRoll::moveSelection(int64_t ticks, int pitchDirection, bool octave) {
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
            deltaPitch = octave ? pitchDirection * 12
                                : mm::core::stepPitch(anchor->pitch, pitchDirection, rules) - anchor->pitch;
        }
        return mm::core::moveNotes(pattern, voice, ids, static_cast<int32_t>(ticks), deltaPitch);
    });
}

void EditableRoll::selectAll() {
    selection_.clear();
    for (const auto& note : source_) {
        selection_.insert(note.id);
    }
    repaint();
}

void EditableRoll::deleteSelection() {
    removeIds(std::vector<uint32_t>(selection_.begin(), selection_.end()));
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

void EditableRoll::visibilityChanged() {
    if (!isVisible()) {
        endGesture();
        repaint();
    }
}

void EditableRoll::mouseDown(const juce::MouseEvent& event) {
    const auto point = (event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble();
    if (event.position.y >= static_cast<float>(field().getBottom())) {
        pressLane({point.x, point.y - field().getBottom()}, event.mods);
        return;
    }
    if (event.mods.isPopupMenu()) {
        // A right click on a note: the note joins the selection if it is not in it, then the menu opens.
        const RollHit hit = haveView_ ? mm::core::hitTest(geometry(), source_, point.x, point.y) : RollHit{};
        if (hit.id != 0) {
            if (selection_.count(hit.id) == 0) {
                selection_ = {hit.id};
                repaint();
            }
            contextMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                                        [safe = juce::Component::SafePointer<EditableRoll>(this)](int result) {
                                            if (safe != nullptr && result != 0) {
                                                safe->runMenuAction(result);
                                            }
                                        });
        }
        return;
    }
    if (event.getNumberOfClicks() >= 2) {
        doubleClick(point);
        return;
    }
    press(point, event.mods);
}

void EditableRoll::mouseDrag(const juce::MouseEvent& event) {
    auto point = (event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble();
    if (mode_ == Mode::Velocity || mode_ == Mode::Draw) {
        point.y -= field().getBottom();
    }
    drag(point);
}

void EditableRoll::mouseUp(const juce::MouseEvent& event) {
    auto point = (event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble();
    if (mode_ == Mode::Velocity || mode_ == Mode::Draw) {
        point.y -= field().getBottom();
    }
    release(point);
}

void EditableRoll::mouseMove(const juce::MouseEvent& event) {
    const auto point = (event.position - juce::Point<float>(static_cast<float>(kGutter), 0.0f)).toDouble();
    if (event.position.y >= static_cast<float>(field().getBottom())) {
        const bool bar = haveView_ && mm::core::hitVelocityBar(geometry(), source_, point.x) != 0;
        setMouseCursor(bar ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        return;
    }
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
        for (size_t index = 0; index < source_.size(); ++index) {
            const auto& note = source_[index];
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
            if (note.accent) {
                // A small triangle above the start of the note.
                juce::Path triangle;
                const float top = box.getY() - 7.0f;
                triangle.addTriangle(box.getX(), top, box.getX() + 7.0f, top, box.getX() + 3.5f, top + 6.0f);
                g.setColour(juce::Colours::white.withAlpha(alpha * 0.95f));
                g.fillPath(triangle);
            }
            if (note.slide) {
                // A line with a diamond from the end of the note to the start of the next one (or a short stub).
                const float y = box.getCentreY();
                const float from = box.getRight();
                const float to = index + 1 < source_.size()
                                     ? toRect(mm::core::noteBox(geo, source_[index + 1]), origin).getX()
                                     : from + 12.0f;
                g.setColour(juce::Colours::white.withAlpha(alpha * 0.85f));
                g.drawLine(from, y, std::max(to, from + 4.0f), y, 1.2f);
                juce::Path diamond;
                diamond.addQuadrilateral(from, y - 3.0f, from + 3.0f, y, from, y + 3.0f, from - 3.0f, y);
                g.fillPath(diamond);
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

    // The velocity lane: one bar per note, the accent ones brighter with a cap.
    {
        const auto lane = juce::Rectangle<int>(area.getX(), area.getBottom(), area.getWidth(), laneHeight());
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(lane);
        g.setColour(juce::Colours::black.withAlpha(0.22f));
        g.fillRect(lane);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine(lane.getY(), static_cast<float>(lane.getX()), static_cast<float>(lane.getRight()));
        for (const auto& note : source_) {
            auto box = toRect(mm::core::velocityBarBox(geo, note, static_cast<double>(lane.getHeight())),
                              {lane.getX(), lane.getY()});
            if (box.getRight() < static_cast<float>(lane.getX()) || box.getX() > static_cast<float>(lane.getRight())) {
                continue;
            }
            const bool selected = selection_.count(note.id) != 0;
            auto colour = note.accent ? accent_.brighter(0.8f) : accent_;
            if (selected) {
                colour = colour.brighter(0.4f);
            }
            g.setColour(colour.withAlpha(alpha * (selected ? 1.0f : 0.8f)));
            g.fillRect(box);
            if (note.accent) {
                g.setColour(juce::Colours::white.withAlpha(alpha * 0.9f));
                g.fillRect(box.withHeight(2.0f));
            }
            if (selected) {
                g.setColour(juce::Colours::white.withAlpha(alpha * 0.8f));
                g.drawRect(box, 1.0f);
            }
            if (hintValue_ != 0 && note.id == hintId_) {
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(11.0f));
                g.drawText(juce::String(hintValue_), juce::roundToInt(box.getX()) + 12, lane.getY() + 2, 30, 12,
                           juce::Justification::centredLeft);
            }
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
