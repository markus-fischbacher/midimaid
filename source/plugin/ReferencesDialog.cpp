#include "plugin/ReferencesDialog.h"

#include "core/Library.h"
#include "core/ReferenceProfile.h"
#include "core/SetProfile.h"
#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

#include <algorithm>
#include <cmath>

namespace mm::plugin {

namespace keys = mm::core::text;

namespace {

constexpr int kWidth = 900;
constexpr int kRowHeight = 34;
constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

juce::String roleName(mm::core::TrackRole role) {
    switch (role) {
    case mm::core::TrackRole::Drums:
        return tr(keys::kReferencesRoleDrums);
    case mm::core::TrackRole::Chords:
        return tr(keys::kReferencesRoleChords);
    case mm::core::TrackRole::Bass:
        return tr(keys::kReferencesRoleBass);
    case mm::core::TrackRole::Melody:
        return tr(keys::kReferencesRoleMelody);
    }
    return {};
}

constexpr mm::core::TrackRole kRoles[4] = {mm::core::TrackRole::Bass, mm::core::TrackRole::Melody,
                                           mm::core::TrackRole::Chords, mm::core::TrackRole::Drums};

juce::String infoText(const mm::core::ReferenceItem& item) {
    const auto length = std::to_string(item.lengthBars);
    const auto notes = std::to_string(item.notes.size());
    juce::String text;
    if (item.role == mm::core::TrackRole::Drums) {
        text = tr(keys::kReferencesInfoDrums, {{"length", length}, {"n", notes}});
    } else {
        text = tr(keys::kReferencesInfo, {{"key", kKeyNames[item.analysis.key.root % 12]},
                                          {"scale", tr(keys::scaleKey(item.analysis.key.scaleId)).toStdString()},
                                          {"length", length},
                                          {"n", notes}});
    }
    if (mm::core::isRoleUnsure(item)) {
        text += " · " + tr(keys::kReferencesRoleUnsure,
                           {{"n", std::to_string(static_cast<int>(std::lround(item.roleConfidence * 100.0)))}});
    }
    return text;
}

} // namespace

/// One entry: name, role, info, weight, switch and the remove button.
class ReferencesDialog::RowComponent : public juce::Component {
public:
    RowComponent(ReferencesDialog& owner) : owner_(owner) {
        name_.setColour(juce::Label::textColourId, juce::Colour(MidiMaidLookAndFeel::kText));
        addAndMakeVisible(name_);
        info_.setColour(juce::Label::textColourId, juce::Colour(MidiMaidLookAndFeel::kTextDim));
        addAndMakeVisible(info_);
        role_.setComponentID("role");
        for (int i = 0; i < 4; ++i) {
            role_.addItem(roleName(kRoles[i]), i + 1);
        }
        role_.onChange = [this] {
            const auto index = role_.getSelectedId() - 1;
            if (!updating_ && index >= 0 && index < 4) {
                owner_.store_().setRole(owner_.styleId_, id_, kRoles[index]);
            }
        };
        addAndMakeVisible(role_);
        weight_.setComponentID("weight");
        for (int w = mm::core::kMinReferenceWeight; w <= mm::core::kMaxReferenceWeight; ++w) {
            weight_.addItem(tr(keys::kReferencesWeight, {{"n", std::to_string(w)}}), w);
        }
        weight_.onChange = [this] {
            if (!updating_ && weight_.getSelectedId() > 0) {
                owner_.store_().setWeight(owner_.styleId_, id_, weight_.getSelectedId());
            }
        };
        addAndMakeVisible(weight_);
        tags_.setComponentID("tags");
        tags_.setTextToShowWhenEmpty(tr(keys::kReferencesTagsHint), juce::Colours::white.withAlpha(0.45f));
        tags_.onReturnKey = [this] { applyTags(); };
        tags_.onFocusLost = [this] { applyTags(); };
        addAndMakeVisible(tags_);
        active_.setComponentID("active");
        active_.setButtonText(tr(keys::kReferencesActive));
        active_.onClick = [this] {
            if (!updating_) {
                owner_.store_().setActive(owner_.styleId_, id_, active_.getToggleState());
            }
        };
        addAndMakeVisible(active_);
        remove_.setComponentID("remove");
        remove_.setButtonText(tr(keys::kReferencesRemove));
        remove_.onClick = [this] { owner_.store_().remove(owner_.styleId_, id_); };
        addAndMakeVisible(remove_);
    }

