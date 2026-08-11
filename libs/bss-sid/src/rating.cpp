#include "bss_sid/rating.hpp"

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

void to_json(nlohmann::json& j, const BillRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
}

void from_json(const nlohmann::json& j, BillRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
}

void to_json(nlohmann::json& j, const BillingAccountRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, BillingAccountRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const AppliedBillingTaxRate& v) {
    j = nlohmann::json::object();
    put_optional(j, "taxCategory", v.taxCategory);
    put_optional(j, "taxRate", v.taxRate);
    put_optional(j, "taxAmount", v.taxAmount);
}

void from_json(const nlohmann::json& j, AppliedBillingTaxRate& v) {
    get_optional(j, "taxCategory", v.taxCategory);
    get_optional(j, "taxRate", v.taxRate);
    get_optional(j, "taxAmount", v.taxAmount);
}

void to_json(nlohmann::json& j, const AppliedBillingRateCharacteristic& v) {
    j = nlohmann::json::object();
    put_optional(j, "name", v.name);
    put_optional(j, "valueType", v.valueType);
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, AppliedBillingRateCharacteristic& v) {
    get_optional(j, "name", v.name);
    get_optional(j, "valueType", v.valueType);
    if (const auto it = j.find("value"); it != j.end()) {
        v.value = *it;
    }
}

void to_json(nlohmann::json& j, const AppliedCustomerBillingRate& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "date", v.date);
    put_optional(j, "description", v.description);
    put_optional(j, "isBilled", v.isBilled);
    put_optional(j, "name", v.name);
    put_optional(j, "type", v.type);
    put_array(j, "appliedTax", v.appliedTax);
    put_optional(j, "bill", v.bill);
    put_optional(j, "billingAccount", v.billingAccount);
    put_array(j, "characteristic", v.characteristic);
    put_optional(j, "periodCoverage", v.periodCoverage);
    put_optional(j, "product", v.product);
    put_optional(j, "taxExcludedAmount", v.taxExcludedAmount);
    put_optional(j, "taxIncludedAmount", v.taxIncludedAmount);
}

void from_json(const nlohmann::json& j, AppliedCustomerBillingRate& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "date", v.date);
    get_optional(j, "description", v.description);
    get_optional(j, "isBilled", v.isBilled);
    get_optional(j, "name", v.name);
    get_optional(j, "type", v.type);
    get_array(j, "appliedTax", v.appliedTax);
    get_optional(j, "bill", v.bill);
    get_optional(j, "billingAccount", v.billingAccount);
    get_array(j, "characteristic", v.characteristic);
    get_optional(j, "periodCoverage", v.periodCoverage);
    get_optional(j, "product", v.product);
    get_optional(j, "taxExcludedAmount", v.taxExcludedAmount);
    get_optional(j, "taxIncludedAmount", v.taxIncludedAmount);
}

} // namespace bss_sid
