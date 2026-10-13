#include "ai/MockProvider.h"
#include "core/Shortcuts.h"
#include "core/TextKeys.h"
#include "plugin/MidiMaidLookAndFeel.h"
#include "plugin/AiBackend.h"
#include "plugin/AiConnection.h"
#include "plugin/EmbeddedTranslation.h"
#include "plugin/KeyChords.h"
#include "plugin/ProcessorBase.h"
#include "plugin/SettingsDialog.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <mutex>

using namespace mm::plugin;
namespace keys = mm::core::text;

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 10000) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) {
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

void pumpFor(int ms) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

class StubClient : public mm::ai::IHttpClient {
public:
    mm::ai::HttpResponse reply;
    mm::ai::HttpResponse send(const mm::ai::HttpRequest& request, const mm::ai::CancellationToken&) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
        return reply;
    }
    size_t count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return requests_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<mm::ai::HttpRequest> requests_;
};

struct Env {
    juce::TemporaryFile file;
    GlobalSettingsStore store{file.getFile()};
    std::shared_ptr<mm::platform::MemorySecretStore> secrets = std::make_shared<mm::platform::MemorySecretStore>();
    std::shared_ptr<StubClient> http = std::make_shared<StubClient>();
    ScopedAiBackend scoped{{}};
    std::unique_ptr<AiConnection> connection;

    Env() {
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        AiConnection::Dependencies dependencies;
        dependencies.settings = [this]() -> GlobalSettingsStore& { return store; };
        dependencies.secrets = secrets;
        dependencies.http = http;
        connection = std::make_unique<AiConnection>(dependencies);
    }
    std::unique_ptr<SettingsDialog> dialog() {
        return std::make_unique<SettingsDialog>(*connection, [this]() -> GlobalSettingsStore& { return store; });
    }
    mm::core::AiSettings ai() const { return store.get().ai; }
};

juce::Component* findById(juce::Component& parent, const juce::String& id) {
    for (auto* component : parent.getChildren()) {
        if (component->getComponentID() == id) {
            return component;
        }
        if (auto* deeper = findById(*component, id)) {
            return deeper;
        }
    }
    return nullptr;
}

template <typename T> T& part(juce::Component& parent, const juce::String& id) {
    auto* found = dynamic_cast<T*>(findById(parent, id));
    REQUIRE(found != nullptr);
    return *found;
}

/// Chooses a provider in the combo box like a click would (by its name).
void choose(SettingsDialog& dialog, const juce::String& name) {
    auto& box = part<juce::ComboBox>(dialog, "provider");
    for (int i = 0; i < box.getNumItems(); ++i) {
        if (box.getItemText(i) == name) {
            box.setSelectedItemIndex(i, juce::sendNotificationSync);
            return;
        }
    }
    FAIL("no such provider: " << name.toStdString());
}

bool backendHas(const std::string& id) {
    const auto settings = AiBackend::instance().get();
    return settings.provider != nullptr && settings.provider->info().id == id;
}

} // namespace

TEST_CASE("the dialog starts with the AI switched off and shows only the provider", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& box = part<juce::ComboBox>(*dialog, "provider");
    CHECK(box.getText() == tr(keys::kProviderOffline));
    CHECK(box.getNumItems() == 7); // off + the six of models.json
    CHECK_FALSE(part<juce::TextEditor>(*dialog, "key").isVisible());
    CHECK_FALSE(part<juce::ComboBox>(*dialog, "model").isVisible());
    CHECK(part<juce::TextButton>(*dialog, "close").isVisible());
}

TEST_CASE("choosing a provider activates it and shows its defaults", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "Anthropic (Claude)");
    CHECK(env.ai().activeProvider == "anthropic");
    CHECK(part<juce::TextEditor>(*dialog, "key").isVisible());
    CHECK(part<juce::ComboBox>(*dialog, "model").getText() == "claude-sonnet-5-5");
    CHECK(part<juce::TextEditor>(*dialog, "url").getText() == "https://api.anthropic.com");
    CHECK(part<juce::TextEditor>(*dialog, "timeout").getText().isEmpty()); // empty: the default
    CHECK(part<juce::TextEditor>(*dialog, "maxTokens").getText().isEmpty());
    REQUIRE(pumpUntil([&] { return backendHas("anthropic"); }));
    REQUIRE(pumpUntil([&] { return part<juce::Label>(*dialog, "keyStatus").getText() == tr(keys::kKeyMissing); }));
}

TEST_CASE("switching back to off clears the backend", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "Ollama (lokal)");
    REQUIRE(pumpUntil([&] { return backendHas("ollama"); }));
    choose(*dialog, tr(keys::kProviderOffline));
    CHECK(env.ai().activeProvider.empty());
    CHECK_FALSE(AiBackend::instance().active());
    CHECK_FALSE(part<juce::TextEditor>(*dialog, "key").isVisible());
}

