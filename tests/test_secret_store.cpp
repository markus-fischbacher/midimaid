#include "platform/SecretStore.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

using namespace mm::platform;

namespace {

void exercise(ISecretStore& store) {
    CHECK_FALSE(store.get("anthropic").has_value());
    REQUIRE(store.set("anthropic", "sk-ant-TEST-1"));
    CHECK(store.get("anthropic") == "sk-ant-TEST-1");
    CHECK_FALSE(store.get("openai").has_value());     // another account
    REQUIRE(store.set("anthropic", "sk-ant-TEST-2")); // replaced
    CHECK(store.get("anthropic") == "sk-ant-TEST-2");
    REQUIRE(store.set("openai", "sk-TEST-3"));
    CHECK(store.get("anthropic") == "sk-ant-TEST-2");
    CHECK(store.get("openai") == "sk-TEST-3");
    CHECK(store.remove("anthropic"));
    CHECK_FALSE(store.get("anthropic").has_value());
    CHECK(store.get("openai") == "sk-TEST-3");
    CHECK(store.remove("anthropic")); // nothing there: still "none afterwards"
    CHECK(store.remove("openai"));
    CHECK_FALSE(store.get("openai").has_value());
}

} // namespace

TEST_CASE("the memory store keeps, replaces and removes secrets per account", "[secret]") {
    MemorySecretStore store;
    CHECK_FALSE(store.persistent());
    exercise(store);
}

TEST_CASE("the memory store is safe with several threads", "[secret]") {
    MemorySecretStore store;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&store, t] {
            for (int i = 0; i < 200; ++i) {
                const auto account = "a" + std::to_string(t);
                store.set(account, "v" + std::to_string(i));
                store.get(account);
                if (i % 50 == 0) {
                    store.remove(account);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    SUCCEED();
}

#ifdef __APPLE__
TEST_CASE("the keychain keeps secrets per account and forgets them when removed", "[secret][keychain]") {
    // A service name of its own, so that the keys of the real plugin are never touched.
    const auto service =
        "com.klirrwerk.midimaid.test." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto store = makeSystemSecretStore(service);
    REQUIRE(store != nullptr);
    CHECK(store->persistent());
    if (!store->set("probe", "x")) {
        WARN("The keychain is not available here (locked or no user session): the keychain test is skipped.");
        return;
    }
    store->remove("probe");
    exercise(*store);
}

TEST_CASE("the keychain handles odd secrets", "[secret][keychain]") {
    const auto service = "com.klirrwerk.midimaid.test.odd." +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto store = makeSystemSecretStore(service);
    if (!store->set("probe", "x")) {
        WARN("The keychain is not available here: skipped.");
        return;
    }
    const std::string unicode = "kéy-ünï-✓";
    const std::string longSecret(4000, 'k');
    REQUIRE(store->set("unicode", unicode));
    REQUIRE(store->set("long", longSecret));
    CHECK(store->get("unicode") == unicode);
    CHECK(store->get("long") == longSecret);
    REQUIRE(store->set("empty", ""));
    CHECK(store->get("empty").value_or("x").empty());
    for (const char* account : {"probe", "unicode", "long", "empty"}) {
        CHECK(store->remove(account));
    }
}
#endif
