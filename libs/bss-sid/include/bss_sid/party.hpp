#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "bss_sid/product.hpp"

// TM Forum SID Party -> TMF632 Party Management, `Individual` and `Organization` resources.
// Hand-written, not codegen'd: unlike the 3GPP OpenAPI YAML this project generates DTOs from
// (tools/sbi-codegen), no TM Forum Open API spec file is vendored in this repo (see
// docs/CHARGING_MAPPING.md's own "Sourcing methodology" section) -- same "hand-roll it, cite the
// real spec text" precedent this project already used for PFCP/GTP-U (protocols with no OpenAPI
// YAML either). Field names/types confirmed against TM Forum's real TMF632 v4.0.0 swagger
// (github.com/tmforum-apis/TMF632_PartyManagement, TMF632-Party-v4.0.0.swagger.json), not recalled
// from memory.
//
// Extended 2026-08-11 (user directive: "no compromise on data model", docs/DECISIONS.md ADR-0060,
// docs/DATA_MODEL.md's E1/E10) to the FULL real field set of both `Individual` and `Organization`
// -- superseding this file's earlier "only what's mapped" minimalism. `Organization` is new here
// (E10's real TMF632 mechanism for the ENTERPRISE account hierarchy --
// `organizationParentRelationship`/`organizationChildRelationship`, already researched and
// resolved in docs/DATA_MODEL.md's own E10 section, not re-derived here).
//
// TimePeriod/Quantity/AttachmentRefOrValue/RelatedParty are already modeled in product.hpp with an
// identical real shape (TM Forum's shared common types across every Open API, not TMF620-specific
// -- confirmed independently against TMF632's own swagger too), reused directly rather than
// redefined here (same precedent balance.hpp already established for TimePeriod/Money/Quantity/
// ChannelRef/RelatedParty).
//
// Real, disclosed deviation from TMF632's own `required: [id]` on both `Individual` and
// `Organization`: `id` is modeled here as `optional<string>`, matching every other server-assigned
// id in this project's bss_sid structs (e.g. ProductOffering.id) -- this project has no real
// Party-management persistence store yet (no BSS-side party ID allocator exists, see
// `map_supi_to_individual` below), so a server-assigned id genuinely does not exist until one is
// created; fabricating one here would misrepresent this as more complete than it is.
//
// Deliberately independent of libs/sbi-core: per CLAUDE.md's own stated goal ("the BSS layer could
// be swapped for a commercial stack", ODA component boundaries), this library models a TM Forum
// Open API resource, a conceptually separate ecosystem from the 3GPP SBI stack sbi-core serves --
// no dependency on sbi_core's JSON helpers or transport, just nlohmann::json directly.

