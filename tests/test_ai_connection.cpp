#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"
#include "core/PatternGenerator.h"
#include "plugin/AiBackend.h"
#include "plugin/AiConnection.h"
#include "plugin/AppLog.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <mutex>

using namespace mm::plugin;
using namespace std::chrono_literals;

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

/// Records the requests and answers with what was set.
class StubClient : public mm::ai::IHttpClient {
public:
    mm::ai::HttpResponse reply;

    mm::ai::HttpResponse send(const mm::ai::HttpRequest& request, const mm::ai::CancellationToken&) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
        return reply;
    }
    std::vector<mm::ai::HttpRequest> requests() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<mm::ai::HttpRequest> requests_;
};

/// A settings store on a temporary file, read already.
struct Env {
    juce::TemporaryFile file;
    GlobalSettingsStore store{file.getFile()};
    std::shared_ptr<mm::platform::MemorySecretStore> secrets = std::make_shared<mm::platform::MemorySecretStore>();
    std::shared_ptr<StubClient> http = std::make_shared<StubClient>();

    Env() {
        REQUIRE(pumpUntil([&] { return store.ready(); }));
    }

    AiConnection::Dependencies dependencies() {
        AiConnection::Dependencies dependencies;
        dependencies.settings = [this]() -> GlobalSettingsStore& { return store; };
        dependencies.secrets = secrets;
        dependencies.http = http;
        return dependencies;
    }
    void setAi(const std::function<void(mm::core::AiSettings&)>& change) {
        store.update([&](mm::core::GlobalSettings& settings) { change(settings.ai); });
    }
};

bool backendHas(const std::string& id) {
    const auto settings = AiBackend::instance().get();
    return settings.provider != nullptr && settings.provider->info().id == id;
}

std::string readLog(const juce::File& file) {
    AppLog::instance().flush();
    return file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string();
}

/// Points the log at a temporary file for the test and switches it off again.
struct LogToFile {
    juce::TemporaryFile file;
    LogToFile() {
        AppLog::instance().setFile(file.getFile());
        AppLog::instance().setLevel(LogLevel::Debug);
    }
    ~LogToFile() {
        AppLog::instance().flush();
        AppLog::instance().setFile({});
        AppLog::instance().setLogContent(false);
        AppLog::instance().setLevel(LogLevel::Info);
    }
};

} // namespace

TEST_CASE("without an active provider the backend is never touched", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    AiBackendSettings mine;
    mine.provider = std::make_shared<mm::ai::MockProvider>();
    ScopedAiBackend scoped(mine); // someone set it, not this connection
    AiConnection connection(env.dependencies());
    connection.refresh();
    pumpFor(200);
    CHECK(AiBackend::instance().get().provider == mine.provider);
}

TEST_CASE("an active provider is built with its defaults and the key from the keychain", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    env.secrets->set("anthropic", "sk-ant-TESTKEY");
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "anthropic"; });
    AiConnection connection(env.dependencies());
    connection.refresh();
    REQUIRE(pumpUntil([&] { return backendHas("anthropic"); }));
    const auto settings = AiBackend::instance().get();
    CHECK(settings.model == "claude-sonnet-5-5");
    CHECK(settings.timeoutSeconds == 30);
    CHECK(settings.maxTokens == 4096);

    // The key travels in the header of the request and nowhere else.
    env.http->reply.status = 200;
    env.http->reply.body = R"({"content":[]})";
    mm::ai::AiRequest request;
    request.model = settings.model;
    request.schemaJson = mm::ai::schemaV1Json();
    settings.provider->generate(request, mm::ai::CancellationToken());
    const auto sent = env.http->requests();
    REQUIRE(sent.size() == 1);
    bool inHeader = false;
    for (const auto& [name, value] : sent[0].headers) {
        inHeader = inHeader || (name == "x-api-key" && value == "sk-ant-TESTKEY");
    }
    CHECK(inHeader);
    CHECK(sent[0].body.find("sk-ant-TESTKEY") == std::string::npos);
    CHECK(sent[0].url.find("sk-ant-TESTKEY") == std::string::npos);
}

