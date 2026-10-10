#pragma once

#include "plugin/LibraryStore.h"
#include "plugin/ProcessorBase.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include <string_view>

namespace mm::plugin {

/// The text key for the outcome of a library action (`GenerationStatus::Library...`), or an empty view for any other
/// status. The editor's status line and the dialog show the same texts.
std::string_view libraryStatusKey(GenerationStatus status);

/// The view "Bibliothek" (SPEC 3.12, D-184): the entries of the library with name, style, key, length and seed; saving
/// the slot or all slots under a name, loading an entry into the slot, renaming and removing (a second click confirms),
/// the export of a pattern as a .mid file and the import of a .mid file (buttons, or dropped on the window). It acts
/// through the processor of the instance that opened it; when that instance is gone the buttons do nothing.
class LibraryDialog : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ListBoxModel,
                      private juce::Timer {
public:
    LibraryDialog(ProcessorBase& processor, std::shared_ptr<bool> processorAlive,
                  std::function<LibraryStore&()> store);
    ~LibraryDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /// Opens the dialog for this instance in a window of its own, or brings the open one to the front. Message thread.
    static void show(ProcessorBase& processor, std::shared_ptr<bool> processorAlive);

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRow) override;
    void timerCallback() override;

    bool alive() const { return *processorAlive_; }
    std::optional<mm::core::LibraryEntryInfo> selected() const;
    void refresh();
    void updateButtons();
    void importFile(const juce::File& file);
    juce::String infoText(const mm::core::LibraryEntryInfo& info) const;

    ProcessorBase& processor_;
    std::shared_ptr<bool> processorAlive_;
    std::function<LibraryStore&()> store_;
    std::vector<mm::core::LibraryEntryInfo> entries_;
    std::string pendingRemove_; ///< the entry whose removal waits for the second click
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    juce::Label summaryLabel_, statusLabel_, emptyLabel_;
    juce::TextEditor nameEditor_;
    juce::ListBox list_{"list", this};
    juce::TextButton saveSlotButton_, saveSetButton_, importButton_, loadButton_, renameButton_, removeButton_,
        exportButton_, closeButton_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryDialog)
};

} // namespace mm::plugin
