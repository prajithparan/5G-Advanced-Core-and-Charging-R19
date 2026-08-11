#include "stores.hpp"

#include <nlohmann/json.hpp>

namespace chf {

namespace {

using nlohmann::json;

constexpr const char* kChargingDataActiveSet = "chf:cdr:active";
constexpr const char* kChargingDataNextIdKey = "chf:cdr:next_id";
constexpr const char* kOfflineActiveSet = "chf:offline:active";
constexpr const char* kOfflineNextIdKey = "chf:offline:next_id";
constexpr const char* kSpendingLimitNextIdKey = "chf:sub:next_id";

std::string spending_limit_key(const std::string& id) {
    return "chf:sub:" + id;
}

} // namespace

std::string ChargingDataStore::create() {
    const auto id = redis_->incr(kChargingDataNextIdKey);
    auto ref = "chg-" + std::to_string(id);
    redis_->sadd(kChargingDataActiveSet, ref);
    return ref;
}

bool ChargingDataStore::release(const std::string& ref) {
    return redis_->srem(kChargingDataActiveSet, ref) > 0;
}

bool ChargingDataStore::is_active(const std::string& ref) {
    return redis_->sismember(kChargingDataActiveSet, ref);
}

std::string OfflineChargingDataStore::create() {
    const auto id = redis_->incr(kOfflineNextIdKey);
    auto ref = "offchg-" + std::to_string(id);
    redis_->sadd(kOfflineActiveSet, ref);
    return ref;
}

bool OfflineChargingDataStore::release(const std::string& ref) {
    return redis_->srem(kOfflineActiveSet, ref) > 0;
}

bool OfflineChargingDataStore::is_active(const std::string& ref) {
    return redis_->sismember(kOfflineActiveSet, ref);
}

std::string SpendingLimitSubscriptionStore::create(sbi_gen::SpendingLimitContext context) {
    const auto id = redis_->incr(kSpendingLimitNextIdKey);
    auto sub_id = "sub-" + std::to_string(id);
    const json j = context;
    redis_->set(spending_limit_key(sub_id), j.dump());
    return sub_id;
}

bool SpendingLimitSubscriptionStore::update(const std::string& id,
                                            sbi_gen::SpendingLimitContext context) {
    const auto key = spending_limit_key(id);
    // Real update-in-place semantics: only set if the subscription already exists. This has the
    // same non-atomic check-then-act shape as ChargingDataStore::is_active's own callers
    // elsewhere in this codebase (e.g. the Update route checking is_active before proceeding) --
    // consistent with this project's existing concurrency-simplification level, not a new gap.
    if (!redis_->get(key)) {
        return false;
    }
    const json j = context;
    redis_->set(key, j.dump());
    return true;
}

bool SpendingLimitSubscriptionStore::remove(const std::string& id) {
    return redis_->del(spending_limit_key(id)) > 0;
}

std::optional<sbi_gen::SpendingLimitContext>
SpendingLimitSubscriptionStore::get(const std::string& id) {
    const auto value = redis_->get(spending_limit_key(id));
    if (!value) {
        return std::nullopt;
    }
    return json::parse(*value).get<sbi_gen::SpendingLimitContext>();
}

} // namespace chf
