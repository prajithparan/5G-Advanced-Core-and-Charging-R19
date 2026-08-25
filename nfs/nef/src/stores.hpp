#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Private to nfs/nef -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far.

namespace nef {

// Backs Nnef_PFDmanagement's `/applications`/`/applications/{appId}`/`/applications/partialpull`
// GET-only resource family. Keyed by `applicationId`. Value is the raw `PfdDataForApp` (TS29551)
// as JSON. Real, disclosed: this YAML has no operation anywhere that lets a caller WRITE PFD
// content into NEF (the real 3GPP AF-to-NEF PFD provisioning path is out of 3GPP's own SBI
// framework scope, not just unbuilt here) -- so this store is seed()-only, same precedent as
// several of UDR's own real "no live write path exists" stores (e.g. `SponsorConnectivityDataStore`
// before ADR-0182's own turn).
class PfdCatalogStore {
public:
    void seed(const std::string& application_id, nlohmann::json pfd_data_for_app);
    std::optional<nlohmann::json> get(const std::string& application_id);
    std::vector<nlohmann::json> get_many(const std::vector<std::string>& application_ids);
    // Real AppFetchPartialUpdate semantics: returns the stored PfdDataForApp only if its own
    // `pfdTimestamp` is later than `since` (ISO8601 UTC strings, lexicographically comparable in
    // this project's own generated DateTime format -- same real, disclosed string-compare
    // precedent used elsewhere), else nullopt ("not changed").
    std::optional<nlohmann::json> get_if_changed_since(const std::string& application_id,
                                                       const std::optional<std::string>& since);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> catalog_;
};

// Backs Nnef_PFDmanagement's `/subscriptions` collection + `/subscriptions/{subscriptionId}`
// individual resource. Keyed by an NEF-generated subscriptionId. Value is the raw
// `PfdSubscription` (TS29551) as JSON.
class PfdSubscriptionStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& sub_id);
    bool put(const std::string& sub_id, nlohmann::json subscription);
    bool remove(const std::string& sub_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// ADR-0207 (gap-closure task #164, first NEF slice). Backs Nnef_DNAIMapping's individual DNAI
// Mapping Subscription resource (TS29591 paths + TS29522_DNAIMapping.yaml schemas) -- keyed by an
// NEF-generated subscriptionId. Same real create/get/remove shape as `PfdSubscriptionStore`
// above, deliberately re-implemented as its own class (not shared) since each real resource type
// has its own real id namespace/lifecycle.
class DnaiMapSubStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& sub_id);
    bool remove(const std::string& sub_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// ADR-0207 (gap-closure task #164, first NEF slice). Backs Nnef_EASDeployment's individual EAS
// Deployment Event Subscription resource (TS29591). Keyed by an NEF-generated subscriptionId.
class EasDeploySubStore {
public:
    std::string create(nlohmann::json subscription);
    std::optional<nlohmann::json> get(const std::string& sub_id);
    bool remove(const std::string& sub_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

} // namespace nef
