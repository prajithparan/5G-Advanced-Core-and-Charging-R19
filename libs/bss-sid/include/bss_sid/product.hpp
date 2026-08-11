#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

// TM Forum SID ProductOffering/ProductOfferingPrice/ProductSpecification -> TMF620 Product Catalog
// Management. Hand-written, not codegen'd -- same "no TMF spec vendored, hand-roll it against a
// real confirmed source" precedent as party.hpp (this file's sibling). Fields confirmed by
// downloading and directly parsing the real TMF620 v4.1.0 swagger JSON
// (github.com/tmforum-apis/TMF620_ProductCatalog/blob/main/TMF620-ProductCatalog-v4.1.0.swagger.json),
// not recalled from memory -- including confirming the real schema marks NO field `required`,
// which every optional<T> below reflects exactly (not a simplification -- the real spec really is
// this permissive). Extended 2026-08-10 (docs/DATA_MODEL.md's E2, ADR-0053/0054) with the fields
// this project's 5G SA enterprise/consumer/GUI-driven-configuration use case actually needs:
// category (now the real CategoryRef[] shape, replacing the earlier vector<string> simplification),
// channel, marketSegment, prodSpecCharValueUse (the key TMF620 mechanism for configurable, typed,
// cardinality/regex-constrained product characteristics -- e.g. S-NSSAI, 5QI, SLA tier as
// characteristics, and the structure a future JSON-schema-driven GUI would introspect to render
// dynamic configuration forms), productSpecification, resourceCandidate, serviceCandidate,
// serviceLevelAgreement, agreement, bundledProductOffering.
//
// Still NOT modeled, disclosed rather than silently dropped: `place`, `attachment`, `statusReason`,
// `productOfferingRelationship`, `productOfferingTerm` on ProductOffering;
// `bundledPopRelationship`, `constraint`, `place`, `popRelationship`, `pricingLogicAlgorithm`,
// `productOfferingTerm`, `tax` on ProductOfferingPrice; `attachment`,
// `bundledProductSpecification`, `productSpecificationRelationship`, `relatedParty`,
// `resourceSpecification`, `serviceSpecification`, `targetProductSchema` on ProductSpecification;
// `productSpecCharRelationship` on ProductSpecificationCharacteristic -- every one a real TMF620
// field, just not modeled here yet, per CLAUDE.md's "no speculative abstraction" rule. Every Ref
// type below also omits TMF620's `@baseType`/`@schemaLocation`/`@type`/
// `@referredType` polymorphism markers, matching the existing ProductOfferingPriceRef precedent.

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
    std::optional<std::string> unit; // ISO 4217 currency code, per the real spec's description
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

// Real TMF620 Ref shapes -- id/href/name(/version), each confirmed individually against the real
// swagger's `definitions` (not assumed identical to each other, even though several happen to share
// the same shape).
struct CategoryRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
};
void to_json(nlohmann::json& j, const CategoryRef& v);
void from_json(const nlohmann::json& j, CategoryRef& v);

struct MarketSegmentRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const MarketSegmentRef& v);
void from_json(const nlohmann::json& j, MarketSegmentRef& v);

struct SLARef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const SLARef& v);
void from_json(const nlohmann::json& j, SLARef& v);

struct ChannelRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const ChannelRef& v);
void from_json(const nlohmann::json& j, ChannelRef& v);

struct AgreementRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const AgreementRef& v);
void from_json(const nlohmann::json& j, AgreementRef& v);

struct ResourceCandidateRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
};
void to_json(nlohmann::json& j, const ResourceCandidateRef& v);
void from_json(const nlohmann::json& j, ResourceCandidateRef& v);

struct ServiceCandidateRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
};
void to_json(nlohmann::json& j, const ServiceCandidateRef& v);
void from_json(const nlohmann::json& j, ServiceCandidateRef& v);

// Real TMF620 field: also carries `targetProductSchema`, unlike the other Refs above -- confirmed,
// not an inconsistency in this file.
struct ProductSpecificationRef {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
    std::optional<std::string> targetProductSchema;
};
void to_json(nlohmann::json& j, const ProductSpecificationRef& v);
void from_json(const nlohmann::json& j, ProductSpecificationRef& v);