TEST_CASE("a key is typed masked, goes to the keychain and leaves the screen", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "OpenAI (ChatGPT)");
    auto& key = part<juce::TextEditor>(*dialog, "key");
    CHECK(key.getPasswordCharacter() != 0);
    key.setText("  sk-DIALOG-KEY  ", false);
    part<juce::TextButton>(*dialog, "keySave").onClick();
    REQUIRE(pumpUntil([&] { return env.secrets->get("openai").has_value(); }));
    CHECK(env.secrets->get("openai") == "sk-DIALOG-KEY"); // trimmed
    REQUIRE(pumpUntil([&] { return key.getText().isEmpty(); }));
    // The memory store of the test is no keychain: the dialog says so.
    REQUIRE(pumpUntil([&] { return part<juce::Label>(*dialog, "keyStatus").getText() == tr(keys::kKeyMemoryOnly); }));
    env.store.waitForWrites();
    CHECK_FALSE(env.file.getFile().loadFileAsString().contains("sk-DIALOG-KEY"));
    REQUIRE(pumpUntil([&] { return backendHas("openai"); })); // saving the key of the active provider applies it
}

TEST_CASE("an empty key is not saved, the key can be removed again", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "OpenAI (ChatGPT)");
    part<juce::TextButton>(*dialog, "keySave").onClick();
    pumpFor(200);
    CHECK_FALSE(env.secrets->get("openai").has_value());

    env.secrets->set("openai", "sk-X");
    part<juce::TextButton>(*dialog, "keyRemove").onClick();
    REQUIRE(pumpUntil([&] { return !env.secrets->get("openai").has_value(); }));
    REQUIRE(pumpUntil([&] {
        return part<juce::Label>(*dialog, "keyStatus").getText() == tr(keys::kKeyMissing) ||
               part<juce::Label>(*dialog, "keyStatus").getText() == tr(keys::kKeyRemoved);
    }));
}

TEST_CASE("a key that was saved earlier is announced when the dialog opens", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.secrets->set("anthropic", "sk-ant-X");
    env.store.update([](mm::core::GlobalSettings& settings) { settings.ai.activeProvider = "anthropic"; });
    const auto dialog = env.dialog();
    REQUIRE(pumpUntil([&] { return part<juce::Label>(*dialog, "keyStatus").getText() == tr(keys::kKeyMemoryOnly); }));
}

TEST_CASE("a provider without key says so", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "Ollama (lokal)");
    CHECK(part<juce::Label>(*dialog, "keyStatus").getText() == tr(keys::kKeyNotRequired));
}

TEST_CASE("the model is saved, the recommended one is the default", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "Anthropic (Claude)");
    auto& model = part<juce::ComboBox>(*dialog, "model");
    model.setText("claude-opus-5-5", juce::sendNotificationSync);
    CHECK(env.ai().providers.at("anthropic").model == "claude-opus-5-5");
    model.setText("claude-sonnet-5-5", juce::sendNotificationSync);
    CHECK(env.ai().providers.at("anthropic").model.empty()); // back to the default of models.json
    model.setText("  my-own-model ", juce::sendNotificationSync);
    CHECK(env.ai().providers.at("anthropic").model == "my-own-model");
}

TEST_CASE("time limit and maximum tokens are kept within their limits", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "Anthropic (Claude)");
    auto& timeout = part<juce::TextEditor>(*dialog, "timeout");
    auto& tokens = part<juce::TextEditor>(*dialog, "maxTokens");
    timeout.setText("1", false);
    tokens.setText("100000", false);
    timeout.onFocusLost();
    CHECK(env.ai().providers.at("anthropic").timeoutSeconds == 5);
    CHECK(env.ai().providers.at("anthropic").maxTokens == 64000);
    CHECK(timeout.getText() == "5"); // the field shows what was kept
    CHECK(tokens.getText() == "64000");
    timeout.setText("", false);
    tokens.setText("", false);
    tokens.onReturnKey();
    CHECK(env.ai().providers.at("anthropic").timeoutSeconds == 0);
    CHECK(env.ai().providers.at("anthropic").maxTokens == 0);
    timeout.setText("90", false);
    timeout.onReturnKey();
    REQUIRE(pumpUntil([&] { return AiBackend::instance().get().timeoutSeconds == 90; }));
}

TEST_CASE("the address is saved unless it is the default", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "LM Studio (lokal)");
    auto& url = part<juce::TextEditor>(*dialog, "url");
    CHECK(url.getText() == "http://localhost:1234/v1");
    url.setText("http://localhost:5555/v1", false);
    url.onReturnKey();
    CHECK(env.ai().providers.at("lmstudio").baseUrl == "http://localhost:5555/v1");
    url.setText("http://localhost:1234/v1", false);
    url.onFocusLost();
    CHECK(env.ai().providers.at("lmstudio").baseUrl.empty());
}

