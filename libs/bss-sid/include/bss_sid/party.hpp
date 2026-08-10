#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

// TM Forum SID Party -> TMF632 Party Management, `Individual` resource. Hand-written, not
// codegen'd: unlike the 3GPP OpenAPI YAML this project generates DTOs from (tools/sbi-codegen), no
// TM Forum Open API spec file is vendored in this repo (see docs/CHARGING_MAPPING.md's own
// "Sourcing methodology" section) -- same "hand-roll it, cite the real spec text" precedent this
// project already used for PFCP/GTP-U (protocols with no OpenAPI YAML either). Field names/types
// below were confirmed against TM Forum's real TMF632 v4.0.0 swagger
// (github.com/tmforum-apis/TMF632_PartyManagement) before writing this, not recalled from memory.
//
// Deliberately NOT the full TMF632 `Individual` schema: that resource has ~25 fields (name parts,
// birth/death dates, contact media, credit rating, disability, skills, ...) this project has no
// real data for and no consumer of yet. Only what docs/CHARGING_MAPPING.md's mapping table
// actually maps is modeled here -- see CLAUDE.md's "no speculative abstraction" rule. Extend when
// a real future need (an actual TMF632 REST surface, real Party CRUD) requires more of it.
//
// Deliberately independent of libs/sbi-core: per CLAUDE.md's own stated goal ("the BSS layer could
// be swapped for a commercial stack", ODA component boundaries), this library models a TM Forum
// Open API resource, a conceptually separate ecosystem from the 3GPP SBI stack sbi-core serves --
// no dependency on sbi_core's JSON helpers or transport, just nlohmann::json directly.

namespace bss_sid {

// TMF632 `IndividualIdentification` -- confirmed real fields: identificationId, identificationType,
// issuingAuthority, issuingDate, attachment, validFor, @type, @baseType, @schemaLocation. Only the
// two this project's mapping (docs/CHARGING_MAPPING.md) actually uses are modeled.
struct IndividualIdentification {
    std::string identificationType;
    std::string identificationId;
};
void to_json(nlohmann::json& j, const IndividualIdentification& v);
void from_json(const nlohmann::json& j, IndividualIdentification& v);

// TMF632 `Individual` -- confirmed real fields include id (required per the real swagger),
// href, ~20 name/demographic fields, and several array-of-object fields (contactMedium,
// individualIdentification, partyCharacteristic, ...). Only `id` and `individualIdentification`
// are modeled -- see file header. `id` is left unset by map_supi_to_individual below: this
// project has no real Party-management store yet (no BSS-side party ID allocator exists), so
// fabricating one here would misrepresent this as more complete than it is -- disclosed, not
// silently defaulted to something that looks real.
struct Individual {
    std::optional<std::string> id;
    std::vector<IndividualIdentification> individualIdentification;
};
void to_json(nlohmann::json& j, const Individual& v);
void from_json(const nlohmann::json& j, Individual& v);

// docs/CHARGING_MAPPING.md's resolved mapping: a 3GPP SUPI becomes one
// IndividualIdentification entry with identificationType="SUPI" -- chosen over
// partyCharacteristic because individualIdentification is TM Forum's purpose-built extensibility
// point for strongly-typed external identifiers, see the mapping doc's own resolution text.
Individual map_supi_to_individual(const std::string& supi);

} // namespace bss_sid
