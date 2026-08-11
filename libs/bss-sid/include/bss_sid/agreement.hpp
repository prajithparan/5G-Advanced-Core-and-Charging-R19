#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "bss_sid/balance.hpp"
#include "bss_sid/party.hpp"

// TM Forum SID `Agreement` -> TMF651 Agreement Management. Hand-written, not codegen'd -- same
// "no TMF spec vendored, hand-roll it against a real confirmed source" precedent as this project's
// other bss_sid headers. Fields confirmed by downloading and directly parsing the real TMF651
// v4.0.0 swagger JSON (github.com/tmforum-apis/TMF651_AgreementManagement,
// TMF651-Agreement-v4.0.0.swagger.json), not recalled from memory. Full real field set (per the
// user's "no compromise on data model" directive).
//
// E7's own real SID mapping (docs/DATA_MODEL.md) -- `InterconnectAgreement` (this project's
// project-internal schema, see ../../bss/roaming-interconnect/schema.sql) is realized as a real
// TMF651 `Agreement`. `ProductRef`/`RelatedParty`/`TimePeriod`/`Characteristic` are reused directly
// from balance.hpp/party.hpp (identical real shapes, confirmed independently against TMF651's own
// swagger too) rather than redefined here.
//
// Real, disclosed limit (docs/DATA_MODEL.md's own E7 section, restated here): TAP3/RAP/NRTRDE
// (the real GSMA roaming-settlement CDR file formats `RoamingCdrFile.format` names) are GSMA
// documents behind membership -- not quoted from memory, not fabricated. `RoamingCdrFile` stores
// `raw_payload` opaquely (`format='STUB'` until a real GSMA spec is supplied) -- see the schema's
// own header.

namespace bss_sid {

struct AgreementAuthorization {
    std::optional<std::string> date;
    std::optional<std::string> signatureRepresentation;
    std::optional<std::string> state;
};
void to_json(nlohmann::json& j, const AgreementAuthorization& v);
void from_json(const nlohmann::json& j, AgreementAuthorization& v);

struct ProductOfferingRef {
    std::string id; // real spec: id is `required`
    std::optional<std::string> href;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const ProductOfferingRef& v);
void from_json(const nlohmann::json& j, ProductOfferingRef& v);

struct AgreementTermOrCondition {
    std::optional<std::string> id;
    std::optional<std::string> description;
    std::optional<TimePeriod> validFor;
};
void to_json(nlohmann::json& j, const AgreementTermOrCondition& v);
void from_json(const nlohmann::json& j, AgreementTermOrCondition& v);

struct AgreementItem {
    std::vector<ProductRef> product;
    std::vector<ProductOfferingRef> productOffering;
    std::vector<AgreementTermOrCondition> termOrCondition;
};
void to_json(nlohmann::json& j, const AgreementItem& v);
void from_json(const nlohmann::json& j, AgreementItem& v);

struct AgreementSpecificationRef {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> description;
    std::optional<std::string> name;
};
void to_json(nlohmann::json& j, const AgreementSpecificationRef& v);
void from_json(const nlohmann::json& j, AgreementSpecificationRef& v);

// AgreementRef itself is already modeled in product.hpp (TMF620's own real, identically-shaped
// `{id, href, name}` Ref, `id` required) and reused directly here -- not redefined, avoiding the
// exact real duplicate-definition bug this project's own E2 pass already hit once for
// `RelatedParty` (docs/DECISIONS.md ADR-0060).

// Real TMF651 Agreement -- full real field set. Real spec `required`: agreementItem,
// agreementType, engagedParty, name (this project's own use, like every other bss_sid resource,
// still models these as optional<T>/vector<T> rather than enforcing requiredness at the type
// level -- consistent with every other resource in this codebase, e.g. TMF620's ProductOffering).
struct Agreement {
    std::optional<std::string> id;
    std::optional<std::string> href;
    std::optional<std::string> agreementType;
    std::optional<std::string> description;
    std::optional<int> documentNumber;
    std::optional<std::string> initialDate;
    std::optional<std::string> name;
    std::optional<std::string> statementOfIntent;
    std::optional<std::string> status;
    std::optional<std::string> version;
    std::vector<AgreementAuthorization> agreementAuthorization;
    std::vector<AgreementItem> agreementItem;
    std::optional<TimePeriod> agreementPeriod;
    std::optional<AgreementSpecificationRef> agreementSpecification;
    std::vector<AgreementRef> associatedAgreement;
    std::vector<Characteristic> characteristic;
    std::optional<TimePeriod> completionDate;
    std::vector<RelatedParty> engagedParty;
};
void to_json(nlohmann::json& j, const Agreement& v);
void from_json(const nlohmann::json& j, Agreement& v);

} // namespace bss_sid
