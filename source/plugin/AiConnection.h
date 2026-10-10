#pragma once

#include "ai/Http.h"
#include "ai/ModelsConfig.h"
#include "platform/SecretStore.h"
#include "plugin/GlobalSettingsStore.h"

#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <string>
#include <vector>

namespace mm::plugin {

/// Connects the settings of the musician to the AI backend (SPEC 7, 8.4, 8.5): reads the provider from the settings
/// file, the key from the keychain (in the background), builds the provider and hands it to `AiBackend`. Without an
/// active provider nothing is set and nothing goes to the network (SPEC 7.5). Everything blocking runs on a worker
/// thread; the results come back on the message thread.
class AiConnection {
public:
    struct Dependencies {
        std::function<GlobalSettingsStore&()> settings = [] { return std::ref(globalSettings()); };
        std::shared_ptr<mm::platform::ISecretStore> secrets;
        std::shared_ptr<mm::ai::IHttpClient> http;
        juce::File userModelsFile; ///< `models.json` of the user (overrides the shipped one); empty: none
    };

    explicit AiConnection(Dependencies dependencies);
    ~AiConnection();

    /// The connection of this process: the keychain of the system, the JUCE network layer, the files of the data
    /// folder. Created at the first call.
    static AiConnection& instance();

    /// Message thread. Applies the settings as soon as the settings file is read (at once when it is). Once.
    void start();
    /// Message thread. Applies the current settings: builds the provider (the key is read in the background) and sets
    /// `AiBackend`. An inactive provider clears the backend, but only when this connection had set it.
    void refresh();

    /// The shipped list of providers with the user's file on top (what the dialog offers).
    mm::ai::ModelsConfig models() const;

    /// Key handling. The key is never kept here: it goes to the keychain and into the provider. Callbacks run on the
    /// message thread unless the connection is gone.
    void saveKey(const std::string& providerId, std::string key, std::function<void(bool)> done);
    void removeKey(const std::string& providerId, std::function<void(bool)> done);
    void hasKey(const std::string& providerId, std::function<void(bool)> done);
    /// True when keys survive the session (a keychain), false when they live in memory only (SPEC 8.5).
    bool keysPersist() const;

    /// Tests the connection with the current settings of `providerId` (SPEC 8.4); it can only lower the level.
    void testConnection(const std::string& providerId, std::function<void(mm::ai::ConnectionStatus)> done);
    /// The models the provider offers right now; empty when it cannot say.
    void loadModels(const std::string& providerId, std::function<void(std::vector<std::string>)> done);

private:
    struct Built {
        std::shared_ptr<mm::ai::IAiProvider> provider;
        mm::ai::ProviderEntry entry;
        mm::ai::ProviderChoice choice;
    };
    /// Worker thread: the provider for `providerId` with the key from the keychain, or null (unknown provider).
    std::shared_ptr<mm::ai::IAiProvider> buildProvider(const std::string& providerId, const mm::core::AiSettings& ai);
    void applyLogSettings(const mm::core::AiSettings& ai);
    void reloadModelsFile();

    Dependencies dependencies_;
    mutable juce::CriticalSection lock_;
    mm::ai::ModelsConfig shipped_;
    mm::ai::ModelsConfig merged_;
    bool started_ = false;        // message thread
    bool appliedBackend_ = false; // message thread: this connection has set a provider
    uint64_t generation_ = 0;     // message thread: the newest refresh wins
    mm::ai::CancellationToken cancel_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true); // message thread
    juce::ThreadPool pool_{1};
};

} // namespace mm::plugin
