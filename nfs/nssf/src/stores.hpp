#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Private to nfs/nssf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far (e.g. nfs/pcf's AmPolicyStore/
// AppSessionStore, see that file's own header for the same reasoning: these hold plain
// nlohmann::json rather than a generated struct because every route handler already builds/reads
// the full JSON representation directly, so a typed store would just add a second place for
// fields to drift out of sync with the wire format).

namespace nssf {

// Backs Nnssf_NSSAIAvailability's `/nssai-availability/{nfId}` resource (NSSAIAvailabilityPut/
// Patch/Delete). Keyed by the NF service consumer's own nfId (an AMF, per the real spec's own
// summary text on all three operations). Value is the raw submitted `NssaiAvailabilityInfo`
// (TS29531) as JSON -- PUT replaces it whole, PATCH applies an RFC 6902 JSON Patch via
// nlohmann::json::patch() (same precedent as nfs/nrf/src/registry.cpp's own NfRegistry::
// apply_patch), DELETE removes it.
class NssaiAvailabilityStore {
public:
    void put(const std::string& nf_id, nlohmann::json info);
    std::optional<nlohmann::json> get(const std::string& nf_id);
    // Returns the patched value, or nullopt if nf_id doesn't exist or the patch itself throws.
    std::optional<nlohmann::json> patch(const std::string& nf_id, const nlohmann::json& patch_ops);
    bool remove(const std::string& nf_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> data_;
};

// Backs Nnssf_NSSAIAvailability's `/nssai-availability/subscriptions` collection +
// `/nssai-availability/subscriptions/{subscriptionId}` individual resource. Keyed by an
// NSSF-generated subscriptionId. Value is the raw submitted `NssfEventSubscriptionCreateData`
// (TS29531) as JSON -- same store shape as NssaiAvailabilityStore above, same reasoning.
class NssaiAvailabilitySubscriptionStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& subscription_id);
    std::optional<nlohmann::json> patch(const std::string& subscription_id,
                                        const nlohmann::json& patch_ops);
    bool remove(const std::string& subscription_id);
    // Real `nssaiAvailabilityNotification` delivery (see main.cpp's deliver_nssai_availability_
    // notification) needs the full {subscriptionId, subscription} set to match each one's own
    // `taiList` against the TAI(s) an update affected.
    std::vector<std::pair<std::string, nlohmann::json>> list_all();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

} // namespace nssf
