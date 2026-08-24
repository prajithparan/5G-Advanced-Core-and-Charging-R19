#include "stores.hpp"

namespace nssf {

void NssaiAvailabilityStore::put(const std::string& nf_id, nlohmann::json info) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_[nf_id] = std::move(info);
}

std::optional<nlohmann::json> NssaiAvailabilityStore::get(const std::string& nf_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(nf_id);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> NssaiAvailabilityStore::patch(const std::string& nf_id,
                                                            const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(nf_id);
    if (it == data_.end()) {
        return std::nullopt;
    }
    try {
        it->second = it->second.patch(patch_ops);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool NssaiAvailabilityStore::remove(const std::string& nf_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.erase(nf_id) > 0;
}

std::string NssaiAvailabilitySubscriptionStore::create(nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "nssai-avail-sub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json>
NssaiAvailabilitySubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json>
NssaiAvailabilitySubscriptionStore::patch(const std::string& subscription_id,
                                          const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    try {
        it->second = it->second.patch(patch_ops);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool NssaiAvailabilitySubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

std::vector<std::pair<std::string, nlohmann::json>> NssaiAvailabilitySubscriptionStore::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, nlohmann::json>> result;
    result.reserve(subscriptions_.size());
    for (const auto& [id, sub] : subscriptions_) {
        result.emplace_back(id, sub);
    }
    return result;
}

} // namespace nssf