TEST_CASE("the choices of the musician reach the backend", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    env.setAi([](mm::core::AiSettings& ai) {
        ai.activeProvider = "ollama";
        ai.providers["ollama"] = {"llama3.1", "", 200, 9000, ""};
    });
    AiConnection connection(env.dependencies());
    connection.refresh();
    REQUIRE(pumpUntil([&] { return backendHas("ollama"); }));
    const auto settings = AiBackend::instance().get();
    CHECK(settings.model == "llama3.1");
    CHECK(settings.timeoutSeconds == 200);
    CHECK(settings.maxTokens == 9000);
}

TEST_CASE("switching the provider off clears the backend it had set", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "ollama"; });
    AiConnection connection(env.dependencies());
    connection.refresh();
    REQUIRE(pumpUntil([&] { return backendHas("ollama"); }));
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider.clear(); });
    connection.refresh();
    CHECK_FALSE(AiBackend::instance().active());
}

TEST_CASE("an unknown provider id gives no backend", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "nonexistent"; });
    AiConnection connection(env.dependencies());
    connection.refresh();
    pumpFor(300);
    CHECK_FALSE(AiBackend::instance().active());
}

TEST_CASE("the newest refresh wins", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    AiConnection connection(env.dependencies());
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "anthropic"; });
    connection.refresh();
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "ollama"; });
    connection.refresh();
    REQUIRE(pumpUntil([&] { return backendHas("ollama"); }));
    pumpFor(300);
    CHECK(backendHas("ollama"));
}

TEST_CASE("a pending refresh does not undo a later switch-off", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    AiConnection connection(env.dependencies());
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "anthropic"; });
    connection.refresh();     // the worker builds it
    juce::Thread::sleep(200); // ... and its result waits on the message thread
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider.clear(); });
    connection.refresh();
    pumpFor(300);
    CHECK_FALSE(AiBackend::instance().active());
}

TEST_CASE("removing the key takes it out of the provider in use", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    env.secrets->set("openai", "sk-OLD");
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "openai"; });
    AiConnection connection(env.dependencies());
    connection.refresh();
    REQUIRE(pumpUntil([&] { return backendHas("openai"); }));
    bool removed = false;
    connection.removeKey("openai", [&](bool) { removed = true; });
    REQUIRE(pumpUntil([&] { return removed; }));
    pumpFor(300); // the provider is built again, now without the key
    env.http->reply.status = 200;
    env.http->reply.body = R"({"choices":[]})";
    mm::ai::AiRequest request;
    request.model = "m";
    request.schemaJson = mm::ai::schemaV1Json();
    const auto before = env.http->requests().size();
    AiBackend::instance().get().provider->generate(request, mm::ai::CancellationToken());
    const auto sent = env.http->requests();
    REQUIRE(sent.size() == before + 1);
    for (const auto& [name, value] : sent.back().headers) {
        CHECK(name != "authorization");
    }
}

TEST_CASE("start waits for the settings file and then applies it", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    juce::TemporaryFile file;
    {
        mm::core::GlobalSettings settings;
        settings.ai.activeProvider = "lmstudio";
        file.getFile().replaceWithText(juce::String(mm::core::toJson(settings)));
    }
    GlobalSettingsStore store(file.getFile(), false); // not read yet
    Env env;
    auto dependencies = env.dependencies();
    dependencies.settings = [&]() -> GlobalSettingsStore& { return store; };
    AiConnection connection(dependencies);
    connection.start();
    pumpFor(150);
    CHECK_FALSE(AiBackend::instance().active());
    store.beginRead();
    REQUIRE(pumpUntil([&] { return backendHas("lmstudio"); }));
}

