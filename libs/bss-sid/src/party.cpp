#include "bss_sid/party.hpp"

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

void to_json(nlohmann::json& j, const MediumCharacteristic& v) {
    j = nlohmann::json::object();
    put_optional(j, "city", v.city);
    put_optional(j, "contactType", v.contactType);
    put_optional(j, "country", v.country);
    put_optional(j, "emailAddress", v.emailAddress);
    put_optional(j, "faxNumber", v.faxNumber);
    put_optional(j, "phoneNumber", v.phoneNumber);
    put_optional(j, "postCode", v.postCode);
    put_optional(j, "socialNetworkId", v.socialNetworkId);
    put_optional(j, "stateOrProvince", v.stateOrProvince);
    put_optional(j, "street1", v.street1);
    put_optional(j, "street2", v.street2);
}

void from_json(const nlohmann::json& j, MediumCharacteristic& v) {
    get_optional(j, "city", v.city);
    get_optional(j, "contactType", v.contactType);
    get_optional(j, "country", v.country);
    get_optional(j, "emailAddress", v.emailAddress);
    get_optional(j, "faxNumber", v.faxNumber);
    get_optional(j, "phoneNumber", v.phoneNumber);
    get_optional(j, "postCode", v.postCode);
    get_optional(j, "socialNetworkId", v.socialNetworkId);
    get_optional(j, "stateOrProvince", v.stateOrProvince);
    get_optional(j, "street1", v.street1);
    get_optional(j, "street2", v.street2);
}

void to_json(nlohmann::json& j, const ContactMedium& v) {
    j = nlohmann::json::object();
    put_optional(j, "mediumType", v.mediumType);
    put_optional(j, "preferred", v.preferred);
    put_optional(j, "characteristic", v.characteristic);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, ContactMedium& v) {
    get_optional(j, "mediumType", v.mediumType);
    get_optional(j, "preferred", v.preferred);
    get_optional(j, "characteristic", v.characteristic);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const PartyCreditProfile& v) {
    j = nlohmann::json::object();
    put_optional(j, "creditAgencyName", v.creditAgencyName);
    put_optional(j, "creditAgencyType", v.creditAgencyType);
    put_optional(j, "ratingReference", v.ratingReference);
    put_optional(j, "ratingScore", v.ratingScore);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, PartyCreditProfile& v) {
    get_optional(j, "creditAgencyName", v.creditAgencyName);
    get_optional(j, "creditAgencyType", v.creditAgencyType);
    get_optional(j, "ratingReference", v.ratingReference);
    get_optional(j, "ratingScore", v.ratingScore);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const Disability& v) {
    j = nlohmann::json::object();
    put_optional(j, "disabilityCode", v.disabilityCode);
    put_optional(j, "disabilityName", v.disabilityName);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, Disability& v) {
    get_optional(j, "disabilityCode", v.disabilityCode);
    get_optional(j, "disabilityName", v.disabilityName);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const ExternalReference& v) {
    j = nlohmann::json::object();
    put_optional(j, "externalReferenceType", v.externalReferenceType);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, ExternalReference& v) {
    get_optional(j, "externalReferenceType", v.externalReferenceType);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const LanguageAbility& v) {
    j = nlohmann::json::object();
    put_optional(j, "isFavouriteLanguage", v.isFavouriteLanguage);
    put_optional(j, "languageCode", v.languageCode);
    put_optional(j, "languageName", v.languageName);
    put_optional(j, "listeningProficiency", v.listeningProficiency);
    put_optional(j, "readingProficiency", v.readingProficiency);
    put_optional(j, "speakingProficiency", v.speakingProficiency);
    put_optional(j, "writingProficiency", v.writingProficiency);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, LanguageAbility& v) {
    get_optional(j, "isFavouriteLanguage", v.isFavouriteLanguage);
    get_optional(j, "languageCode", v.languageCode);
    get_optional(j, "languageName", v.languageName);
    get_optional(j, "listeningProficiency", v.listeningProficiency);
    get_optional(j, "readingProficiency", v.readingProficiency);
    get_optional(j, "speakingProficiency", v.speakingProficiency);
    get_optional(j, "writingProficiency", v.writingProficiency);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const OtherNameIndividual& v) {
    j = nlohmann::json::object();
    put_optional(j, "aristocraticTitle", v.aristocraticTitle);
    put_optional(j, "familyName", v.familyName);
    put_optional(j, "familyNamePrefix", v.familyNamePrefix);
    put_optional(j, "formattedName", v.formattedName);
    put_optional(j, "fullName", v.fullName);
    put_optional(j, "generation", v.generation);
    put_optional(j, "givenName", v.givenName);
    put_optional(j, "legalName", v.legalName);
    put_optional(j, "middleName", v.middleName);
    put_optional(j, "preferredGivenName", v.preferredGivenName);
    put_optional(j, "title", v.title);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, OtherNameIndividual& v) {
    get_optional(j, "aristocraticTitle", v.aristocraticTitle);
    get_optional(j, "familyName", v.familyName);
    get_optional(j, "familyNamePrefix", v.familyNamePrefix);
    get_optional(j, "formattedName", v.formattedName);
    get_optional(j, "fullName", v.fullName);
    get_optional(j, "generation", v.generation);
    get_optional(j, "givenName", v.givenName);
    get_optional(j, "legalName", v.legalName);
    get_optional(j, "middleName", v.middleName);
    get_optional(j, "preferredGivenName", v.preferredGivenName);
    get_optional(j, "title", v.title);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const Characteristic& v) {
    j = nlohmann::json::object();
    j["name"] = v.name;
    put_optional(j, "valueType", v.valueType);
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, Characteristic& v) {
    j.at("name").get_to(v.name);
    get_optional(j, "valueType", v.valueType);
    v.value = j.at("value");
}

