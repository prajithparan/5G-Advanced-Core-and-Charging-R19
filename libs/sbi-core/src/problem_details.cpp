#include "sbi_core/problem_details.hpp"

#include "sbi_core/json_serde.hpp"

namespace sbi_core {

void to_json(nlohmann::json& j, const InvalidParam& v) {
    j = nlohmann::json::object();
    j["param"] = v.param;
    put_optional(j, "reason", v.reason);
}

void from_json(const nlohmann::json& j, InvalidParam& v) {
    j.at("param").get_to(v.param);
    get_optional(j, "reason", v.reason);
}

void to_json(nlohmann::json& j, const ProblemDetails& v) {
    j = nlohmann::json::object();
    put_optional(j, "type", v.type);
    put_optional(j, "title", v.title);
    put_optional(j, "status", v.status);
    put_optional(j, "detail", v.detail);
    put_optional(j, "instance", v.instance);
    put_optional(j, "cause", v.cause);
    put_optional(j, "invalidParams", v.invalid_params);
    put_optional(j, "supportedFeatures", v.supported_features);
    put_optional(j, "accessTokenError", v.access_token_error);
    put_optional(j, "accessTokenRequest", v.access_token_request);
    put_optional(j, "nrfId", v.nrf_id);
    put_optional(j, "supportedApiVersions", v.supported_api_versions);
    put_optional(j, "noProfileMatchInfo", v.no_profile_match_info);
}

void from_json(const nlohmann::json& j, ProblemDetails& v) {
    get_optional(j, "type", v.type);
    get_optional(j, "title", v.title);
    get_optional(j, "status", v.status);
    get_optional(j, "detail", v.detail);
    get_optional(j, "instance", v.instance);
    get_optional(j, "cause", v.cause);
    get_optional(j, "invalidParams", v.invalid_params);
    get_optional(j, "supportedFeatures", v.supported_features);
    get_optional(j, "accessTokenError", v.access_token_error);
    get_optional(j, "accessTokenRequest", v.access_token_request);
    get_optional(j, "nrfId", v.nrf_id);
    get_optional(j, "supportedApiVersions", v.supported_api_versions);
    get_optional(j, "noProfileMatchInfo", v.no_profile_match_info);
}

ProblemDetails make_problem_details(std::int32_t status,
                                    std::string title,
                                    std::string detail,
                                    std::optional<std::string> cause) {
    ProblemDetails pd;
    pd.status = status;
    pd.title = std::move(title);
    pd.detail = std::move(detail);
    pd.cause = std::move(cause);
    return pd;
}

} // namespace sbi_core
