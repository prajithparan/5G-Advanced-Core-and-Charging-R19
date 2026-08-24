#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Private to nfs/bsf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far (see e.g. nfs/pcf/src/stores.hpp's own
// header for the full reasoning: these hold plain nlohmann::json rather than a generated struct
// because every route handler already builds/reads the full JSON representation directly).

namespace bsf {

// Backs Nbsf_Management's `/pcfBindings` collection + `/pcfBindings/{bindingId}` individual
// resource. Keyed by a BSF-generated bindingId. Value is the raw `PcfBinding` (TS29521) as JSON.
// `find_by_combination` backs both CreatePCFBinding's real duplicate-check (the spec's own
// `ParameterCombination` = supi+dnn+snssai -- a second create for the same combination must
// return 403 with the existing binding's info, not create a second one) and GetPCFBindings' own
// exact-match query (which also filters on supi/dnn/snssai plus several other fields -- see
// main.cpp's own `matches_pcf_binding_filter` for the full filter set).
class PcfBindingStore {
public:
    std::string create(nlohmann::json binding);
    std::optional<nlohmann::json> get(const std::string& binding_id);
    // Returns the patched value (RFC 7396 merge-patch via nlohmann::json::merge_patch()), or
    // nullopt if binding_id doesn't exist.
    std::optional<nlohmann::json> patch(const std::string& binding_id,
                                        const nlohmann::json& merge_patch);
    // Returns the removed value (so the caller can still read its own supi/dnn/snssai to fire the
    // real onDataChange-style BsfNotification), or nullopt if binding_id didn't exist.
    std::optional<nlohmann::json> remove(const std::string& binding_id);
    // Real per-supi+dnn+snssai uniqueness check -- returns the {bindingId, binding} pair if one
    // already exists for this exact combination, else nullopt.
    std::optional<std::pair<std::string, nlohmann::json>> find_by_combination(
        const std::string& supi, const std::string& dnn, const nlohmann::json& snssai);
    std::vector<nlohmann::json> list_all();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> bindings_;
    std::uint64_t next_id_ = 1;
};

// Backs Nbsf_Management's `/subscriptions` collection + `/subscriptions/{subId}` individual
// resource. Keyed by a BSF-generated subId. Value is the raw `BsfSubscription` (TS29521) as JSON.
class BsfSubscriptionStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& sub_id);
    bool put(const std::string& sub_id, nlohmann::json subscription);
    // Returns the removed value, or nullopt if sub_id didn't exist.
    std::optional<nlohmann::json> remove(const std::string& sub_id);
    // Real `BsfNotification` delivery (main.cpp's own deliver_bsf_notification) needs the full
    // {subId, subscription} set to match each one's own `supi`/`snssaiDnnPairs`/`events` against
    // a fired PCF-binding create/delete.
    std::vector<std::pair<std::string, nlohmann::json>> list_all();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// Backs Nbsf_Management's `/pcf-ue-bindings` collection + `/pcf-ue-bindings/{bindingId}`
// individual resource. Keyed by a BSF-generated bindingId. Value is the raw `PcfForUeBinding`
// (TS29521) as JSON. Real spec: at most one PCF-for-UE binding should exist per real `supi`
// (there is no documented multi-binding-per-UE case, unlike per-PDU-session bindings) -- `
// find_by_supi` backs both the natural one-UE-one-binding invariant this project chooses to
// enforce and part of `GetPCFForUeBindings`' own supi/gpsi filter.
class PcfForUeBindingStore {
public:
    std::string create(nlohmann::json binding);
    std::optional<nlohmann::json> get(const std::string& binding_id);
    std::optional<nlohmann::json> patch(const std::string& binding_id,
                                        const nlohmann::json& merge_patch);
    std::optional<nlohmann::json> remove(const std::string& binding_id);
    std::optional<std::pair<std::string, nlohmann::json>> find_by_supi(const std::string& supi);
    std::vector<nlohmann::json> list_all();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> bindings_;
    std::uint64_t next_id_ = 1;
};

// Backs Nbsf_Management's `/pcf-mbs-bindings` collection + `/pcf-mbs-bindings/{bindingId}`
// individual resource. Keyed by a BSF-generated bindingId. Value is the raw `PcfMbsBinding`
// (TS29521) as JSON. `find_by_mbs_session_id` backs both CreatePCFMbsBinding's real duplicate
// check (same 403-with-existing-info pattern as `PcfBindingStore::find_by_combination`) and
// GetPCFMbsBinding's own required `mbs-session-id` query.
class PcfMbsBindingStore {
public:
    std::string create(nlohmann::json binding);
    std::optional<nlohmann::json> patch(const std::string& binding_id,
                                        const nlohmann::json& merge_patch);
    std::optional<nlohmann::json> remove(const std::string& binding_id);
    std::optional<std::pair<std::string, nlohmann::json>>
    find_by_mbs_session_id(const nlohmann::json& mbs_session_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> bindings_;
    std::uint64_t next_id_ = 1;
};

} // namespace bsf
