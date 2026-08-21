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
// subscription-data, sm-data, and -- ADR-0106, gap-closure task #106 -- lcs-bca-data) -- ADR-0069,
// gap-closure Tier 1b. Real, disclosed: this real resource group is GET-only per the spec (no
// create/update operation exists at all), so there is no put()/apply_patch() here -- only seed()
// (used once, at startup, same real-data-source reasoning as this NF's own schema.postgres.sql
// header) and the four real get*() accessors.
class ProvisionedDataStore {
public:
    explicit ProvisionedDataStore(const std::string& conninfo);

    // Real UPSERT -- idempotent, safe to call every startup even if rows already exist from a
    // prior run (same real persistence property Tier 1a's own stores already have).
    void seed(const std::string& ue_id,
              const std::string& serving_plmn_id,
              std::optional<nlohmann::json> am_data,
              std::optional<nlohmann::json> smf_sel_data,
              std::optional<nlohmann::json> sm_data,
              std::optional<nlohmann::json> lcs_bca_data,
              std::optional<nlohmann::json> sms_mng_data,
              std::optional<nlohmann::json> sms_data,
              std::optional<nlohmann::json> trace_data);

    std::optional<nlohmann::json> get_am_data(const std::string& ue_id,
                                              const std::string& serving_plmn_id);
    std::optional<nlohmann::json> get_smf_sel_data(const std::string& ue_id,
                                                   const std::string& serving_plmn_id);
    std::optional<nlohmann::json> get_sm_data(const std::string& ue_id,
                                              const std::string& serving_plmn_id);
    // ADR-0106, gap-closure task #106: real LCS Broadcast Assistance Subscription Data
    // (QueryLcsBcaData), same real GET-only path shape as the other three sub-resources above.
    std::optional<nlohmann::json> get_lcs_bca_data(const std::string& ue_id,
                                                   const std::string& serving_plmn_id);
    // ADR-0125, gap-closure task #106: real SMS Management Subscription Data (QuerySmsMngData),
    // same real GET-only path shape as the other sub-resources above.
    std::optional<nlohmann::json> get_sms_mng_data(const std::string& ue_id,
                                                   const std::string& serving_plmn_id);
    // ADR-0126, gap-closure task #106: real SMS Subscription Data (QuerySmsData), same real
    // GET-only path shape as the other sub-resources above.
    std::optional<nlohmann::json> get_sms_data(const std::string& ue_id,
                                               const std::string& serving_plmn_id);
    // ADR-0127, gap-closure task #106: real Trace Data (QueryTraceData), same real GET-only path
    // shape as the other sub-resources above. Real response schema is a `oneOf` (full `TraceData`
    // object or a bare `SharedDataId` string) -- returned as opaque JSON, same as every other
    // sub-resource in this store.
    std::optional<nlohmann::json> get_trace_data(const std::string& ue_id,
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

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0104). Backs the real LCS
// Subscription Data resource (QueryLcsSubscriptionData -- real GET-only, no create/update
// operation exists in the spec at all, same shape as LcsPrivacyDataStore above).
class LcsSubscriptionDataStore {
public:
    explicit LcsSubscriptionDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0105). Backs the real LCS Mobile
// Originated Subscription Data resource (QueryLcsMoData -- real GET-only, no create/update
// operation exists in the spec at all, same shape as LcsSubscriptionDataStore above).
class LcsMoDataStore {
public:
    explicit LcsMoDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0107). Backs the real Parameter
// Provision (Document) resource (GetppData/ModifyPpData -- real GET+PATCH, RFC 6902, no
// PUT/DELETE exists for this resource in the spec). No POST/create operation exists either, so
// apply_patch() is upsert-capable (missing ueId = start from an empty document) -- same disclosed,
// deliberate precedent already established for AuthenticationSubscriptionDataStore/
// SmPolicyDataStore.
class PpDataStore {
public:
    explicit PpDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Throws nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as AuthenticationSubscriptionDataStore's own apply_patch.
    nlohmann::json apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0108). Backs the real Parameter
// Provision profile Data (Document) resource (QueryPPData -- real GET-only, no create/update
// operation exists in the spec at all, same shape as the other GET-only UDR resources).
class PpProfileDataStore {
public:
    explicit PpProfileDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0109). Backs the real Provisioned
// Parameter Data Entry resource (Create/Get/Delete PP Data Entry -- real PUT+GET+DELETE) and its
// real sibling collection resource (Get Multiple PP Data Entries). Composite key
// (ue_id, af_instance_id), same real shape as SmfRegistrationStore's own (ue_id, pdu_session_id).
class PpDataEntryStore {
public:
    explicit PpDataEntryStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, const std::string& af_instance_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id, const std::string& af_instance_id);
    bool remove(const std::string& ue_id, const std::string& af_instance_id);
    std::vector<nlohmann::json> list_for_ue(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0110). Backs the real individual
// Shared Data resource (GetIndividualSharedData -- real GET-only, no create/update operation
// exists in the spec at all). Genuinely NOT per-UE -- keyed by shared_data_id alone.
class SharedDataStore {
public:
    explicit SharedDataStore(const std::string& conninfo);

