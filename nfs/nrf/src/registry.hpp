#pragma once

#include <nlohmann/json.hpp>

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

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> profiles_;
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

} // namespace nrf