TEST_CASE("the structured output level is an expert setting", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    {
        const auto dialog = env.dialog();
        choose(*dialog, "Eigene Adresse (OpenAI-kompatibel)");
        CHECK_FALSE(part<juce::ComboBox>(*dialog, "level").isVisible());
    }
    env.store.update([](mm::core::GlobalSettings& settings) { settings.expert = true; });
    const auto dialog = env.dialog();
    auto& level = part<juce::ComboBox>(*dialog, "level");
    CHECK(level.isVisible());
    CHECK(level.getSelectedId() == 1); // the default of models.json
    level.setSelectedId(2, juce::sendNotificationSync);
    CHECK(env.ai().providers.at("custom").schemaLevel == "enforced");
    level.setSelectedId(4, juce::sendNotificationSync);
    CHECK(env.ai().providers.at("custom").schemaLevel == "prompt");
    level.setSelectedId(1, juce::sendNotificationSync);
    CHECK(env.ai().providers.at("custom").schemaLevel.empty());
}

TEST_CASE("the connection test shows its result", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.secrets->set("anthropic", "sk-ant-X");
    const auto dialog = env.dialog();
    choose(*dialog, "Anthropic (Claude)");
    auto& status = part<juce::Label>(*dialog, "testStatus");
    env.http->reply.status = 200;
    env.http->reply.body = R"({"data":[]})";
    part<juce::TextButton>(*dialog, "test").onClick();
    CHECK(status.getText() == tr(keys::kTestRunning));
    REQUIRE(pumpUntil([&] { return status.getText() == tr(keys::kTestOk); }));

    env.http->reply.status = 401;
    env.http->reply.body = R"({"error":{"message":"bad"}})";
    part<juce::TextButton>(*dialog, "test").onClick();
    REQUIRE(pumpUntil([&] { return status.getText().contains(tr(keys::kAiReasonAuth)); }));
    CHECK(
        status.getText().startsWith(tr(keys::kTestFailed, {{"reason", ""}}).upToFirstOccurrenceOf(":", false, false)));
}

TEST_CASE("a connection test that finds a weaker level says so", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "LM Studio (lokal)");
    part<juce::ComboBox>(*dialog, "model").setText("gemma-test", juce::sendNotificationSync);
    env.http->reply.status = 200;
    env.http->reply.body =
        R"({"choices":[{"message":{"content":"{\"answer\":\"dog\"}"}}]})"; // JSON, but not the schema
    part<juce::TextButton>(*dialog, "test").onClick();
    auto& status = part<juce::Label>(*dialog, "testStatus");
    REQUIRE(pumpUntil(
        [&] { return status.getText() == tr(keys::kTestOkLowered, {{"level", tr(keys::kLevelJson).toStdString()}}); }));
}

TEST_CASE("the models of the provider fill the list and keep the chosen one", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "LM Studio (lokal)");
    auto& model = part<juce::ComboBox>(*dialog, "model");
    model.setText("gemma-test", juce::sendNotificationSync);
    env.http->reply.status = 200;
    env.http->reply.body = R"({"data":[{"id":"qwen-a"},{"id":"gemma-test"},{"id":"zeta"}]})";
    part<juce::TextButton>(*dialog, "loadModels").onClick();
    REQUIRE(pumpUntil([&] { return model.getNumItems() == 3; }));
    CHECK(model.getText() == "gemma-test");
    CHECK(model.getItemText(0) == "gemma-test");
    CHECK(part<juce::Label>(*dialog, "modelStatus").getText() == tr(keys::kModelsLoaded, {{"n", "3"}}));
    CHECK(env.ai().providers.at("lmstudio").model == "gemma-test"); // loading the list changes nothing
}

TEST_CASE("no models from the provider is said plainly", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    choose(*dialog, "Ollama (lokal)");
    env.http->reply = {};
    env.http->reply.error = mm::ai::HttpError::Connection;
    part<juce::TextButton>(*dialog, "loadModels").onClick();
    REQUIRE(pumpUntil([&] { return part<juce::Label>(*dialog, "modelStatus").getText() == tr(keys::kModelsNone); }));
}

