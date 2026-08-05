#include "ue_context_store.hpp"

namespace amf {

void UeContextStore::put(const std::string& ue_context_id, nlohmann::json context) {
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_[ue_context_id] = std::move(context);
}

std::optional<nlohmann::json> UeContextStore::get(const std::string& ue_context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(ue_context_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool UeContextStore::remove(const std::string& ue_context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.erase(ue_context_id) > 0;
}

} // namespace amf
