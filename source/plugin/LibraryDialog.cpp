#include "plugin/LibraryDialog.h"

#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"
#include "plugin/Favorites.h"
#include "plugin/GlobalSettingsStore.h"

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

    searchEditor_.setComponentID("search");
    searchEditor_.setTextToShowWhenEmpty(tr(keys::kLibrarySearch), juce::Colours::white.withAlpha(0.45f));
    searchEditor_.onTextChange = [this] { filterChanged(); };
    addAndMakeVisible(searchEditor_);
    filterBpmEditor_.setComponentID("filterBpm");
    filterBpmEditor_.setTextToShowWhenEmpty(tr(keys::kLibraryBpmFilter), juce::Colours::white.withAlpha(0.45f));
    filterBpmEditor_.setInputRestrictions(3, "0123456789");
    filterBpmEditor_.onTextChange = [this] { filterChanged(); };
    addAndMakeVisible(filterBpmEditor_);
    for (auto* box : {&filterStarsBox_, &filterTagBox_, &filterStyleBox_}) {
        box->onChange = [this] {
            if (!filling_) {
                filterChanged();
            }
        };
        addAndMakeVisible(*box);
    }
    filterStarsBox_.setComponentID("filterStars");
    filterTagBox_.setComponentID("filterTag");
    filterStyleBox_.setComponentID("filterStyle");
    tagsEditor_.setComponentID("tags");
    tagsEditor_.setTextToShowWhenEmpty(tr(keys::kLibraryTagsHint), juce::Colours::white.withAlpha(0.45f));
    tagsEditor_.onReturnKey = [this] { applyMeta(); };
    addAndMakeVisible(tagsEditor_);
    for (auto* editor : {&bpmLowEditor_, &bpmHighEditor_}) {
        editor->setInputRestrictions(3, "0123456789");
        editor->onReturnKey = [this] { applyMeta(); };
        addAndMakeVisible(*editor);
    }
    bpmLowEditor_.setComponentID("bpmLow");
    bpmLowEditor_.setTextToShowWhenEmpty(tr(keys::kLibraryBpmLow), juce::Colours::white.withAlpha(0.45f));
    bpmHighEditor_.setComponentID("bpmHigh");
    bpmHighEditor_.setTextToShowWhenEmpty(tr(keys::kLibraryBpmHigh), juce::Colours::white.withAlpha(0.45f));
    bpmDashLabel_.setText(juce::String::fromUTF8("\xe2\x80\x93"), juce::dontSendNotification);
    bpmDashLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(bpmDashLabel_);
    starsBox_.setComponentID("stars");
    starsBox_.addItem(tr(keys::kLibraryNoRating), 1);
    for (int stars = 1; stars <= 5; ++stars) {
        juce::String text;
        for (int i = 0; i < 5; ++i) {
            text += juce::String::fromUTF8(i < stars ? "\xe2\x98\x85" : "\xe2\x98\x86");
        }
        starsBox_.addItem(text, stars + 1);
    }
    starsBox_.setSelectedId(1, juce::dontSendNotification);
    starsBox_.onChange = [this] {
        if (!filling_) {
            applyMeta(); // a rating is one click
        }
    };
    addAndMakeVisible(starsBox_);

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
    setup(applyButton_, "applyMeta", keys::kLibraryApply);
    setup(previewButton_, "preview", keys::kButtonPreview);
    applyButton_.onClick = [this] { applyMeta(); };
    previewButton_.onClick = [this] {
        if (!alive()) {
            return;
        }
        if (processor_.libraryPreviewing()) {
            processor_.stopLibraryPreview();
        } else if (const auto entry = selected()) {
            processor_.previewLibraryEntry(entry->id);
        }
    };

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
                syncFavorite(entry->id);
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
        syncFavorite(entry->id);
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
    setSize(kWidth, 640);
    refresh();
    startTimerHz(4);
}

LibraryDialog::~LibraryDialog() {
    if (alive()) {
        processor_.stopLibraryPreview(); // the slot comes back when the dialog goes
    }
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
    juce::String extra;
    if (info.rating > 0) {
        extra += "  ";
        for (int i = 0; i < 5; ++i) {
            extra += juce::String::fromUTF8(i < info.rating ? "\xe2\x98\x85" : "\xe2\x98\x86");
        }
    }
    if (info.bpmHigh > 0) {
        extra += juce::String("  ") + (info.bpmLow == info.bpmHigh ? juce::String(info.bpmLow) : juce::String(info.bpmLow) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(info.bpmHigh)) + " BPM";
    }
    if (!info.tags.empty()) {
        extra += "  #" + juce::String::fromUTF8(mm::core::libraryTagsToText(info.tags).c_str()).replace(", ", "  #");
    }
    return infoTextBase(info) + extra;
}