namespace bss_sid {

// Real TMF632 MediumCharacteristic -- the actual contact detail a ContactMedium carries (its
// `mediumType` sibling field says which of these fields is populated: "email" -> emailAddress,
// "phone" -> phoneNumber, "postalAddress" -> the street/city/etc. fields, ...).
struct MediumCharacteristic {
    std::optional<std::string> city;
    std::optional<std::string> contactType;
    std::optional<std::string> country;
    std::optional<std::string> emailAddress;
    std::optional<std::string> faxNumber;
    std::optional<std::string> phoneNumber;
    std::optional<std::string> postCode;
    std::optional<std::string> socialNetworkId;
    std::optional<std::string> stateOrProvince;
    std::optional<std::string> street1;
    std::optional<std::string> street2;
};
void to_json(nlohmann::json& j, const MediumCharacteristic& v);
void from_json(const nlohmann::json& j, MediumCharacteristic& v);

struct ContactMedium {
    std::optional<std::string> mediumType;
    std::optional<bool> preferred;
    std::optional<MediumCharacteristic> characteristic;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const ContactMedium& v);
void from_json(const nlohmann::json& j, ContactMedium& v);

struct PartyCreditProfile {
    std::optional<std::string> creditAgencyName;
    std::optional<std::string> creditAgencyType;
    std::optional<std::string> ratingReference;
    std::optional<int> ratingScore;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const PartyCreditProfile& v);
void from_json(const nlohmann::json& j, PartyCreditProfile& v);

struct Disability {
    std::optional<std::string> disabilityCode;
    std::optional<std::string> disabilityName;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const Disability& v);
void from_json(const nlohmann::json& j, Disability& v);

struct ExternalReference {
    std::optional<std::string> externalReferenceType;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const ExternalReference& v);
void from_json(const nlohmann::json& j, ExternalReference& v);

struct LanguageAbility {
    std::optional<bool> isFavouriteLanguage;
    std::optional<std::string> languageCode;
    std::optional<std::string> languageName;
    std::optional<std::string> listeningProficiency;
    std::optional<std::string> readingProficiency;
    std::optional<std::string> speakingProficiency;
    std::optional<std::string> writingProficiency;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const LanguageAbility& v);
void from_json(const nlohmann::json& j, LanguageAbility& v);

struct OtherNameIndividual {
    std::optional<std::string> aristocraticTitle;
    std::optional<std::string> familyName;
    std::optional<std::string> familyNamePrefix;
    std::optional<std::string> formattedName;
    std::optional<std::string> fullName;
    std::optional<std::string> generation;
    std::optional<std::string> givenName;
    std::optional<std::string> legalName;
    std::optional<std::string> middleName;
    std::optional<std::string> preferredGivenName;
    std::optional<std::string> title;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const OtherNameIndividual& v);
void from_json(const nlohmann::json& j, OtherNameIndividual& v);

// Real TMF632 Characteristic -- generic name/value bag (distinct from
// ProductSpecificationCharacteristic in product.hpp, which is TMF620's own, differently-shaped
// characteristic-definition type). Real spec: both `name` and `value` are `required`.
struct Characteristic {
    std::string name;
    std::optional<std::string> valueType;
    nlohmann::json value;
};
void to_json(nlohmann::json& j, const Characteristic& v);
void from_json(const nlohmann::json& j, Characteristic& v);

struct Skill {
    std::optional<std::string> comment;
    std::optional<std::string> evaluatedLevel;
    std::optional<std::string> skillCode;
    std::optional<std::string> skillName;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const Skill& v);
void from_json(const nlohmann::json& j, Skill& v);

struct TaxDefinition {
    std::optional<std::string> id;
    std::optional<std::string> name;
    std::optional<std::string> taxType;
};
void to_json(nlohmann::json& j, const TaxDefinition& v);
void from_json(const nlohmann::json& j, TaxDefinition& v);

struct TaxExemptionCertificate {
    std::optional<std::string> id;
    std::optional<AttachmentRefOrValue> attachment;
    std::vector<TaxDefinition> taxDefinition;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const TaxExemptionCertificate& v);
void from_json(const nlohmann::json& j, TaxExemptionCertificate& v);

// TMF632 `IndividualIdentification` -- full real field set: identificationId, identificationType,
// issuingAuthority, issuingDate, attachment, validFor.
struct IndividualIdentification {
    std::optional<std::string> identificationType;
    std::optional<std::string> identificationId;
    std::optional<std::string> issuingAuthority;
    std::optional<std::string> issuingDate;
    std::optional<AttachmentRefOrValue> attachment;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const IndividualIdentification& v);
void from_json(const nlohmann::json& j, IndividualIdentification& v);

// TMF632 `Individual` -- full real field set (id, href, ~19 name/demographic scalars, 11
// array/object fields, status). See this file's own header for the real, disclosed `id`
// optionality deviation.
struct Individual {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> aristocraticTitle;
    std::optional<std::string> birthDate;
    std::optional<std::string> countryOfBirth;
    std::optional<std::string> deathDate;
    std::optional<std::string> familyName;
    std::optional<std::string> familyNamePrefix;
    std::optional<std::string> formattedName;
    std::optional<std::string> fullName;
    std::optional<std::string> gender;
    std::optional<std::string> generation;
    std::optional<std::string> givenName;
    std::optional<std::string> legalName;
    std::optional<std::string> location;
    std::optional<std::string> maritalStatus;
    std::optional<std::string> middleName;
    std::optional<std::string> nationality;
    std::optional<std::string> placeOfBirth;
    std::optional<std::string> preferredGivenName;
    std::optional<std::string> title;
    std::vector<ContactMedium> contactMedium;
    std::vector<PartyCreditProfile> creditRating;
    std::vector<Disability> disability;
    std::vector<ExternalReference> externalReference;
    std::vector<IndividualIdentification> individualIdentification;
    std::vector<LanguageAbility> languageAbility;
    std::vector<OtherNameIndividual> otherName;
    std::vector<Characteristic> partyCharacteristic;
    std::vector<RelatedParty> relatedParty;
    std::vector<Skill> skill;
    std::optional<std::string> status; // IndividualStateType: initialized/validated/deceaded
    std::vector<TaxExemptionCertificate> taxExemptionCertificate;
};
void to_json(nlohmann::json& j, const Individual& v);
void from_json(const nlohmann::json& j, Individual& v);

// docs/CHARGING_MAPPING.md's resolved mapping: a 3GPP SUPI becomes one
// IndividualIdentification entry with identificationType="SUPI" -- chosen over
// partyCharacteristic because individualIdentification is TM Forum's purpose-built extensibility
// point for strongly-typed external identifiers, see the mapping doc's own resolution text.
Individual map_supi_to_individual(const std::string& supi);

// Real TMF632 OrganizationRef -- id/href/name only (the "Ref" half of a full Organization value;
// used by OrganizationChildRelationship/OrganizationParentRelationship below).
struct OrganizationRef {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const OrganizationRef& v);
void from_json(const nlohmann::json& j, OrganizationRef& v);

// Real TMF632 mechanism for the ENTERPRISE account hierarchy (docs/DATA_MODEL.md's own E10
// resolution, already confirmed there): a chain of Organization resources linked by these two real
// fields -- NOT a generic partyRelationship (that earlier candidate was checked and found wrong,
// see DATA_MODEL.md's own "resolved" note).
struct OrganizationChildRelationship {
    std::optional<std::string> relationshipType;
    std::optional<OrganizationRef> organization;
};
void to_json(nlohmann::json& j, const OrganizationChildRelationship& v);
void from_json(const nlohmann::json& j, OrganizationChildRelationship& v);

struct OrganizationParentRelationship {
    std::optional<std::string> relationshipType;
    std::optional<OrganizationRef> organization;
};
void to_json(nlohmann::json& j, const OrganizationParentRelationship& v);
void from_json(const nlohmann::json& j, OrganizationParentRelationship& v);

struct OrganizationIdentification {
    std::optional<std::string> identificationType;
    std::optional<std::string> identificationId;
    std::optional<std::string> issuingAuthority;
    std::optional<std::string> issuingDate;
    std::optional<AttachmentRefOrValue> attachment;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const OrganizationIdentification& v);
void from_json(const nlohmann::json& j, OrganizationIdentification& v);

struct OtherNameOrganization {
    std::optional<std::string> name;
    std::optional<std::string> nameType;
    std::optional<std::string> tradingName;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const OtherNameOrganization& v);
void from_json(const nlohmann::json& j, OtherNameOrganization& v);

// TMF632 `Organization` -- full real field set. Real spec quirk, confirmed not an inconsistency in
// this file: `organizationParentRelationship` is a SINGLE ref (an organization has at most one
// parent) while `organizationChildRelationship` is an ARRAY (an organization can have many
// children) -- deliberately asymmetric, matching the real swagger exactly.
struct Organization {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<bool> isHeadOffice;
    std::optional<bool> isLegalEntity;
    std::optional<std::string> name;
    std::optional<std::string> nameType;
    std::optional<std::string> organizationType;
    std::optional<std::string> tradingName;
    std::vector<ContactMedium> contactMedium;
    std::vector<PartyCreditProfile> creditRating;
    std::optional<TimePeriod> existsDuring;
    std::vector<ExternalReference> externalReference;
    std::vector<OrganizationChildRelationship> organizationChildRelationship;
    std::vector<OrganizationIdentification> organizationIdentification;
    std::optional<OrganizationParentRelationship> organizationParentRelationship;
    std::vector<OtherNameOrganization> otherName;
    std::vector<Characteristic> partyCharacteristic;
    std::vector<RelatedParty> relatedParty;
    std::optional<std::string> status; // OrganizationStateType: initialized/validated/closed
    std::vector<TaxExemptionCertificate> taxExemptionCertificate;
};
void to_json(nlohmann::json& j, const Organization& v);
void from_json(const nlohmann::json& j, Organization& v);

} // namespace bss_sid