    void seed(const std::string& shared_data_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& shared_data_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0111). Backs the real
// Operator-Specific Data Container (Document) resource (QueryOperSpecData/ModifyOperSpecData --
// real GET+PATCH, RFC 6902, no PUT/DELETE exists for this resource in the spec). No POST/create
// operation exists either, so apply_patch() is upsert-capable -- same disclosed, deliberate
// precedent already established for PpDataStore.
class OperatorSpecificDataStore {
public:
    explicit OperatorSpecificDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Throws nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as PpDataStore's own apply_patch.
    nlohmann::json apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0112). Backs the real Event
// Exposure Data (Document) resource (QueryEEData -- real GET-only, no create/update operation
// exists in the spec at all, same shape as the other GET-only UDR resources).
class EeProfileDataStore {
public:
    explicit EeProfileDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0113). Backs the real `policy-data`
// group's UE Policy Set resource (ReadUEPolicySet/CreateOrReplaceUEPolicySet/UpdateUEPolicySet --
// real GET+PUT+PATCH, RFC 7396 merge-patch, no DELETE exists for this resource in the spec). Real
// distinct 201-vs-204 PUT response codes, same real `xmax = 0` UPSERT idiom already established.
class UePolicySetStore {
public:
    explicit UePolicySetStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Real RFC 7396 JSON Merge Patch -- upsert-capable, matching AmPolicyDataStore's own
    // merge_patch() shape.
    nlohmann::json merge_patch(const std::string& ue_id, const nlohmann::json& patch);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0114). Backs the real
// `policy-data` group's Operator-Specific Data resource (ReadOperatorSpecificData/
// UpdateOperatorSpecificData -- real GET+PATCH, RFC 6902, no PUT/DELETE exists for this resource
// in the spec). Real, genuinely distinct resource from OperatorSpecificDataStore above (separate
// real path/operationId pair, same schema reused via a real cross-file $ref). No POST/create
// operation exists either, so apply_patch() is upsert-capable.
class PolicyOperatorSpecificDataStore {
public:
    explicit PolicyOperatorSpecificDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Throws nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as OperatorSpecificDataStore's own apply_patch.
    nlohmann::json apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0115). Backs the real
// `policy-data` group's Sponsor Connectivity Data resource (ReadSponsorConnectivityData -- real
// GET-only, no create/update operation exists in the spec at all). Genuinely NOT per-UE -- keyed
// by sponsor_id alone.
class SponsorConnectivityDataStore {
public:
    explicit SponsorConnectivityDataStore(const std::string& conninfo);

    void seed(const std::string& sponsor_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& sponsor_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0116). Backs the real
// `policy-data` group's individual BDT (Background Data Transfer) Data resource
// (ReadIndividualBdtData/CreateIndividualBdtData/UpdateIndividualBdtData/
// DeleteIndividualBdtData -- real GET+PUT+PATCH+DELETE). Real, disclosed: put() is internally
// upsert-capable (idempotent-safe for retries) but the real spec's own PUT documents ONLY `201`
// as a success response (operationId literally "Create...", no update-via-PUT status
// documented) -- the caller always responds 201, not 204, matching the real spec literally.
class BdtDataStore {
public:
    explicit BdtDataStore(const std::string& conninfo);