    void applyTags() {
        if (!updating_ && !id_.empty()) {
            const auto tags = mm::core::parseLibraryTags(tags_.getText().toStdString());
            if (tags != shownTags_) {
                owner_.store_().setTags(owner_.styleId_, id_, tags);
            }
        }
    }

    void show(const mm::core::ReferenceItem& item) {
        updating_ = true;
        shownTags_ = item.tags;
        if (!tags_.hasKeyboardFocus(true) || id_ != item.id) {
            tags_.setText(juce::String::fromUTF8(mm::core::libraryTagsToText(item.tags).c_str()), juce::dontSendNotification);
        }
        id_ = item.id;
        name_.setText(juce::String::fromUTF8(item.name.c_str()), juce::dontSendNotification);
        info_.setText(infoText(item), juce::dontSendNotification);
        info_.setColour(juce::Label::textColourId, mm::core::isRoleUnsure(item) ? juce::Colours::orange.withAlpha(0.9f)
                                                                                : juce::Colour(MidiMaidLookAndFeel::kTextDim));
        for (int i = 0; i < 4; ++i) {
            if (kRoles[i] == item.role) {
                role_.setSelectedId(i + 1, juce::dontSendNotification);
            }
        }
        weight_.setSelectedId(item.weight, juce::dontSendNotification);
        active_.setToggleState(item.active, juce::dontSendNotification);
        setAlpha(item.active ? 1.0f : 0.55f);
        updating_ = false;
    }

    void resized() override {
        auto area = getLocalBounds().reduced(4, 3);
        remove_.setBounds(area.removeFromRight(90));
        area.removeFromRight(6);
        active_.setBounds(area.removeFromRight(56));
        weight_.setBounds(area.removeFromRight(100));
        area.removeFromRight(6);
        tags_.setBounds(area.removeFromRight(130));
        area.removeFromRight(6);
        role_.setBounds(area.removeFromRight(110));
        area.removeFromRight(6);
        name_.setBounds(area.removeFromLeft(area.getWidth() / 2));
        info_.setBounds(area);
    }

private:
    ReferencesDialog& owner_;
    std::string id_;
    bool updating_ = false;
    juce::Label name_, info_;
    juce::ComboBox role_, weight_;
    juce::TextEditor tags_;
    std::vector<std::string> shownTags_;
    juce::ToggleButton active_;
    juce::TextButton remove_;
};

ReferencesDialog::ReferencesDialog(std::function<ReferenceStore&()> store, std::string styleId, juce::String styleName)
    : store_(std::move(store)), styleId_(std::move(styleId)) {
    (void)styleName;
    setLookAndFeel(&look_);
    setBox_.setComponentID("set");
    setBox_.onChange = [this] {
        const auto index = static_cast<size_t>(setBox_.getSelectedId() - 1);
        if (!filling_ && index < setIds_.size()) {
            pendingDelete_ = false;
            store_().setActiveSet(styleId_, setIds_[index]);
            refresh();
        }
    };
    addAndMakeVisible(setBox_);
    setNameEditor_.setComponentID("setName");
    setNameEditor_.setTextToShowWhenEmpty(tr(keys::kReferencesSetNameHint), juce::Colours::white.withAlpha(0.45f));
    setNameEditor_.setInputRestrictions(static_cast<int>(mm::core::kMaxReferenceSetNameBytes));
    addAndMakeVisible(setNameEditor_);
    newSetButton_.setComponentID("newSet");
    newSetButton_.setButtonText(tr(keys::kReferencesSetNew));
    newSetButton_.onClick = [this] {
        if (!store_().createSet(styleId_, setNameEditor_.getText().toStdString()).empty()) {
            setNameEditor_.clear();
        }
        refresh();
    };
    addAndMakeVisible(newSetButton_);
    renameSetButton_.setComponentID("renameSet");
    renameSetButton_.setButtonText(tr(keys::kReferencesSetRename));
    renameSetButton_.onClick = [this] {
        if (store_().renameSet(styleId_, collection_.activeSetId, setNameEditor_.getText().toStdString())) {
            setNameEditor_.clear();
        }
        refresh();
    };
    addAndMakeVisible(renameSetButton_);
    deleteSetButton_.setComponentID("deleteSet");
    deleteSetButton_.setButtonText(tr(keys::kReferencesSetDelete));
    deleteSetButton_.onClick = [this] {
        if (!pendingDelete_) {
            pendingDelete_ = true; // the first click asks, the second one deletes
            refresh();
            return;
        }
        pendingDelete_ = false;
        store_().deleteSet(styleId_, collection_.activeSetId);
        refresh();
    };
    addAndMakeVisible(deleteSetButton_);
    fromPoolButton_.setComponentID("fromPool");
    fromPoolButton_.setButtonText(tr(keys::kReferencesFromPool));
    fromPoolButton_.onClick = [this] { showPoolMenu(); };
    addAndMakeVisible(fromPoolButton_);

    summaryLabel_.setComponentID("summary");
    summaryLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    addAndMakeVisible(summaryLabel_);
    statusLabel_.setComponentID("status");
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
    addAndMakeVisible(statusLabel_);
    profileLabel_.setComponentID("profile");
    profileLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
    addAndMakeVisible(profileLabel_);
    emptyLabel_.setComponentID("empty");
    emptyLabel_.setText(tr(keys::kReferencesEmpty), juce::dontSendNotification);
    emptyLabel_.setJustificationType(juce::Justification::centred);
    emptyLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    addChildComponent(emptyLabel_);
    list_.setComponentID("list");
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(MidiMaidLookAndFeel::kSurface));
    addAndMakeVisible(list_);