TEST_CASE("consent is given and revoked per cloud provider, local providers need none", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& toggle = part<juce::ToggleButton>(*dialog, "consent");
    auto& list = part<juce::Label>(*dialog, "consentList");
    choose(*dialog, "Anthropic (Claude)");
    CHECK(toggle.isEnabled());
    CHECK_FALSE(toggle.getToggleState());
    CHECK(list.getText() == tr(keys::kConsentNone));
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick();
    CHECK(env.ai().cloudConsent.count("anthropic") == 1);
    CHECK(list.getText().contains("Anthropic"));

    choose(*dialog, "OpenAI (ChatGPT)");
    CHECK_FALSE(toggle.getToggleState()); // each provider has its own consent
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick();
    CHECK(env.ai().cloudConsent.size() == 2);

    choose(*dialog, "Ollama (lokal)");
    CHECK_FALSE(toggle.isEnabled());
    CHECK(toggle.getButtonText() == tr(keys::kConsentLocal, {{"provider", "Ollama (lokal)"}}));
    CHECK(list.getText().contains("Anthropic"));

    choose(*dialog, "Anthropic (Claude)");
    toggle.setToggleState(false, juce::dontSendNotification);
    toggle.onClick(); // revoked
    CHECK(env.ai().cloudConsent == std::set<std::string>{"openai"});
    CHECK_FALSE(list.getText().contains("Anthropic"));
}

TEST_CASE("prompt logging is off by default and is switched here", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& toggle = part<juce::ToggleButton>(*dialog, "logPrompts");
    CHECK(toggle.isVisible()); // also without a provider
    CHECK_FALSE(toggle.getToggleState());
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick();
    CHECK(env.ai().logPrompts);
    toggle.setToggleState(false, juce::dontSendNotification);
    toggle.onClick();
    CHECK_FALSE(env.ai().logPrompts);
}

TEST_CASE("the aids of the piano roll are switched here and saved", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& fold = part<juce::ToggleButton>(*dialog, "rollFold");
    auto& guides = part<juce::ToggleButton>(*dialog, "rollGuides");
    auto& markers = part<juce::ToggleButton>(*dialog, "rollMarkers");
    CHECK(fold.isVisible());
    CHECK_FALSE(fold.getToggleState()); // the defaults of D-232
    CHECK(guides.getToggleState());
    CHECK_FALSE(markers.getToggleState());
    fold.setToggleState(true, juce::dontSendNotification);
    fold.onClick();
    CHECK(env.store.get().rollFold);
    guides.setToggleState(false, juce::dontSendNotification);
    guides.onClick();
    CHECK_FALSE(env.store.get().rollGuides);
    markers.setToggleState(true, juce::dontSendNotification);
    markers.onClick();
    CHECK(env.store.get().rollMarkers);
    CHECK(env.store.get().rollFold); // the others stayed
    // a dialog that opens later shows what was saved
    const auto later = env.dialog();
    CHECK(part<juce::ToggleButton>(*later, "rollFold").getToggleState());
    CHECK_FALSE(part<juce::ToggleButton>(*later, "rollGuides").getToggleState());
    CHECK(part<juce::ToggleButton>(*later, "rollMarkers").getToggleState());
}

TEST_CASE("a key is assigned to an action by pressing it", "[settings-dialog][shortcuts]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& actions = part<juce::ComboBox>(*dialog, "shortcutAction");
    auto& key = part<juce::Label>(*dialog, "shortcutKey");
    auto& assign = part<juce::TextButton>(*dialog, "shortcutAssign");
    auto& remove = part<juce::TextButton>(*dialog, "shortcutRemove");
    CHECK(actions.getNumItems() == static_cast<int>(mm::core::shortcutActions().size()));
    CHECK(actions.isVisible());
    CHECK(key.getText() == "keine"); // nothing is assigned in advance
    CHECK_FALSE(remove.isEnabled());
    CHECK_FALSE(dialog->capturing());
    CHECK_FALSE(dialog->keyPressed(juce::KeyPress('k', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0))); // not capturing

    // the action "undo" is the first
    actions.setSelectedId(1, juce::sendNotificationSync);
    assign.onClick();
    CHECK(dialog->capturing());
    CHECK(key.getText() == "Taste drücken …");
    CHECK(dialog->keyPressed(juce::KeyPress('k', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0)));
    CHECK_FALSE(dialog->capturing());
    CHECK(env.store.get().shortcuts == std::map<std::string, std::string>{{"undo", "cmd+shift+k"}});
    CHECK(key.getText().endsWith("Shift+K"));
    CHECK(remove.isEnabled());

    // the next action: another key, and the same key moves from one action to the other
    actions.setSelectedId(2, juce::sendNotificationSync);
    CHECK(key.getText() == "keine"); // "redo" has none
    assign.onClick();
    CHECK(dialog->keyPressed(juce::KeyPress('k', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0)));
    CHECK(env.store.get().shortcuts == std::map<std::string, std::string>{{"redo", "cmd+shift+k"}});
    actions.setSelectedId(1, juce::sendNotificationSync);
    CHECK(key.getText() == "keine"); // taken away from "undo"

    // remove
    actions.setSelectedId(2, juce::sendNotificationSync);
    remove.onClick();
    CHECK(env.store.get().shortcuts.empty());
    CHECK(key.getText() == "keine");
}

