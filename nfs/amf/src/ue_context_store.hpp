#pragma once

#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/amf -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule.
//
// Backs the four per-ueContextId Namf_Communication operations implemented so far
// (ReleaseUEContext, EBIAssignment, UEContextTransfer, RegistrationStatusUpdate --
// TS29518_Namf_Communication.yaml). In-memory only, no persistence across restarts -- same
// disclosed simplification as nfs/nrf/src/registry.hpp (see docs/DECISIONS.md ADR-0015).
//
// IMPORTANT, disclosed gap: nothing currently calls put(). CreateUEContext (the operation that
// would populate this store for real) requires multipart/related request bodies, which
// libs/sbi-core does not support yet -- deferred per docs/DECISIONS.md ADR-0016's multipart
// discussion. Until CreateUEContext lands, every lookup here returns nullopt and the four
// operations above can only ever take their "no such UE context" (404) branch in practice. Their
// "found" branches are still implemented for real (see main.cpp) -- correct per spec, exercised by
// nothing but a future CreateUEContext, not silently skipped.

namespace amf {

class UeContextStore {
public:
    void put(const std::string& ue_context_id, nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& ue_context_id);
    bool remove(const std::string& ue_context_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> contexts_;
};

} // namespace amf