    addFilesButton_.setComponentID("addFiles");
    addFilesButton_.setButtonText(tr(keys::kReferencesAddFiles));
    addFilesButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(tr(keys::kReferencesAddFiles), juce::File(), "*.mid;*.midi");
        const auto alive = alive_;
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, alive](const juce::FileChooser& chooser) {
                                  if (*alive) {
                                      std::vector<juce::File> paths;
                                      for (const auto& file : chooser.getResults()) {
                                          paths.push_back(file);
                                      }
                                      importPaths(std::move(paths));
                                  }
                              });
    };
    addAndMakeVisible(addFilesButton_);
    addFolderButton_.setComponentID("addFolder");
    addFolderButton_.setButtonText(tr(keys::kReferencesAddFolder));
    addFolderButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(tr(keys::kReferencesAddFolder), juce::File());
        const auto alive = alive_;
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, alive](const juce::FileChooser& chooser) {
                                  if (*alive) {
                                      std::vector<juce::File> paths;
                                      for (const auto& file : chooser.getResults()) {
                                          paths.push_back(file);
                                      }
                                      importPaths(std::move(paths));
                                  }
                              });
    };
    addAndMakeVisible(addFolderButton_);
    cancelButton_.setComponentID("cancel");
    cancelButton_.setButtonText(tr(keys::kReferencesCancel));
    cancelButton_.onClick = [this] { store_().cancelImport(); };
    addChildComponent(cancelButton_);
    closeButton_.setComponentID("close");
    closeButton_.setButtonText(tr(keys::kButtonClose));
    closeButton_.onClick = [this] {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) {
            window->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);

    store_().setOnChanged([this, alive = alive_] {
        if (*alive) {
            refresh();
        }
    });
    setSize(kWidth, 560);
    refresh();
}

ReferencesDialog::~ReferencesDialog() {
    setLookAndFeel(nullptr);
    *alive_ = false;
    store_().setOnChanged({});
}

void ReferencesDialog::importPaths(std::vector<juce::File> paths) {
    if (!paths.empty()) {
        store_().importFiles(styleId_, std::move(paths));
        refresh();
    }
}

bool ReferencesDialog::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) {
        const juce::File file(path);
        const auto extension = file.getFileExtension().toLowerCase();
        if (file.isDirectory() || extension == ".mid" || extension == ".midi") {
            return true;
        }
    }
    return false;
}

void ReferencesDialog::filesDropped(const juce::StringArray& files, int, int) {
    std::vector<juce::File> paths;
    for (const auto& path : files) {
        const juce::File file(path);
        const auto extension = file.getFileExtension().toLowerCase();
        if (file.isDirectory() || extension == ".mid" || extension == ".midi") {
            paths.push_back(file);
        }
    }
    importPaths(std::move(paths));
}

juce::String ReferencesDialog::summaryText(const mm::core::ReferenceSet& set) const {
    auto text = tr(keys::kReferencesSummary, {{"n", std::to_string(set.items.size())},
                                              {"m", std::to_string(mm::core::activeCount(set))}});
    juce::StringArray roles;
    for (const auto role : kRoles) {
        if (const auto count = mm::core::countOfRole(set, role); count > 0) {
            roles.add(roleName(role) + " " + juce::String(static_cast<int>(count)));
        }
    }
    if (!roles.isEmpty()) {
        text += " (" + roles.joinIntoString(", ") + ")";
    }
    if (const auto unsure = mm::core::unsureRoleCount(set); unsure > 0) {
        text += ", " + tr(keys::kReferencesSummaryUnsure, {{"n", std::to_string(unsure)}});
    }
    return text;
}