juce::String LibraryDialog::infoTextBase(const mm::core::LibraryEntryInfo& info) const {
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
    const auto all = store_().list();
    fillFilterBoxes(); // from all entries: a tag that the filter hides is still one to choose
    entries_ = mm::core::filterLibrary(all, filter());
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
    emptyLabel_.setVisible(all.empty());
    fillDetail();
    updateButtons();
}

mm::core::LibraryFilter LibraryDialog::filter() const {
    mm::core::LibraryFilter result;
    result.text = searchEditor_.getText().trim().toStdString();
    result.minRating = std::max(filterStarsBox_.getSelectedId() - 1, 0);
    result.tag = filterTagBox_.getSelectedId() > 1 ? filterTagBox_.getText().toStdString() : std::string();
    result.styleId = filterStyleBox_.getSelectedId() > 1 ? filterStyleIds_[static_cast<size_t>(filterStyleBox_.getSelectedId() - 2)] : std::string();
    const auto bpm = filterBpmEditor_.getText().getIntValue();
    if (filterBpmEditor_.getText().isNotEmpty() && bpm > 0) {
        result.bpm = bpm;
    }
    return result;
}

void LibraryDialog::filterChanged() {
    if (!filling_) {
        refresh();
    }
}

void LibraryDialog::fillFilterBoxes() {
    filling_ = true;
    if (filterStarsBox_.getNumItems() == 0) {
        filterStarsBox_.addItem(tr(keys::kLibraryAllStars), 1);
        for (int stars = 1; stars <= 5; ++stars) {
            filterStarsBox_.addItem(juce::String(stars) + juce::String::fromUTF8("\xe2\x98\x85+"), stars + 1);
        }
        filterStarsBox_.setSelectedId(1, juce::dontSendNotification);
    }
    const auto all = store_().list();
    // tags: all those of the library, sorted without regard to case; the choice stays when it is still there
    const juce::String chosenTag = filterTagBox_.getSelectedId() > 1 ? filterTagBox_.getText() : juce::String();
    std::vector<std::string> tags;
    std::vector<std::string> styles;
    for (const auto& entry : all) {
        for (const auto& tag : entry.tags) {
            if (std::none_of(tags.begin(), tags.end(), [&](const std::string& t) { return juce::String(t).equalsIgnoreCase(juce::String(tag)); })) {
                tags.push_back(tag);
            }
        }
        if (std::find(styles.begin(), styles.end(), entry.styleId) == styles.end()) {
            styles.push_back(entry.styleId);
        }
    }
    std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) { return juce::String(a).compareIgnoreCase(juce::String(b)) < 0; });
    filterTagBox_.clear(juce::dontSendNotification);
    filterTagBox_.addItem(tr(keys::kLibraryAllTags), 1);
    int tagId = 1;
    for (size_t i = 0; i < tags.size(); ++i) {
        filterTagBox_.addItem(juce::String::fromUTF8(tags[i].c_str()), static_cast<int>(i) + 2);
        if (chosenTag.isNotEmpty() && juce::String::fromUTF8(tags[i].c_str()).equalsIgnoreCase(chosenTag)) {
            tagId = static_cast<int>(i) + 2;
        }
    }
    filterTagBox_.setSelectedId(tagId, juce::dontSendNotification);
    const std::string chosenStyle = filterStyleBox_.getSelectedId() > 1 && static_cast<size_t>(filterStyleBox_.getSelectedId() - 2) < filterStyleIds_.size()
                                        ? filterStyleIds_[static_cast<size_t>(filterStyleBox_.getSelectedId() - 2)]
                                        : std::string();
    std::sort(styles.begin(), styles.end());
    filterStyleIds_ = styles;
    filterStyleBox_.clear(juce::dontSendNotification);
    filterStyleBox_.addItem(tr(keys::kLibraryAllStyles), 1);
    int styleId = 1;
    for (size_t i = 0; i < styles.size(); ++i) {
        const auto* style = alive() ? processor_.styles().find(styles[i]) : nullptr;
        filterStyleBox_.addItem(style != nullptr ? juce::String(localized(style->nameDe, style->nameEn)) : juce::String(styles[i]),
                                static_cast<int>(i) + 2);
        if (styles[i] == chosenStyle) {
            styleId = static_cast<int>(i) + 2;
        }
    }
    filterStyleBox_.setSelectedId(styleId, juce::dontSendNotification);
    filling_ = false;
}

