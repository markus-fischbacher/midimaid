#include "plugin/LibraryDialog.h"

#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

namespace keys = mm::core::text;

namespace {

constexpr int kWidth = 760;
constexpr int kRowHeight = 40;
constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace

std::string_view libraryStatusKey(GenerationStatus status) {
    switch (status) {
    case GenerationStatus::LibrarySaved:
        return keys::kStatusLibrarySaved;
    case GenerationStatus::LibrarySetSaved:
        return keys::kStatusLibrarySetSaved;
    case GenerationStatus::LibraryNothingToSave:
        return keys::kStatusLibraryNothing;
    case GenerationStatus::LibraryFull:
        return keys::kStatusLibraryFull;
    case GenerationStatus::LibraryLoaded:
        return keys::kStatusLibraryLoaded;
    case GenerationStatus::LibrarySetLoaded:
        return keys::kStatusLibrarySetLoaded;
    case GenerationStatus::LibraryLoadFailed:
        return keys::kStatusLibraryLoadFailed;
    case GenerationStatus::LibraryImported:
        return keys::kStatusLibraryImported;
    case GenerationStatus::LibraryExported:
        return keys::kStatusLibraryExported;
    case GenerationStatus::LibraryExportFailed:
        return keys::kStatusLibraryExportFailed;
    default:
        return {};
    }
}

LibraryDialog::LibraryDialog(ProcessorBase& processor, std::shared_ptr<bool> processorAlive,
                             std::function<LibraryStore&()> store)
    : processor_(processor), processorAlive_(std::move(processorAlive)), store_(std::move(store)) {
    setLookAndFeel(&look_);
    summaryLabel_.setComponentID("summary");
    summaryLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    addAndMakeVisible(summaryLabel_);
    statusLabel_.setComponentID("status");
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addAndMakeVisible(statusLabel_);
    emptyLabel_.setComponentID("empty");
    emptyLabel_.setText(tr(keys::kLibraryEmpty), juce::dontSendNotification);
    emptyLabel_.setJustificationType(juce::Justification::centred);
    emptyLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    addChildComponent(emptyLabel_);

    nameEditor_.setJustification(juce::Justification::centredLeft);
    nameEditor_.setComponentID("name");
    nameEditor_.setTextToShowWhenEmpty(tr(keys::kLibraryNameHint), juce::Colours::white.withAlpha(0.45f));
    nameEditor_.setInputRestrictions(static_cast<int>(mm::core::kMaxLibraryNameBytes));
    addAndMakeVisible(nameEditor_);

    list_.setComponentID("list");
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(MidiMaidLookAndFeel::kSurface));
    addAndMakeVisible(list_);

    const auto setup = [this](juce::TextButton& button, const char* id, std::string_view key) {
        button.setComponentID(id);
        button.setButtonText(tr(key));
        addAndMakeVisible(button);
    };
    setup(saveSlotButton_, "saveSlot", keys::kLibrarySaveSlot);
    setup(saveSetButton_, "saveSet", keys::kLibrarySaveSet);
    setup(importButton_, "import", keys::kLibraryImport);
    setup(loadButton_, "load", keys::kLibraryLoad);
    setup(renameButton_, "rename", keys::kLibraryRename);
    setup(removeButton_, "remove", keys::kLibraryRemove);
    setup(exportButton_, "export", keys::kLibraryExport);
    setup(closeButton_, "close", keys::kButtonClose);

