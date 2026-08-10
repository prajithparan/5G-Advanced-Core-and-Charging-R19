#include "stores.hpp"

namespace chf {

std::string ChargingDataStore::create() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ref = "chg-" + std::to_string(next_id_++);
    active_refs_.insert(ref);
    return ref;
}

bool ChargingDataStore::release(const std::string& ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_refs_.erase(ref) > 0;
}

} // namespace chf