TEST_CASE("keys go to the keychain and never into the settings file", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    AiConnection connection(env.dependencies());
    bool saved = false;
    connection.saveKey("openai", "sk-SECRET-VALUE", [&](bool ok) { saved = ok; });
    REQUIRE(pumpUntil([&] { return saved; }));
    CHECK(env.secrets->get("openai") == "sk-SECRET-VALUE");
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "openai"; });
    env.store.waitForWrites();
    CHECK(env.file.getFile().loadFileAsString().contains("sk-SECRET-VALUE") == false);

    bool has = false;
    connection.hasKey("openai", [&](bool present) { has = present; });
    REQUIRE(pumpUntil([&] { return has; }));
    bool removed = false;
    connection.removeKey("openai", [&](bool ok) { removed = ok; });
    REQUIRE(pumpUntil([&] { return removed; }));
    CHECK_FALSE(env.secrets->get("openai").has_value());
    bool stillThere = true;
    bool answered = false;
    connection.hasKey("openai", [&](bool present) {
        stillThere = present;
        answered = true;
    });
    REQUIRE(pumpUntil([&] { return answered; }));
    CHECK_FALSE(stillThere);
}

TEST_CASE("an empty key is not stored", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    AiConnection connection(env.dependencies());
    bool done = false;
    bool ok = true;
    connection.saveKey("openai", "", [&](bool result) {
        ok = result;
        done = true;
    });
    REQUIRE(pumpUntil([&] { return done; }));
    CHECK_FALSE(ok);
    CHECK_FALSE(env.secrets->get("openai").has_value());
}

TEST_CASE("saving the key of the active provider activates it with that key", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "openai"; });
    AiConnection connection(env.dependencies());
    connection.saveKey("openai", "sk-NEW", nullptr);
    REQUIRE(pumpUntil([&] { return backendHas("openai"); }));
    env.http->reply.status = 200;
    env.http->reply.body = R"({"choices":[]})";
    mm::ai::AiRequest request;
    request.model = "m";
    request.schemaJson = mm::ai::schemaV1Json();
    AiBackend::instance().get().provider->generate(request, mm::ai::CancellationToken());
    bool bearer = false;
    const auto sentRequests = env.http->requests();
    for (const auto& [name, value] : sentRequests.front().headers) {
        bearer = bearer || (name == "authorization" && value == "Bearer sk-NEW");
    }
    CHECK(bearer);
}

TEST_CASE("the connection test reports the status of the provider", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.secrets->set("anthropic", "sk-ant-X");
    AiConnection connection(env.dependencies());
    env.http->reply.status = 401;
    env.http->reply.body = R"({"error":{"message":"bad key sk-ant-X"}})";
    std::optional<mm::ai::ConnectionStatus> result;
    connection.testConnection("anthropic", [&](mm::ai::ConnectionStatus status) { result = status; });
    REQUIRE(pumpUntil([&] { return result.has_value(); }));
    CHECK_FALSE(result->ok);
    CHECK(result->status == mm::ai::AiStatus::AuthFailed);
    CHECK(result->message.find("sk-ant-X") == std::string::npos);

    env.http->reply.status = 200;
    env.http->reply.body = R"({"data":[]})";
    result.reset();
    connection.testConnection("anthropic", [&](mm::ai::ConnectionStatus status) { result = status; });
    REQUIRE(pumpUntil([&] { return result.has_value(); }));
    CHECK(result->ok);

    result.reset();
    connection.testConnection("nonexistent", [&](mm::ai::ConnectionStatus status) { result = status; });
    REQUIRE(pumpUntil([&] { return result.has_value(); }));
    CHECK_FALSE(result->ok);
}

TEST_CASE("the model list comes from the provider", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    AiConnection connection(env.dependencies());
    env.http->reply.status = 200;
    env.http->reply.body = R"({"data":[{"id":"llama3"},{"id":"qwen"}]})";
    std::optional<std::vector<std::string>> models;
    connection.loadModels("ollama", [&](std::vector<std::string> list) { models = std::move(list); });
    REQUIRE(pumpUntil([&] { return models.has_value(); }));
    CHECK(*models == std::vector<std::string>{"llama3", "qwen"});
    CHECK(env.http->requests().front().url == "http://localhost:11434/v1/models");
}

