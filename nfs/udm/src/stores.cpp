#include "stores.hpp"

namespace udm {

void AmfRegistrationStore::put(const std::string& ue_id, nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[ue_id] = std::move(registration);
}

std::optional<nlohmann::json> AmfRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> AmfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

bool AmfRegistrationStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return registrations_.erase(ue_id) > 0;
}

void SmfRegistrationStore::put(const std::string& ue_id,
                               const std::string& pdu_session_id,
                               nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[ue_id][pdu_session_id] = std::move(registration);
}

std::optional<nlohmann::json> SmfRegistrationStore::get(const std::string& ue_id,
                                                        const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    return std::make_optional(session_it->second);
}

std::optional<nlohmann::json> SmfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                const std::string& pdu_session_id,
                                                                const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    session_it->second.merge_patch(patch);
    return std::make_optional(session_it->second);
}

bool SmfRegistrationStore::remove(const std::string& ue_id, const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return false;
    }
    return ue_it->second.erase(pdu_session_id) > 0;
}

std::vector<nlohmann::json> SmfRegistrationStore::list_for_ue(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return result;
    }
    result.reserve(ue_it->second.size());
    for (const auto& [pdu_session_id, registration] : ue_it->second) {
        result.push_back(registration);
    }
    return result;
}

std::string SdmSubscriptionStore::create(SdmSubscriptionEntry entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "sdmsub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(entry));
    return id;
}

std::optional<SdmSubscriptionEntry> SdmSubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SdmSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

} // namespace udm
