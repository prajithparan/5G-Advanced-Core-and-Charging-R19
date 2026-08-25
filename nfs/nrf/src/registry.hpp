#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Private to nfs/nrf -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule. In-memory storage is a deliberate lab simplification (no persistence
// across restarts); see docs/DECISIONS.md for the ADR this NF's implementation supports.

namespace nrf {

// Backs RegisterNFInstance/GetNFInstance/UpdateNFInstance/DeregisterNFInstance/GetNFInstances/
// SearchNFInstances (TS29510_Nnrf_NFManagement.yaml, TS29510_Nnrf_NFDiscovery.yaml).
class NfRegistry {
public:
    // Returns true if this was a new registration (201), false if it replaced an existing
    // profile (200).
    bool put(const std::string& nf_instance_id, nlohmann::json profile);

    std::optional<nlohmann::json> get(const std::string& nf_instance_id);

    // Applies an RFC 6902 JSON Patch (already parsed) to the stored profile via nlohmann::json's
    // built-in .patch(). Throws nlohmann::json::exception (invalid patch op, failed "test", etc.)
    // -- caller is responsible for turning that into a ProblemDetails 4xx.
    std::optional<nlohmann::json> apply_patch(const std::string& nf_instance_id,
                                              const nlohmann::json& patch_ops);

    bool remove(const std::string& nf_instance_id);

    std::vector<nlohmann::json> list_all();

    // Simplification: filters only on nfType (target-nf-type), which is the one mandatory
    // discriminating query parameter SearchNFInstances actually needs for a single-NRF lab to be
    // useful. Other SearchNFInstances query parameters (service-names, snssais, dnn, ...) are
    // real per TS29510_Nnrf_NFDiscovery.yaml but not implemented -- disclosed gap, not silently
    // dropped; see docs/DECISIONS.md.
    std::vector<nlohmann::json> search_by_type(const std::string& target_nf_type);

    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #102, ADR-0079): records "now" as this
    // NF instance's most recent heartbeat, for sweep_expired's own use. Called on both initial
    // registration (put, below) and every later PATCH heartbeat.
    void touch_heartbeat(const std::string& nf_instance_id);

    // Real heartbeat-expiry sweep (open5GS's own real per-NF `t_no_heartbeat` mechanism,
    // src/nrf/nf-sm.c -- this project's own equivalent, not a byte-for-byte port). Removes and
    // returns the nfInstanceIds of every NF whose OWN profile declares a heartBeatTimer and whose
    // last heartbeat is older than heartBeatTimer + margin -- an NF that never specified
    // heartBeatTimer is never swept (nothing in the spec to expire it against, not invented).
    // Atomic with the check (single lock), so a heartbeat racing the sweep can't be lost.
    std::vector<std::string> sweep_expired(std::chrono::seconds margin);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> profiles_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_heartbeat_;
};

// Backs CreateSubscription/UpdateSubscription/RemoveSubscription
// (TS29510_Nnrf_NFManagement.yaml's SubscriptionData).
class SubscriptionRegistry {
public:
    // Assigns and returns a subscriptionId, stores subscription_data with that id filled in.
    nlohmann::json create(nlohmann::json subscription_data);

    std::optional<nlohmann::json> update(const std::string& subscription_id,
                                         nlohmann::json subscription_data);

    bool remove(const std::string& subscription_id);

    // All currently active subscriptions' nfStatusNotificationUri, for notification fan-out.
    // Simplification: does not filter by SubscriptionData.subscrCond (a real, conditionally-typed
    // filter in the schema) -- every subscriber gets every NF_REGISTERED/NF_DEREGISTERED event.
    // Disclosed in docs/DECISIONS.md, not silently dropped.
    std::vector<std::string> all_notification_uris();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md's Tier-B NRF audit, ADR-0193's own follow-up).
// Backs RetrieveStoredSearch/RetrieveCompleteSearch (TS29510_Nnrf_NFDiscovery.yaml) -- caches the
// real `nfInstances` array SearchNFInstances already computed for a given searchId, so a later
// `GET /searches/{searchId}` can re-fetch it without re-running the query. Real, disclosed: this
// project's own SearchNFInstances has no partial-vs-complete-profile distinction (no field
// filtering is implemented), so "stored" and "complete" results are the same real data -- see
// docs/DECISIONS.md. In-memory only, no TTL eviction, same disclosed simplification as
// NfRegistry/SubscriptionRegistry above.
class StoredSearchStore {
public:
    // Assigns and returns a new searchId for this real nfInstances result.
    std::string put(nlohmann::json nf_instances);

    std::optional<nlohmann::json> get(const std::string& search_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> results_;
    std::uint64_t next_id_ = 1;
};

} // namespace nrf
