#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/pcf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store so far. See docs/DECISIONS.md ADR-0028.
//
// Both stores hold plain nlohmann::json rather than a generated struct: PolicyAssociation and
// SmPolicyControl are large DTOs (dozens of optional fields each) and every route handler already
// builds/reads the full JSON representation directly (matching nfs/udm's SdmSubscriptionStore/
// AuthEventStore precedent for exactly this shape of resource), so a typed store would just add a
// second, redundant place these fields could drift out of sync with the wire format.

namespace pcf {

// Backs Npcf_AMPolicyControl's individual AM Policy Association resource. Keyed by a PCF-generated
// polAssoId. Value is a full `PolicyAssociation` (TS29507) serialized as JSON.
class AmPolicyStore {
public:
    std::string create(nlohmann::json policy_association);
    std::optional<nlohmann::json> get(const std::string& pol_asso_id);
    // Returns false if pol_asso_id doesn't exist.
    bool put(const std::string& pol_asso_id, nlohmann::json policy_association);
    bool remove(const std::string& pol_asso_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> associations_;
    std::uint64_t next_id_ = 1;
};

// Backs Npcf_SMPolicyControl's individual SM Policy resource. Keyed by a PCF-generated
// smPolicyId. Value is a full `SmPolicyControl` (TS29512, i.e. {context, policy} together) --
// GetSMPolicy returns both; Create/Update only return `policy`, so callers slice what they need.
class SmPolicyStore {
public:
    std::string create(nlohmann::json sm_policy_control);
    std::optional<nlohmann::json> get(const std::string& sm_policy_id);
    bool put(const std::string& sm_policy_id, nlohmann::json sm_policy_control);
    bool remove(const std::string& sm_policy_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> policies_;
    std::uint64_t next_id_ = 1;
};

// ADR-0072 (gap-closure: real N28 end-to-end). Backs PCF's own real subscription lifecycle with
// CHF's Nchf_SpendingLimitControl -- keyed by the smPolicyId this subscription was opened for (so
// DeleteSMPolicy can find and unsubscribe it, and the real statusNotification callback, PCF's own
// notifUri target, can find which SM policy a pushed SpendingLimitStatus belongs to). In-memory
// only, same disclosed simplification as every other PCF store (see this file's own header).
class SpendingLimitTrackingStore {
public:
    struct Entry {
        std::string chf_subscription_id;
        std::string supi;
        nlohmann::json last_status; // most recent real SpendingLimitStatus (initial subscribe
                                    // response, updated by later statusNotification pushes)
    };

