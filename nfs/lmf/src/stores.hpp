#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/lmf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far.

namespace lmf {

// Backs Nlmf_Location's `/up-subscriptions` collection + `/up-subscriptions/{subscriptionId}`
// individual resource -- the one LMF-family resource in this NF with a real, full create+delete
// lifecycle declared in the YAML (unlike GMLC's own create-only `/loc-update-subs`, ADR-0189).
// Real, disclosed gap: the real `201` response for `UpSubscriptions` declares no `Location` (or
// any other) header and its own body schema (`UpSubscription`) carries no id field either -- the
// real YAML never tells a caller what `subscriptionId` to use for a later `DeleteSubscription`.
// This implementation adds a real `Location` header at the HTTP layer (not a fabricated JSON
// field) as the only way the resource is actually usable, matching this project's own convention
// on every other subscription-creation route -- disclosed here as filling a genuine spec gap for
// usability, not as content the YAML itself declares.
class UpSubscriptionStore {
public:
    std::string create(nlohmann::json subscription);
    bool remove(const std::string& subscription_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// Backs Nlmf_Location's `/location-context-transfer` operation -- a real LMF-to-LMF (or
// AMF-relocation-triggered) location-context push, keyed by the real, required `ldrReference`.
// No GET operation exists for this resource either (same disclosed shape as GMLC's own
// `/location-update`, ADR-0189); kept for future introspection/tests, not because a live route
// reads it back this turn.
class LocationContextStore {
public:
    void put(const std::string& ldr_reference, nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& ldr_reference);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> contexts_;
};

// Backs Nlmf_Location's `/configure-up` operation -- a real secure-LCS-UP-connection
// setup/modify/terminate, keyed by whichever of supi/gpsi the real spec's own `anyOf` requires
// (unlike GMLC's own store-key workaround, this key requirement IS a real, declared spec
// constraint, enforced as a real `400`, not invented). `terminate(key)` removes the entry
// (`lcsUpConnectionInd: TERMINATION`); `put()` creates/updates it otherwise (SETUP or unspecified).
class UpConfigStore {
public:
    void put(const std::string& key, nlohmann::json config);
    void terminate(const std::string& key);
    std::optional<nlohmann::json> get(const std::string& key);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> configs_;
};

} // namespace lmf