juce::String ReferencesDialog::profileText(const mm::core::ReferenceSet& set) const {
    const auto profile = mm::core::profileOf(set);
    if (!profile.valid) {
        return set.items.empty() ? juce::String()
                                 : tr(keys::kReferencesProfileTooSmall,
                                      {{"n", std::to_string(mm::core::kMinActiveForProgressions)}});
    }
    const auto facts = mm::core::factsOf(profile);
    const auto tenth = [](int hundredths) {
        return std::to_string(hundredths / 100) + "." + std::to_string(hundredths % 100 / 10);
    };
    const auto density = tr(keys::kReferencesProfileDensity,
                            {{"n", tenth(facts.bassNotesPerBar100)}, {"m", tenth(facts.melodyNotesPerBar100)}});
    const auto scale = facts.scaleId.empty() ? juce::String("-")
                                             : tr(keys::scaleKey(facts.scaleId)) + " " + juce::String(facts.scalePercent) + " %";
    return tr(keys::kReferencesProfile, {{"list", facts.progression.empty() ? "-" : facts.progression},
                                         {"value", density.toStdString()},
                                         {"scale", scale.toStdString()},
                                         {"n", std::to_string(facts.cells)}});
}

juce::String ReferencesDialog::statusText(const ReferenceStore::Progress& progress) const {
    if (progress.running) {
        return tr(keys::kReferencesProgress,
                  {{"n", std::to_string(progress.done)}, {"m", std::to_string(progress.total)}});
    }
    const auto& report = progress.report;
    if (progress.total == 0) {
        return {};
    }
    juce::StringArray more;
    const auto add = [&](size_t count, std::string_view key) {
        if (count > 0) {
            more.add(tr(key, {{"n", std::to_string(count)}}));
        }
    };
    add(report.duplicates, keys::kReferencesDuplicates);
    add(report.notFourFour, keys::kReferencesNotFourFour);
    add(report.unreadable, keys::kReferencesUnreadable);
    add(report.tooSmall, keys::kReferencesTooSmall);
    add(report.overLimit, keys::kReferencesOverLimit);
    add(report.truncated, keys::kReferencesTruncated);
    const auto added = std::to_string(report.added);
    return more.isEmpty() ? tr(keys::kReferencesReport, {{"n", added}})
                          : tr(keys::kReferencesReportMore, {{"n", added}, {"list", more.joinIntoString(", ").toStdString()}});
}

void ReferencesDialog::fillSetBox() {
    filling_ = true;
    setIds_.clear();
    setBox_.clear(juce::dontSendNotification);
    int selected = 0;
    for (const auto& set : collection_.sets) {
        setIds_.push_back(set.id);
        const int id = static_cast<int>(setIds_.size());
        setBox_.addItem(set.id == mm::core::kFavoritesSetId ? tr(keys::kReferencesSetFavorites)
                                                            : juce::String::fromUTF8(set.name.c_str()),
                        id);
        if (set.id == collection_.activeSetId) {
            selected = id;
        }
    }
    setBox_.setSelectedId(selected, juce::dontSendNotification);
    filling_ = false;
}

void ReferencesDialog::showPoolMenu() {
    // the items of the pool that the active set does not hold: they go in with one click (a file is in the pool once)
    juce::PopupMenu menu;
    std::vector<std::string> ids;
    const auto* active = mm::core::findSet(collection_, collection_.activeSetId);
    for (const auto& item : collection_.pool) {
        const bool in = active != nullptr && std::any_of(active->members.begin(), active->members.end(),
                                                         [&](const auto& m) { return m.id == item.id; });
        if (!in && ids.size() < 60) {
            ids.push_back(item.id);
            menu.addItem(static_cast<int>(ids.size()), juce::String::fromUTF8(item.name.c_str()));
        }
    }
    if (ids.empty()) {
        menu.addItem(1, tr(keys::kReferencesFromPoolNone), false);
    }
    const auto alive = alive_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fromPoolButton_),
                       [this, alive, ids](int result) {
                           if (*alive && result >= 1 && static_cast<size_t>(result) <= ids.size()) {
                               store_().addToSet(styleId_, collection_.activeSetId, ids[static_cast<size_t>(result) - 1]);
                           }
                       });
}