    void put(const std::string& sm_policy_id, Entry entry);
    std::optional<Entry> get(const std::string& sm_policy_id);
    // Updates last_status only (used by the statusNotification callback) -- no-op, returns false,
    // if sm_policy_id isn't tracked.
    bool update_status(const std::string& sm_policy_id, nlohmann::json status);
    // Removes and returns the entry (so the caller can still read chf_subscription_id to issue the
    // real DELETE to CHF) -- std::nullopt if nothing was tracked for this smPolicyId (a real,
    // valid case: not every SM policy subscribes to spending limits).
    std::optional<Entry> remove(const std::string& sm_policy_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #103, ADR-0080). Backs
// Npcf_PolicyAuthorization's individual Application Session Context resource. Keyed by a
// PCF-generated appSessionId. Value is a full `AppSessionContext` (TS29514, i.e. {ascReqData,
// ascRespData, evsNotif} together) serialized as JSON -- same nlohmann::json-store precedent as
// AmPolicyStore/SmPolicyStore above (a large DTO whose route handlers already build/read the full
// JSON directly).
class AppSessionStore {
public:
    std::string create(nlohmann::json app_session_context);
    std::optional<nlohmann::json> get(const std::string& app_session_id);
    // Returns false if app_session_id doesn't exist.
    bool put(const std::string& app_session_id, nlohmann::json app_session_context);
    bool remove(const std::string& app_session_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> sessions_;
    std::uint64_t next_id_ = 1;
};

// ADR-0204 (gap-closure task #163). Backs Npcf_UEPolicyControl's individual UE Policy Association
// resource (TS29525). Keyed by a PCF-generated polAssoId -- a distinct id space from
// AmPolicyStore's own polAssoId above (different resource, different YAML, same real 3GPP naming
// convention for the path parameter). Value is a full `PolicyAssociation_Npcf_UEPolicyControl`
// serialized as JSON, same store-shape precedent as AmPolicyStore/SmPolicyStore above.
class UePolicyStore {
public:
    std::string create(nlohmann::json policy_association);
    std::optional<nlohmann::json> get(const std::string& pol_asso_id);
    bool put(const std::string& pol_asso_id, nlohmann::json policy_association);
    bool remove(const std::string& pol_asso_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> associations_;
    std::uint64_t next_id_ = 1;
};

// ADR-0204 (gap-closure task #163). Backs Npcf_EventExposure's individual Policy Control Events
// Subscription resource (TS29523). Keyed by a PCF-generated subscriptionId. Value is a full
// `PcEventExposureSubsc` serialized as JSON, same store-shape precedent as the stores above.
class PcEventExposureStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& subscription_id);
    bool put(const std::string& subscription_id, nlohmann::json subscription);
    bool remove(const std::string& subscription_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// ADR-0205 (gap-closure task #163, second PCF slice). Backs Npcf_AMPolicyAuthorization's
// individual Application AM Context resource (TS29534). Keyed by a PCF-generated appAmContextId.
// The `events-subscription` subresource (`updateAmEventsSubsc`/`DeleteAmEventsSubsc`) is stored
// nested inside the context's own `evSubsc` field, same real approach the pre-existing
// `Npcf_PolicyAuthorization` app-sessions events-subscription routes already use on
// `AppSessionStore`.
class AppAmContextStore {
public:
    std::string create(nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& app_am_context_id);
    bool put(const std::string& app_am_context_id, nlohmann::json context);
    bool remove(const std::string& app_am_context_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> contexts_;
    std::uint64_t next_id_ = 1;
};

// ADR-0205 (gap-closure task #163, second PCF slice). Backs Npcf_MBSPolicyAuthorization's
// individual MBS Application Session Context resource (TS29537). Keyed by a PCF-generated
// contextId.
class MbsAppSessionStore {
public:
    std::string create(nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& context_id);
    bool put(const std::string& context_id, nlohmann::json context);
    bool remove(const std::string& context_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> contexts_;
    std::uint64_t next_id_ = 1;
};

// ADR-0205 (gap-closure task #163, second PCF slice). Backs Npcf_MBSPolicyControl's individual
// MBS Policy resource (TS29537). Keyed by a PCF-generated mbsPolicyId.
class MbsPolicyStore {
public:
    std::string create(nlohmann::json policy);
    std::optional<nlohmann::json> get(const std::string& mbs_policy_id);
    bool put(const std::string& mbs_policy_id, nlohmann::json policy);
    bool remove(const std::string& mbs_policy_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> policies_;
    std::uint64_t next_id_ = 1;
};

// ADR-0206 (gap-closure task #163, third and final PCF slice). Backs Npcf_PDTQPolicyControl's
// individual PDTQ policy resource (TS29543). Keyed by a PCF-generated pdtqPolicyId.
class PdtqPolicyStore {
public:
    std::string create(nlohmann::json policy);
    std::optional<nlohmann::json> get(const std::string& pdtq_policy_id);
    bool put(const std::string& pdtq_policy_id, nlohmann::json policy);
    bool remove(const std::string& pdtq_policy_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> policies_;
    std::uint64_t next_id_ = 1;
};

// ADR-0206 (gap-closure task #163, third and final PCF slice). Backs Npcf_BDTPolicyControl's
// individual BDT policy resource (TS29554). Keyed by a PCF-generated bdtPolicyId.
class BdtPolicyStore {
public:
    std::string create(nlohmann::json policy);
    std::optional<nlohmann::json> get(const std::string& bdt_policy_id);
    bool put(const std::string& bdt_policy_id, nlohmann::json policy);
    bool remove(const std::string& bdt_policy_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> policies_;
    std::uint64_t next_id_ = 1;
};

} // namespace pcf