TEST_CASE("the space bar is refused, Escape and a lone modifier are not keys", "[settings-dialog][shortcuts]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& assign = part<juce::TextButton>(*dialog, "shortcutAssign");
    auto& key = part<juce::Label>(*dialog, "shortcutKey");
    auto& hint = part<juce::Label>(*dialog, "shortcutHint");
    assign.onClick();
    CHECK(dialog->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    CHECK(dialog->capturing()); // still waiting
    CHECK(hint.getText() == "Die Leertaste bleibt bei der DAW.");
    CHECK(env.store.get().shortcuts.empty());
    CHECK(dialog->keyPressed(juce::KeyPress(juce::KeyPress::numberPad0))); // not a key of ours: waits
    CHECK(dialog->capturing());
    CHECK(env.store.get().shortcuts.empty());
    CHECK(dialog->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))); // cancels
    CHECK_FALSE(dialog->capturing());
    CHECK(key.getText() == "keine");
    CHECK(env.store.get().shortcuts.empty());
    // an action chosen anew ends the waiting
    assign.onClick();
    part<juce::ComboBox>(*dialog, "shortcutAction").setSelectedId(3, juce::sendNotificationSync);
    CHECK_FALSE(dialog->capturing());
    // a key without modifier is a key
    assign.onClick();
    CHECK(dialog->keyPressed(juce::KeyPress(juce::KeyPress::F5Key)));
    CHECK(env.store.get().shortcuts.size() == 1);
    CHECK(env.store.get().shortcuts.begin()->second == "f5");
    CHECK(key.getText() == "F5");
}

TEST_CASE("restoring the default takes every assignment away", "[settings-dialog][shortcuts]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.store.update([](mm::core::GlobalSettings& settings) {
        settings.shortcuts = {{"undo", "cmd+z"}, {"generate", "cmd+g"}, {"vary", "f6"}};
    });
    const auto dialog = env.dialog();
    auto& key = part<juce::Label>(*dialog, "shortcutKey");
    auto& actions = part<juce::ComboBox>(*dialog, "shortcutAction");
    actions.setSelectedId(1, juce::sendNotificationSync);
    CHECK(key.getText().endsWith("Z")); // the dialog shows what was saved before
    part<juce::TextButton>(*dialog, "shortcutReset").onClick();
    CHECK(env.store.get().shortcuts.empty());
    CHECK(key.getText() == "keine");
    CHECK_FALSE(part<juce::TextButton>(*dialog, "shortcutRemove").isEnabled());
    // and it ends the waiting for a key
    part<juce::TextButton>(*dialog, "shortcutAssign").onClick();
    part<juce::TextButton>(*dialog, "shortcutReset").onClick();
    CHECK_FALSE(dialog->capturing());
}

TEST_CASE("the export of all notes is switched here and saved", "[settings-dialog][probability]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& toggle = part<juce::ToggleButton>(*dialog, "exportAllNotes");
    CHECK(toggle.isVisible());
    CHECK_FALSE(toggle.getToggleState()); // by default the passes are resolved (D-233)
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick();
    CHECK(env.store.get().exportAllNotes);
    const auto later = env.dialog();
    CHECK(part<juce::ToggleButton>(*later, "exportAllNotes").getToggleState());
    toggle.setToggleState(false, juce::dontSendNotification);
    toggle.onClick();
    CHECK_FALSE(env.store.get().exportAllNotes);
}

TEST_CASE("the dialog shows what was saved before", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.store.update([](mm::core::GlobalSettings& settings) {
        settings.ai.activeProvider = "lmstudio";
        settings.ai.providers["lmstudio"] = {"qwen-test", "http://localhost:4321/v1", 77, 5555, ""};
        settings.ai.logPrompts = true;
    });
    const auto dialog = env.dialog();
    CHECK(part<juce::ComboBox>(*dialog, "provider").getText() == "LM Studio (lokal)");
    CHECK(part<juce::ComboBox>(*dialog, "model").getText() == "qwen-test");
    CHECK(part<juce::TextEditor>(*dialog, "url").getText() == "http://localhost:4321/v1");
    CHECK(part<juce::TextEditor>(*dialog, "timeout").getText() == "77");
    CHECK(part<juce::TextEditor>(*dialog, "maxTokens").getText() == "5555");
    CHECK(part<juce::ToggleButton>(*dialog, "logPrompts").getToggleState());
}

TEST_CASE("an unknown provider in the settings shows as off", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.store.update([](mm::core::GlobalSettings& settings) { settings.ai.activeProvider = "gone"; });
    const auto dialog = env.dialog();
    CHECK(part<juce::ComboBox>(*dialog, "provider").getText() == tr(keys::kProviderOffline));
}

