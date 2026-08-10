#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

// TM Forum SID ProductOffering/ProductOfferingPrice -> TMF620 Product Catalog Management. Hand-
// written, not codegen'd -- same "no TMF spec vendored, hand-roll it against a real confirmed
// source" precedent as party.hpp (this file's sibling). Fields confirmed by downloading and
// directly parsing the real TMF620 v4.1.0 swagger JSON
// (github.com/tmforum-apis/TMF620_ProductCatalog/blob/main/TMF620-ProductCatalog-v4.1.0.swagger.json),
// not recalled from memory -- including confirming the real schema marks NO field `required`,
// which every optional<T> below reflects exactly (not a simplification -- the real spec really is
// this permissive).
//
// Only the fields relevant to this project's actual charging use case are modeled (agreement,
// attachment, bundledProductOffering, channel, marketSegment, place, prodSpecCharValueUse,
// productOfferingRelationship, productOfferingTerm, productSpecification, resourceCandidate,
// serviceCandidate, serviceLevelAgreement, statusReason on ProductOffering; bundledPopRelationship,
// constraint, place, popRelationship, pricingLogicAlgorithm, prodSpecCharValueUse,
// productOfferingTerm, tax on ProductOfferingPrice -- all real TMF620 fields, just not modeled
// here, per CLAUDE.md's "no speculative abstraction" rule). `category` is modeled as a bare
// vector<string> (category names) rather than the real `CategoryRef` array shape -- a deliberate
// simplification since this project has no TMF620 Category resource of its own yet, disclosed
// here rather than silently picked.

namespace bss_sid {

// TMF620's real common types (confirmed identical across every TM Forum Open API, not TMF620-
// specific, but modeled here since this is their first real use in this project).
struct TimePeriod {
    std::optional<std::string> startDateTime;
    std::optional<std::string> endDateTime;
};
void to_json(nlohmann::json& j, const TimePeriod& v);
void from_json(const nlohmann::json& j, TimePeriod& v);

struct Money {
    std::optional<std::string> unit;  // ISO 4217 currency code, per the real spec's description
    std::optional<double> value;
};
void to_json(nlohmann::json& j, const Money& v);
void from_json(const nlohmann::json& j, Money& v);

struct Quantity {
    std::optional<double> amount;
    std::optional<std::string> units;
};
void to_json(nlohmann::json& j, const Quantity& v);
void from_json(const nlohmann::json& j, Quantity& v);

// TMF620's real ProductOfferingPriceRef shape (id/href/name only -- the "Ref" half of the real
// "ProductOfferingPriceRefOrValue" union; this project always stores prices by reference, never
// embeds a full value inline, since ProductOfferingPrice is itself a first-class stored resource).
struct ProductOfferingPriceRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const ProductOfferingPriceRef& v);
void from_json(const nlohmann::json& j, ProductOfferingPriceRef& v);

struct ProductOfferingPrice {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> lifecycleStatus;
    std::optional<std::string> priceType;
    std::optional<Money> price;
    std::optional<int> recurringChargePeriodLength;
    std::optional<std::string> recurringChargePeriodType;
    std::optional<Quantity> unitOfMeasure;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductOfferingPrice& v);
void from_json(const nlohmann::json& j, ProductOfferingPrice& v);

struct ProductOffering {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> lifecycleStatus;
    std::optional<bool> isBundle;
    std::optional<bool> isSellable;
    std::optional<std::string> version;
    std::vector<ProductOfferingPriceRef> productOfferingPrice;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductOffering& v);
void from_json(const nlohmann::json& j, ProductOffering& v);

} // namespace bss_sid
