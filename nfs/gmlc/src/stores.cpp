#include "stores.hpp"

namespace gmlc {

void LocationContextStore::put(const std::string& key, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_[key] = std::move(context);
}

std::optional<nlohmann::json> LocationContextStore::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(key);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::string LocUpdateSubscriptionStore::create(nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "gmlcsub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

void GpsiAppLayerIdMappingStore::seed(const std::string& gpsi, const std::string& app_layer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpsi_to_app_layer_id_[gpsi] = app_layer_id;
    app_layer_id_to_gpsi_[app_layer_id] = gpsi;
}

std::optional<std::string> GpsiAppLayerIdMappingStore::gpsi_for(const std::string& app_layer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = app_layer_id_to_gpsi_.find(app_layer_id);
    if (it == app_layer_id_to_gpsi_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<std::string> GpsiAppLayerIdMappingStore::app_layer_id_for(const std::string& gpsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gpsi_to_app_layer_id_.find(gpsi);
    if (it == gpsi_to_app_layer_id_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

} // namespace gmlc