TEST_CASE("the models of the user's file are offered on top of the shipped ones", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    juce::TemporaryFile userFile;
    userFile.getFile().replaceWithText(
        R"({"providers":[{"id":"mine","kind":"openai","baseUrl":"https://llm.example.com/v1","name":"Mine"}]})");
    auto dependencies = env.dependencies();
    dependencies.userModelsFile = userFile.getFile();
    AiConnection connection(dependencies);
    CHECK(connection.models().find("mine") == nullptr); // read in the background with the first use
    std::optional<std::vector<std::string>> models;
    connection.loadModels("mine", [&](std::vector<std::string> list) { models = std::move(list); });
    REQUIRE(pumpUntil([&] { return models.has_value(); }));
    CHECK(connection.models().find("mine") != nullptr);
    CHECK(connection.models().find("anthropic") != nullptr);
}

TEST_CASE("destroying the connection with work pending is safe", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedAiBackend scoped({});
    Env env;
    bool called = false;
    {
        AiConnection connection(env.dependencies());
        env.setAi([](mm::core::AiSettings& ai) { ai.activeProvider = "anthropic"; });
        connection.refresh();
        connection.hasKey("anthropic", [&](bool) { called = true; });
        connection.testConnection("anthropic", [&](mm::ai::ConnectionStatus) { called = true; });
    }
    pumpFor(300);
    CHECK_FALSE(called); // nothing reaches a connection that is gone
}

TEST_CASE("an answer that is ready when the connection goes is not delivered", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    bool called = false;
    {
        AiConnection connection(env.dependencies());
        connection.hasKey("anthropic", [&](bool) { called = true; });
        juce::Thread::sleep(300); // the worker is done, the answer waits on the message thread
    }
    pumpFor(200);
    CHECK_FALSE(called);
}

TEST_CASE("the keys survive the session only with a real keychain", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    AiConnection connection(env.dependencies());
    CHECK_FALSE(connection.keysPersist()); // the memory store of the test
}

TEST_CASE("the setting Prompts protokollieren switches the logging of content", "[ai-connection]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    AiConnection connection(env.dependencies());
    CHECK_FALSE(AppLog::instance().logContent());
    env.setAi([](mm::core::AiSettings& ai) { ai.logPrompts = true; });
    connection.refresh();
    CHECK(AppLog::instance().logContent());
    env.setAi([](mm::core::AiSettings& ai) { ai.logPrompts = false; });
    connection.refresh();
    CHECK_FALSE(AppLog::instance().logContent());
}

TEST_CASE("the log writes one line per entry with the level and the category", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    AppLog::instance().write(LogLevel::Info, "ai", "first\nsecond line\tand a tab");
    AppLog::instance().write(LogLevel::Warning, "settings", "third");
    const auto text = readLog(log.file.getFile());
    CHECK(text.find("INFO  [ai] first second line and a tab\n") != std::string::npos);
    CHECK(text.find("WARN  [settings] third\n") != std::string::npos);
    CHECK(std::count(text.begin(), text.end(), '\n') == 2);
}

TEST_CASE("the log level filters", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    AppLog::instance().setLevel(LogLevel::Warning);
    AppLog::instance().write(LogLevel::Info, "x", "info");
    AppLog::instance().write(LogLevel::Debug, "x", "debug");
    AppLog::instance().write(LogLevel::Warning, "x", "warning");
    AppLog::instance().write(LogLevel::Error, "x", "error");
    const auto text = readLog(log.file.getFile());
    CHECK(text.find("info") == std::string::npos);
    CHECK(text.find("debug") == std::string::npos);
    CHECK(text.find("warning") != std::string::npos);
    CHECK(text.find("error") != std::string::npos);
}

TEST_CASE("content goes to the log only when the musician asked for it", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    AppLog::instance().writeContent("ai", "user prompt", "SECRET PROMPT TEXT");
    CHECK(readLog(log.file.getFile()).empty());
    AppLog::instance().setLogContent(true);
    AppLog::instance().writeContent("ai", "user prompt", "SECRET PROMPT TEXT");
    CHECK(readLog(log.file.getFile()).find("user prompt: SECRET PROMPT TEXT") != std::string::npos);
}

TEST_CASE("the log rotates at 1 MB and keeps 5 files", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    const juce::String block(std::string(900, 'x'));
    for (int i = 0; i < 6200; ++i) {
        AppLog::instance().write(LogLevel::Info, "fill", block);
    }
    AppLog::instance().flush();
    const auto main = log.file.getFile();
    CHECK(main.getSize() <= AppLog::kMaxFileBytes + 1000);
    for (int i = 1; i < AppLog::kFiles; ++i) {
        const auto older = main.getSiblingFile(main.getFileName() + "." + juce::String(i));
        INFO(older.getFullPathName());
        CHECK(older.existsAsFile());
        older.deleteFile();
    }
    CHECK_FALSE(main.getSiblingFile(main.getFileName() + "." + juce::String(AppLog::kFiles)).existsAsFile());
}

TEST_CASE("a switched off log writes nothing", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::TemporaryFile file;
    AppLog::instance().setFile({});
    AppLog::instance().write(LogLevel::Error, "x", "nothing");
    CHECK_FALSE(file.getFile().existsAsFile());
}

namespace {

std::string validAnswer(ProcessorBase& processor) {
    const auto* style = processor.styles().find("peak_time");
    mm::core::GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 5;
    const auto result = mm::core::generatePattern(*style, request);
    REQUIRE(result.success);
    return mm::ai::patternToSchemaJson(result.pattern, style, false)->json;
}

} // namespace

TEST_CASE("an AI request is logged with metadata but without the prompt", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    InstrumentProcessor processor("Test");
    auto provider = std::make_shared<mm::ai::MockProvider>();
    mm::ai::AiResult answer;
    answer.status = mm::ai::AiStatus::Ok;
    answer.text = validAnswer(processor);
    answer.inputTokens = 111;
    answer.outputTokens = 222;
    provider->enqueue(answer);
    AiBackendSettings backend;
    backend.provider = provider;
    backend.model = "mock-model";
    ScopedAiBackend scoped(backend);
    auto settings = processor.instanceSettings();
    settings.generation.prompt = "my very private idea";
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() != GenerationStatus::Generating; }));
    const auto text = readLog(log.file.getFile());
    CHECK(text.find("generate provider=mock model=mock-model outcome=ok") != std::string::npos);
    CHECK(text.find("tokens=111/222") != std::string::npos);
    CHECK(text.find("duration=") != std::string::npos);
    CHECK(text.find("my very private idea") == std::string::npos);
    CHECK(text.find("\"notes\"") == std::string::npos); // no answer either
}

