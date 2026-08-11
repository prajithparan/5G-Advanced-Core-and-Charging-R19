#include "bss_sid/balance.hpp"

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

void to_json(nlohmann::json& j, const PartyAccountRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "description", v.description);
    put_optional(j, "name", v.name);
    put_optional(j, "status", v.status);
}

void from_json(const nlohmann::json& j, PartyAccountRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "description", v.description);
    get_optional(j, "name", v.name);
    get_optional(j, "status", v.status);
}

void to_json(nlohmann::json& j, const ProductRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, ProductRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const LogicalResourceRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, LogicalResourceRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const BucketRef& v) {
    j = nlohmann::json::object();
    j["id"] = v.id;
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, BucketRef& v) {
    j.at("id").get_to(v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const Bucket& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "confirmationDate", v.confirmationDate);
    put_optional(j, "description", v.description);
    put_optional(j, "isShared", v.isShared);
    put_optional(j, "name", v.name);
    put_optional(j, "remainingValueName", v.remainingValueName);
    put_optional(j, "requestedDate", v.requestedDate);
    put_optional(j, "logicalResource", v.logicalResource);
    put_optional(j, "partyAccount", v.partyAccount);
    put_optional(j, "product", v.product);
    put_array(j, "relatedParty", v.relatedParty);
    put_optional(j, "remainingValue", v.remainingValue);
    put_optional(j, "reservedValue", v.reservedValue);
    put_optional(j, "status", v.status);
    put_optional(j, "usageType", v.usageType);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, Bucket& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "confirmationDate", v.confirmationDate);
    get_optional(j, "description", v.description);
    get_optional(j, "isShared", v.isShared);
    get_optional(j, "name", v.name);
    get_optional(j, "remainingValueName", v.remainingValueName);
    get_optional(j, "requestedDate", v.requestedDate);
    get_optional(j, "logicalResource", v.logicalResource);
    get_optional(j, "partyAccount", v.partyAccount);
    get_optional(j, "product", v.product);
    get_array(j, "relatedParty", v.relatedParty);
    get_optional(j, "remainingValue", v.remainingValue);
    get_optional(j, "reservedValue", v.reservedValue);
    get_optional(j, "status", v.status);
    get_optional(j, "usageType", v.usageType);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const AccumulatedBalance& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "description", v.description);
    put_optional(j, "name", v.name);
    put_array(j, "bucket", v.bucket);
    put_optional(j, "logicalResource", v.logicalResource);
    put_optional(j, "partyAccount", v.partyAccount);
    put_optional(j, "product", v.product);
    put_array(j, "relatedParty", v.relatedParty);
    put_optional(j, "totalBalance", v.totalBalance);
}

void from_json(const nlohmann::json& j, AccumulatedBalance& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "description", v.description);
    get_optional(j, "name", v.name);
    get_array(j, "bucket", v.bucket);
    get_optional(j, "logicalResource", v.logicalResource);
    get_optional(j, "partyAccount", v.partyAccount);
    get_optional(j, "product", v.product);
    get_array(j, "relatedParty", v.relatedParty);
    get_optional(j, "totalBalance", v.totalBalance);
}

void to_json(nlohmann::json& j, const TopupBalance& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "confirmationDate", v.confirmationDate);
    put_optional(j, "description", v.description);
    put_optional(j, "reason", v.reason);
    put_optional(j, "requestedDate", v.requestedDate);
    put_optional(j, "amount", v.amount);
    put_optional(j, "bucket", v.bucket);
    put_optional(j, "partyAccount", v.partyAccount);
    put_optional(j, "product", v.product);
    put_optional(j, "status", v.status);
    put_optional(j, "usageType", v.usageType);
}

void from_json(const nlohmann::json& j, TopupBalance& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "confirmationDate", v.confirmationDate);
    get_optional(j, "description", v.description);
    get_optional(j, "reason", v.reason);
    get_optional(j, "requestedDate", v.requestedDate);
    get_optional(j, "amount", v.amount);
    get_optional(j, "bucket", v.bucket);
    get_optional(j, "partyAccount", v.partyAccount);
    get_optional(j, "product", v.product);
    get_optional(j, "status", v.status);
    get_optional(j, "usageType", v.usageType);
}

void to_json(nlohmann::json& j, const AdjustBalance& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "confirmationDate", v.confirmationDate);
    put_optional(j, "description", v.description);
    put_optional(j, "reason", v.reason);
    put_optional(j, "requestedDate", v.requestedDate);
    put_optional(j, "adjustType", v.adjustType);
    put_optional(j, "amount", v.amount);
    put_optional(j, "bucket", v.bucket);
    put_optional(j, "partyAccount", v.partyAccount);
    put_optional(j, "product", v.product);
    put_optional(j, "status", v.status);
    put_optional(j, "usageType", v.usageType);
}

void from_json(const nlohmann::json& j, AdjustBalance& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "confirmationDate", v.confirmationDate);
    get_optional(j, "description", v.description);
    get_optional(j, "reason", v.reason);
    get_optional(j, "requestedDate", v.requestedDate);
    get_optional(j, "adjustType", v.adjustType);
    get_optional(j, "amount", v.amount);
    get_optional(j, "bucket", v.bucket);
    get_optional(j, "partyAccount", v.partyAccount);
    get_optional(j, "product", v.product);
    get_optional(j, "status", v.status);
    get_optional(j, "usageType", v.usageType);
}

void to_json(nlohmann::json& j, const ReserveBalance& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "confirmationDate", v.confirmationDate);
    put_optional(j, "description", v.description);
    put_optional(j, "reason", v.reason);
    put_optional(j, "requestedDate", v.requestedDate);
    put_optional(j, "amount", v.amount);
    put_optional(j, "bucket", v.bucket);
    put_optional(j, "partyAccount", v.partyAccount);
    put_optional(j, "product", v.product);
    put_optional(j, "status", v.status);
    put_optional(j, "usageType", v.usageType);
}

void from_json(const nlohmann::json& j, ReserveBalance& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "confirmationDate", v.confirmationDate);
    get_optional(j, "description", v.description);
    get_optional(j, "reason", v.reason);
    get_optional(j, "requestedDate", v.requestedDate);
    get_optional(j, "amount", v.amount);
    get_optional(j, "bucket", v.bucket);
    get_optional(j, "partyAccount", v.partyAccount);
    get_optional(j, "product", v.product);
    get_optional(j, "status", v.status);
    get_optional(j, "usageType", v.usageType);
}

} // namespace bss_sid
