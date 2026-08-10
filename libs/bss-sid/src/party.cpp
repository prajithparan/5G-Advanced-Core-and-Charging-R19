#include "bss_sid/party.hpp"

namespace bss_sid {

void to_json(nlohmann::json& j, const IndividualIdentification& v) {
    j = nlohmann::json::object();
    j["identificationType"] = v.identificationType;
    j["identificationId"] = v.identificationId;
}

void from_json(const nlohmann::json& j, IndividualIdentification& v) {
    j.at("identificationType").get_to(v.identificationType);
    j.at("identificationId").get_to(v.identificationId);
}

void to_json(nlohmann::json& j, const Individual& v) {
    j = nlohmann::json::object();
    if (v.id.has_value()) {
        j["id"] = *v.id;
    }
    j["individualIdentification"] = v.individualIdentification;
}

void from_json(const nlohmann::json& j, Individual& v) {
    if (const auto it = j.find("id"); it != j.end() && !it->is_null()) {
        v.id = it->get<std::string>();
    } else {
        v.id = std::nullopt;
    }
    if (const auto it = j.find("individualIdentification"); it != j.end() && !it->is_null()) {
        v.individualIdentification = it->get<std::vector<IndividualIdentification>>();
    } else {
        v.individualIdentification.clear();
    }
}

Individual map_supi_to_individual(const std::string& supi) {
    Individual individual{};
    individual.individualIdentification.push_back(
        IndividualIdentification{.identificationType = "SUPI", .identificationId = supi});
    return individual;
}

} // namespace bss_sid
