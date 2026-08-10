#include "bss_sid/product.hpp"

namespace bss_sid {

namespace {

template <typename T> void put_optional(nlohmann::json& j, const char* key, const std::optional<T>& v) {
    if (v.has_value()) {
        j[key] = *v;
    }
}

template <typename T> void get_optional(const nlohmann::json& j, const char* key, std::optional<T>& v) {
    if (const auto it = j.find(key); it != j.end() && !it->is_null()) {
        v = it->template get<T>();
    } else {
        v = std::nullopt;
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
    put_optional(j, "priceType", v.priceType);
    put_optional(j, "price", v.price);
    put_optional(j, "recurringChargePeriodLength", v.recurringChargePeriodLength);
    put_optional(j, "recurringChargePeriodType", v.recurringChargePeriodType);
    put_optional(j, "unitOfMeasure", v.unitOfMeasure);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductOfferingPrice& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "description", v.description);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "priceType", v.priceType);
    get_optional(j, "price", v.price);
    get_optional(j, "recurringChargePeriodLength", v.recurringChargePeriodLength);
    get_optional(j, "recurringChargePeriodType", v.recurringChargePeriodType);
    get_optional(j, "unitOfMeasure", v.unitOfMeasure);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const ProductOffering& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
    put_optional(j, "description", v.description);
    put_optional(j, "lifecycleStatus", v.lifecycleStatus);
    put_optional(j, "isBundle", v.isBundle);
    put_optional(j, "isSellable", v.isSellable);
    put_optional(j, "version", v.version);
    if (!v.productOfferingPrice.empty()) {
        j["productOfferingPrice"] = v.productOfferingPrice;
    }
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ProductOffering& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
    get_optional(j, "description", v.description);
    get_optional(j, "lifecycleStatus", v.lifecycleStatus);
    get_optional(j, "isBundle", v.isBundle);
    get_optional(j, "isSellable", v.isSellable);
    get_optional(j, "version", v.version);
    if (const auto it = j.find("productOfferingPrice"); it != j.end() && !it->is_null()) {
        v.productOfferingPrice = it->get<std::vector<ProductOfferingPriceRef>>();
    } else {
        v.productOfferingPrice.clear();
    }
    get_optional(j, "validFor", v.validFor);
}

} // namespace bss_sid
