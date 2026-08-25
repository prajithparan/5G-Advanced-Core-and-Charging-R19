#include "registry.hpp"

namespace nrf {

bool NfRegistry::put(const std::string& nf_instance_id, nlohmann::json profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool is_new = !profiles_.contains(nf_instance_id);
    profiles_[nf_instance_id] = std::move(profile);
    last_heartbeat_[nf_instance_id] = std::chrono::steady_clock::now();
    return is_new;
}

std::optional<nlohmann::json> NfRegistry::get(const std::string& nf_instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(nf_instance_id);
    if (it == profiles_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> NfRegistry::apply_patch(const std::string& nf_instance_id,
                                                      const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(nf_instance_id);
    if (it == profiles_.end()) {
        return std::nullopt;
    }
    it->second = it->second.patch(patch_ops);
    return std::make_optional(it->second);
}

bool NfRegistry::remove(const std::string& nf_instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_heartbeat_.erase(nf_instance_id);
    return profiles_.erase(nf_instance_id) > 0;
}

std::vector<nlohmann::json> NfRegistry::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    result.reserve(profiles_.size());
    for (const auto& [id, profile] : profiles_) {
        result.push_back(profile);
    }
    return result;
}

std::vector<nlohmann::json> NfRegistry::search_by_type(const std::string& target_nf_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    for (const auto& [id, profile] : profiles_) {
        if (profile.value("nfType", "") == target_nf_type) {
            result.push_back(profile);
        }
    }
    return result;
}

void NfRegistry::touch_heartbeat(const std::string& nf_instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_heartbeat_[nf_instance_id] = std::chrono::steady_clock::now();
}

std::vector<std::string> NfRegistry::sweep_expired(std::chrono::seconds margin) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expired;
    for (auto it = profiles_.begin(); it != profiles_.end();) {
        const auto& profile = it->second;
        if (!profile.contains("heartBeatTimer") || !profile.at("heartBeatTimer").is_number()) {
            ++it;
            continue;
        }
        const auto heartbeat_timer =
            std::chrono::seconds(profile.at("heartBeatTimer").get<std::int64_t>());
        const auto last_it = last_heartbeat_.find(it->first);
        const auto last = (last_it != last_heartbeat_.end()) ? last_it->second : now;
        if (now - last > heartbeat_timer + margin) {
            expired.push_back(it->first);
            last_heartbeat_.erase(it->first);
            it = profiles_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

nlohmann::json SubscriptionRegistry::create(nlohmann::json subscription_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string id = std::to_string(next_id_++);
    subscription_data["subscriptionId"] = id;
    subscriptions_[id] = subscription_data;
    return subscription_data;
}

std::optional<nlohmann::json> SubscriptionRegistry::update(const std::string& subscription_id,
                                                           nlohmann::json subscription_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    subscription_data["subscriptionId"] = subscription_id;
    it->second = subscription_data;
    return std::make_optional(it->second);
}

bool SubscriptionRegistry::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

std::vector<std::string> SubscriptionRegistry::all_notification_uris() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> uris;
    uris.reserve(subscriptions_.size());
    for (const auto& [id, sub] : subscriptions_) {
        if (sub.contains("nfStatusNotificationUri")) {
            uris.push_back(sub.at("nfStatusNotificationUri").get<std::string>());
        }
    }
    return uris;
}

std::string StoredSearchStore::put(nlohmann::json nf_instances) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string id = "search-" + std::to_string(next_id_++);
    results_.emplace(id, std::move(nf_instances));
    return id;
}

std::optional<nlohmann::json> StoredSearchStore::get(const std::string& search_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = results_.find(search_id);
    if (it == results_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

} // namespace nrf
