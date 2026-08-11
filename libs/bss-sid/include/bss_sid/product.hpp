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
// Extended again 2026-08-11 (user directive: "no compromise on data model") to the full, real
// top-level field set of ProductOffering/ProductOfferingPrice/ProductSpecification, re-confirmed
// by re-fetching the same real TMF620 v4.1.0 swagger directly (not recalled from the file's own
// earlier comment, in case the earlier pass had missed something -- it had: `percentage` on
// ProductOfferingPrice was not previously disclosed as missing at all). Newly modeled:
// `attachment`, `lastUpdate`, `place`, `productOfferingRelationship`, `productOfferingTerm`,
// `statusReason` on ProductOffering; `bundledPopRelationship`, `constraint`, `lastUpdate`,
// `percentage`, `place`, `popRelationship`, `pricingLogicAlgorithm`, `productOfferingTerm`, `tax`
// on ProductOfferingPrice; `attachment`, `bundledProductSpecification`, `lastUpdate`,
// `productSpecificationRelationship`, `relatedParty`, `resourceSpecification`,
// `serviceSpecification`, `targetProductSchema` on ProductSpecification. Still NOT modeled:
// `productSpecCharRelationship` on ProductSpecificationCharacteristic (a real, further-nested
// TMF620 field describing relationships *between* characteristics, e.g. "dependent on"/"exclusive
// with" -- genuinely deferred, not silently dropped, since nothing in this project's real use case
// yet needs cross-characteristic constraints). Every Ref/Value type below also omits TMF620's
// `@baseType`/`@schemaLocation`/`@type`/`@referredType` polymorphism markers, matching the existing
// ProductOfferingPriceRef precedent -- these are real fields but pure JSON-LD-style type
// discriminators with no business meaning this project's own (de)serialization needs.

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

// Real TMF620 Duration -- "a time interval in a given unit of time" (the real spec's own
// description). Used by ProductOfferingTerm.duration.
struct Duration {
    std::optional<int> amount;
    std::optional<std::string> units;
};
void to_json(nlohmann::json& j, const Duration& v);
void from_json(const nlohmann::json& j, Duration& v);

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

// Real TMF620 AttachmentRefOrValue -- used by
// ProductOffering.attachment/ProductSpecification.attachment.
struct AttachmentRefOrValue {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> attachmentType;
    std::optional<std::string> content;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<std::string> name;
    std::optional<std::string> url;
    std::optional<Quantity> size;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const AttachmentRefOrValue& v);
void from_json(const nlohmann::json& j, AttachmentRefOrValue& v);

struct PlaceRef {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const PlaceRef& v);
void from_json(const nlohmann::json& j, PlaceRef& v);

// Real TMF620 ProductOfferingRelationship -- a relationship to ANOTHER ProductOffering (e.g.
// "requires"/"excludes"), distinct from BundledProductOffering (which is bundle membership).
struct ProductOfferingRelationship {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> relationshipType;
    std::optional<std::string> role;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductOfferingRelationship& v);
void from_json(const nlohmann::json& j, ProductOfferingRelationship& v);

// Real TMF620 ProductOfferingTerm -- commitment terms (e.g. "24-month contract"). Also a real
// field on ProductOfferingPrice, not just ProductOffering.
struct ProductOfferingTerm {
    std::optional<std::string> description;
    std::optional<std::string> name;
    std::optional<Duration> duration;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductOfferingTerm& v);
void from_json(const nlohmann::json& j, ProductOfferingTerm& v);

struct BundledProductOfferingPriceRelationship {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const BundledProductOfferingPriceRelationship& v);
void from_json(const nlohmann::json& j, BundledProductOfferingPriceRelationship& v);

struct ConstraintRef {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
};
void to_json(nlohmann::json& j, const ConstraintRef& v);
void from_json(const nlohmann::json& j, ConstraintRef& v);

struct ProductOfferingPriceRelationship {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> relationshipType;
    std::optional<std::string> role;
};
void to_json(nlohmann::json& j, const ProductOfferingPriceRelationship& v);
void from_json(const nlohmann::json& j, ProductOfferingPriceRelationship& v);

