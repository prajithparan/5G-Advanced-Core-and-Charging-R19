#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/scp -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far.

namespace scp {

// Backs Nscp_EventExposure's `/subscriptions` collection + `/subscriptions/{subscriptionId}`
// individual resource. Keyed by an SCP-generated subscriptionId. Value is the raw
// `ScpEventExposureSubscription` (TS29570) as JSON. Real, disclosed (see nfs/scp/src/main.cpp's
// own top comment): this project's SCP does not perform the real TS 29.500 SS6.10-6.11 message-
// forwarding role that would generate genuine `ScpSignallingInfo` activity to report on, so this
// store is real, tested CRUD on the subscription resource itself, with no notification ever
// fired -- same disclosed-gap shape as nfs/nef's own `PfdSubscriptionStore` (ADR-0185).
class ScpEventSubscriptionStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& sub_id);
    // Returns the patched value (RFC 6902 JSON Patch via nlohmann::json::patch(), same precedent
    // as nfs/nrf's own NfRegistry::apply_patch), or nullopt if sub_id doesn't exist or the patch
    // itself throws.
    std::optional<nlohmann::json> patch(const std::string& sub_id, const nlohmann::json& patch_ops);
    bool remove(const std::string& sub_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

} // namespace scp
