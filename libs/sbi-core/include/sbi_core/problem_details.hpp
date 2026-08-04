#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Fields transcribed verbatim from TS29571_CommonData.yaml, components.schemas.ProblemDetails
// and .InvalidParam (3GPP R19 OpenAPI, branch REL-19, commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6, lines 518-559 and 615-630). See docs/DECISIONS.md for
// why this type is hand-written here instead of coming from tools/sbi-codegen: sbi-core is a Phase
// 0 dependency of the codegen pipeline itself, so ProblemDetails/InvalidParam are the one
// deliberate, disclosed exception to "never hand-write a DTO." Phase 1 generated code should reuse
// these types rather than regenerating them.
//
// accessTokenError, accessTokenRequest, and noProfileMatchInfo reference schemas defined in
// TS29510_Nnrf_NFAccessToken.yaml / TS29510_Nnrf_NFDiscovery.yaml. Modeling those fully would pull
// in the NRF access-token and discovery schemas before Phase 1 exists, so for now they are passed
// through as opaque JSON. This is a disclosed simplification, not a fabricated shape.

namespace sbi_core {

struct InvalidParam {
    std::string param;
    std::optional<std::string> reason;
};

void to_json(nlohmann::json& j, const InvalidParam& v);
void from_json(const nlohmann::json& j, InvalidParam& v);

struct ProblemDetails {
    std::optional<std::string> type;
    std::optional<std::string> title;
    std::optional<std::int32_t> status;
    std::optional<std::string> detail;
    std::optional<std::string> instance;
    std::optional<std::string> cause;
    std::optional<std::vector<InvalidParam>> invalid_params;
    std::optional<std::string> supported_features;
    std::optional<nlohmann::json> access_token_error;   // opaque, see file header
    std::optional<nlohmann::json> access_token_request; // opaque, see file header
    std::optional<std::string> nrf_id;
    std::optional<std::vector<std::string>> supported_api_versions;
    std::optional<nlohmann::json> no_profile_match_info; // opaque, see file header
};

void to_json(nlohmann::json& j, const ProblemDetails& v);
void from_json(const nlohmann::json& j, ProblemDetails& v);

// Convenience builders for the cases sbi-core itself needs (TS 29.500 clause 5.2.7 generic errors).
ProblemDetails make_problem_details(std::int32_t status,
                                    std::string title,
                                    std::string detail,
                                    std::optional<std::string> cause = std::nullopt);

} // namespace sbi_core