void to_json(nlohmann::json& j, const Skill& v) {
    j = nlohmann::json::object();
    put_optional(j, "comment", v.comment);
    put_optional(j, "evaluatedLevel", v.evaluatedLevel);
    put_optional(j, "skillCode", v.skillCode);
    put_optional(j, "skillName", v.skillName);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, Skill& v) {
    get_optional(j, "comment", v.comment);
    get_optional(j, "evaluatedLevel", v.evaluatedLevel);
    get_optional(j, "skillCode", v.skillCode);
    get_optional(j, "skillName", v.skillName);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const TaxDefinition& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "name", v.name);
    put_optional(j, "taxType", v.taxType);
}

void from_json(const nlohmann::json& j, TaxDefinition& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "name", v.name);
    get_optional(j, "taxType", v.taxType);
}

void to_json(nlohmann::json& j, const TaxExemptionCertificate& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "attachment", v.attachment);
    put_array(j, "taxDefinition", v.taxDefinition);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, TaxExemptionCertificate& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "attachment", v.attachment);
    get_array(j, "taxDefinition", v.taxDefinition);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const IndividualIdentification& v) {
    j = nlohmann::json::object();
    put_optional(j, "identificationType", v.identificationType);
    put_optional(j, "identificationId", v.identificationId);
    put_optional(j, "issuingAuthority", v.issuingAuthority);
    put_optional(j, "issuingDate", v.issuingDate);
    put_optional(j, "attachment", v.attachment);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, IndividualIdentification& v) {
    get_optional(j, "identificationType", v.identificationType);
    get_optional(j, "identificationId", v.identificationId);
    get_optional(j, "issuingAuthority", v.issuingAuthority);
    get_optional(j, "issuingDate", v.issuingDate);
    get_optional(j, "attachment", v.attachment);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const Individual& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "aristocraticTitle", v.aristocraticTitle);
    put_optional(j, "birthDate", v.birthDate);
    put_optional(j, "countryOfBirth", v.countryOfBirth);
    put_optional(j, "deathDate", v.deathDate);
    put_optional(j, "familyName", v.familyName);
    put_optional(j, "familyNamePrefix", v.familyNamePrefix);
    put_optional(j, "formattedName", v.formattedName);
    put_optional(j, "fullName", v.fullName);
    put_optional(j, "gender", v.gender);
    put_optional(j, "generation", v.generation);
    put_optional(j, "givenName", v.givenName);
    put_optional(j, "legalName", v.legalName);
    put_optional(j, "location", v.location);
    put_optional(j, "maritalStatus", v.maritalStatus);
    put_optional(j, "middleName", v.middleName);
    put_optional(j, "nationality", v.nationality);
    put_optional(j, "placeOfBirth", v.placeOfBirth);
    put_optional(j, "preferredGivenName", v.preferredGivenName);
    put_optional(j, "title", v.title);
    put_array(j, "contactMedium", v.contactMedium);
    put_array(j, "creditRating", v.creditRating);
    put_array(j, "disability", v.disability);
    put_array(j, "externalReference", v.externalReference);
    put_array(j, "individualIdentification", v.individualIdentification);
    put_array(j, "languageAbility", v.languageAbility);
    put_array(j, "otherName", v.otherName);
    put_array(j, "partyCharacteristic", v.partyCharacteristic);
    put_array(j, "relatedParty", v.relatedParty);
    put_array(j, "skill", v.skill);
    put_optional(j, "status", v.status);
    put_array(j, "taxExemptionCertificate", v.taxExemptionCertificate);
}

