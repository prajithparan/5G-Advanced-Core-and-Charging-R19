#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/upf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// Backs Nupf_EventExposure's /ee-subscriptions collection (ADR-0203): CreateSubscription assigns
// a server-generated subscriptionId; Modify/DeleteSubscription look it up by that ref. Same real
// assign-id/store/remove shape as nfs/smf/src/event_subscription_store.hpp's own
// EventSubscriptionStore (ADR-0201) -- deliberately mirrored, not reused directly, since NFs may
// not include each other's private headers. In-memory only, no persistence across restarts --
// same disclosed simplification as every other id-keyed store in this project.

namespace upf {

class EventSubscriptionStore {
public:
    // Assigns and returns a new subscriptionId, stores the subscription under it.
    std::string create(nlohmann::json subscription);

    std::optional<nlohmann::json> get(const std::string& subscription_id);

    // Replaces the stored subscription for an existing subscriptionId. Returns false if it
    // doesn't exist.
    bool update(const std::string& subscription_id, nlohmann::json subscription);

    bool remove(const std::string& subscription_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

} // namespace upf