    void put(const std::string& bdt_ref_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& bdt_ref_id);
    // Real RFC 7396 JSON Merge Patch.
    std::optional<nlohmann::json> merge_patch(const std::string& bdt_ref_id,
                                              const nlohmann::json& patch);
    bool remove(const std::string& bdt_ref_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0117). Backs the real PLMN UE
// Policy Set resource (/policy-data/plmns/{plmnId}/ue-policy-set, ReadPlmnUePolicySet -- real
// GET-only, no create/update operation exists for this resource at all, same real "provisioned
// out-of-band, seeded at startup" shape as CoverageRestrictionDataStore above). Reuses the real
// UePolicySet schema (same type as udr_ue_policy_set's own per-UE resource) but keyed by plmn_id,
// a genuinely distinct resource per TS29519_Policy_Data.yaml -- not a UE-scoped alias.
class PlmnUePolicySetStore {
public:
    explicit PlmnUePolicySetStore(const std::string& conninfo);

    void seed(const std::string& plmn_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& plmn_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0118). Backs the real Slice-specific
// Policy Control Data resource (/policy-data/slice-control-data/{snssai}, real GET+PATCH-only,
// no PUT/POST create operation exists at all -- confirmed by direct YAML read). Same disclosed,
// deliberate "no create operation exists, so merge_patch is upsert-capable" precedent already
// established for AmPolicyDataStore/SmPolicyDataStore, byte-for-byte matching AmPolicyDataStore's
// own class shape. Keyed by snssai (a plain string per this project's own established Snssai
// string-key convention).
class SlicePolicyDataStore {
public:
    explicit SlicePolicyDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& snssai);
    nlohmann::json merge_patch(const std::string& snssai, const nlohmann::json& patch);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0119). Backs the real group-specific
// Policy Control Data resource (/policy-data/group-control-data/{intGroupId}, real
// GET+PATCH-only, no PUT/POST create operation exists at all -- confirmed by direct YAML read).
// Same disclosed, deliberate "no create operation exists, so merge_patch is upsert-capable"
// precedent already established for AmPolicyDataStore/SlicePolicyDataStore. Keyed by intGroupId
// (real GroupId schema, TS29571_CommonData.yaml -- plain string, real pattern cited from
// TS 23.003 clause 19.9, no encoding ambiguity unlike SlicePolicyDataStore's own snssai key).
class GroupPolicyDataStore {
public:
    explicit GroupPolicyDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& int_group_id);
    nlohmann::json merge_patch(const std::string& int_group_id, const nlohmann::json& patch);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0120). Backs the real GetRoutingIDs
// resource (/routing-ids, TS29504_Nudr_GroupIDmap.yaml -- a genuinely DIFFERENT real Nudr API from
// every other store in this file, `Nudr_GroupIDmap` not `Nudr_DataRepository`: distinct real
// server base path (`/nudr-group-id-map/v1`, not `/nudr-dr/v2`) and distinct real OAuth2 scope
// (`nudr-group-id-map`, not `nudr-dr`). Real GET-only, no create/update operation exists for this
// resource at all -- same "provisioned out-of-band, seeded at startup" shape as every other
// GET-only store in this file, composite-keyed by (nf_type, nf_group_id) per the real spec's own
// two required query parameters, matching PpDataEntryStore's own composite-key precedent.
class RoutingIdStore {
public:
    explicit RoutingIdStore(const std::string& conninfo);

    void seed(const std::string& nf_type, const std::string& nf_group_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& nf_type, const std::string& nf_group_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0121). Backs the real NIDD
// Authorization Info context-data resource (CreateNIDDAuthorizationInfo/GetNiddAuthorizationInfo/
// ModifyNiddAuthorizationInfo/RemoveNiddAuthorizationInfo -- real PUT+GET+PATCH+DELETE). Real,
// disclosed correction: this project's own header comments previously lumped `nidd-authorizations`
// in with `ee-subscriptions`/`sdm-subscriptions` as a deferred "deeply nested sub-subscription"
// resource without individually checking the real YAML -- it is genuinely a flat per-UE document,
// same shape as AmfContextStore's own real distinct-201-vs-204 PUT + RFC 6902 JSON Patch, plus a
// real DELETE (which AmfContextStore's own resource doesn't have). Keyed by ueId (Supi).
class NiddAuthorizationInfoStore {
public:
    explicit NiddAuthorizationInfoStore(const std::string& conninfo);

