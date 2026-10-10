#pragma once

#include "ai/Http.h"

namespace mm::plugin {

/// The network layer of the AI providers (SPEC 7.1), on `juce::WebInputStream`. Blocking, for a background thread.
/// A watcher thread per request ends the open connection when the token is cancelled or the time is up, so a cancel
/// does not wait for the network. No redirects are followed (a key must not follow a redirect to another host).
class JuceHttpClient : public mm::ai::IHttpClient {
public:
    mm::ai::HttpResponse send(const mm::ai::HttpRequest& request, const mm::ai::CancellationToken& token) override;
};

} // namespace mm::plugin
