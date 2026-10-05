#pragma once

// Shared by the JSON loaders (patterns, style profiles): type and fit checks with the path of the first problem.
// Internal to `core`; not part of the public interface.

#include <array>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace mm::core::detail {

using nlohmann::json;

template <typename E> struct EnumName {
    E value;
    std::string_view name;
};

class Reader {
public:
    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

    void fail(const std::string& path, const std::string& message) {
        if (error_.empty()) {
            error_ = path + ": " + message;
        }
    }

    static std::string child(const std::string& path, std::string_view key) {
        return path.empty() ? std::string(key) : path + "." + std::string(key);
    }
    static std::string element(const std::string& path, size_t i) { return path + "[" + std::to_string(i) + "]"; }

    /// The member `key` of an object, or nullptr (reports an error when required but missing).
    const json* member(const json& object, std::string_view key, const std::string& path, bool required) {
        if (failed()) {
            return nullptr;
        }
        const auto it = object.find(std::string(key));
        if (it == object.end()) {
            if (required) {
                fail(child(path, key), "missing");
            }
            return nullptr;
        }
        return &*it;
    }

    const json* objectMember(const json& parent, std::string_view key, const std::string& path, bool required) {
        const json* value = member(parent, key, path, required);
        if (value != nullptr && !value->is_object()) {
            fail(child(path, key), "expected an object");
            return nullptr;
        }
        return value;
    }

    const json* arrayMember(const json& parent, std::string_view key, const std::string& path, bool required) {
        const json* value = member(parent, key, path, required);
        if (value != nullptr && !value->is_array()) {
            fail(child(path, key), "expected an array");
            return nullptr;
        }
        return value;
    }

    template <typename T>
    void uintValue(const json& object, std::string_view key, const std::string& path, bool required, T& out) {
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_number_integer() || (!value->is_number_unsigned() && value->get<int64_t>() < 0)) {
            fail(child(path, key), "expected a non-negative integer");
            return;
        }
        const uint64_t raw = value->get<uint64_t>();
        if (raw > std::numeric_limits<T>::max()) {
            fail(child(path, key), "number too large");
            return;
        }
        out = static_cast<T>(raw);
    }

    template <typename T>
    void intValue(const json& object, std::string_view key, const std::string& path, bool required, T& out) {
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_number_integer()) {
            fail(child(path, key), "expected an integer");
            return;
        }
        if (value->is_number_unsigned() &&
            value->get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<T>::max())) {
            fail(child(path, key), "number out of range");
            return;
        }
        const int64_t raw =
            value->is_number_unsigned() ? static_cast<int64_t>(value->get<uint64_t>()) : value->get<int64_t>();
        if (raw < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
            raw > static_cast<int64_t>(std::numeric_limits<T>::max())) {
            fail(child(path, key), "number out of range");
            return;
        }
        out = static_cast<T>(raw);
    }

    void boolValue(const json& object, std::string_view key, const std::string& path, bool required, bool& out) {
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_boolean()) {
            fail(child(path, key), "expected true or false");
            return;
        }
        out = value->get<bool>();
    }

    void floatValue(const json& object, std::string_view key, const std::string& path, bool required, float& out) {
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_number()) {
            fail(child(path, key), "expected a number");
            return;
        }
        out = static_cast<float>(value->get<double>());
    }

    void stringValue(const json& object, std::string_view key, const std::string& path, bool required,
                     std::string& out) {
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_string()) {
            fail(child(path, key), "expected a string");
            return;
        }
        out = value->get<std::string>();
    }

    template <typename E, size_t N>
    void enumValue(const json& object, std::string_view key, const std::string& path, bool required,
                   const std::array<EnumName<E>, N>& table, E& out) {
        std::string text;
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_string()) {
            fail(child(path, key), "expected a string");
            return;
        }
        text = value->get<std::string>();
        for (const auto& entry : table) {
            if (entry.name == text) {
                out = entry.value;
                return;
            }
        }
        fail(child(path, key), "unknown value '" + text + "'");
    }

    /// A double (integers are accepted, too).
    void doubleValue(const json& object, std::string_view key, const std::string& path, bool required, double& out) {
        const json* value = member(object, key, path, required);
        if (value == nullptr) {
            return;
        }
        if (!value->is_number()) {
            fail(child(path, key), "expected a number");
            return;
        }
        out = value->get<double>();
    }

    /// An element of an array (or any value that is not a member of an object).
    template <typename T> void uintElement(const json& value, const std::string& path, T& out) {
        if (failed()) {
            return;
        }
        if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0)) {
            fail(path, "expected a non-negative integer");
            return;
        }
        const uint64_t raw = value.get<uint64_t>();
        if (raw > std::numeric_limits<T>::max()) {
            fail(path, "number too large");
            return;
        }
        out = static_cast<T>(raw);
    }

    void stringElement(const json& value, const std::string& path, std::string& out) {
        if (failed()) {
            return;
        }
        if (!value.is_string()) {
            fail(path, "expected a string");
            return;
        }
        out = value.get<std::string>();
    }

private:
    std::string error_;
};

} // namespace mm::core::detail
