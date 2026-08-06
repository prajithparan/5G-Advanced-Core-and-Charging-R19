// nfs/udm: UDM (Unified Data Management), Nudm_UECM + Nudm_SDM surfaces.
// Source: specs/5G_APIs-REL-19/TS29503_Nudm_UECM.yaml, TS29503_Nudm_SDM.yaml (commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 2's fourth NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope, agreed with the user before implementation:
// Nudm_UECM -- 3GppRegistration, Update3GppRegistration, Get3GppRegistration, deregAMF (the AMF
// 3GPP-access registration group), GetSmfRegistration, Registration, RetrieveSmfRegistration,
// UpdateSmfRegistration, SmfDeregistration (the SMF registration group).
// Nudm_SDM -- GetAmData, GetSmfSelData, GetSmData, Subscribe, Unsubscribe.
//
// Deliberately deferred, not dropped: Nudm_EE, Nudm_MT, Nudm_NIDDAU, Nudm_PP, Nudm_RSDS,
// Nudm_SSAU, Nudm_UEAU, Nudm_UEID (separate Nudm services); UECM's non-3GPP-AMF, SMSF (3GPP and
// non-3GPP), IP-SM-GW, and NWDAF registration groups; SDM's remaining ~25 GET operations
// (GetNSSAI, GetEcrData, GetUeCtxInAmfData, GetUeCtxInSmfData, LCS/V2X/ProSe/MBS/UC data, shared-
// data operations, GetSupiOrGpsi, Sor/Upu Ack, GetGroupIdentifiers, ...).
//
// Disclosed simplification, stated up front rather than discovered in review: UDM normally
// proxies subscriber-provisioned data from UDR (Nudr_DataRepository), which doesn't exist yet in
// this build order (UDR comes after UDM). GetAmData/GetSmfSelData/GetSmData therefore return a
// schema-valid but empty/default response for ANY supi -- there is no UDR-backed store to return
// real provisioned subscriber data from yet. UECM's registration operations are real bookkeeping
// (an AMF or SMF really did register), not a UDR-dependent gap.

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