TEST_CASE("closing the dialog with work pending is safe", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.http->reply.status = 200;
    env.http->reply.body = R"({"data":[]})";
    {
        const auto dialog = env.dialog();
        choose(*dialog, "Anthropic (Claude)");
        part<juce::TextButton>(*dialog, "test").onClick();
        part<juce::TextButton>(*dialog, "loadModels").onClick();
        part<juce::TextEditor>(*dialog, "key").setText("sk-x", false);
        part<juce::TextButton>(*dialog, "keySave").onClick();
    }
    pumpFor(400);
    SUCCEED();
}

TEST_CASE("the editor has a settings button in the hub UI and an error shortcut for a bad key",
          "[settings-dialog][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto& settings = part<juce::TextButton>(*editor, "settings");
    auto& shortcut = part<juce::TextButton>(*editor, "errorSettings");
    CHECK(settings.isVisible());
    CHECK(settings.getButtonText() == tr(keys::kButtonSettings));
    CHECK_FALSE(shortcut.isVisible());

    auto provider = std::make_shared<mm::ai::MockProvider>();
    provider->enqueueError(mm::ai::AiStatus::AuthFailed, "bad key", 401);
    provider->enqueueError(mm::ai::AiStatus::RateLimited, "slow", 429);
    provider->enqueueError(mm::ai::AiStatus::ModelNotFound, "no such model", 404);
    AiBackendSettings backend;
    backend.provider = provider;
    backend.model = "m";
    ScopedAiBackend scoped(backend);
    part<juce::TextButton>(*editor, "generate").onClick();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::AiFailed; }));
    REQUIRE(pumpUntil([&] { return shortcut.isVisible(); }));

    part<juce::TextButton>(*editor, "retry").onClick(); // the second error is about the rate: no shortcut
    REQUIRE(pumpUntil([&] {
        return processor.generationStatus() == GenerationStatus::AiFailed &&
               processor.lastReport().status == mm::ai::AiStatus::RateLimited;
    }));
    REQUIRE(pumpUntil([&] { return !shortcut.isVisible(); }));

    part<juce::TextButton>(*editor, "retry").onClick(); // a model that does not exist is also solved in the settings
    REQUIRE(pumpUntil([&] {
        return processor.generationStatus() == GenerationStatus::AiFailed &&
               processor.lastReport().status == mm::ai::AiStatus::ModelNotFound;
    }));
    REQUIRE(pumpUntil([&] { return shortcut.isVisible(); }));
}

TEST_CASE("the voice UI does not show the settings button", "[settings-dialog][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    CHECK_FALSE(part<juce::TextButton>(*editor, "settings").isVisible());
}

TEST_CASE("the drum mapping is typed into the dialog, cleaned and kept", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    {
        const auto dialog = env.dialog();
        auto& editor = part<juce::TextEditor>(*dialog, "drumMap");
        CHECK(editor.getText().isEmpty());
        editor.setText(" 60 = kick; 61=closed_hat, 300=ride, nonsense", true);
        editor.onReturnKey();
        CHECK(env.store.get().drumMap == "60=kick, 61=closed_hat");
        CHECK(editor.getText() == "60=kick, 61=closed_hat"); // what is kept is what the field shows
    }
    const auto again = env.dialog();
    CHECK(part<juce::TextEditor>(*again, "drumMap").getText() == "60=kick, 61=closed_hat");
}

TEST_CASE("the dialog has the look of the plug-in", "[settings-dialog][look]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    auto owned = env.dialog();
    auto& dialog = *owned;
    auto& close = *findById(dialog, "close");
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&dialog.getLookAndFeel()) != nullptr);
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&close.getLookAndFeel()) != nullptr); // the children inherit it
    const auto image = dialog.createComponentSnapshot(dialog.getLocalBounds());
    CHECK(image.getPixelAt(1, 1) == juce::Colour(MidiMaidLookAndFeel::kBackground));
    CHECK(image.getPixelAt(dialog.getWidth() - 2, dialog.getHeight() - 2) == juce::Colour(MidiMaidLookAndFeel::kBackground));
}

namespace {

void writeSnapshot(juce::Component& component, const juce::String& name) {
    const auto folder = juce::SystemStats::getEnvironmentVariable("MM_SNAPSHOT_DIR", {});
    REQUIRE(folder.isNotEmpty());
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), false, 2.0f);
    juce::FileOutputStream stream(juce::File(folder).getChildFile(name));
    REQUIRE(stream.openedOk());
    stream.setPosition(0);
    stream.truncate();
    juce::PNGImageFormat png;
    REQUIRE(png.writeImageToStream(image, stream));
}

} // namespace

// Renders the dialog to a PNG file for a look at the design (not a check): MM_SNAPSHOT_DIR=<folder> run with "[.snapshot]".
TEST_CASE("snapshot of the settings", "[.snapshot]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    auto owned = env.dialog();
    choose(*owned, "Anthropic (Claude)");
    writeSnapshot(*owned, "settings.png");
}