    // Returns true if this was a new entry (for 201-vs-204 response selection).
    bool put(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Real RFC 6902 JSON Patch (already parsed) via nlohmann::json's built-in .patch(). Throws
    // nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as AmfContextStore's own apply_patch. Returns nullopt if ue_id doesn't
    // exist.
    std::optional<nlohmann::json> apply_patch(const std::string& ue_id,
                                              const nlohmann::json& patch_ops);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0122). Backs the real Query/Modify
// Identity Data by SUPI or GPSI resource (GetIdentityData/ModifyIdentityData -- real GET+PATCH,
// no PUT/POST create operation exists at all -- confirmed by direct YAML read). Same disclosed,
// deliberate "no create operation exists, so apply_patch is upsert-capable" precedent already
// established for PpDataStore/OperatorSpecificDataStore (real RFC 6902 JSON Patch, not RFC 7396
// merge-patch, unlike slice-control-data/group-control-data's own PATCH standard). Real, disclosed
// simplification: the real spec's optional `app-port-id` query param (GET) and conditional-request
// headers (If-None-Match/If-Modified-Since, Cache-Control/ETag/Last-Modified on the response) are
// not implemented -- same "no conditional-GET semantics anywhere in this project yet" gap as every
// other GET route.
class IdentityDataStore {
public:
    explicit IdentityDataStore(const std::string& conninfo);

    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Throws nlohmann::json::exception on a malformed patch -- caller turns that into a 400
    // ProblemDetails, same as PpDataStore's own apply_patch.
    nlohmann::json apply_patch(const std::string& ue_id, const nlohmann::json& patch_ops);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0123). Backs the real Query ODB
// Data by SUPI or GPSI resource (GetOdbData -- real GET-only, no create/update operation exists
// in the spec at all, same real "provisioned out-of-band, seeded at startup" shape as
// CoverageRestrictionDataStore above).
class OdbDataStore {
public:
    explicit OdbDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0128). Backs the real V2X
// Subscription Data resource (QueryV2xData -- real GET-only, no create/update operation exists in
// the spec at all, same real "provisioned out-of-band, seeded at startup" shape as
// CoverageRestrictionDataStore above). Keyed by `ueId` alone -- genuinely NOT part of the
// `provisioned-data` group's own `(ueId, servingPlmnId)` composite key shape.
class V2xDataStore {
public:
    explicit V2xDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0129). Backs the real ProSe Service
// Subscription Data resource -- real GET-only, no create/update operation exists in the spec at
// all, same real "provisioned out-of-band, seeded at startup" shape as V2xDataStore above. Real,
// disclosed: the spec's own operationId for this path is `QueryPorseData` (a real, literal typo
// in TS29505_Subscription_Data.yaml -- "Porse" not "Prose"), cited as-is, not corrected, since
// this project never invents or "fixes" spec text. Keyed by `ueId` alone.
class ProseDataStore {
public:
    explicit ProseDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0130). Backs the real User Consent
// Subscription Data resource (QueryUserConsentData -- real GET-only, no create/update operation
// exists in the spec at all, same real "provisioned out-of-band, seeded at startup" shape as
// ProseDataStore above). Real schema `UcSubscriptionData` (TS29503_Nudm_SDM.yaml) is a single
// optional `userConsentPerPurposeList` map, no `required` fields at all. Keyed by `ueId` alone.
class UcDataStore {
public:
    explicit UcDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0131). Backs the real Time
// Synchronization Subscription Data resource (QueryTimeSyncSubscriptionData -- real GET-only, no
// create/update operation exists in the spec at all, same real "provisioned out-of-band, seeded
// at startup" shape as UcDataStore above). Real schema `TimeSyncSubscriptionData`
// (TS29503_Nudm_SDM.yaml) requires `afReqAuthorizations` + `serviceIds`, unlike the last several
// GET-only resources closed which had every field optional. Keyed by `ueId` alone.
class TimeSyncDataStore {
public:
    explicit TimeSyncDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0133). Backs the real UE's Location
// Information (Document) resource (QueryUeLocation -- real GET-only, no create/update operation
// exists in the spec at all, same real "provisioned out-of-band, seeded at startup" shape as
// TimeSyncDataStore above). Real schema `LocationInfo` (TS29503_Nudm_UECM.yaml) requires a
// non-empty `registrationLocationInfoList`. Keyed by `ueId` alone.
class LocationDataStore {
public:
    explicit LocationDataStore(const std::string& conninfo);

    void seed(const std::string& ue_id, nlohmann::json data);
    std::optional<nlohmann::json> get(const std::string& ue_id);

private:
    std::mutex mutex_;
    pqxx::connection conn_;
};

} // namespace udr
