#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

// Private to nfs/udr -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule. Real PostgreSQL persistence (libpqxx), same "one shared connection, one
// mutex" discipline every other PostgreSQL-backed store in this project already uses (see
// bss/product-catalog/src/store.hpp) -- ADR-0068, gap-closure Tier 1a from the free5GC/open5gs
// source comparison (both real references treat UDR as a genuinely persistent repository; this
// project's own in-memory std::unordered_map version did not survive a restart, unlike either
// reference's own real store).
//
// Deliberately NOT the same class as nfs/udm/src/stores.hpp's AmfRegistrationStore/
// SmfRegistrationStore, even though the shape is similar: UDR's context-data group uses RFC 6902
// JSON Patch (nlohmann::json::patch(), matching nfs/nrf's own UpdateNFInstance), not UDM's RFC
// 7396 JSON Merge Patch (nlohmann::json::merge_patch()) -- see docs/DECISIONS.md ADR-0025 for why
// the two Nudr_DataRepository PATCH operations use a different patch standard than UDM's.

namespace udr {

// Backs the AMF 3GPP-access context group (QueryAmfContext3gpp, CreateAmfContext3gpp,
// AmfContext3gpp). Keyed by ueId (Supi) -- one AMF context per UE, per
// TS29505_Subscription_Data.yaml's `/subscription-data/{ueId}/context-data/amf-3gpp-access`
// resource (singular, not a collection). No delete operation exists for this resource in the
// spec (checked, not assumed) -- disclosed in nfs/udr/src/main.cpp's file header.
class AmfContextStore {
public:
    explicit AmfContextStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Applies an RFC 6902 JSON Patch (already parsed) via nlohmann::json's built-in .patch().
    // Throws nlohmann::json::exception (invalid patch op, failed "test", ...) on a malformed
    // patch -- caller turns that into a 400 ProblemDetails, same as nfs/nrf's apply_patch.
    // Returns nullopt if ue_id doesn't exist.
    std::optional<nlohmann::json> apply_patch(const std::string& ue_id,
                                              const nlohmann::json& patch_ops);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Backs the SMF registration context group (QuerySmfRegList, QuerySmfRegistration,
// CreateOrUpdateSmfRegistration, UpdateSmfContext, DeleteSmfRegistration). Keyed by
// (ueId, pduSessionId), same nested-key shape as nfs/udm's SmfRegistrationStore and for the same
// reason (QuerySmfRegList needs to list every registration for a given ueId).
class SmfRegistrationStore {
public:
    explicit SmfRegistrationStore(const std::string& conninfo);

    bool
    put(const std::string& ue_id, const std::string& pdu_session_id, nlohmann::json registration);
    std::optional<nlohmann::json> get(const std::string& ue_id, const std::string& pdu_session_id);
    std::optional<nlohmann::json> apply_patch(const std::string& ue_id,
                                              const std::string& pdu_session_id,
                                              const nlohmann::json& patch_ops);
    bool remove(const std::string& ue_id, const std::string& pdu_session_id);
    std::vector<nlohmann::json> list_for_ue(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Backs the real Nudr_DataRepository `provisioned-data` group (am-data, smf-selection-
// subscription-data, sm-data) -- ADR-0069, gap-closure Tier 1b. Real, disclosed: this real
// resource group is GET-only per the spec (no create/update operation exists at all), so there is
// no put()/apply_patch() here -- only seed() (used once, at startup, same real-data-source
// reasoning as this NF's own schema.postgres.sql header) and the three real get*() accessors.
class ProvisionedDataStore {
public:
    explicit ProvisionedDataStore(const std::string& conninfo);

    // Real UPSERT -- idempotent, safe to call every startup even if rows already exist from a
    // prior run (same real persistence property Tier 1a's own stores already have).
    void seed(const std::string& ue_id,
              const std::string& serving_plmn_id,
              std::optional<nlohmann::json> am_data,
              std::optional<nlohmann::json> smf_sel_data,
              std::optional<nlohmann::json> sm_data);

    std::optional<nlohmann::json> get_am_data(const std::string& ue_id,
                                              const std::string& serving_plmn_id);
    std::optional<nlohmann::json> get_smf_sel_data(const std::string& ue_id,
                                                   const std::string& serving_plmn_id);
    std::optional<nlohmann::json> get_sm_data(const std::string& ue_id,
                                              const std::string& serving_plmn_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// ADR-0072 (gap-closure: real N28 end-to-end): backs the real Nudr_DataRepository `policy-data`
// group's SM policy resource (TS29519_Policy_Data.yaml, `/policy-data/ues/{ueId}/sm-data`, real
// schema SmPolicyData -- genuinely distinct from ProvisionedDataStore's own `sm_data` column
// above, see schema.postgres.sql's own comment). Real RFC 7396 JSON Merge Patch
// (application/merge-patch+json, confirmed directly against the YAML -- same patch standard as
// UDM's own AmfRegistrationStore/SmfRegistrationStore, NOT AmfContextStore's RFC 6902 above).
// Deliberately upsert-capable (merge_patch creates a fresh document from `{}` if ueId doesn't
// exist yet) so this resource can be created from a future GUI even though the real spec defines
// no POST/create operation for it at all -- see schema.postgres.sql's own comment for why this is
// a disclosed, deliberate choice.
class SmPolicyDataStore {
public:
    explicit SmPolicyDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    nlohmann::json merge_patch(const std::string& ue_id, const nlohmann::json& patch);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

} // namespace udr
