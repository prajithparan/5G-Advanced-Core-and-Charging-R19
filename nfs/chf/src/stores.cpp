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

bool ChargingDataStore::is_active(const std::string& ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_refs_.count(ref) > 0;
}

std::string OfflineChargingDataStore::create() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ref = "offchg-" + std::to_string(next_id_++);
    active_refs_.insert(ref);
    return ref;
}

bool OfflineChargingDataStore::release(const std::string& ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_refs_.erase(ref) > 0;
}

bool OfflineChargingDataStore::is_active(const std::string& ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_refs_.count(ref) > 0;
}

std::string SpendingLimitSubscriptionStore::create(sbi_gen::SpendingLimitContext context) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = "sub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(context));
    return id;
}

bool SpendingLimitSubscriptionStore::update(const std::string& id,
                                            sbi_gen::SpendingLimitContext context) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(context);
    return true;
}

bool SpendingLimitSubscriptionStore::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(id) > 0;
}

std::optional<sbi_gen::SpendingLimitContext>
SpendingLimitSubscriptionStore::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace chf