void from_json(const nlohmann::json& j, Individual& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "aristocraticTitle", v.aristocraticTitle);
    get_optional(j, "birthDate", v.birthDate);
    get_optional(j, "countryOfBirth", v.countryOfBirth);
    get_optional(j, "deathDate", v.deathDate);
    get_optional(j, "familyName", v.familyName);
    get_optional(j, "familyNamePrefix", v.familyNamePrefix);
    get_optional(j, "formattedName", v.formattedName);
    get_optional(j, "fullName", v.fullName);
    get_optional(j, "gender", v.gender);
    get_optional(j, "generation", v.generation);
    get_optional(j, "givenName", v.givenName);
    get_optional(j, "legalName", v.legalName);
    get_optional(j, "location", v.location);
    get_optional(j, "maritalStatus", v.maritalStatus);
    get_optional(j, "middleName", v.middleName);
    get_optional(j, "nationality", v.nationality);
    get_optional(j, "placeOfBirth", v.placeOfBirth);
    get_optional(j, "preferredGivenName", v.preferredGivenName);
    get_optional(j, "title", v.title);
    get_array(j, "contactMedium", v.contactMedium);
    get_array(j, "creditRating", v.creditRating);
    get_array(j, "disability", v.disability);
    get_array(j, "externalReference", v.externalReference);
    get_array(j, "individualIdentification", v.individualIdentification);
    get_array(j, "languageAbility", v.languageAbility);
    get_array(j, "otherName", v.otherName);
    get_array(j, "partyCharacteristic", v.partyCharacteristic);
    get_array(j, "relatedParty", v.relatedParty);
    get_array(j, "skill", v.skill);
    get_optional(j, "status", v.status);
    get_array(j, "taxExemptionCertificate", v.taxExemptionCertificate);
}

Individual map_supi_to_individual(const std::string& supi) {
    Individual individual{};
    individual.individualIdentification.push_back(
        IndividualIdentification{.identificationType = "SUPI", .identificationId = supi});
    return individual;
}

void to_json(nlohmann::json& j, const OrganizationRef& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "name", v.name);
}

void from_json(const nlohmann::json& j, OrganizationRef& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "name", v.name);
}

void to_json(nlohmann::json& j, const OrganizationChildRelationship& v) {
    j = nlohmann::json::object();
    put_optional(j, "relationshipType", v.relationshipType);
    put_optional(j, "organization", v.organization);
}

void from_json(const nlohmann::json& j, OrganizationChildRelationship& v) {
    get_optional(j, "relationshipType", v.relationshipType);
    get_optional(j, "organization", v.organization);
}

void to_json(nlohmann::json& j, const OrganizationParentRelationship& v) {
    j = nlohmann::json::object();
    put_optional(j, "relationshipType", v.relationshipType);
    put_optional(j, "organization", v.organization);
}

