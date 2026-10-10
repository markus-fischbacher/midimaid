#include "plugin/AiConnection.h"

#include "MidiMaidConfig.h"
#include "plugin/AiBackend.h"
#include "plugin/AppLog.h"
#include "plugin/JuceHttpClient.h"

#include <juce_events/juce_events.h>
#include <string_view>

namespace mm::plugin {

namespace {

mm::ai::ModelsConfig embeddedModels() {
    int size = 0;
    const char* data = MidiMaidConfig::getNamedResource("models_json", size);
    return data != nullptr ? mm::ai::parseModelsConfig(std::string_view(data, static_cast<size_t>(size)))
                           : mm::ai::ModelsConfig{};
}

mm::ai::ProviderChoice choiceFrom(const mm::core::AiProviderSettings& settings) {
    mm::ai::ProviderChoice choice;
    choice.model = settings.model;
    choice.baseUrl = settings.baseUrl;
    choice.timeoutSeconds = settings.timeoutSeconds;
    choice.maxTokens = settings.maxTokens;
    choice.schemaLevel = settings.schemaLevel;
    return choice;
}

juce::File userDataFolder() {
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0) {
        folder = folder.getChildFile("Application Support");
    }
    return folder.getChildFile("Klirrwerk").getChildFile("MidiMaid");
}

/// Runs `done` on the message thread if the connection still exists.
void backToMessageThread(const std::shared_ptr<bool>& alive, std::function<void()> done) {
    juce::MessageManager::callAsync([alive, done = std::move(done)] {
        if (*alive) {
            done();
        }
    });
}

} // namespace

AiConnection::AiConnection(Dependencies dependencies) : dependencies_(std::move(dependencies)) {
    shipped_ = embeddedModels();
    merged_ = shipped_;
    if (!dependencies_.secrets) {
        dependencies_.secrets = std::shared_ptr<mm::platform::ISecretStore>(mm::platform::makeSystemSecretStore());
    }
    if (!dependencies_.http) {
        dependencies_.http = std::make_shared<JuceHttpClient>();
    }
}

AiConnection::~AiConnection() {
    *alive_ = false;
    cancel_.cancel();
    pool_.removeAllJobs(true, 10000);
}

AiConnection& AiConnection::instance() {
    static AiConnection connection([] {
        Dependencies dependencies;
        dependencies.userModelsFile = userDataFolder().getChildFile("models.json");
        return dependencies;
    }());
    return connection;
}

mm::ai::ModelsConfig AiConnection::models() const {
    const juce::ScopedLock lock(lock_);
    return merged_;
}

bool AiConnection::keysPersist() const {
    return dependencies_.secrets->persistent();
}

void AiConnection::reloadModelsFile() {
    if (dependencies_.userModelsFile == juce::File() || !dependencies_.userModelsFile.existsAsFile()) {
        return;
    }
    const auto user = mm::ai::parseModelsConfig(dependencies_.userModelsFile.loadFileAsString().toStdString());
    const juce::ScopedLock lock(lock_);
    merged_ = mm::ai::mergeModelsConfig(shipped_, user);
}

void AiConnection::applyLogSettings(const mm::core::AiSettings& ai) {
    if (dependencies_.settings().enabled()) { // a disabled store (tests) has no say over the log
        AppLog::instance().setLogContent(ai.logPrompts);
    }
}

void AiConnection::start() {
    if (started_) {
        return;
    }
    started_ = true;
    auto& store = dependencies_.settings();
    if (store.enabled()) {
        AppLog::instance().setFile(AppLog::defaultFile()); // tests run with a disabled store: no log file there
    }
    const auto alive = alive_;
    const auto tryRefresh = std::make_shared<std::function<void()>>();
    *tryRefresh = [this, alive, tryRefresh] {
        if (!*alive) {
            return;
        }
        if (dependencies_.settings().ready()) {
            refresh();
        } else {
            juce::Timer::callAfterDelay(100, [tryRefresh] { (*tryRefresh)(); });
        }
    };
    (*tryRefresh)();
}

std::shared_ptr<mm::ai::IAiProvider> AiConnection::buildProvider(const std::string& providerId,
                                                                 const mm::core::AiSettings& ai) {
    const mm::ai::ProviderEntry* entry = nullptr;
    mm::ai::ModelsConfig config;
    {
        const juce::ScopedLock lock(lock_);
        config = merged_;
    }
    entry = config.find(providerId);
    if (entry == nullptr) {
        return nullptr;
    }
    mm::ai::ProviderChoice choice;
    if (const auto it = ai.providers.find(providerId); it != ai.providers.end()) {
        choice = choiceFrom(it->second);
    }
    const auto key = dependencies_.secrets->get(providerId).value_or(std::string());
    return mm::ai::makeProvider(*entry, choice, key, dependencies_.http);
}

