#include "stores.hpp"

namespace lmf {

std::string UpSubscriptionStore::create(nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "upsub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

bool UpSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

void LocationContextStore::put(const std::string& ldr_reference, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_[ldr_reference] = std::move(context);
}

std::optional<nlohmann::json> LocationContextStore::get(const std::string& ldr_reference) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(ldr_reference);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

void UpConfigStore::put(const std::string& key, nlohmann::json config) {
    std::lock_guard<std::mutex> lock(mutex_);
    configs_[key] = std::move(config);
}

void UpConfigStore::terminate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    configs_.erase(key);
}

std::optional<nlohmann::json> UpConfigStore::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = configs_.find(key);
    if (it == configs_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

} // namespace lmf
