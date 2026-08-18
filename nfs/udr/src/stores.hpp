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

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0093): backs the AMF non-3GPP-access
// context group (QueryAmfContextNon3gpp, CreateAmfContextNon3gpp -- real
// TS29505_Subscription_Data.yaml `/subscription-data/{ueId}/context-data/amf-non-3gpp-access`).
// Deliberately NOT the same class/table as AmfContextStore above -- a real, distinct resource per
// spec (schema `AmfNon3GppAccessRegistration`, not `Amf3GppAccessRegistration`), same "one UE can
// have both a 3GPP and a non-3GPP AMF context simultaneously" real architecture the two separate
// spec paths already imply. Real, confirmed (not assumed): no PATCH/DELETE operation exists for
// this resource in the spec, same as its 3GPP-access sibling.
class AmfNon3GppContextStore {
public:
    explicit AmfNon3GppContextStore(const std::string& conninfo);

    bool put(const std::string& ue_id, nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& ue_id);

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

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0083). Backs the real
// Authentication Data group's `authentication-subscription` document (QueryAuthSubsData,
// ModifyAuthenticationSubscription -- RFC 6902 JSON Patch, same standard AmfContextStore above
// uses, NOT SmPolicyDataStore's RFC 7396 merge-patch). See schema.postgres.sql's own comment for
// why this is a real, genuinely distinct table from UDM's own in-process
// AuthenticationSubscriptionStore. No create/delete operation exists in the real spec for this
// resource (checked, not assumed) -- `apply_patch` is upsert-capable (same disclosed,
// deliberate divergence SmPolicyDataStore's own header already established) so this store still
// has a real way to originate a document.
class AuthenticationSubscriptionDataStore {
public:
    explicit AuthenticationSubscriptionDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    nlohmann::json apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Backs the real Authentication Data group's `authentication-status` document
// (CreateAuthenticationStatus/QueryAuthenticationStatus/DeleteAuthenticationStatus -- real PUT
// (replace, not patch) + GET + DELETE, confirmed per-operation from the YAML).
class AuthenticationStatusStore {
public:
    explicit AuthenticationStatusStore(const std::string& conninfo);

    void put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Backs the real `policy-data` group's AM Policy resource (ReadAccessAndMobilityPolicyData,
// UpdateAccessAndMobilityPolicyData -- real GET + RFC 7396 merge-patch, the real UDR-side backing
// for PCF's own Npcf_AMPolicyControl). Same real upsert-on-PATCH shape as SmPolicyDataStore
// above.
class AmPolicyDataStore {
public:
    explicit AmPolicyDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    nlohmann::json merge_patch(const std::string& ue_id, const nlohmann::json& patch);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0097). Backs the real SMSF
// 3GPP-access context-data resource (CreateSmsfContext3gpp/QuerySmsfContext3gpp/
// DeleteSmsfContext3gpp -- real GET+PUT+DELETE, same shape as AuthenticationStatusStore above).
// Deliberately NOT the same class as SmsfNon3GppContextStore below, even though both real spec
// resources share the identical schema (`SmsfRegistration`) -- same "real, distinct resource, not
// a rename" precedent AmfContextStore/AmfNon3GppContextStore already established.
class SmsfContext3gppStore {
public:
    explicit SmsfContext3gppStore(const std::string& conninfo);

    void put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Backs the real SMSF non-3GPP-access context-data resource (CreateSmsfContextNon3gpp/
// QuerySmsfContextNon3gpp/DeleteSmsfContextNon3gpp) -- see SmsfContext3gppStore's own comment for
// why this is a separate class/table.
class SmsfNon3GppContextStore {
public:
    explicit SmsfNon3GppContextStore(const std::string& conninfo);

    void put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0098). Backs the real IP-SM-GW
// Registration context-data resource (CreateIpSmGwContext/QueryIpSmGwContext/
// ModifyIpSmGwContext/DeleteIpSmGwContext -- real PUT+GET+PATCH+DELETE, the richest operation set
// of any context-data resource this project has closed so far). Real RFC 6902 JSON Patch (same
// standard AmfContextStore's own apply_patch already uses), not RFC 7396 merge-patch.
class IpSmGwContextStore {
public:
    explicit IpSmGwContextStore(const std::string& conninfo);

    void put(const std::string& ue_id, nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Throws nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as AmfContextStore's own apply_patch. Returns nullopt if ue_id doesn't
    // exist.
    std::optional<nlohmann::json> apply_patch(const std::string& ue_id,
                                              const nlohmann::json& patch_ops);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0099). Backs the real Message
// Waiting Data (Document) resource (CreateMessageWaitingData/QueryMessageWaitingData/
// ModifyMessageWaitingData/DeleteMessageWaitingData -- real PUT+GET+PATCH+DELETE). Unlike
// IpSmGwContextStore's own always-204 put(), MWD's real PUT genuinely distinguishes 201-Created
// from 204-updated per the YAML -- same real "xmax = 0" UPSERT idiom AmfContextStore's own put()
// already established, reused here rather than IpSmGwContextStore's simpler always-update one.
class MessageWaitingDataStore {
public:
    explicit MessageWaitingDataStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Throws nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as IpSmGwContextStore's own apply_patch. Returns nullopt if ue_id
    // doesn't exist.
    std::optional<nlohmann::json> apply_patch(const std::string& ue_id,
                                              const nlohmann::json& patch_ops);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0100). Backs the real Roaming
// Information (Document) resource (UpdateRoamingInformation/QueryRoamingInformation -- real
// GET+PUT, same shape as AmfNon3GppContextStore above, including the real distinct 201-vs-204 PUT
// response codes). No PATCH/DELETE exists for this resource in the spec (checked, not assumed).
class RoamingInformationStore {
public:
    explicit RoamingInformationStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0101). Backs the real PEI
// Information (Document) resource (CreateOrUpdatePeiInformation/QueryPeiInformation -- real
// GET+PUT, same shape as RoamingInformationStore above, including the real distinct 201-vs-204
// PUT response codes). No PATCH/DELETE exists for this resource in the spec (checked, not
// assumed).
class PeiInfoStore {
public:
    explicit PeiInfoStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0102). Backs the real Enhanced
// Coverage Restriction Data resource (QueryCoverageRestrictionData -- real GET-only, no
// create/update operation exists in the spec at all, same real "provisioned out-of-band, seeded
// at startup" shape as ProvisionedDataStore above).
class CoverageRestrictionDataStore {
public:
    explicit CoverageRestrictionDataStore(const std::string& conninfo);

    // Real UPSERT -- idempotent, safe to call every startup even if rows already exist from a
    // prior run (same real persistence property ProvisionedDataStore's own seed() has).
    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0103). Backs the real LCS Privacy
// Subscription Data resource (QueryLcsPrivacyData -- real GET-only, no create/update operation
// exists in the spec at all, same shape as CoverageRestrictionDataStore above).
class LcsPrivacyDataStore {
public:
    explicit LcsPrivacyDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

} // namespace udr
