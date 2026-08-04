#pragma once

#include <nlohmann/json.hpp>

#include <optional>

// Generic (de)serialization helpers shared by hand-written framework types (problem_details.hpp)
// and, from Phase 1 onward, by codegen-emitted DTOs -- OpenAPI's `nullable`/optional properties map
// naturally onto std::optional<T> via these two functions rather than each generated type
// reimplementing the same null-check boilerplate.

namespace sbi_core {

template <typename T>
void put_optional(nlohmann::json& j, const char* key, const std::optional<T>& v) {
    if (v.has_value()) {
        j[key] = *v;
    }
}

template <typename T>
void get_optional(const nlohmann::json& j, const char* key, std::optional<T>& v) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        v = it->template get<T>();
    } else {
        v = std::nullopt;
    }
}

} // namespace sbi_core