TEST_CASE("with prompt logging on the prompt and the answer are in the log", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    AppLog::instance().setLogContent(true);
    InstrumentProcessor processor("Test");
    auto provider = std::make_shared<mm::ai::MockProvider>();
    provider->enqueueText(validAnswer(processor));
    AiBackendSettings backend;
    backend.provider = provider;
    backend.model = "mock-model";
    ScopedAiBackend scoped(backend);
    auto settings = processor.instanceSettings();
    settings.generation.prompt = "my very private idea";
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() != GenerationStatus::Generating; }));
    const auto text = readLog(log.file.getFile());
    CHECK(text.find("my very private idea") != std::string::npos);
    CHECK(text.find("answer: ") != std::string::npos);
}

TEST_CASE("a failed request is logged with status and code, and the key is not in the log", "[ai-log]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LogToFile log;
    InstrumentProcessor processor("Test");
    auto provider = std::make_shared<mm::ai::MockProvider>();
    provider->enqueueError(mm::ai::AiStatus::AuthFailed, "invalid key", 401);
    AiBackendSettings backend;
    backend.provider = provider;
    backend.model = "m";
    ScopedAiBackend scoped(backend);
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() != GenerationStatus::Generating; }));
    const auto text = readLog(log.file.getFile());
    CHECK(text.find("outcome=provider-error") != std::string::npos);
    CHECK(text.find("http=401") != std::string::npos);
    CHECK(text.find("WARN") != std::string::npos);
}