#include "TS29122_CommonData_grp.hpp"
#include "stores.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/udm/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7780;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9467";
constexpr const char* kNfType = "UDM";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kUecmApiRoot = "/nudm-uecm/v1";
constexpr const char* kSdmApiRoot = "/nudm-sdm/v2";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

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
void run_nrf_lifecycle(const std::string& udm_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/udm/cert.pem",
        .key_path = CERTS_DIR "/udm/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", udm_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", udm_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("udm: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + udm_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("udm: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("udm: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("udm: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + udm_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("udm: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("udm");
    sbi_core::init_tracing("udm");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string udm_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("udm: starting, nfInstanceId={}", udm_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/udm/cert.pem",
        .key_path = CERTS_DIR "/udm/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    udm::AmfRegistrationStore amf_registrations;
    udm::SmfRegistrationStore smf_registrations;
    udm::SdmSubscriptionStore sdm_subscriptions;

    auto meter = sbi_core::get_meter("udm");
    auto amf_reg_counter =
        meter->CreateUInt64Counter("udm_amf_registration_total", "Total 3GppRegistration calls");
    auto amf_dereg_counter =
        meter->CreateUInt64Counter("udm_amf_deregistration_total", "Total deregAMF calls");
    auto smf_reg_counter =
        meter->CreateUInt64Counter("udm_smf_registration_total", "Total SMF Registration calls");
    auto smf_dereg_counter =
        meter->CreateUInt64Counter("udm_smf_deregistration_total", "Total SmfDeregistration calls");
    auto sdm_get_counter = meter->CreateUInt64Counter(
        "udm_sdm_data_get_total", "Total GetAmData/GetSmfSelData/GetSmData calls");
    auto sdm_subscribe_counter =
        meter->CreateUInt64Counter("udm_sdm_subscribe_total", "Total SDM Subscribe calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    // --- Nudm_UECM: AMF 3GPP-access registration group ---

    server.add_route(
        "PUT",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/amf-3gpp-access",
        [&verifier, &amf_registrations, &amf_reg_counter](const sbi_core::http2::Request& req) {
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
            const bool is_new = !amf_registrations.get(ue_id).has_value();
            json j = *body;
            amf_registrations.put(ue_id, j);
            amf_reg_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = is_new ? 201 : 200;
            resp.headers.emplace("content-type", "application/json");
            if (is_new) {
                resp.headers.emplace("location",
                                     std::string(kUecmApiRoot) + "/" + ue_id +
                                         "/registrations/amf-3gpp-access");
            }
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/amf-3gpp-access",
        [&verifier, &amf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto registration = amf_registrations.get(ue_id);
            if (!registration.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF 3GPP-access registration for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, registration->dump());
        });

    server.add_route(
        "PATCH",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/amf-3gpp-access",
        [&verifier, &amf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            // application/merge-patch+json (RFC 7396) -- validate it parses as the documented
            // Amf3GppAccessRegistrationModification shape before applying it, even though
            // .merge_patch() itself would accept any JSON object.
            sbi_core::http2::Response err;
            auto patch_dto =
                sbi_core::http2::parse_json_body<sbi_gen::Amf3GppAccessRegistrationModification>(
                    req, err);
            if (!patch_dto.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            auto patched = amf_registrations.merge_patch(ue_id, json::parse(req.body));
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF 3GPP-access registration for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, patched->dump());
        });

    server.add_route(
        "POST",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/amf-3gpp-access/dereg-amf",
        [&verifier, &amf_registrations, &amf_dereg_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::AmfDeregInfo>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!amf_registrations.get(ue_id).has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF 3GPP-access registration for ueId " + ue_id);
            }
            amf_registrations.remove(ue_id);
            amf_dereg_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudm_UECM: SMF registration group ---

    server.add_route(
        "GET",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/smf-registrations",
        [&verifier, &smf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            json arr = json::array();
            for (const auto& registration : smf_registrations.list_for_ue(ue_id)) {
                arr.push_back(registration);
            }
            sbi_gen::SmfRegistrationInfo resp_data;
            resp_data.smfRegistrationList = arr.get<std::vector<sbi_gen::SmfRegistration>>();
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "PUT",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/smf-registrations/{pduSessionId}",
        [&verifier, &smf_registrations, &smf_reg_counter](const sbi_core::http2::Request& req) {
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
            const bool is_new = !smf_registrations.get(ue_id, pdu_session_id).has_value();
            json j = *body;
            smf_registrations.put(ue_id, pdu_session_id, j);
            smf_reg_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = is_new ? 201 : 200;
            resp.headers.emplace("content-type", "application/json");
            if (is_new) {
                resp.headers.emplace("location",
                                     std::string(kUecmApiRoot) + "/" + ue_id +
                                         "/registrations/smf-registrations/" + pdu_session_id);
            }
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/smf-registrations/{pduSessionId}",
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
        "PATCH",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/smf-registrations/{pduSessionId}",
        [&verifier, &smf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto patch_dto =
                sbi_core::http2::parse_json_body<sbi_gen::SmfRegistrationModification>(req, err);
            if (!patch_dto.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            auto patched =
                smf_registrations.merge_patch(ue_id, pdu_session_id, json::parse(req.body));
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            return sbi_core::http2::Response::json(200, patched->dump());
        });

    server.add_route(
        "DELETE",
        std::string(kUecmApiRoot) + "/{ueId}/registrations/smf-registrations/{pduSessionId}",
        [&verifier, &smf_registrations, &smf_dereg_counter](const sbi_core::http2::Request& req) {
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
            smf_dereg_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudm_SDM: subscriber data retrieval (disclosed stub -- no UDR yet, see file header) ---

    server.add_route(
        "GET",
        std::string(kSdmApiRoot) + "/{supi}/am-data",
        [&verifier, &sdm_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sdm_get_counter->Add(1);
            sbi_gen::AccessAndMobilitySubscriptionData resp_data{};
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "GET",
        std::string(kSdmApiRoot) + "/{supi}/smf-select-data",
        [&verifier, &sdm_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sdm_get_counter->Add(1);
            sbi_gen::SmfSelectionSubscriptionData resp_data{};
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "GET",
        std::string(kSdmApiRoot) + "/{supi}/sm-data",
        [&verifier, &sdm_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sdm_get_counter->Add(1);
            sbi_gen::SessionManagementSubscriptionData resp_data{};
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    // --- Nudm_SDM: notification subscriptions ---

    server.add_route(
        "POST",
        std::string(kSdmApiRoot) + "/{ueId}/sdm-subscriptions",
        [&verifier, &sdm_subscriptions, &sdm_subscribe_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SdmSubscription>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto id =
                sdm_subscriptions.create(udm::SdmSubscriptionEntry{.ue_id = ue_id, .data = *body});
            sdm_subscribe_counter->Add(1);

            body->subscriptionId = id;
            json j = *body;
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace(
                "location", std::string(kSdmApiRoot) + "/" + ue_id + "/sdm-subscriptions/" + id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "DELETE",
        std::string(kSdmApiRoot) + "/{ueId}/sdm-subscriptions/{subscriptionId}",
        [&verifier, &sdm_subscriptions](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto subscription_id = req.path_params.at("subscriptionId");
            auto existing = sdm_subscriptions.get(subscription_id);
            if (!existing.has_value() || existing->ue_id != ue_id) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No such SDM subscription");
            }
            sdm_subscriptions.remove(subscription_id);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, udm_instance_id).detach();

    server.start();
    spdlog::info("udm: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("udm: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
