#pragma once

#include "core/EditGrid.h"
#include "core/RollEdit.h"
#include "plugin/ProcessorBase.h"
#include "plugin/RollView.h"

#include <set>

namespace mm::plugin {

/// The piano roll of one voice of the hub UI (SPEC 3.5, D-157): draws the source notes of the pattern and edits them
/// through `ProcessorBase::editNotes`. Click selects, drag in the empty rubber-bands, double click adds or deletes,
/// dragging a note moves the selection, dragging its right edge changes the length. Zoom with Cmd/Ctrl + wheel, scroll
/// with the wheel (Shift: sideways). One gesture is one undo step.
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
    void drag(juce::Point<double> point);
    void release(juce::Point<double> point);
    void doubleClick(juce::Point<double> point);
    /// Cmd/Ctrl+Z (Shift: redo), Delete, Cmd/Ctrl+A, the arrow keys. False for a key that is not ours.
    bool handleKey(const juce::KeyPress& key);
    /// Zooms horizontally around the field position `x`; `factor` > 1 shows more.
    void zoomAt(double x, double factor);
    /// Scrolls by ticks and rows.
    void scrollBy(int64_t deltaTicks, int deltaRows);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int kGutter = 34; ///< width of the key labels at the left
    static constexpr int kRowPixels = 7;

private:
    enum class Mode { Idle, Moving, Resizing, Rubber };

    juce::Rectangle<int> field() const;
    const mm::core::RollNote* find(uint32_t id) const;
    int rowsForHeight() const;
    void endGesture();
    void stepMove(double dx, double dy);
    void stepResize(double dx);
    bool editSelection(const std::function<size_t(mm::core::Pattern&)>& change, bool merge);
    void removeIds(const std::vector<uint32_t>& ids);
    void moveByKey(int64_t ticks, int pitchDirection, int pitchSteps);

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
};

} // namespace mm::plugin
