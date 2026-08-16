#include "stores.hpp"

namespace pcf {

std::string AmPolicyStore::create(nlohmann::json policy_association) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "polasso-" + std::to_string(next_id_++);
    associations_.emplace(id, std::move(policy_association));
    return id;
}

std::optional<nlohmann::json> AmPolicyStore::get(const std::string& pol_asso_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = associations_.find(pol_asso_id);
    if (it == associations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool AmPolicyStore::put(const std::string& pol_asso_id, nlohmann::json policy_association) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = associations_.find(pol_asso_id);
    if (it == associations_.end()) {
        return false;
    }
    it->second = std::move(policy_association);
    return true;
}

bool AmPolicyStore::remove(const std::string& pol_asso_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return associations_.erase(pol_asso_id) > 0;
}

std::string SmPolicyStore::create(nlohmann::json sm_policy_control) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "smpolicy-" + std::to_string(next_id_++);
    policies_.emplace(id, std::move(sm_policy_control));
    return id;
}

std::optional<nlohmann::json> SmPolicyStore::get(const std::string& sm_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(sm_policy_id);
    if (it == policies_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SmPolicyStore::put(const std::string& sm_policy_id, nlohmann::json sm_policy_control) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(sm_policy_id);
    if (it == policies_.end()) {
        return false;
    }
    it->second = std::move(sm_policy_control);
    return true;
}

bool SmPolicyStore::remove(const std::string& sm_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return policies_.erase(sm_policy_id) > 0;
}

void SpendingLimitTrackingStore::put(const std::string& sm_policy_id, Entry entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_[sm_policy_id] = std::move(entry);
}

std::optional<SpendingLimitTrackingStore::Entry>
SpendingLimitTrackingStore::get(const std::string& sm_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(sm_policy_id);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SpendingLimitTrackingStore::update_status(const std::string& sm_policy_id,
                                               nlohmann::json status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(sm_policy_id);
    if (it == entries_.end()) {
        return false;
    }
    it->second.last_status = std::move(status);
    return true;
}

std::optional<SpendingLimitTrackingStore::Entry>
SpendingLimitTrackingStore::remove(const std::string& sm_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(sm_policy_id);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    auto entry = std::move(it->second);
    entries_.erase(it);
    return std::make_optional(std::move(entry));
}

} // namespace pcf
