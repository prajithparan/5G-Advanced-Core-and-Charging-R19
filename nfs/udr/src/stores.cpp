#include "stores.hpp"

namespace udr {

bool AmfContextStore::put(const std::string& ue_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool is_new = contexts_.find(ue_id) == contexts_.end();
    contexts_[ue_id] = std::move(context);
    return is_new;
}

std::optional<nlohmann::json> AmfContextStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(ue_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> AmfContextStore::apply_patch(const std::string& ue_id,
                                                           const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(ue_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    it->second = it->second.patch(patch_ops);
    return std::make_optional(it->second);
}

bool SmfRegistrationStore::put(const std::string& ue_id,
                               const std::string& pdu_session_id,
                               nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ue_map = registrations_[ue_id];
    const bool is_new = ue_map.find(pdu_session_id) == ue_map.end();
    ue_map[pdu_session_id] = std::move(registration);
    return is_new;
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

std::optional<nlohmann::json> SmfRegistrationStore::apply_patch(const std::string& ue_id,
                                                                const std::string& pdu_session_id,
                                                                const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    session_it->second = session_it->second.patch(patch_ops);
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

} // namespace udr
