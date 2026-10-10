#pragma once

#include "plugin/ReferenceStore.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>

namespace mm::plugin {

/// The view "Referenzen" (SPEC 3.20, D-181): the set of one style with the role, weight and on/off switch of every
/// entry, a summary, the progress of the background analysis and the import of files and folders (buttons or drag and
/// drop). Every change goes to the store at once and is saved by it; there is no OK button. The dialog works on the
/// store and does not depend on an editor.
class ReferencesDialog : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::ListBoxModel {
public:
    ReferencesDialog(std::function<ReferenceStore&()> store, std::string styleId, juce::String styleName);
    ~ReferencesDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /// Opens the dialog for the style in a window of its own, or brings the open one to the front. Message thread.
    static void show(const std::string& styleId, const juce::String& styleName);

private:
    class RowComponent;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;

    void refresh();
    void importPaths(std::vector<juce::File> paths);
    juce::String summaryText(const mm::core::ReferenceSet& set) const;
    juce::String statusText(const ReferenceStore::Progress& progress) const;

    std::function<ReferenceStore&()> store_;
    std::string styleId_;
    mm::core::ReferenceSet set_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    juce::Label summaryLabel_, statusLabel_, emptyLabel_;
    juce::ListBox list_{"list", this};
    juce::TextButton addFilesButton_, addFolderButton_, cancelButton_, closeButton_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReferencesDialog)
};

} // namespace mm::plugin
