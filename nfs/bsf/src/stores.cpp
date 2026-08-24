#include "stores.hpp"

namespace bsf {

std::string PcfBindingStore::create(nlohmann::json binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "pcfbinding-" + std::to_string(next_id_++);
    bindings_.emplace(id, std::move(binding));
    return id;
}

std::optional<nlohmann::json> PcfBindingStore::get(const std::string& binding_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PcfBindingStore::patch(const std::string& binding_id,
                                                     const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(merge_patch);
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PcfBindingStore::remove(const std::string& binding_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    auto removed = std::move(it->second);
    bindings_.erase(it);
    return std::make_optional(std::move(removed));
}

std::optional<std::pair<std::string, nlohmann::json>> PcfBindingStore::find_by_combination(
    const std::string& supi, const std::string& dnn, const nlohmann::json& snssai) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, binding] : bindings_) {
        if (binding.value("supi", "") == supi && binding.value("dnn", "") == dnn &&
            binding.value("snssai", nlohmann::json::object()) == snssai) {
            return std::make_optional(std::make_pair(id, binding));
        }
    }
    return std::nullopt;
}

std::vector<nlohmann::json> PcfBindingStore::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    result.reserve(bindings_.size());
    for (const auto& [id, binding] : bindings_) {
        result.push_back(binding);
    }
    return result;
}

std::string BsfSubscriptionStore::create(nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "bsfsub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> BsfSubscriptionStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool BsfSubscriptionStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

std::optional<nlohmann::json> BsfSubscriptionStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    auto removed = std::move(it->second);
    subscriptions_.erase(it);
    return std::make_optional(std::move(removed));
}

std::vector<std::pair<std::string, nlohmann::json>> BsfSubscriptionStore::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, nlohmann::json>> result;
    result.reserve(subscriptions_.size());
    for (const auto& [id, sub] : subscriptions_) {
        result.emplace_back(id, sub);
    }
    return result;
}

std::string PcfForUeBindingStore::create(nlohmann::json binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "pcfuebinding-" + std::to_string(next_id_++);
    bindings_.emplace(id, std::move(binding));
    return id;
}

std::optional<nlohmann::json> PcfForUeBindingStore::get(const std::string& binding_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PcfForUeBindingStore::patch(const std::string& binding_id,
                                                          const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(merge_patch);
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PcfForUeBindingStore::remove(const std::string& binding_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    auto removed = std::move(it->second);
    bindings_.erase(it);
    return std::make_optional(std::move(removed));
}

std::optional<std::pair<std::string, nlohmann::json>>
PcfForUeBindingStore::find_by_supi(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, binding] : bindings_) {
        if (binding.value("supi", "") == supi) {
            return std::make_optional(std::make_pair(id, binding));
        }
    }
    return std::nullopt;
}

std::vector<nlohmann::json> PcfForUeBindingStore::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    result.reserve(bindings_.size());
    for (const auto& [id, binding] : bindings_) {
        result.push_back(binding);
    }
    return result;
}

std::string PcfMbsBindingStore::create(nlohmann::json binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "pcfmbsbinding-" + std::to_string(next_id_++);
    bindings_.emplace(id, std::move(binding));
    return id;
}

std::optional<nlohmann::json> PcfMbsBindingStore::patch(const std::string& binding_id,
                                                        const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(merge_patch);
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PcfMbsBindingStore::remove(const std::string& binding_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(binding_id);
    if (it == bindings_.end()) {
        return std::nullopt;
    }
    auto removed = std::move(it->second);
    bindings_.erase(it);
    return std::make_optional(std::move(removed));
}

std::optional<std::pair<std::string, nlohmann::json>>
PcfMbsBindingStore::find_by_mbs_session_id(const nlohmann::json& mbs_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, binding] : bindings_) {
        if (binding.value("mbsSessionId", nlohmann::json::object()) == mbs_session_id) {
            return std::make_optional(std::make_pair(id, binding));
        }
    }
    return std::nullopt;
}

} // namespace bsf