    saveSlotButton_.onClick = [this] {
        if (alive()) {
            processor_.saveToLibrary(nameEditor_.getText().toStdString());
            nameEditor_.clear();
        }
    };
    saveSetButton_.onClick = [this] {
        if (alive()) {
            processor_.saveSetToLibrary(nameEditor_.getText().toStdString());
            nameEditor_.clear();
        }
    };
    importButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(tr(keys::kLibraryImport), juce::File(), "*.mid;*.midi");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, alive = alive_](const juce::FileChooser& chooser) {
                                  if (*alive && chooser.getResult() != juce::File()) {
                                      importFile(chooser.getResult());
                                  }
                              });
    };
    loadButton_.onClick = [this] {
        if (const auto entry = selected(); entry && alive()) {
            processor_.loadFromLibrary(entry->id);
        }
    };
    renameButton_.onClick = [this] {
        if (const auto entry = selected()) {
            if (store_().rename(entry->id, nameEditor_.getText().toStdString())) {
                nameEditor_.clear();
            }
        }
    };
    removeButton_.onClick = [this] {
        const auto entry = selected();
        if (!entry) {
            return;
        }
        if (pendingRemove_ != entry->id) {
            pendingRemove_ = entry->id; // the first click asks, the second one removes
            updateButtons();
            return;
        }
        pendingRemove_.clear();
        store_().remove(entry->id);
    };
    exportButton_.onClick = [this] {
        const auto entry = selected();
        if (!entry || !alive()) {
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            tr(keys::kLibraryExport), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                          .getChildFile(juce::String::fromUTF8(entry->name.c_str()) + ".mid"),
            "*.mid");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, alive = alive_, id = entry->id](const juce::FileChooser& chooser) {
                                  if (*alive && this->alive() && chooser.getResult() != juce::File()) {
                                      processor_.exportLibraryEntry(id, chooser.getResult().withFileExtension("mid"));
                                  }
                              });
    };
    closeButton_.onClick = [this] {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) {
            window->exitModalState(0);
        }
    };

    store_().setOnChanged([this, alive = alive_] {
        if (*alive) {
            refresh();
        }
    });
    setSize(kWidth, 540);
    refresh();
    startTimerHz(4);
}

LibraryDialog::~LibraryDialog() {
    setLookAndFeel(nullptr);
    *alive_ = false;
    stopTimer();
    store_().setOnChanged({});
}

std::optional<mm::core::LibraryEntryInfo> LibraryDialog::selected() const {
    const int row = list_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return std::nullopt;
    }
    return entries_[static_cast<size_t>(row)];
}

void LibraryDialog::importFile(const juce::File& file) {
    if (alive()) {
        processor_.importMidiToLibrary(file);
    }
}

bool LibraryDialog::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) {
        const auto extension = juce::File(path).getFileExtension().toLowerCase();
        if (extension == ".mid" || extension == ".midi") {
            return true;
        }
    }
    return false;
}

void LibraryDialog::filesDropped(const juce::StringArray& files, int, int) {
    for (const auto& path : files) {
        const juce::File file(path);
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension == ".mid" || extension == ".midi") {
            importFile(file);
        }
    }
}

juce::String LibraryDialog::infoText(const mm::core::LibraryEntryInfo& info) const {
    const auto* style = alive() ? processor_.styles().find(info.styleId) : nullptr;
    const std::string styleName = style != nullptr ? localized(style->nameDe, style->nameEn) : info.styleId;
    const std::string scale = info.scaleId.empty() ? std::string("?") : tr(keys::scaleKey(info.scaleId)).toStdString();
    const std::string key = kKeyNames[info.root % 12];
    if (info.kind == mm::core::LibraryKind::Set) {
        return tr(keys::kLibraryInfoSet, {{"n", std::to_string(info.slotCount)}, {"style", styleName}, {"key", key}, {"scale", scale}});
    }
    return tr(keys::kLibraryInfoPattern, {{"style", styleName},
                                           {"key", key},
                                           {"scale", scale},
                                           {"length", std::to_string(info.lengthBars)},
                                           {"seed", std::to_string(info.seed)}});
}

void LibraryDialog::refresh() {
    const auto previous = selected();
    entries_ = store_().list();
    list_.updateContent();
    if (previous) {
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [&](const mm::core::LibraryEntryInfo& e) { return e.id == previous->id; });
        if (it != entries_.end()) {
            list_.selectRow(static_cast<int>(it - entries_.begin()), false, true);
        } else {
            list_.deselectAllRows();
        }
    }
    list_.repaint();
    summaryLabel_.setText(tr(keys::kLibrarySummary, {{"n", std::to_string(entries_.size())}}),
                          juce::dontSendNotification);
    emptyLabel_.setVisible(entries_.empty());
    updateButtons();
}