void ReferencesDialog::refresh() {
    collection_ = store_().collection(styleId_);
    set_ = mm::core::activeView(collection_);
    fillSetBox();
    const bool favorites = collection_.activeSetId == mm::core::kFavoritesSetId;
    renameSetButton_.setEnabled(!favorites);
    deleteSetButton_.setEnabled(!favorites);
    deleteSetButton_.setButtonText(pendingDelete_ && !favorites ? tr(keys::kLibraryRemoveConfirm) : tr(keys::kReferencesSetDelete));
    if (favorites) {
        pendingDelete_ = false;
    }
    list_.updateContent();
    for (int row = 0; row < static_cast<int>(set_.items.size()); ++row) {
        if (auto* component = dynamic_cast<RowComponent*>(list_.getComponentForRowNumber(row))) {
            component->show(set_.items[static_cast<size_t>(row)]);
        }
    }
    list_.repaint();
    const auto progress = store_().progress();
    summaryLabel_.setText(summaryText(set_), juce::dontSendNotification);
    profileLabel_.setText(profileText(set_), juce::dontSendNotification);
    statusLabel_.setText(statusText(progress), juce::dontSendNotification);
    emptyLabel_.setVisible(set_.items.empty());
    cancelButton_.setVisible(progress.running);
    addFilesButton_.setEnabled(!progress.running);
    addFolderButton_.setEnabled(!progress.running);
}

int ReferencesDialog::getNumRows() {
    return static_cast<int>(set_.items.size());
}

void ReferencesDialog::paintListBoxItem(int, juce::Graphics& g, int width, int height, bool) {
    g.setColour(juce::Colour(MidiMaidLookAndFeel::kOutline));
    g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
}

juce::Component* ReferencesDialog::refreshComponentForRow(int row, bool, juce::Component* existing) {
    if (row < 0 || row >= static_cast<int>(set_.items.size())) {
        delete existing;
        return nullptr;
    }
    auto* component = dynamic_cast<RowComponent*>(existing);
    if (component == nullptr) {
        delete existing;
        component = new RowComponent(*this);
    }
    component->show(set_.items[static_cast<size_t>(row)]);
    return component;
}

void ReferencesDialog::resized() {
    auto area = getLocalBounds().reduced(16);
    auto setRow = area.removeFromTop(32);
    setBox_.setBounds(setRow.removeFromLeft(200));
    setRow.removeFromLeft(8);
    deleteSetButton_.setBounds(setRow.removeFromRight(130));
    setRow.removeFromRight(8);
    renameSetButton_.setBounds(setRow.removeFromRight(120));
    setRow.removeFromRight(8);
    newSetButton_.setBounds(setRow.removeFromRight(120));
    setRow.removeFromRight(8);
    setNameEditor_.setBounds(setRow);
    area.removeFromTop(8);
    summaryLabel_.setBounds(area.removeFromTop(26));
    profileLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    auto bottom = area.removeFromBottom(32);
    closeButton_.setBounds(bottom.removeFromRight(110));
    bottom.removeFromRight(8);
    cancelButton_.setBounds(bottom.removeFromRight(110));
    addFilesButton_.setBounds(bottom.removeFromLeft(120));
    bottom.removeFromLeft(8);
    addFolderButton_.setBounds(bottom.removeFromLeft(120));
    bottom.removeFromLeft(8);
    fromPoolButton_.setBounds(bottom.removeFromLeft(170));
    area.removeFromBottom(8);
    statusLabel_.setBounds(area.removeFromBottom(24));
    area.removeFromBottom(6);
    list_.setBounds(area);
    emptyLabel_.setBounds(area.reduced(30));
}

void ReferencesDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(MidiMaidLookAndFeel::kBackground));
}

void ReferencesDialog::show(const std::string& styleId, const juce::String& styleName) {
    static juce::Component::SafePointer<juce::DialogWindow> open;
    if (open != nullptr) {
        open->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(
        new ReferencesDialog([] { return std::ref(referenceStore()); }, styleId, styleName));
    options.dialogTitle = tr(keys::kReferencesTitle, {{"name", styleName.toStdString()}});
    options.dialogBackgroundColour = juce::Colour(MidiMaidLookAndFeel::kBackground);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    open = options.launchAsync();
}

} // namespace mm::plugin