// Real TMF620 CharacteristicValueSpecification -- one concrete value (or value range/regex/unit
// constraint) a ProductSpecificationCharacteristic(ValueUse) can take on. `value` is intentionally
// untyped JSON here (real spec's `valueType` sibling field says what shape to expect: numeric,
// text, etc. -- this project doesn't attempt static per-valueType C++ typing for it).
struct CharacteristicValueSpecification {
    std::optional<bool> isDefault;
    std::optional<std::string> rangeInterval;
    std::optional<std::string> regex;
    std::optional<Quantity> unitOfMeasure;
    std::optional<std::string> valueFrom;
    std::optional<std::string> valueTo;
    std::optional<std::string> valueType;
    std::optional<TimePeriod> validFor;
    std::optional<nlohmann::json> value;
};
void to_json(nlohmann::json& j, const CharacteristicValueSpecification& v);
void from_json(const nlohmann::json& j, CharacteristicValueSpecification& v);

// Real TMF620 ProductSpecificationCharacteristicValueUse (`prodSpecCharValueUse`) -- THE key
// mechanism for representing configurable, typed, cardinality-and-regex-constrained product
// characteristics (this project's intended use: S-NSSAI, 5QI/QoS class, SLA tier as configurable
// characteristics on an enterprise slice offering; a future JSON-schema-driven GUI would introspect
// exactly this structure to render dynamic configuration forms).
struct ProductSpecificationCharacteristicValueUse {
    std::string id;
    std::optional<std::string> description;
    std::optional<int> maxCardinality;
    std::optional<int> minCardinality;
    std::optional<std::string> name;
    std::optional<std::string> valueType;
    std::vector<CharacteristicValueSpecification> productSpecCharacteristicValue;
    std::optional<ProductSpecificationRef> productSpecification;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductSpecificationCharacteristicValueUse& v);
void from_json(const nlohmann::json& j, ProductSpecificationCharacteristicValueUse& v);

// Real TMF620 ProductSpecificationCharacteristic -- the characteristic's own definition (as opposed
// to ProductSpecificationCharacteristicValueUse, which is how a specific ProductOffering/Price
// *uses* one). Lives on ProductSpecification.productSpecCharacteristic.
struct ProductSpecificationCharacteristic {
    std::string id;
    std::optional<bool> configurable;
    std::optional<std::string> description;
    std::optional<bool> extensible;
    std::optional<bool> isUnique;
    std::optional<int> maxCardinality;
    std::optional<int> minCardinality;
    std::optional<std::string> name;
    std::optional<std::string> regex;
    std::optional<std::string> valueType;
    std::vector<CharacteristicValueSpecification> productSpecCharacteristicValue;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductSpecificationCharacteristic& v);
void from_json(const nlohmann::json& j, ProductSpecificationCharacteristic& v);

// Real TMF620 BundledProductOffering. `bundledProductOfferingOption` (a real, further-nested
// TMF620 sub-structure) is NOT modeled here -- disclosed, not silently dropped.
struct BundledProductOffering {
    std::string id;
    std::optional<std::string> href;
    std::optional<std::string> lifecycleStatus;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const BundledProductOffering& v);
void from_json(const nlohmann::json& j, BundledProductOffering& v);

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
    std::vector<ProductSpecificationCharacteristicValueUse> prodSpecCharValueUse;
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
    std::vector<CategoryRef> category;
    std::vector<ChannelRef> channel;
    std::vector<MarketSegmentRef> marketSegment;
    std::vector<ProductSpecificationCharacteristicValueUse> prodSpecCharValueUse;
    std::optional<ProductSpecificationRef> productSpecification;
    std::optional<ResourceCandidateRef> resourceCandidate;
    std::optional<ServiceCandidateRef> serviceCandidate;
    std::optional<SLARef> serviceLevelAgreement;
    std::vector<AgreementRef> agreement;
    std::vector<BundledProductOffering> bundledProductOffering;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductOffering& v);
void from_json(const nlohmann::json& j, ProductOffering& v);

// Real TMF620 ProductSpecification -- the third stored resource this project adds (alongside
// ProductOffering/ProductOfferingPrice), per docs/DATA_MODEL.md's E2 entry: what a customer *can*
// buy (ProductOffering) references a ProductSpecification for its underlying definition, including
// its configurable characteristics (productSpecCharacteristic).
struct ProductSpecification {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> brand;
    std::optional<std::string> description;
    std::optional<bool> isBundle;
    std::optional<std::string> lifecycleStatus;
    std::optional<std::string> name;
    std::optional<std::string> productNumber;
    std::optional<std::string> version;
    std::vector<ProductSpecificationCharacteristic> productSpecCharacteristic;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductSpecification& v);
void from_json(const nlohmann::json& j, ProductSpecification& v);

} // namespace bss_sid
