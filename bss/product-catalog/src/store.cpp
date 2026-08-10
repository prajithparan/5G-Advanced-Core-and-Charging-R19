#include "store.hpp"

namespace product_catalog {

std::string ProductOfferingStore::create(bss_sid::ProductOffering offering) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = std::to_string(next_id_++);
    offering.id = id;
    offering.href = resource_url_ + "/" + id;
    offerings_.emplace(id, std::move(offering));
    return id;
}

std::optional<bss_sid::ProductOffering> ProductOfferingStore::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = offerings_.find(id);
    if (it == offerings_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<bss_sid::ProductOffering> ProductOfferingStore::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<bss_sid::ProductOffering> result;
    result.reserve(offerings_.size());
    for (const auto& [id, offering] : offerings_) {
        result.push_back(offering);
    }
    return result;
}

bool ProductOfferingStore::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return offerings_.erase(id) > 0;
}

std::string ProductOfferingPriceStore::create(bss_sid::ProductOfferingPrice price) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = std::to_string(next_id_++);
    price.id = id;
    price.href = resource_url_ + "/" + id;
    prices_.emplace(id, std::move(price));
    return id;
}

std::optional<bss_sid::ProductOfferingPrice> ProductOfferingPriceStore::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = prices_.find(id);
    if (it == prices_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<bss_sid::ProductOfferingPrice> ProductOfferingPriceStore::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<bss_sid::ProductOfferingPrice> result;
    result.reserve(prices_.size());
    for (const auto& [id, price] : prices_) {
        result.push_back(price);
    }
    return result;
}

bool ProductOfferingPriceStore::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return prices_.erase(id) > 0;
}

} // namespace product_catalog
