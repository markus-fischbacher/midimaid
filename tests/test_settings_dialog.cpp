#include "ai/MockProvider.h"
#include "core/TextKeys.h"
#include "plugin/AiBackend.h"
#include "plugin/AiConnection.h"
#include "plugin/EmbeddedTranslation.h"
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
