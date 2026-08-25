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

std::string AppSessionStore::create(nlohmann::json app_session_context) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "appsess-" + std::to_string(next_id_++);
    sessions_.emplace(id, std::move(app_session_context));
    return id;
}

std::optional<nlohmann::json> AppSessionStore::get(const std::string& app_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(app_session_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool AppSessionStore::put(const std::string& app_session_id, nlohmann::json app_session_context) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(app_session_id);
    if (it == sessions_.end()) {
        return false;
    }
    it->second = std::move(app_session_context);
    return true;
}

bool AppSessionStore::remove(const std::string& app_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(app_session_id) > 0;
}

std::string UePolicyStore::create(nlohmann::json policy_association) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "uepolasso-" + std::to_string(next_id_++);
    associations_.emplace(id, std::move(policy_association));
    return id;
}

std::optional<nlohmann::json> UePolicyStore::get(const std::string& pol_asso_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = associations_.find(pol_asso_id);
    if (it == associations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool UePolicyStore::put(const std::string& pol_asso_id, nlohmann::json policy_association) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = associations_.find(pol_asso_id);
    if (it == associations_.end()) {
        return false;
    }
    it->second = std::move(policy_association);
    return true;
}

bool UePolicyStore::remove(const std::string& pol_asso_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return associations_.erase(pol_asso_id) > 0;
}

std::string PcEventExposureStore::create(nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "pceesub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> PcEventExposureStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool PcEventExposureStore::put(const std::string& subscription_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool PcEventExposureStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

std::string AppAmContextStore::create(nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "appamctx-" + std::to_string(next_id_++);
    contexts_.emplace(id, std::move(context));
    return id;
}

std::optional<nlohmann::json> AppAmContextStore::get(const std::string& app_am_context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(app_am_context_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool AppAmContextStore::put(const std::string& app_am_context_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(app_am_context_id);
    if (it == contexts_.end()) {
        return false;
    }
    it->second = std::move(context);
    return true;
}

bool AppAmContextStore::remove(const std::string& app_am_context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.erase(app_am_context_id) > 0;
}

std::string MbsAppSessionStore::create(nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "mbsappsess-" + std::to_string(next_id_++);
    contexts_.emplace(id, std::move(context));
    return id;
}

std::optional<nlohmann::json> MbsAppSessionStore::get(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool MbsAppSessionStore::put(const std::string& context_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(context_id);
    if (it == contexts_.end()) {
        return false;
    }
    it->second = std::move(context);
    return true;
}

bool MbsAppSessionStore::remove(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.erase(context_id) > 0;
}

std::string MbsPolicyStore::create(nlohmann::json policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "mbspolicy-" + std::to_string(next_id_++);
    policies_.emplace(id, std::move(policy));
    return id;
}

std::optional<nlohmann::json> MbsPolicyStore::get(const std::string& mbs_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(mbs_policy_id);
    if (it == policies_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool MbsPolicyStore::put(const std::string& mbs_policy_id, nlohmann::json policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(mbs_policy_id);
    if (it == policies_.end()) {
        return false;
    }
    it->second = std::move(policy);
    return true;
}

bool MbsPolicyStore::remove(const std::string& mbs_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return policies_.erase(mbs_policy_id) > 0;
}

std::string PdtqPolicyStore::create(nlohmann::json policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "pdtqpolicy-" + std::to_string(next_id_++);
    policies_.emplace(id, std::move(policy));
    return id;
}

std::optional<nlohmann::json> PdtqPolicyStore::get(const std::string& pdtq_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(pdtq_policy_id);
    if (it == policies_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool PdtqPolicyStore::put(const std::string& pdtq_policy_id, nlohmann::json policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(pdtq_policy_id);
    if (it == policies_.end()) {
        return false;
    }
    it->second = std::move(policy);
    return true;
}

bool PdtqPolicyStore::remove(const std::string& pdtq_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return policies_.erase(pdtq_policy_id) > 0;
}

std::string BdtPolicyStore::create(nlohmann::json policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "bdtpolicy-" + std::to_string(next_id_++);
    policies_.emplace(id, std::move(policy));
    return id;
}

std::optional<nlohmann::json> BdtPolicyStore::get(const std::string& bdt_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(bdt_policy_id);
    if (it == policies_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool BdtPolicyStore::put(const std::string& bdt_policy_id, nlohmann::json policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = policies_.find(bdt_policy_id);
    if (it == policies_.end()) {
        return false;
    }
    it->second = std::move(policy);
    return true;
}

bool BdtPolicyStore::remove(const std::string& bdt_policy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return policies_.erase(bdt_policy_id) > 0;
}

} // namespace pcf