void AiConnection::refresh() {
    const auto ai = dependencies_.settings().get().ai;
    applyLogSettings(ai);
    const auto number = ++generation_;
    if (ai.activeProvider.empty()) {
        if (appliedBackend_) {
            AiBackend::instance().set({});
            appliedBackend_ = false;
        }
        return;
    }
    const auto alive = alive_;
    pool_.addJob([this, alive, ai, number] {
        reloadModelsFile();
        mm::ai::ModelsConfig config;
        {
            const juce::ScopedLock lock(lock_);
            config = merged_;
        }
        const auto* entry = config.find(ai.activeProvider);
        std::shared_ptr<mm::ai::IAiProvider> provider = buildProvider(ai.activeProvider, ai);
        AiBackendSettings settings;
        if (entry != nullptr) {
            mm::ai::ProviderChoice choice;
            if (const auto it = ai.providers.find(ai.activeProvider); it != ai.providers.end()) {
                choice = choiceFrom(it->second);
            }
            settings.provider = provider;
            settings.model = mm::ai::effectiveModel(*entry, choice);
            settings.timeoutSeconds = mm::ai::effectiveTimeout(*entry, choice);
            settings.maxTokens = mm::ai::effectiveMaxTokens(choice);
        }
        backToMessageThread(alive, [this, number, settings = std::move(settings), id = ai.activeProvider] {
            if (number != generation_) {
                return; // a newer refresh follows
            }
            if (settings.provider == nullptr) {
                AppLog::instance().write(LogLevel::Warning, "ai",
                                         "unknown provider in the settings: " + juce::String(id));
                if (appliedBackend_) {
                    AiBackend::instance().set({});
                    appliedBackend_ = false;
                }
                return;
            }
            AiBackend::instance().set(settings);
            appliedBackend_ = true;
            AppLog::instance().write(LogLevel::Info, "ai",
                                     "provider " + juce::String(id) + ", model " + juce::String(settings.model));
        });
    });
}

void AiConnection::saveKey(const std::string& providerId, std::string key, std::function<void(bool)> done) {
    const auto alive = alive_;
    pool_.addJob([this, alive, providerId, key = std::move(key), done = std::move(done)] {
        const bool ok = !key.empty() && dependencies_.secrets->set(providerId, key);
        backToMessageThread(alive, [this, ok, done] {
            if (ok) {
                refresh();
            }
            if (done) {
                done(ok);
            }
        });
    });
}

void AiConnection::removeKey(const std::string& providerId, std::function<void(bool)> done) {
    const auto alive = alive_;
    pool_.addJob([this, alive, providerId, done = std::move(done)] {
        const bool ok = dependencies_.secrets->remove(providerId);
        backToMessageThread(alive, [this, ok, done] {
            refresh();
            if (done) {
                done(ok);
            }
        });
    });
}

void AiConnection::hasKey(const std::string& providerId, std::function<void(bool)> done) {
    const auto alive = alive_;
    pool_.addJob([this, alive, providerId, done = std::move(done)] {
        const auto key = dependencies_.secrets->get(providerId);
        const bool present = key.has_value() && !key->empty();
        backToMessageThread(alive, [present, done] {
            if (done) {
                done(present);
            }
        });
    });
}

void AiConnection::testConnection(const std::string& providerId, std::function<void(mm::ai::ConnectionStatus)> done) {
    const auto ai = dependencies_.settings().get().ai;
    const auto alive = alive_;
    const auto token = cancel_;
    pool_.addJob([this, alive, providerId, ai, token, done = std::move(done)] {
        reloadModelsFile();
        mm::ai::ConnectionStatus status;
        const auto provider = buildProvider(providerId, ai);
        if (provider == nullptr) {
            status.status = mm::ai::AiStatus::BadRequest;
            status.message = "unknown provider";
        } else {
            status = provider->testConnection(token);
        }
        AppLog::instance().write(LogLevel::Info, "ai",
                                 "connection test " + juce::String(providerId) + ": status " +
                                     juce::String(static_cast<int>(status.status)) + (status.ok ? " ok" : " failed"));
        backToMessageThread(alive, [status, done] {
            if (done) {
                done(status);
            }
        });
    });
}

void AiConnection::loadModels(const std::string& providerId, std::function<void(std::vector<std::string>)> done) {
    const auto ai = dependencies_.settings().get().ai;
    const auto alive = alive_;
    const auto token = cancel_;
    pool_.addJob([this, alive, providerId, ai, token, done = std::move(done)] {
        reloadModelsFile();
        std::vector<std::string> models;
        if (const auto provider = buildProvider(providerId, ai)) {
            models = provider->listModels(token);
        }
        backToMessageThread(alive, [models = std::move(models), done] {
            if (done) {
                done(models);
            }
        });
    });
}

} // namespace mm::plugin
