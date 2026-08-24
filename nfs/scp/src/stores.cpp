#include "stores.hpp"

namespace scp {

std::string ScpEventSubscriptionStore::create(nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "scpsub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> ScpEventSubscriptionStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> ScpEventSubscriptionStore::patch(const std::string& sub_id,
                                                               const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
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

bool ScpEventSubscriptionStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(sub_id) > 0;
}

} // namespace scp
