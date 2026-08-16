// nfs/udr: UDR (Unified Data Repository), Nudr_DataRepository context-data group.
// Source: specs/5G_APIs-REL-19/TS29505_Subscription_Data.yaml (the file TS29504_Nudr_DR.yaml's
// paths $ref into -- TS29504 itself defines almost no schemas of its own), commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6. Phase 2's fifth NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope, agreed with the user before implementation: the `context-data` group --
// QueryAmfContext3gpp, CreateAmfContext3gpp, AmfContext3gpp (AMF 3GPP-access context, singular
// per UE -- no delete operation exists for this resource in the spec, checked not assumed) and
// QuerySmfRegList, QuerySmfRegistration, CreateOrUpdateSmfRegistration, UpdateSmfContext,
// DeleteSmfRegistration (SMF registration context, one per UE+pduSessionId). These mirror
// nfs/udm's UECM registration groups almost exactly -- 3GPP's real intended backing store for
// that data -- but per explicit user decision this turn, UDM's own AmfRegistrationStore/
// SmfRegistrationStore are NOT wired to call UDR yet; that remains a separate, deliberate future
// turn touching already-committed UDM code, not silently done here.
//
// UPDATE (ADR-0069, gap-closure Tier 1b): the `provisioned-data` group (am-data/smf-selection-
// subscription-data/sm-data) is now implemented -- real GET routes, keyed by (ueId,
// servingPlmnId) per the real path shape. Still real, disclosed: this group is genuinely GET-only
// in the spec (no create/update operation exists at all), so there is no live provisioning path;
// data is seeded at startup instead (see main() below), and nfs/udm's own GetAmData/GetSmfSelData/
// GetSmData now call these real routes instead of returning the old permanently-empty stub.
//
// UPDATE (ADR-0072, gap-closure: real N28 end-to-end): the `policy-data` group's SM policy
// resource (/policy-data/ues/{ueId}/sm-data, real schema SmPolicyData -- source
// TS29519_Policy_Data.yaml) is now implemented, real GET+PATCH, keyed by ueId alone. Unlike
// provisioned-data above, this real resource DOES support PATCH (application/merge-patch+json) --
// but the real spec still has no POST/create operation for it, so this project's own store treats
// PATCH as upsert-capable (a disclosed, deliberate choice enabling GUI-driven creation -- see
// stores.hpp's own comment on SmPolicyDataStore). PCF is the real consumer, fetching a
// subscriber's subscSpendingLimits/policyCounterIds per DNN to decide whether to subscribe to
// CHF's Nchf_SpendingLimitControl.
//
// Deliberately still deferred, not dropped: authentication-data (real AUSF/UDM auth flow exists
// now, but this UDR resource group itself is separate, still not implemented here);
// ue-update-confirmation-data (SoR/UPU); context-data's other sub-resources (non-3gpp-access,
// smsf-3gpp/non-3gpp, ip-sm-gw, mwd, roaming-information, pei-info, ee-subscriptions,
// sdm-subscriptions, nidd-authorizations); operator-specific-data; lcs-*; pp-data; group-data;
// shared-data; subs-to-notify; policy-data's own other resources (am-data, ue-policy-set,
// sponsor-connectivity-data, bdt-data, slice-control-data, and others -- SM policy data is the
// only policy-data resource this project's real N28 use case needs); all of
// TS29504_Nudr_GroupIDmap.yaml.
//
// RFC 6902 JSON Patch, not RFC 7396 Merge Patch: AmfContext3gpp and UpdateSmfContext both use
// application/json-patch+json (confirmed by reading the YAML directly), unlike UDM's
// Update3GppRegistration/UpdateSmfRegistration which use application/merge-patch+json. Applied
// via nlohmann::json::patch() (matching nfs/nrf's own UpdateNFInstance), not .merge_patch(). Both
// patch responses always return 204 here (spec permits either 204-no-body or 200 with a
// PatchResult report body listing per-operation outcomes; 204 is simpler and doesn't require
// fabricating report items with no real per-op tracking behind them -- a disclosed, deliberate
// choice, not an oversight).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"
#include "sbi_core/jwt.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <thread>

// TS29505_Subscription_Data's own types now live in TS29122_CommonData_grp.hpp -- see
// nfs/chf/src/stores.hpp's own comment (ADR-0072).
#include "TS29122_CommonData_grp.hpp"
#include "stores.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/udr/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7781;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9468";
constexpr const char* kNfType = "UDR";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kApiRoot = "/nudr-dr/v2";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// Real PostgreSQL persistence (ADR-0068, gap-closure Tier 1a) -- same getenv-based config pattern
// as bss/product-catalog's own database_conninfo(). Deliberately NOT try-catch-degraded the way
// CHF's RatingDecisionStore is (that's a best-effort audit trail; UDR's context-data group IS this
// NF's entire purpose, so a Postgres it can't reach means UDR has nothing meaningful to serve --
// fail fast at startup instead of silently degrading, same "hard-require" choice
// bss/product-catalog's own ProductOfferingStore already makes).
std::string database_conninfo() {
    if (const char* env = std::getenv("UDR_DATABASE_URL")) {
        return env;
    }
    return "postgresql://udr:udr@localhost:5432/udr";
}

