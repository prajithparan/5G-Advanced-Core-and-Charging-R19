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

} // namespace pcf