void from_json(const nlohmann::json& j, OrganizationParentRelationship& v) {
    get_optional(j, "relationshipType", v.relationshipType);
    get_optional(j, "organization", v.organization);
}

void to_json(nlohmann::json& j, const OrganizationIdentification& v) {
    j = nlohmann::json::object();
    put_optional(j, "identificationType", v.identificationType);
    put_optional(j, "identificationId", v.identificationId);
    put_optional(j, "issuingAuthority", v.issuingAuthority);
    put_optional(j, "issuingDate", v.issuingDate);
    put_optional(j, "attachment", v.attachment);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, OrganizationIdentification& v) {
    get_optional(j, "identificationType", v.identificationType);
    get_optional(j, "identificationId", v.identificationId);
    get_optional(j, "issuingAuthority", v.issuingAuthority);
    get_optional(j, "issuingDate", v.issuingDate);
    get_optional(j, "attachment", v.attachment);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const OtherNameOrganization& v) {
    j = nlohmann::json::object();
    put_optional(j, "name", v.name);
    put_optional(j, "nameType", v.nameType);
    put_optional(j, "tradingName", v.tradingName);
    put_optional(j, "validFor", v.validFor);
}

void from_json(const nlohmann::json& j, OtherNameOrganization& v) {
    get_optional(j, "name", v.name);
    get_optional(j, "nameType", v.nameType);
    get_optional(j, "tradingName", v.tradingName);
    get_optional(j, "validFor", v.validFor);
}

void to_json(nlohmann::json& j, const Organization& v) {
    j = nlohmann::json::object();
    put_optional(j, "id", v.id);
    put_optional(j, "href", v.href);
    put_optional(j, "isHeadOffice", v.isHeadOffice);
    put_optional(j, "isLegalEntity", v.isLegalEntity);
    put_optional(j, "name", v.name);
    put_optional(j, "nameType", v.nameType);
    put_optional(j, "organizationType", v.organizationType);
    put_optional(j, "tradingName", v.tradingName);
    put_array(j, "contactMedium", v.contactMedium);
    put_array(j, "creditRating", v.creditRating);
    put_optional(j, "existsDuring", v.existsDuring);
    put_array(j, "externalReference", v.externalReference);
    put_array(j, "organizationChildRelationship", v.organizationChildRelationship);
    put_array(j, "organizationIdentification", v.organizationIdentification);
    put_optional(j, "organizationParentRelationship", v.organizationParentRelationship);
    put_array(j, "otherName", v.otherName);
    put_array(j, "partyCharacteristic", v.partyCharacteristic);
    put_array(j, "relatedParty", v.relatedParty);
    put_optional(j, "status", v.status);
    put_array(j, "taxExemptionCertificate", v.taxExemptionCertificate);
}

void from_json(const nlohmann::json& j, Organization& v) {
    get_optional(j, "id", v.id);
    get_optional(j, "href", v.href);
    get_optional(j, "isHeadOffice", v.isHeadOffice);
    get_optional(j, "isLegalEntity", v.isLegalEntity);
    get_optional(j, "name", v.name);
    get_optional(j, "nameType", v.nameType);
    get_optional(j, "organizationType", v.organizationType);
    get_optional(j, "tradingName", v.tradingName);
    get_array(j, "contactMedium", v.contactMedium);
    get_array(j, "creditRating", v.creditRating);
    get_optional(j, "existsDuring", v.existsDuring);
    get_array(j, "externalReference", v.externalReference);
    get_array(j, "organizationChildRelationship", v.organizationChildRelationship);
    get_array(j, "organizationIdentification", v.organizationIdentification);
    get_optional(j, "organizationParentRelationship", v.organizationParentRelationship);
    get_array(j, "otherName", v.otherName);
    get_array(j, "partyCharacteristic", v.partyCharacteristic);
    get_array(j, "relatedParty", v.relatedParty);
    get_optional(j, "status", v.status);
    get_array(j, "taxExemptionCertificate", v.taxExemptionCertificate);
}

} // namespace bss_sid