// Same pattern as every other NF's check_bearer -- see nfs/nrf/src/main.cpp's comment for why a
// missing Authorization header is not itself a 401 (bootstrap security alternative:
// `security: [{}, oAuth2ClientCredentials:[...]]` in the YAML).
std::optional<sbi_core::jwt::VerifyResult> check_bearer(const sbi_core::http2::Request& req,
                                                        sbi_core::jwt::Verifier& verifier) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) {
        return std::nullopt;
    }
    const std::string& value = it->second;
    constexpr std::string_view kPrefix = "Bearer ";
    if (value.size() <= kPrefix.size() || value.compare(0, kPrefix.size(), kPrefix) != 0) {
        sbi_core::jwt::VerifyResult r;
        r.valid = false;
        r.error = "Authorization header present but not a Bearer token";
        return r;
    }
    return verifier.verify(value.substr(kPrefix.size()));
}

// Runs on a dedicated thread, never on the server's io_context -- same reasoning as
// nfs/amf/src/main.cpp's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& udr_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/udr/cert.pem",
        .key_path = CERTS_DIR "/udr/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client http_client(std::move(client_tls));

    for (int attempt = 0; attempt < 300; ++attempt) {
        sbi_core::http2::ClientRequest probe;
        probe.method = "GET";
        probe.url = std::string(kNrfBase) +
                    "/nnrf-nfm/v1/nf-instances/00000000-0000-4000-8000-000000000000";
        if (http_client.send(probe).has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sbi_core::OAuth2Client oauth(
        http_client, std::string(kNrfBase) + "/oauth2/token", udr_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", udr_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("udr: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + udr_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("udr: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("udr: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("udr: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + udr_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("udr: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("udr");
    sbi_core::init_tracing("udr");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string udr_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("udr: starting, nfInstanceId={}", udr_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/udr/cert.pem",
        .key_path = CERTS_DIR "/udr/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    const auto conninfo = database_conninfo();
    udr::AmfContextStore amf_contexts(conninfo);
    udr::SmfRegistrationStore smf_registrations(conninfo);
    udr::ProvisionedDataStore provisioned_data(conninfo);
    udr::SmPolicyDataStore sm_policy_data(conninfo);

    // Real seed data (ADR-0069, gap-closure Tier 1b) -- the real provisioned-data group is
    // GET-only per spec (no create/update operation exists at all, see schema.postgres.sql's own
    // header), so there is no live provisioning path yet; seeded here for the same two real test
    // SUPIs nfs/udm/src/main.cpp's own AuthenticationSubscriptionStore already seeds
    // ("imsi-999700000000001"/"...002", UERANSIM's own real test values), so a real end-to-end
    // AUSF->UDM->UDR chain has real, non-empty data to return for at least these subscribers.
    // servingPlmnId "99970" = this project's own real lab PLMN, mcc=999/mnc=70 (ADR-0016),
    // VarPlmnId's real format per TS29505_Subscription_Data.yaml (mcc+mnc concatenated).
    // sst=1/sd="000001" matches that same ADR-0016 lab S-NSSAI. dnn="internet" is the real,
    // standard default DNN/APN name used industry-wide (free5gc/open5gs both default to it too),
    // not invented for this project. SmfSelectionSubscriptionData.subscribedSnssaiInfos and
    // SessionManagementSubscriptionData.dnnConfigurations are real, cited, opaque-JSON fields this
    // codegen couldn't strongly type (OPAQUE FALLBACK) -- left unpopulated here, a real, disclosed
    // gap rather than a guessed nested shape.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json am_data;
        am_data["nssai"]["defaultSingleNssais"] = json::array({json{{"sst", 1}, {"sd", "000001"}}});
        json sm_data;
        sm_data["singleNssai"] = json{{"sst", 1}, {"sd", "000001"}};
        provisioned_data.seed(supi,
                              "99970",
                              std::make_optional(am_data),
                              std::make_optional(json::object()),
                              std::make_optional(sm_data));
    }

    auto meter = sbi_core::get_meter("udr");
    auto amf_ctx_write_counter = meter->CreateUInt64Counter(
        "udr_amf_context_write_total", "Total CreateAmfContext3gpp/AmfContext3gpp calls");
    auto smf_reg_write_counter =
        meter->CreateUInt64Counter("udr_smf_registration_write_total",
                                   "Total CreateOrUpdateSmfRegistration/UpdateSmfContext calls");
    auto smf_reg_delete_counter = meter->CreateUInt64Counter("udr_smf_registration_delete_total",
                                                             "Total DeleteSmfRegistration calls");
    auto provisioned_data_get_counter = meter->CreateUInt64Counter(
        "udr_provisioned_data_get_total",
        "Total provisioned-data am-data/smf-selection-subscription-data/sm-data GET calls");
    auto sm_policy_data_get_counter = meter->CreateUInt64Counter(
        "udr_sm_policy_data_get_total", "Total ReadSessionManagementPolicyData calls");
    auto sm_policy_data_patch_counter = meter->CreateUInt64Counter(
        "udr_sm_policy_data_patch_total", "Total UpdateSessionManagementPolicyData calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    const std::string amf_ctx_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/amf-3gpp-access";

    server.add_route(
        "GET",
        amf_ctx_path_pattern,
        [&verifier, &amf_contexts](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto context = amf_contexts.get(ue_id);
            if (!context.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF context for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, context->dump());
        });

    server.add_route(
        "PUT",
        amf_ctx_path_pattern,
        [&verifier, &amf_contexts, &amf_ctx_write_counter, amf_ctx_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<sbi_gen::Amf3GppAccessRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = amf_contexts.put(ue_id, j);
            amf_ctx_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", amf_ctx_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        amf_ctx_path_pattern,
        [&verifier, &amf_contexts, &amf_ctx_write_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            std::optional<json> patched;
            try {
                patched = amf_contexts.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF context for ueId " + ue_id);
            }
            amf_ctx_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    const std::string smf_reg_list_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/smf-registrations";
    const std::string smf_reg_path_pattern = smf_reg_list_path_pattern + "/{pduSessionId}";

    server.add_route(
        "GET",
        smf_reg_list_path_pattern,
        [&verifier, &smf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            sbi_gen::SmfRegList list;
            for (const auto& registration : smf_registrations.list_for_ue(ue_id)) {
                list.push_back(registration.get<sbi_gen::SmfRegistration>());
            }
            json j = list;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "GET",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            auto registration = smf_registrations.get(ue_id, pdu_session_id);
            if (!registration.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            return sbi_core::http2::Response::json(200, registration->dump());
        });

    server.add_route(
        "PUT",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations, &smf_reg_write_counter, smf_reg_list_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmfRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            json j = *body;
            const bool is_new = smf_registrations.put(ue_id, pdu_session_id, j);
            smf_reg_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", smf_reg_list_path_pattern + "/" + pdu_session_id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations, &smf_reg_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            std::optional<json> patched;
            try {
                patched = smf_registrations.apply_patch(ue_id, pdu_session_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            smf_reg_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "DELETE",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations, &smf_reg_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            if (!smf_registrations.get(ue_id, pdu_session_id).has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            smf_registrations.remove(ue_id, pdu_session_id);
            smf_reg_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: provisioned-data group (ADR-0069, gap-closure Tier 1b) -- real,
    // GET-only per spec (see this file's own header), keyed by (ueId, servingPlmnId) per the real
    // path shape TS29505_Subscription_Data.yaml defines. ---

    const std::string provisioned_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/{servingPlmnId}/provisioned-data";

    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/am-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_am_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned am-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/smf-selection-subscription-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_smf_sel_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No provisioned smf-selection-subscription-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/sm-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_sm_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned sm-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: policy-data group, SM policy resource (ADR-0072, gap-closure: real
    // N28 end-to-end) -- real GET+PATCH per TS29519_Policy_Data.yaml, keyed by ueId alone (no
    // servingPlmnId in the real path, unlike provisioned-data above -- genuinely different
    // resource, see schema.postgres.sql's own comment). ---

    const std::string sm_policy_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/ues/{ueId}/sm-data";

    server.add_route(
        "GET",
        sm_policy_data_path_pattern,
        [&verifier, &sm_policy_data, &sm_policy_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = sm_policy_data.get(ue_id);
            sm_policy_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM policy data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        sm_policy_data_path_pattern,
        [&verifier, &sm_policy_data, &sm_policy_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto patched = sm_policy_data.merge_patch(ue_id, patch);
            sm_policy_data_patch_counter->Add(1);
            // Real spec: 204 (no body) or 200 (with the updated SmPolicyData) are both valid --
            // this project returns 200 with the real updated document, same real information a
            // future GUI editing this resource would want back without a second GET round-trip.
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    std::thread(run_nrf_lifecycle, udr_instance_id).detach();

    server.start();
    spdlog::info("udr: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("udr: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
