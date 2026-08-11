#include "bss_sid/product.hpp"

namespace bss_sid {

namespace {

template <typename T>
void put_optional(nlohmann::json& j, const char* key, const std::optional<T>& v) {
    if (v.has_value()) {
        j[key] = *v;
    }
}

template <typename T>
void get_optional(const nlohmann::json& j, const char* key, std::optional<T>& v) {
    if (const auto it = j.find(key); it != j.end() && !it->is_null()) {
        v = it->template get<T>();
    } else {
        v = std::nullopt;
    }
}

template <typename T> void put_array(nlohmann::json& j, const char* key, const std::vector<T>& v) {
    if (!v.empty()) {
        j[key] = v;
    }
}

template <typename T> void get_array(const nlohmann::json& j, const char* key, std::vector<T>& v) {
    if (const auto it = j.find(key); it != j.end() && !it->is_null()) {
        v = it->template get<std::vector<T>>();
    } else {
        v.clear();
    }
}

} // namespace

void to_json(nlohmann::json& j, const TimePeriod& v) {
    j = nlohmann::json::object();
    put_optional(j, "startDateTime", v.startDateTime);
    put_optional(j, "endDateTime", v.endDateTime);
}

void from_json(const nlohmann::json& j, TimePeriod& v) {
    get_optional(j, "startDateTime", v.startDateTime);
    get_optional(j, "endDateTime", v.endDateTime);
}

void to_json(nlohmann::json& j, const Money& v) {
    j = nlohmann::json::object();
    put_optional(j, "unit", v.unit);
    put_optional(j, "value", v.value);
}

void from_json(const nlohmann::json& j, Money& v) {
    get_optional(j, "unit", v.unit);
    get_optional(j, "value", v.value);
}

void to_json(nlohmann::json& j, const Quantity& v) {
    j = nlohmann::json::object();
    put_optional(j, "amount", v.amount);
    put_optional(j, "units", v.units);
}

void from_json(const nlohmann::json& j, Quantity& v) {
    get_optional(j, "amount", v.amount);
    get_optional(j, "units", v.units);
}

void to_json(nlohmann::json& j, const Duration& v) {
    j = nlohmann::json::object();
    put_optional(j, "amount", v.amount);
    put_optional(j, "units", v.units);
}

void from_json(const nlohmann::json& j, Duration& v) {
    get_optional(j, "amount", v.amount);
    get_optional(j, "units", v.units);
}

void to_json(nlohmann::json& j, const CategoryRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
}

void from_json(const nlohmann::json& j, CategoryRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
}

void to_json(nlohmann::json& j, const MarketSegmentRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, MarketSegmentRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const SLARef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, SLARef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ChannelRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, ChannelRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const AgreementRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, AgreementRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ResourceCandidateRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
}

void from_json(const nlohmann::json& j, ResourceCandidateRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
}

void to_json(nlohmann::json& j, const ServiceCandidateRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
}

void from_json(const nlohmann::json& j, ServiceCandidateRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
}

void to_json(nlohmann::json& j, const AttachmentRefOrValue& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "attachmentType", v.attachmentType);
    put_optional(j, "content", v.content);
    put_optional(j, "description", v.description);
    put_optional(j, "mimeType", v.mimeType);
    put_optional(j, "name", v.name);
    put_optional(j, "url", v.url);
    put_optional(j, "size", v.size);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, AttachmentRefOrValue& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "attachmentType", v.attachmentType);
    get_optional(j, "content", v.content);
    get_optional(j, "description", v.description);
    get_optional(j, "mimeType", v.mimeType);
    get_optional(j, "name", v.name);
    get_optional(j, "url", v.url);
    get_optional(j, "size", v.size);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const PlaceRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, PlaceRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ProductOfferingRelationship& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "relationshipType", v.relationshipType);
    put_optional(j, "role", v.role);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductOfferingRelationship& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "relationshipType", v.relationshipType);
    get_optional(j, "role", v.role);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const ProductOfferingTerm& v) {
    j = nlohmann::json::object();
    put_optional(j, "description", v.description);
    put_optional(j, "name", v.name);
    put_optional(j, "duration", v.duration);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductOfferingTerm& v) {
    get_optional(j, "description", v.description);
    get_optional(j, "name", v.name);
    get_optional(j, "duration", v.duration);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const BundledProductOfferingPriceRelationship& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, BundledProductOfferingPriceRelationship& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ConstraintRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
}