TEST_CASE("the dialog sits on one grid: wide key field, small buttons, one control height", "[settings-dialog][layout]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    auto owned = env.dialog();
    auto& dialog = *owned;
    choose(dialog, "Anthropic (Claude)");
    auto& key = part<juce::TextEditor>(dialog, "key");
    auto& save = part<juce::TextButton>(dialog, "keySave");
    auto& remove = part<juce::TextButton>(dialog, "keyRemove");
    auto& url = part<juce::TextEditor>(dialog, "url");
    auto& model = part<juce::ComboBox>(dialog, "model");
    auto& provider = part<juce::ComboBox>(dialog, "provider");
    // the key needs room (a key is long); the buttons next to it do not take it
    CHECK(key.getWidth() >= 330);
    CHECK(save.getWidth() <= 110);
    CHECK(remove.getWidth() <= 110);
    CHECK(save.getWidth() == remove.getWidth());
    CHECK(key.getRight() < save.getX());
    CHECK(remove.getRight() == url.getRight()); // the right edge is shared
    CHECK(key.getX() == url.getX());            // and the left edge of the controls
    CHECK(provider.getX() == url.getX());
    CHECK(model.getX() == url.getX());
    // one control height
    for (auto* component : {static_cast<juce::Component*>(&key), static_cast<juce::Component*>(&save),
                            static_cast<juce::Component*>(&remove), static_cast<juce::Component*>(&url),
                            static_cast<juce::Component*>(&model), static_cast<juce::Component*>(&provider),
                            findById(dialog, "loadModels"), findById(dialog, "test"), findById(dialog, "close")}) {
        REQUIRE(component != nullptr);
        INFO(component->getComponentID().toStdString());
        CHECK(component->getHeight() == 34);
    }
    // everything visible lies inside the margins and nothing overlaps
    std::vector<juce::Component*> shown;
    for (auto* child : dialog.getChildren()) {
        if (child->isVisible() && child->getWidth() > 0) {
            shown.push_back(child);
            INFO(child->getComponentID().toStdString());
            CHECK(dialog.getLocalBounds().reduced(20, 0).contains(child->getBounds()));
        }
    }
    for (size_t i = 0; i < shown.size(); ++i) {
        for (size_t j = i + 1; j < shown.size(); ++j) {
            INFO(shown[i]->getComponentID().toStdString() << " / " << shown[j]->getComponentID().toStdString());
            CHECK_FALSE(shown[i]->getBounds().intersects(shown[j]->getBounds()));
        }
    }
}

// --- the keyboard transposition (SPEC 3.16, 8.4) ----------------------------------------------------------------

namespace {

void choose(juce::ComboBox& box, int id) {
    box.setSelectedId(id, juce::sendNotificationSync);
}

} // namespace

TEST_CASE("the transposition settings are in the dialog, show the saved values and are saved at once", "[settings-dialog][transpose]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    {
        const auto dialog = env.dialog();
        // the defaults
        CHECK(part<juce::ComboBox>(*dialog, "transposeMode").getSelectedId() == 1);
        CHECK(part<juce::ComboBox>(*dialog, "transposeBehavior").getSelectedId() == 1);
        CHECK(part<juce::ComboBox>(*dialog, "transposeTiming").getSelectedId() == 1);
        CHECK(part<juce::ComboBox>(*dialog, "transposeTarget").getSelectedId() == 1);
        CHECK(part<juce::ComboBox>(*dialog, "transposeReference").getSelectedId() == 1);
        CHECK(part<juce::ComboBox>(*dialog, "transposeChannel").getSelectedId() == 1);
        CHECK(part<juce::ToggleButton>(*dialog, "transposeExport").getToggleState());
        CHECK_FALSE(part<juce::ToggleButton>(*dialog, "midiThru").getToggleState());
        CHECK(part<juce::ComboBox>(*dialog, "transposeMode").isVisible());
        CHECK(part<juce::ComboBox>(*dialog, "transposeMode").getText() == tr(keys::kTransposeModeChromatic));

        choose(part<juce::ComboBox>(*dialog, "transposeMode"), 2);
        choose(part<juce::ComboBox>(*dialog, "transposeBehavior"), 2);
        choose(part<juce::ComboBox>(*dialog, "transposeTiming"), 3);
        choose(part<juce::ComboBox>(*dialog, "transposeTarget"), 3);
        choose(part<juce::ComboBox>(*dialog, "transposeReference"), 45 + 2); // note 45
        choose(part<juce::ComboBox>(*dialog, "transposeChannel"), 4);        // channel 3
        part<juce::ToggleButton>(*dialog, "transposeExport").setToggleState(false, juce::sendNotificationSync);
        part<juce::ToggleButton>(*dialog, "midiThru").setToggleState(true, juce::sendNotificationSync);
        const auto saved = env.store.get().transpose;
        CHECK(saved.mode == mm::core::TransposeMode::Diatonic);
        CHECK(saved.behavior == mm::core::TransposeBehavior::Momentary);
        CHECK(saved.timing == mm::core::TransposeTiming::NextBar);
        CHECK(saved.target == mm::core::TransposeTarget::Melody);
        CHECK(saved.referenceNote == 45);
        CHECK(saved.channel == 3);
        CHECK_FALSE(saved.exportTransposed);
        CHECK(saved.midiThru);
        // back to automatic and all channels
        choose(part<juce::ComboBox>(*dialog, "transposeReference"), 1);
        choose(part<juce::ComboBox>(*dialog, "transposeChannel"), 1);
        CHECK(env.store.get().transpose.referenceNote == -1);
        CHECK(env.store.get().transpose.channel == 0);
        choose(part<juce::ComboBox>(*dialog, "transposeReference"), 45 + 2);
        choose(part<juce::ComboBox>(*dialog, "transposeChannel"), 4);
    }
    env.store.waitForWrites();
    // a dialog that opens later shows what was saved
    const auto again = env.dialog();
    CHECK(part<juce::ComboBox>(*again, "transposeMode").getSelectedId() == 2);
    CHECK(part<juce::ComboBox>(*again, "transposeBehavior").getSelectedId() == 2);
    CHECK(part<juce::ComboBox>(*again, "transposeTiming").getSelectedId() == 3);
    CHECK(part<juce::ComboBox>(*again, "transposeTarget").getSelectedId() == 3);
    CHECK(part<juce::ComboBox>(*again, "transposeReference").getSelectedId() == 47);
    CHECK(part<juce::ComboBox>(*again, "transposeReference").getText().contains("A1 (45)"));
    CHECK(part<juce::ComboBox>(*again, "transposeChannel").getSelectedId() == 4);
    CHECK_FALSE(part<juce::ToggleButton>(*again, "transposeExport").getToggleState());
    CHECK(part<juce::ToggleButton>(*again, "midiThru").getToggleState());
}

