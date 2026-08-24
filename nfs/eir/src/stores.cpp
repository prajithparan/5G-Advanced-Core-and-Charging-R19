#include "stores.hpp"

namespace eir {

void EquipmentStatusStore::seed(const std::string& pei, std::string status) {
    std::lock_guard<std::mutex> lock(mutex_);
    statuses_[pei] = std::move(status);
}

std::optional<std::string> EquipmentStatusStore::get(const std::string& pei) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = statuses_.find(pei);
    if (it == statuses_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

} // namespace eir