void LibraryDialog::updateButtons() {
    const auto entry = selected();
    if (!entry || pendingRemove_ != entry->id) {
        pendingRemove_.clear();
    }
    const bool has = entry.has_value();
    loadButton_.setEnabled(has);
    renameButton_.setEnabled(has);
    removeButton_.setEnabled(has);
    exportButton_.setEnabled(has && entry->kind == mm::core::LibraryKind::Pattern);
    removeButton_.setButtonText(tr(pendingRemove_.empty() ? keys::kLibraryRemove : keys::kLibraryRemoveConfirm));
}

void LibraryDialog::selectedRowsChanged(int) {
    updateButtons();
}

void LibraryDialog::timerCallback() {
    if (!alive()) {
        statusLabel_.setText({}, juce::dontSendNotification);
        return;
    }
    const auto key = libraryStatusKey(processor_.generationStatus());
    statusLabel_.setText(key.empty() ? juce::String() : tr(key), juce::dontSendNotification);
}

int LibraryDialog::getNumRows() {
    return static_cast<int>(entries_.size());
}

void LibraryDialog::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool isSelected) {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    const auto& entry = entries_[static_cast<size_t>(row)];
    if (isSelected) {
        g.fillAll(juce::Colour(MidiMaidLookAndFeel::kAccent).withAlpha(0.22f));
    }
    g.setColour(juce::Colour(MidiMaidLookAndFeel::kText));
    g.setFont(15.0f);
    g.drawText(juce::String::fromUTF8(entry.name.c_str()), 10, 3, width - 20, 18, juce::Justification::centredLeft);
    g.setColour(juce::Colour(MidiMaidLookAndFeel::kTextDim));
    g.setFont(12.5f);
    g.drawText(infoText(entry), 10, 21, width - 20, 16, juce::Justification::centredLeft);
    g.setColour(juce::Colour(MidiMaidLookAndFeel::kOutline));
    g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
}

void LibraryDialog::resized() {
    auto area = getLocalBounds().reduced(16);
    auto top = area.removeFromTop(30);
    importButton_.setBounds(top.removeFromRight(170));
    top.removeFromRight(8);
    summaryLabel_.setBounds(top);
    area.removeFromTop(6);
    auto bottom = area.removeFromBottom(32);
    closeButton_.setBounds(bottom.removeFromRight(110));
    bottom.removeFromRight(8);
    exportButton_.setBounds(bottom.removeFromRight(120));
    bottom.removeFromRight(8);
    removeButton_.setBounds(bottom.removeFromRight(130));
    bottom.removeFromRight(8);
    renameButton_.setBounds(bottom.removeFromRight(110));
    bottom.removeFromRight(8);
    loadButton_.setBounds(bottom.removeFromRight(90));
    area.removeFromBottom(8);
    statusLabel_.setBounds(area.removeFromBottom(24));
    area.removeFromBottom(6);
    auto save = area.removeFromBottom(32);
    saveSetButton_.setBounds(save.removeFromRight(140));
    save.removeFromRight(8);
    saveSlotButton_.setBounds(save.removeFromRight(140));
    save.removeFromRight(8);
    nameEditor_.setBounds(save);
    area.removeFromBottom(8);
    list_.setBounds(area);
    emptyLabel_.setBounds(area.reduced(30));
}

void LibraryDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(MidiMaidLookAndFeel::kBackground));
}

void LibraryDialog::show(ProcessorBase& processor, std::shared_ptr<bool> processorAlive) {
    static juce::Component::SafePointer<juce::DialogWindow> open;
    if (open != nullptr) {
        open->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(
        new LibraryDialog(processor, std::move(processorAlive), [] { return std::ref(libraryStore()); }));
    options.dialogTitle = tr(keys::kLibraryTitle);
    options.dialogBackgroundColour = juce::Colour(MidiMaidLookAndFeel::kBackground);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    open = options.launchAsync();
}

} // namespace mm::plugin
