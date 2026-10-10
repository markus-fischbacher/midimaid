#include "plugin/ReferencesDialog.h"

#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

#include <algorithm>

namespace mm::plugin {

namespace keys = mm::core::text;

namespace {

constexpr int kWidth = 760;
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
    if (item.role == mm::core::TrackRole::Drums) {
        return tr(keys::kReferencesInfoDrums, {{"length", length}, {"n", notes}});
    }
    return tr(keys::kReferencesInfo, {{"key", kKeyNames[item.analysis.key.root % 12]},
                                      {"scale", tr(keys::scaleKey(item.analysis.key.scaleId)).toStdString()},
                                      {"length", length},
                                      {"n", notes}});
}

} // namespace

/// One entry: name, role, info, weight, switch and the remove button.
class ReferencesDialog::RowComponent : public juce::Component {
public:
    RowComponent(ReferencesDialog& owner) : owner_(owner) {
        name_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(name_);
        info_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
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

    void show(const mm::core::ReferenceItem& item) {
        updating_ = true;
        id_ = item.id;
        name_.setText(juce::String::fromUTF8(item.name.c_str()), juce::dontSendNotification);
        info_.setText(infoText(item), juce::dontSendNotification);
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
    juce::ToggleButton active_;
    juce::TextButton remove_;
};

ReferencesDialog::ReferencesDialog(std::function<ReferenceStore&()> store, std::string styleId, juce::String styleName)
    : store_(std::move(store)), styleId_(std::move(styleId)) {
    (void)styleName;
    summaryLabel_.setComponentID("summary");
    summaryLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    addAndMakeVisible(summaryLabel_);
    statusLabel_.setComponentID("status");
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
    addAndMakeVisible(statusLabel_);
    emptyLabel_.setComponentID("empty");
    emptyLabel_.setText(tr(keys::kReferencesEmpty), juce::dontSendNotification);
    emptyLabel_.setJustificationType(juce::Justification::centred);
    emptyLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    addChildComponent(emptyLabel_);
    list_.setComponentID("list");
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff222228));
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
    setSize(kWidth, 520);
    refresh();
}

ReferencesDialog::~ReferencesDialog() {
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
    return roles.isEmpty() ? text : text + " (" + roles.joinIntoString(", ") + ")";
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

void ReferencesDialog::refresh() {
    set_ = store_().get(styleId_);
    list_.updateContent();
    for (int row = 0; row < static_cast<int>(set_.items.size()); ++row) {
        if (auto* component = dynamic_cast<RowComponent*>(list_.getComponentForRowNumber(row))) {
            component->show(set_.items[static_cast<size_t>(row)]);
        }
    }
    list_.repaint();
    const auto progress = store_().progress();
    summaryLabel_.setText(summaryText(set_), juce::dontSendNotification);
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
    g.setColour(juce::Colours::white.withAlpha(0.06f));
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
    summaryLabel_.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    auto bottom = area.removeFromBottom(32);
    closeButton_.setBounds(bottom.removeFromRight(110));
    bottom.removeFromRight(8);
    cancelButton_.setBounds(bottom.removeFromRight(110));
    addFilesButton_.setBounds(bottom.removeFromLeft(120));
    bottom.removeFromLeft(8);
    addFolderButton_.setBounds(bottom.removeFromLeft(120));
    area.removeFromBottom(8);
    statusLabel_.setBounds(area.removeFromBottom(24));
    area.removeFromBottom(6);
    list_.setBounds(area);
    emptyLabel_.setBounds(area.reduced(30));
}

void ReferencesDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1b1b1f));
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
    options.dialogBackgroundColour = juce::Colour(0xff1b1b1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    open = options.launchAsync();
}

} // namespace mm::plugin
