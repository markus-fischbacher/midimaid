#pragma once

#include "core/EditGrid.h"
#include "core/RollEdit.h"
#include "plugin/ProcessorBase.h"
#include "plugin/RollView.h"

#include <map>
#include <set>

namespace mm::plugin {

/// The piano roll of one voice of the hub UI (SPEC 3.5, D-157): draws the source notes of the pattern and edits them
/// through `ProcessorBase::editNotes`. Click selects, drag in the empty rubber-bands, double click adds or deletes,
/// dragging a note moves the selection, dragging its right edge changes the length. Zoom with Cmd/Ctrl + wheel, scroll
/// with the wheel (Shift: sideways). One gesture is one undo step. The roll handles no keys (D-126, D-159).
///
/// All gesture methods take positions in the coordinates of the note field (the component without the key gutter), so
/// that the mouse handlers only translate and tests can call them directly.
class EditableRoll : public RollView {
public:
    EditableRoll(ProcessorBase& processor, int voice);

    /// Takes over what the roll shows and edits: null or a view without pattern empties it. The selection keeps the
    /// ids that still exist; another slot, voice or pattern length starts a fresh view (selection and window).
    void setSource(const ProcessorBase::VoiceView* view);
    void setEditSettings(mm::core::EditGrid grid, mm::core::PitchSnap snap);

    const std::vector<mm::core::RollNote>& source() const { return source_; }
    const std::set<uint32_t>& selection() const { return selection_; }
    const mm::core::RollViewport& viewport() const { return viewport_; }
    mm::core::RollGeometry geometry() const;
    mm::core::RollSnap snap() const;
    bool gestureActive() const { return mode_ != Mode::Idle; }
    /// True once the current gesture has made an undo step.
    bool gestureEdited() const { return stepped_; }

    void press(juce::Point<double> point, juce::ModifierKeys mods);
    /// A press in the velocity lane: `point.x` is the field x, `point.y` the height from the top of the lane. The
    /// drag and release that follow take lane positions too. Dragging a bar sets its velocity (a selection moves by the
    /// same delta); with Alt a stroke over empty lane sets every bar it crosses.
    void pressLane(juce::Point<double> point, juce::ModifierKeys mods);
    /// The position is in field coordinates, or in lane coordinates while a lane gesture runs.
    void drag(juce::Point<double> point);
    void release(juce::Point<double> point);
    void doubleClick(juce::Point<double> point);
    /// The editing actions the roll offers without a key (D-126: no predefined keys; the menu and, from v1.1, the keys
    /// the user assigns call them). Each is one undo step.
    void selectAll();
    void deleteSelection();
    /// Moves the selection as a block: `ticks` later (negative: earlier), and one pitch step up or down
    /// (`pitchDirection` +1 or -1, the next tone the snapping allows), or an octave with `octave`.
    void moveSelection(int64_t ticks, int pitchDirection = 0, bool octave = false);
    /// Accent and slide of the selection: all set takes them away, otherwise they are set (one undo step).
    void toggleAccent();
    void toggleSlide();
    /// Adds `delta` to the velocity of the selection (cut to 1-127), one undo step.
    void changeVelocity(int delta);
    /// The menu of a right click on a note and what its items do (`kMenu...`).
    juce::PopupMenu contextMenu() const;
    void runMenuAction(int id);
    /// The value shown next to the lane while a bar is dragged (0: none).
    int velocityHint() const { return hintValue_; }
    /// Zooms horizontally around the field position `x`; `factor` > 1 shows more.
    void zoomAt(double x, double factor);
    /// Scrolls by ticks and rows.
    void scrollBy(int64_t deltaTicks, int deltaRows);

    void paint(juce::Graphics& g) override;
    void resized() override;
    /// A roll that disappears ends the gesture it is in (a collapsed row, D-159).
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    static constexpr int kGutter = 34; ///< width of the key labels at the left
    static constexpr int kRowPixels = 7;
    static constexpr int kLaneHeight = 40; ///< the velocity lane under the notes
    static constexpr int kMenuAccent = 1;
    static constexpr int kMenuSlide = 2;
    static constexpr int kMenuDelete = 3;

private:
    enum class Mode { Idle, Moving, Resizing, Rubber, Velocity, Draw };

    juce::Rectangle<int> field() const;
    int laneHeight() const;
    void flipFlag(bool accent);
    void stepVelocity(double y);
    void stepDraw(juce::Point<double> point);
    const mm::core::RollNote* find(uint32_t id) const;
    int rowsForHeight() const;
    void endGesture();
    void stepMove(double dx, double dy);
    void stepResize(double dx);
    bool editSelection(const std::function<size_t(mm::core::Pattern&)>& change, bool merge);
    void removeIds(const std::vector<uint32_t>& ids);

    ProcessorBase& processor_;
    int voice_;
    int slotIndex_ = -1; // 0-based, -1 without pattern
    std::vector<mm::core::RollNote> source_;
    uint32_t patternTicks_ = 0;
    mm::core::PitchClass root_ = 9;
    std::string scaleId_;
    mm::core::EditGrid grid_;
    mm::core::PitchSnap pitchSnap_ = mm::core::PitchSnap::Scale;
    mm::core::RollViewport viewport_;
    bool haveView_ = false;
    std::set<uint32_t> selection_;

    Mode mode_ = Mode::Idle;
    juce::Point<double> pressPoint_;
    bool dragged_ = false;
    bool narrowOnRelease_ = false;
    bool shiftHeld_ = false;
    std::set<uint32_t> rubberBase_;
    juce::Point<double> rubberEnd_;
    mm::core::RollNote anchor_; // the grabbed note as it was when the gesture started
    std::optional<mm::core::RollNote> expected_;
    bool stepped_ = false;
    std::map<uint32_t, uint8_t> velocityStart_; // the velocities of the selection when a lane drag started
    juce::Point<double> lastLanePoint_;
    int hintValue_ = 0;
    uint32_t hintId_ = 0;
};

} // namespace mm::plugin
