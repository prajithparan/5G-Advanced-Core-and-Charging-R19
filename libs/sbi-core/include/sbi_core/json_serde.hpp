#pragma once

#include <nlohmann/json.hpp>

#include <memory>
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

// std::shared_ptr<T> overloads, for the same optional-field use as above but where T is a
// genuinely cyclic type in 3GPP's own schema (tools/sbi-codegen detects this and switches field
// representation to shared_ptr specifically because T is only forward-declared, not complete, at
// this field's point of use -- see docs/DECISIONS.md ADR-0052 and render.py's own comment on why
// std::optional<T>/std::vector<T> direct embedding is ill-formed there but shared_ptr<T> is not:
// its deleter is type-erased at construction time, in this very .cpp file, where T IS complete by
// then, not at the point the containing struct's destructor is implicitly instantiated in the
// header).
template <typename T>
void put_optional(nlohmann::json& j, const char* key, const std::shared_ptr<T>& v) {
    if (v) {
        j[key] = *v;
    }
}

template <typename T>
void get_optional(const nlohmann::json& j, const char* key, std::shared_ptr<T>& v) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        v = std::make_shared<T>(it->template get<T>());
    } else {
        v = nullptr;
    }
}

} // namespace sbi_core
