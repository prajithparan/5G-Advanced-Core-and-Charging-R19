#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "aka_crypto/milenage.hpp"

// Private to nfs/udm -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store so far (ADR-0015).

namespace udm {

// Backs Nudm_UEAU's GenerateAuthData. Keyed by SUPI. In-memory-only subscriber authentication
// data (K, OPc, SQN, AMF, and which method -- 5G_AKA or EAP_AKA_PRIME -- this subscriber uses) --
// in a real deployment this is UDR-provisioned data (Nudr_DataRepository's authentication-data
// group), which nfs/udr deliberately does not implement yet (see docs/DECISIONS.md ADR-0025's
// deferred list). Seeded at startup with a small, fixed set of test subscribers -- see
// nfs/udm/src/main.cpp -- not provisionable via any API. See ADR-0026 for the original bare-
// monotonic-counter SQN model, and ADR-0037 for AUTS/SQN-resynchronisation support added on top of
// it (still no full TS 33.102 Annex C.2/C.3 windowing/array scheme -- SQN_MS+1 on a verified
// resync, nothing more sophisticated).
struct AuthenticationSubscription {
    aka_crypto::Key128 k;
    aka_crypto::Key128 opc;
    aka_crypto::Sqn sqn;
    aka_crypto::Amf amf;
    std::string authentication_method; // "5G_AKA" or "EAP_AKA_PRIME"
};

class AuthenticationSubscriptionStore {
public:
    void seed(const std::string& supi, AuthenticationSubscription sub);
    // Returns the subscriber's current data and, in the same locked step, advances its stored SQN
    // by 1 (mod 2^48) so the next GenerateAuthData call for this SUPI gets a fresh vector.
    // nullopt if supi is unknown.
    std::optional<AuthenticationSubscription> get_and_advance_sqn(const std::string& supi);

    // Verifies AUTS (TS 24.501 §9.11.3.1) against this subscriber's own K/OPc and the RAND from
    // the AuthenticationRequest the UE is resynchronising against, and, iff genuine, resets the
    // stored SQN to the UE's real SQN_MS + 1 (mod 2^48) -- the simplest re-sync scheme (TS 33.102
    // Annex C.2), matching this store's existing bare-monotonic-counter model rather than
    // introducing full Annex C windowing. Returns std::nullopt if supi is unknown; true if AUTS
    // verified and SQN was reset; false if AUTS failed to verify (SQN is left untouched in that
    // case -- a failed verification must not silently move the subscriber's SQN state).
    std::optional<bool> resync_sqn(const std::string& supi,
                                   const aka_crypto::Key128& rand,
                                   const aka_crypto::Auts& auts);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, AuthenticationSubscription> subs_;
};

// Backs Nudm_UEAU's ConfirmAuth (create) and DeleteAuth (remove). Keyed by a UDM-generated
// authEventId; also tracks the owning supi so DeleteAuth can be scoped correctly, same pattern as
// nfs/udm's own SdmSubscriptionStore.
class AuthEventStore {
public:
    std::string create(const std::string& supi, nlohmann::json event);
    bool remove(const std::string& supi, const std::string& auth_event_id);

private:
    struct Entry {
        std::string supi;
        nlohmann::json event;
    };
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> events_;
    std::uint64_t next_id_ = 1;
};

// Backs Nudm_UECM's AMF-3GPP-access registration group (3GppRegistration, Get3GppRegistration,
// Update3GppRegistration, deregAMF). Keyed by ueId (Supi) -- one AMF registration per UE, per
// TS29503_Nudm_UECM.yaml's `/{ueId}/registrations/amf-3gpp-access` resource (singular, not a
// collection).
class AmfRegistrationStore {
public:
    void put(const std::string& ue_id, nlohmann::json registration);
    std::optional<nlohmann::json> get(const std::string& ue_id);
    // Applies an RFC 7396 JSON Merge Patch (already parsed) via nlohmann::json's built-in
    // .merge_patch(). Returns nullopt if ue_id doesn't exist.
    std::optional<nlohmann::json> merge_patch(const std::string& ue_id,
                                              const nlohmann::json& patch);
    bool remove(const std::string& ue_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> registrations_;
};

// Backs Nudm_UECM's SMF registration group (Registration, RetrieveSmfRegistration,
// UpdateSmfRegistration, SmfDeregistration, GetSmfRegistration). Keyed by (ueId, pduSessionId) --
// a UE can have multiple concurrent PDU sessions, each with its own SMF registration
// (`/{ueId}/registrations/smf-registrations/{pduSessionId}`); GetSmfRegistration additionally
// needs to list every registration for a given ueId
// (`/{ueId}/registrations/smf-registrations`), hence the nested-map shape rather than a single
// flat map keyed by a composed string.
class SmfRegistrationStore {
public:
    void
    put(const std::string& ue_id, const std::string& pdu_session_id, nlohmann::json registration);
    std::optional<nlohmann::json> get(const std::string& ue_id, const std::string& pdu_session_id);
    std::optional<nlohmann::json> merge_patch(const std::string& ue_id,
                                              const std::string& pdu_session_id,
                                              const nlohmann::json& patch);
    bool remove(const std::string& ue_id, const std::string& pdu_session_id);
    // All registrations for ue_id, in no particular order. Empty if ue_id has none.
    std::vector<nlohmann::json> list_for_ue(const std::string& ue_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_map<std::string, nlohmann::json>> registrations_;
};

// Backs Nudm_SDM's Subscribe/Unsubscribe (`/{ueId}/sdm-subscriptions`). Same
// assign-id/store/remove shape as nfs/nrf's SubscriptionRegistry and nfs/amf's IdKeyedStore --
// UDM-local rather than reused from another NF's private header, per CLAUDE.md. Scoped by ueId
// (stored alongside the subscription data) the same way nfs/amf/src/subscriptions.hpp's
// UeN1N2Subscription pairs a subscription with its owning ueContextId, so Unsubscribe can 404 a
// subscriptionId that exists but belongs to a different ueId.
struct SdmSubscriptionEntry {
    std::string ue_id;
    nlohmann::json data;
};

class SdmSubscriptionStore {
public:
    std::string create(SdmSubscriptionEntry entry);
    std::optional<SdmSubscriptionEntry> get(const std::string& subscription_id);
    bool remove(const std::string& subscription_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, SdmSubscriptionEntry> subscriptions_;
    std::uint64_t next_id_ = 1;
};

} // namespace udm
