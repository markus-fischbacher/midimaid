#include "platform/ProcessInstances.h"

#include <algorithm>
#include <cstdlib>
#include <map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mm::platform {

namespace {

constexpr const char* kVariable = "MIDIMAID_INSTANCES";

std::map<std::string, int> parse(std::string_view state) {
    std::map<std::string, int> counts;
    while (!state.empty()) {
        const auto end = std::min(state.find(';'), state.size());
        const auto item = state.substr(0, end);
        const auto eq = item.find('=');
        if (eq != std::string_view::npos && eq > 0) {
            const std::string number(item.substr(eq + 1));
            if (!number.empty() && number.size() < 6 &&
                std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                counts[std::string(item.substr(0, eq))] = std::stoi(number);
            }
        }
        state.remove_prefix(std::min(end + 1, state.size()));
    }
    return counts;
}

std::string current() {
    const char* value = std::getenv(kVariable);
    return value != nullptr ? value : "";
}

void store(const std::string& state) {
#if defined(_WIN32)
    SetEnvironmentVariableA(kVariable, state.c_str());
#else
    setenv(kVariable, state.c_str(), 1);
#endif
}

} // namespace

std::string withInstanceCount(std::string_view state, std::string_view tag, int delta) {
    auto counts = parse(state);
    auto& count = counts[std::string(tag)];
    count = std::max(0, count + delta);
    std::string out;
    for (const auto& [name, value] : counts) {
        if (value > 0) {
            out += (out.empty() ? "" : ";") + name + "=" + std::to_string(value);
        }
    }
    return out;
}

int instanceCount(std::string_view state, std::string_view tag) {
    const auto counts = parse(state);
    const auto it = counts.find(std::string(tag));
    return it == counts.end() ? 0 : it->second;
}

bool otherTagsPresent(std::string_view state, std::string_view tag) {
    const auto counts = parse(state);
    return std::any_of(counts.begin(), counts.end(),
                       [&](const auto& entry) { return entry.first != tag && entry.second > 0; });
}

void registerInstance(std::string_view tag) {
    store(withInstanceCount(current(), tag, 1));
}

void unregisterInstance(std::string_view tag) {
    store(withInstanceCount(current(), tag, -1));
}

bool otherFormatsPresent(std::string_view tag) {
    return otherTagsPresent(current(), tag);
}

} // namespace mm::platform