void from_json(const nlohmann::json& j, ConstraintRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
}

void to_json(nlohmann::json& j, const ProductOfferingPriceRelationship& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "relationshipType", v.relationshipType);
    put_optional(j, "role", v.role);
}

void from_json(const nlohmann::json& j, ProductOfferingPriceRelationship& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "relationshipType", v.relationshipType);
    get_optional(j, "role", v.role);
}

void to_json(nlohmann::json& j, const PricingLogicAlgorithm& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "description", v.description);
    put_optional(j, "name", v.name);
    put_optional(j, "plaSpecId", v.plaSpecId);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, PricingLogicAlgorithm& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "description", v.description);
    get_optional(j, "name", v.name);
    get_optional(j, "plaSpecId", v.plaSpecId);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const TaxItem& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "taxCategory", v.taxCategory);
    put_optional(j, "taxRate", v.taxRate);
    put_optional(j, "taxAmount", v.taxAmount);
}

void from_json(const nlohmann::json& j, TaxItem& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "taxCategory", v.taxCategory);
    get_optional(j, "taxRate", v.taxRate);
    get_optional(j, "taxAmount", v.taxAmount);
}

void to_json(nlohmann::json& j, const BundledProductSpecification& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "lifecycleStatus", v.lifecycleStatus);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, BundledProductSpecification& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ProductSpecificationRelationship& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "relationshipType", v.relationshipType);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductSpecificationRelationship& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "relationshipType", v.relationshipType);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const RelatedParty& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "role", v.role);
}

void from_json(const nlohmann::json& j, RelatedParty& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "role", v.role);
}

void to_json(nlohmann::json& j, const ResourceSpecificationRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
}

void from_json(const nlohmann::json& j, ResourceSpecificationRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
}

void to_json(nlohmann::json& j, const ServiceSpecificationRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
}

void from_json(const nlohmann::json& j, ServiceSpecificationRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
}

void to_json(nlohmann::json& j, const ProductSpecificationRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "version", v.version);
    put_optional(j, "targetProductSchema", v.targetProductSchema);
}

void from_json(const nlohmann::json& j, ProductSpecificationRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "version", v.version);
    get_optional(j, "targetProductSchema", v.targetProductSchema);
}

void to_json(nlohmann::json& j, const CharacteristicValueSpecification& v) {
    j = nlohmann::json::object();
    put_optional(j, "isDefault", v.isDefault);
    put_optional(j, "rangeInterval", v.rangeInterval);
    put_optional(j, "regex", v.regex);
    put_optional(j, "unitOfMeasure", v.unitOfMeasure);
    put_optional(j, "valueFrom", v.valueFrom);
    put_optional(j, "valueTo", v.valueTo);
    put_optional(j, "valueType", v.valueType);
    put_optional(j, "validFor", v.validFor);
    put_optional(j, "value", v.value);
}

void from_json(const nlohmann::json& j, CharacteristicValueSpecification& v) {
    get_optional(j, "isDefault", v.isDefault);
    get_optional(j, "rangeInterval", v.rangeInterval);
    get_optional(j, "regex", v.regex);
    get_optional(j, "unitOfMeasure", v.unitOfMeasure);
    get_optional(j, "valueFrom", v.valueFrom);
    get_optional(j, "valueTo", v.valueTo);
    get_optional(j, "valueType", v.valueType);
    get_optional(j, "validFor", v.validFor);
    get_optional(j, "value", v.value);
}