TEST_CASE("the reference note box lists the notes by the convention of Live and Logic", "[settings-dialog][transpose]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    auto& box = part<juce::ComboBox>(*dialog, "transposeReference");
    CHECK(box.getNumItems() == 1 + (96 - 24 + 1));
    CHECK(box.getItemText(0) == tr(keys::kTransposeReferenceAuto));
    CHECK(box.getItemText(box.indexOfItemId(60 + 2)).contains("C3 (60)")); // middle C is C3
    CHECK(box.getItemText(box.indexOfItemId(36 + 2)).contains("C1 (36)"));
}

TEST_CASE("the dialog with the transposition stays within a screen and nothing overlaps", "[settings-dialog][transpose][layout]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto dialog = env.dialog();
    CHECK(dialog->getHeight() < 1000);
    juce::Array<juce::Component*> parts;
    for (const char* id : {"transposeMode", "transposeBehavior", "transposeTiming", "transposeTarget",
                           "transposeReference", "transposeChannel", "transposeExport", "midiThru", "transposeHint"}) {
        parts.add(&part<juce::Component>(*dialog, id));
    }
    for (auto* one : parts) {
        INFO(one->getComponentID().toStdString());
        CHECK(one->isVisible());
        CHECK(one->getWidth() > 60);
        CHECK(dialog->getLocalBounds().contains(one->getBounds()));
        for (auto* other : dialog->getChildren()) {
            if (other != one && other->isVisible() && other->getWidth() > 0) {
                INFO(other->getComponentID().toStdString());
                CHECK_FALSE(one->getBounds().intersects(other->getBounds()));
            }
        }
    }
}

TEST_CASE("the language box saves the choice and switches the texts", "[settings-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    auto dialog = env.dialog();
    auto& box = part<juce::ComboBox>(*dialog, "language");
    CHECK(box.getSelectedId() == 1); // automatic
    CHECK(env.store.get().language == "auto");

    box.setSelectedId(3, juce::sendNotificationSync);
    CHECK(env.store.get().language == "en");
    CHECK(uiLanguage() == "en");
    CHECK(tr(keys::kButtonClose) == "Close");
    CHECK(localized("Deutsch", "English") == "English");
    CHECK(localized("Deutsch", "") == "Deutsch");

    box.setSelectedId(2, juce::sendNotificationSync);
    CHECK(env.store.get().language == "de");
    CHECK(uiLanguage() == "de");
    CHECK(tr(keys::kButtonClose) == "Schließen");
    CHECK(localized("Deutsch", "English") == "Deutsch");

    env.store.waitForWrites();
    auto reopened = env.dialog(); // a dialog opened later shows the choice
    CHECK(part<juce::ComboBox>(*reopened, "language").getSelectedId() == 2);
    setUiLanguage("de");
}