void LibraryDialog::fillDetail() {
    filling_ = true;
    const auto entry = selected();
    tagsEditor_.setText(entry ? juce::String::fromUTF8(mm::core::libraryTagsToText(entry->tags).c_str()) : juce::String(),
                        juce::dontSendNotification);
    bpmLowEditor_.setText(entry && entry->bpmHigh > 0 ? juce::String(entry->bpmLow) : juce::String(), juce::dontSendNotification);
    bpmHighEditor_.setText(entry && entry->bpmHigh > 0 ? juce::String(entry->bpmHigh) : juce::String(), juce::dontSendNotification);
    starsBox_.setSelectedId(entry ? entry->rating + 1 : 1, juce::dontSendNotification);
    for (auto* component : std::initializer_list<juce::Component*>{&tagsEditor_, &bpmLowEditor_, &bpmHighEditor_, &starsBox_, &applyButton_}) {
        component->setEnabled(entry.has_value());
    }
    filling_ = false;
}

void LibraryDialog::applyMeta() {
    const auto entry = selected();
    if (!entry || filling_) {
        return;
    }
    mm::core::LibraryMeta meta;
    meta.tags = mm::core::parseLibraryTags(tagsEditor_.getText().toStdString());
    meta.rating = static_cast<uint8_t>(std::max(starsBox_.getSelectedId() - 1, 0));
    meta.bpmLow = static_cast<uint16_t>(std::clamp(bpmLowEditor_.getText().getIntValue(), 0, 65535));
    meta.bpmHigh = static_cast<uint16_t>(std::clamp(bpmHighEditor_.getText().getIntValue(), 0, 65535));
    store_().setMeta(entry->id, meta);
    syncFavorite(entry->id);
}

void LibraryDialog::syncFavorite(const std::string& id) {
    updateFavorite(store_(), referenceStore(), id, globalSettings().ready() && globalSettings().get().favoritesToActiveSet);
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
    previewButton_.setEnabled(has || (alive() && processor_.libraryPreviewing()));
    removeButton_.setButtonText(tr(pendingRemove_.empty() ? keys::kLibraryRemove : keys::kLibraryRemoveConfirm));
}

void LibraryDialog::selectedRowsChanged(int) {
    fillDetail();
    updateButtons();
}

void LibraryDialog::timerCallback() {
    if (!alive()) {
        statusLabel_.setText({}, juce::dontSendNotification);
        return;
    }
    processor_.pollPreview();
    previewButton_.setButtonText(tr(processor_.libraryPreviewing() ? keys::kButtonPreviewStop : keys::kButtonPreview));
    previewButton_.setEnabled(selected().has_value() || processor_.libraryPreviewing());
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
    auto filterRow = area.removeFromTop(30);
    filterBpmEditor_.setBounds(filterRow.removeFromRight(70));
    filterRow.removeFromRight(6);
    filterStyleBox_.setBounds(filterRow.removeFromRight(140));
    filterRow.removeFromRight(6);
    filterTagBox_.setBounds(filterRow.removeFromRight(130));
    filterRow.removeFromRight(6);
    filterStarsBox_.setBounds(filterRow.removeFromRight(110));
    filterRow.removeFromRight(6);
    searchEditor_.setBounds(filterRow);
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
    auto detail = area.removeFromBottom(32);
    previewButton_.setBounds(detail.removeFromRight(100));
    detail.removeFromRight(6);
    applyButton_.setBounds(detail.removeFromRight(100));
    detail.removeFromRight(6);
    bpmHighEditor_.setBounds(detail.removeFromRight(62));
    bpmDashLabel_.setBounds(detail.removeFromRight(14));
    bpmLowEditor_.setBounds(detail.removeFromRight(62));
    detail.removeFromRight(6);
    starsBox_.setBounds(detail.removeFromRight(120));
    detail.removeFromRight(6);
    tagsEditor_.setBounds(detail);
    area.removeFromBottom(8);
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
