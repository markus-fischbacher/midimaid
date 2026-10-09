#include "ai/MockProvider.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

using namespace mm::ai;
using namespace std::chrono_literals;

TEST_CASE("the mock answers in the order it was given and records the requests", "[ai][mock]") {
    MockProvider mock;
    mock.enqueueText("first");
    mock.enqueueText("second");
    mock.enqueueError(AiStatus::AuthFailed, "bad key", 401);
    CancellationToken token;
    AiRequest request;
    request.userPrompt = "one";
    request.temperature = 0.7;
    auto a = mock.generate(request, token);
    request.userPrompt = "two";
    auto b = mock.generate(request, token);
    auto c = mock.generate(request, token);
    CHECK(a.ok());
    CHECK(a.text == "first");
    CHECK(b.text == "second");
    CHECK_FALSE(c.ok());
    CHECK(c.status == AiStatus::AuthFailed);
    CHECK(c.httpStatus == 401);
    CHECK(c.message == "bad key");
    REQUIRE(mock.requestCount() == 3);
    CHECK(mock.requests()[0].userPrompt == "one");
    CHECK(mock.requests()[1].userPrompt == "two");
    CHECK(mock.requests()[0].temperature == 0.7);
}

TEST_CASE("an empty queue is a network error, not a crash", "[ai][mock]") {
    MockProvider mock;
    const auto result = mock.generate({}, CancellationToken());
    CHECK(result.status == AiStatus::NetworkError);
    CHECK_FALSE(result.message.empty());
    CHECK(mock.requestCount() == 1);
}

TEST_CASE("a cancelled token ends a waiting request at once and discards the answer", "[ai][mock][cancel]") {
    MockProvider mock;
    mock.enqueueText("never delivered", 10s);
    CancellationToken token;
    std::thread canceller([token] {
        std::this_thread::sleep_for(30ms);
        token.cancel();
    });
    const auto before = std::chrono::steady_clock::now();
    const auto result = mock.generate({}, token);
    canceller.join();
    CHECK(result.status == AiStatus::Cancelled);
    CHECK(result.text.empty());
    CHECK(std::chrono::steady_clock::now() - before < 2s);
}

TEST_CASE("a token that was cancelled before the request cancels it without waiting", "[ai][mock][cancel]") {
    MockProvider mock;
    mock.enqueueText("x", 5s);
    CancellationToken token;
    token.cancel();
    const auto before = std::chrono::steady_clock::now();
    CHECK(mock.generate({}, token).status == AiStatus::Cancelled);
    CHECK(std::chrono::steady_clock::now() - before < 2s);
}

TEST_CASE("copies of a token share the flag", "[ai][cancel]") {
    CancellationToken a;
    const CancellationToken b = a;
    CHECK_FALSE(b.cancelled());
    a.cancel();
    CHECK(b.cancelled());
    CHECK(b.flag()->load());
    CHECK_FALSE(CancellationToken().cancelled()); // a new token is its own
}

TEST_CASE("the connection test reports what was set and respects a cancelled token", "[ai][mock]") {
    MockProvider mock;
    CHECK(mock.testConnection(CancellationToken()).ok);
    mock.setConnectionStatus({false, AiStatus::NetworkError, "no route", SchemaSupport::PromptOnly});
    const auto status = mock.testConnection(CancellationToken());
    CHECK_FALSE(status.ok);
    CHECK(status.message == "no route");
    CancellationToken token;
    token.cancel();
    CHECK(mock.testConnection(token).status == AiStatus::Cancelled);
    CHECK(mock.info().id == "mock");
}

TEST_CASE("requests from several threads each get exactly one answer", "[ai][mock][stress]") {
    MockProvider mock;
    constexpr int kThreads = 4;
    constexpr int kEach = 50;
    for (int i = 0; i < kThreads * kEach; ++i) {
        mock.enqueueText(std::to_string(i));
    }
    std::atomic<int> okCount{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kEach; ++i) {
                if (mock.generate({}, CancellationToken()).ok()) {
                    ++okCount;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(okCount.load() == kThreads * kEach);
    CHECK(mock.requestCount() == static_cast<size_t>(kThreads * kEach));
    CHECK_FALSE(mock.generate({}, CancellationToken()).ok()); // the queue is empty now
}