// Real TMF620 PricingLogicAlgorithm -- a reference to an external rating algorithm (`plaSpecId`)
// this price delegates to, for pricing too complex for the declarative price/unitOfMeasure fields
// alone. This project's own rating engine (nfs/chf) does not yet consume this field -- modeled for
// real schema completeness, not yet wired into a decision path.
struct PricingLogicAlgorithm {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> description;
    std::optional<std::string> name;
    std::optional<std::string> plaSpecId;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const PricingLogicAlgorithm& v);
void from_json(const nlohmann::json& j, PricingLogicAlgorithm& v);

struct TaxItem {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> taxCategory;
    std::optional<double> taxRate;
    std::optional<Money> taxAmount;
};
void to_json(nlohmann::json& j, const TaxItem& v);
void from_json(const nlohmann::json& j, TaxItem& v);

struct BundledProductSpecification {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> lifecycleStatus;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const BundledProductSpecification& v);
void from_json(const nlohmann::json& j, BundledProductSpecification& v);

struct ProductSpecificationRelationship {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> relationshipType;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductSpecificationRelationship& v);
void from_json(const nlohmann::json& j, ProductSpecificationRelationship& v);

// Real TMF620 RelatedParty -- generic party-to-party reference (e.g. the manufacturer/owner of a
// ProductSpecification). Distinct from bss_sid::Bucket's own RelatedParty in balance.hpp (same
// real TMF shape, independently confirmed, redefined per-file rather than shared across TMF620/654
// since each TMF Open API's swagger defines its own copy of this common type).
struct RelatedParty {
    std::string id; // real TMF620/654 spec: id is `required`, unlike most Ref shapes in this file
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> role;
};
void to_json(nlohmann::json& j, const RelatedParty& v);
void from_json(const nlohmann::json& j, RelatedParty& v);

struct ResourceSpecificationRef {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
};
void to_json(nlohmann::json& j, const ResourceSpecificationRef& v);
void from_json(const nlohmann::json& j, ResourceSpecificationRef& v);

struct ServiceSpecificationRef {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
    std::optional<std::string> version;
};
void to_json(nlohmann::json& j, const ServiceSpecificationRef& v);
void from_json(const nlohmann::json& j, ServiceSpecificationRef& v);

// Real TMF620 TargetProductSchema -- real spec has only the two polymorphism markers themselves as
// its "content" (`@type`/`@schemaLocation`); modeled as an opaque JSON passthrough rather than an
// empty placeholder struct, since a real value here (when present) is exactly those two fields.
using TargetProductSchema = nlohmann::json;

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
    std::optional<std::string> lastUpdate;
    std::optional<std::string> priceType;
    std::optional<double> percentage;
    std::optional<Money> price;
    std::optional<int> recurringChargePeriodLength;
    std::optional<std::string> recurringChargePeriodType;
    std::optional<Quantity> unitOfMeasure;
    std::vector<ProductSpecificationCharacteristicValueUse> prodSpecCharValueUse;
    std::vector<BundledProductOfferingPriceRelationship> bundledPopRelationship;
    std::vector<ConstraintRef> constraint;
    std::vector<PlaceRef> place;
    std::vector<ProductOfferingPriceRelationship> popRelationship;
    std::vector<PricingLogicAlgorithm> pricingLogicAlgorithm;
    std::vector<ProductOfferingTerm> productOfferingTerm;
    std::vector<TaxItem> tax;
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
    std::optional<std::string> lastUpdate;
    std::optional<std::string> statusReason;
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
    std::vector<AttachmentRefOrValue> attachment;
    std::vector<PlaceRef> place;
    std::vector<ProductOfferingRelationship> productOfferingRelationship;
    std::vector<ProductOfferingTerm> productOfferingTerm;
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
    std::optional<std::string> lastUpdate;
    std::optional<std::string> name;
    std::optional<std::string> productNumber;
    std::optional<std::string> version;
    std::vector<ProductSpecificationCharacteristic> productSpecCharacteristic;
    std::vector<AttachmentRefOrValue> attachment;
    std::vector<BundledProductSpecification> bundledProductSpecification;
    std::vector<ProductSpecificationRelationship> productSpecificationRelationship;
    std::vector<RelatedParty> relatedParty;
    std::vector<ResourceSpecificationRef> resourceSpecification;
    std::vector<ServiceSpecificationRef> serviceSpecification;
    std::optional<TargetProductSchema> targetProductSchema;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ProductSpecification& v);
void from_json(const nlohmann::json& j, ProductSpecification& v);

} // namespace bss_sid
