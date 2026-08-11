#include "bss_sid/agreement.hpp"

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

void to_json(nlohmann::json& j, const AgreementAuthorization& v) {
    j = nlohmann::json::object();
    put_optional(j, "date", v.date);
    put_optional(j, "signatureRepresentation", v.signatureRepresentation);
    put_optional(j, "state", v.state);
}

void from_json(const nlohmann::json& j, AgreementAuthorization& v) {
    get_optional(j, "date", v.date);
    get_optional(j, "signatureRepresentation", v.signatureRepresentation);
    get_optional(j, "state", v.state);
}

void to_json(nlohmann::json& j, const ProductOfferingRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, ProductOfferingRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const AgreementTermOrCondition& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "description", v.description);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, AgreementTermOrCondition& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "description", v.description);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const AgreementItem& v) {
    j = nlohmann::json::object();
    put_array(j, "product", v.product);
    put_array(j, "productOffering", v.productOffering);
    put_array(j, "termOrCondition", v.termOrCondition);
}

void from_json(const nlohmann::json& j, AgreementItem& v) {
    get_array(j, "product", v.product);
    get_array(j, "productOffering", v.productOffering);
    get_array(j, "termOrCondition", v.termOrCondition);
}

void to_json(nlohmann::json& j, const AgreementSpecificationRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "description", v.description);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, AgreementSpecificationRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "description", v.description);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const Agreement& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "agreementType", v.agreementType);
    put_optional(j, "description", v.description);
    put_optional(j, "documentNumber", v.documentNumber);
    put_optional(j, "initialDate", v.initialDate);
    put_optional(j, "name", v.name);
    put_optional(j, "statementOfIntent", v.statementOfIntent);
    put_optional(j, "status", v.status);
    put_optional(j, "version", v.version);
    put_array(j, "agreementAuthorization", v.agreementAuthorization);
    put_array(j, "agreementItem", v.agreementItem);
    put_optional(j, "agreementPeriod", v.agreementPeriod);
    put_optional(j, "agreementSpecification", v.agreementSpecification);
    put_array(j, "associatedAgreement", v.associatedAgreement);
    put_array(j, "characteristic", v.characteristic);
    put_optional(j, "completionDate", v.completionDate);
    put_array(j, "engagedParty", v.engagedParty);
}

void from_json(const nlohmann::json& j, Agreement& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "agreementType", v.agreementType);
    get_optional(j, "description", v.description);
    get_optional(j, "documentNumber", v.documentNumber);
    get_optional(j, "initialDate", v.initialDate);
    get_optional(j, "name", v.name);
    get_optional(j, "statementOfIntent", v.statementOfIntent);
    get_optional(j, "status", v.status);
    get_optional(j, "version", v.version);
    get_array(j, "agreementAuthorization", v.agreementAuthorization);
    get_array(j, "agreementItem", v.agreementItem);
    get_optional(j, "agreementPeriod", v.agreementPeriod);
    get_optional(j, "agreementSpecification", v.agreementSpecification);
    get_array(j, "associatedAgreement", v.associatedAgreement);
    get_array(j, "characteristic", v.characteristic);
    get_optional(j, "completionDate", v.completionDate);
    get_array(j, "engagedParty", v.engagedParty);
}

} // namespace bss_sid