void to_json(nlohmann::json& j, const ProductSpecificationCharacteristicValueUse& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "description", v.description);
    put_optional(j, "maxCardinality", v.maxCardinality);
    put_optional(j, "minCardinality", v.minCardinality);
    put_optional(j, "name", v.name);
    put_optional(j, "valueType", v.valueType);
    put_array(j, "productSpecCharacteristicValue", v.productSpecCharacteristicValue);
    put_optional(j, "productSpecification", v.productSpecification);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductSpecificationCharacteristicValueUse& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "description", v.description);
    get_optional(j, "maxCardinality", v.maxCardinality);
    get_optional(j, "minCardinality", v.minCardinality);
    get_optional(j, "name", v.name);
    get_optional(j, "valueType", v.valueType);
    get_array(j, "productSpecCharacteristicValue", v.productSpecCharacteristicValue);
    get_optional(j, "productSpecification", v.productSpecification);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const ProductSpecificationCharacteristic& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "configurable", v.configurable);
    put_optional(j, "description", v.description);
    put_optional(j, "extensible", v.extensible);
    put_optional(j, "isUnique", v.isUnique);
    put_optional(j, "maxCardinality", v.maxCardinality);
    put_optional(j, "minCardinality", v.minCardinality);
    put_optional(j, "name", v.name);
    put_optional(j, "regex", v.regex);
    put_optional(j, "valueType", v.valueType);
    put_array(j, "productSpecCharacteristicValue", v.productSpecCharacteristicValue);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductSpecificationCharacteristic& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "configurable", v.configurable);
    get_optional(j, "description", v.description);
    get_optional(j, "extensible", v.extensible);
    get_optional(j, "isUnique", v.isUnique);
    get_optional(j, "maxCardinality", v.maxCardinality);
    get_optional(j, "minCardinality", v.minCardinality);
    get_optional(j, "name", v.name);
    get_optional(j, "regex", v.regex);
    get_optional(j, "valueType", v.valueType);
    get_array(j, "productSpecCharacteristicValue", v.productSpecCharacteristicValue);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const BundledProductOffering& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "lifecycleStatus", v.lifecycleStatus);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, BundledProductOffering& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ProductOfferingPriceRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, ProductOfferingPriceRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const ProductOfferingPrice& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "description", v.description);
    put_optional(j, "lifecycleStatus", v.lifecycleStatus);
    put_optional(j, "lastUpdate", v.lastUpdate);
    put_optional(j, "priceType", v.priceType);
    put_optional(j, "percentage", v.percentage);
    put_optional(j, "price", v.price);
    put_optional(j, "recurringChargePeriodLength", v.recurringChargePeriodLength);
    put_optional(j, "recurringChargePeriodType", v.recurringChargePeriodType);
    put_optional(j, "unitOfMeasure", v.unitOfMeasure);
    put_array(j, "prodSpecCharValueUse", v.prodSpecCharValueUse);
    put_array(j, "bundledPopRelationship", v.bundledPopRelationship);
    put_array(j, "constraint", v.constraint);
    put_array(j, "place", v.place);
    put_array(j, "popRelationship", v.popRelationship);
    put_array(j, "pricingLogicAlgorithm", v.pricingLogicAlgorithm);
    put_array(j, "productOfferingTerm", v.productOfferingTerm);
    put_array(j, "tax", v.tax);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductOfferingPrice& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "description", v.description);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "lastUpdate", v.lastUpdate);
    get_optional(j, "priceType", v.priceType);
    get_optional(j, "percentage", v.percentage);
    get_optional(j, "price", v.price);
    get_optional(j, "recurringChargePeriodLength", v.recurringChargePeriodLength);
    get_optional(j, "recurringChargePeriodType", v.recurringChargePeriodType);
    get_optional(j, "unitOfMeasure", v.unitOfMeasure);
    get_array(j, "prodSpecCharValueUse", v.prodSpecCharValueUse);
    get_array(j, "bundledPopRelationship", v.bundledPopRelationship);
    get_array(j, "constraint", v.constraint);
    get_array(j, "place", v.place);
    get_array(j, "popRelationship", v.popRelationship);
    get_array(j, "pricingLogicAlgorithm", v.pricingLogicAlgorithm);
    get_array(j, "productOfferingTerm", v.productOfferingTerm);
    get_array(j, "tax", v.tax);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const ProductOffering& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "description", v.description);
    put_optional(j, "lifecycleStatus", v.lifecycleStatus);
    put_optional(j, "lastUpdate", v.lastUpdate);
    put_optional(j, "statusReason", v.statusReason);
    put_optional(j, "isBundle", v.isBundle);
    put_optional(j, "isSellable", v.isSellable);
    put_optional(j, "version", v.version);
    put_array(j, "productOfferingPrice", v.productOfferingPrice);
    put_array(j, "category", v.category);
    put_array(j, "channel", v.channel);
    put_array(j, "marketSegment", v.marketSegment);
    put_array(j, "prodSpecCharValueUse", v.prodSpecCharValueUse);
    put_optional(j, "productSpecification", v.productSpecification);
    put_optional(j, "resourceCandidate", v.resourceCandidate);
    put_optional(j, "serviceCandidate", v.serviceCandidate);
    put_optional(j, "serviceLevelAgreement", v.serviceLevelAgreement);
    put_array(j, "agreement", v.agreement);
    put_array(j, "bundledProductOffering", v.bundledProductOffering);
    put_array(j, "attachment", v.attachment);
    put_array(j, "place", v.place);
    put_array(j, "productOfferingRelationship", v.productOfferingRelationship);
    put_array(j, "productOfferingTerm", v.productOfferingTerm);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductOffering& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "description", v.description);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "lastUpdate", v.lastUpdate);
    get_optional(j, "statusReason", v.statusReason);
    get_optional(j, "isBundle", v.isBundle);
    get_optional(j, "isSellable", v.isSellable);
    get_optional(j, "version", v.version);
    get_array(j, "productOfferingPrice", v.productOfferingPrice);
    get_array(j, "category", v.category);
    get_array(j, "channel", v.channel);
    get_array(j, "marketSegment", v.marketSegment);
    get_array(j, "prodSpecCharValueUse", v.prodSpecCharValueUse);
    get_optional(j, "productSpecification", v.productSpecification);
    get_optional(j, "resourceCandidate", v.resourceCandidate);
    get_optional(j, "serviceCandidate", v.serviceCandidate);
    get_optional(j, "serviceLevelAgreement", v.serviceLevelAgreement);
    get_array(j, "agreement", v.agreement);
    get_array(j, "bundledProductOffering", v.bundledProductOffering);
    get_array(j, "attachment", v.attachment);
    get_array(j, "place", v.place);
    get_array(j, "productOfferingRelationship", v.productOfferingRelationship);
    get_array(j, "productOfferingTerm", v.productOfferingTerm);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const ProductSpecification& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "brand", v.brand);
    put_optional(j, "description", v.description);
    put_optional(j, "isBundle", v.isBundle);
    put_optional(j, "lifecycleStatus", v.lifecycleStatus);
    put_optional(j, "lastUpdate", v.lastUpdate);
    put_optional(j, "name", v.name);
    put_optional(j, "productNumber", v.productNumber);
    put_optional(j, "version", v.version);
    put_array(j, "productSpecCharacteristic", v.productSpecCharacteristic);
    put_array(j, "attachment", v.attachment);
    put_array(j, "bundledProductSpecification", v.bundledProductSpecification);
    put_array(j, "productSpecificationRelationship", v.productSpecificationRelationship);
    put_array(j, "relatedParty", v.relatedParty);
    put_array(j, "resourceSpecification", v.resourceSpecification);
    put_array(j, "serviceSpecification", v.serviceSpecification);
    put_optional(j, "targetProductSchema", v.targetProductSchema);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductSpecification& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "brand", v.brand);
    get_optional(j, "description", v.description);
    get_optional(j, "isBundle", v.isBundle);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "lastUpdate", v.lastUpdate);
    get_optional(j, "name", v.name);
    get_optional(j, "productNumber", v.productNumber);
    get_optional(j, "version", v.version);
    get_array(j, "productSpecCharacteristic", v.productSpecCharacteristic);
    get_array(j, "attachment", v.attachment);
    get_array(j, "bundledProductSpecification", v.bundledProductSpecification);
    get_array(j, "productSpecificationRelationship", v.productSpecificationRelationship);
    get_array(j, "relatedParty", v.relatedParty);
    get_array(j, "resourceSpecification", v.resourceSpecification);
    get_array(j, "serviceSpecification", v.serviceSpecification);
    get_optional(j, "targetProductSchema", v.targetProductSchema);
    get_optional(j, "validFor", v.validFor);
}

} // namespace bss_sid
