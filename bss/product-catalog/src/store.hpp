#pragma once

#include "bss_sid/product.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to bss/product-catalog. In-memory only, no persistence across restarts -- same disclosed
// simplification as every NF's own store in nfs/*/src/stores.hpp so far; this isn't a 3GPP NF, but
// the same "disclose the gap, don't hide it" discipline applies regardless.

namespace product_catalog {

class ProductOfferingStore {
public:
    // resource_url is the collection's own real TMF620 URL (e.g.
    // "https://host:port/tmf-api/productCatalogManagement/v4/productOffering"), used to build each
    // stored resource's real `href` -- a genuine TMF620 field, not this project's invention.
    explicit ProductOfferingStore(std::string resource_url) : resource_url_(std::move(resource_url)) {}

    // Server always assigns a fresh id/href on create, overwriting any client-supplied value --
    // matching real REST resource-creation semantics (the server owns identity assignment for a
    // POST to a collection). Returns the assigned id.
    std::string create(bss_sid::ProductOffering offering);
    std::optional<bss_sid::ProductOffering> get(const std::string& id);
    std::vector<bss_sid::ProductOffering> list();
    bool remove(const std::string& id);

private:
    std::string resource_url_;
    std::mutex mutex_;
    std::unordered_map<std::string, bss_sid::ProductOffering> offerings_;
    std::uint64_t next_id_ = 1;
};

class ProductOfferingPriceStore {
public:
    explicit ProductOfferingPriceStore(std::string resource_url)
        : resource_url_(std::move(resource_url)) {}

    std::string create(bss_sid::ProductOfferingPrice price);
    std::optional<bss_sid::ProductOfferingPrice> get(const std::string& id);
    std::vector<bss_sid::ProductOfferingPrice> list();
    bool remove(const std::string& id);

private:
    std::string resource_url_;
    std::mutex mutex_;
    std::unordered_map<std::string, bss_sid::ProductOfferingPrice> prices_;
    std::uint64_t next_id_ = 1;
};

} // namespace product_catalog
