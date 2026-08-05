// nfs/smf: SMF (Session Management Function), Nsmf_PDUSession /sm-contexts surface.
// Source: specs/5G_APIs-REL-19/TS29502_Nsmf_PDUSession.yaml (commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 2's third NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope: the /sm-contexts collection -- CreateSMContext (PostSmContexts),
// RetrieveSMContext (RetrieveSmContext), UpdateSMContext (UpdateSmContext),
// ReleaseSMContext (ReleaseSmContext) -- the actual AMF-triggered PDU Session Establishment flow
// (TS 23.502 clause 4.3.2.2.1), agreed with the user as this turn's scope after
// docs/DECISIONS.md ADR-0020 (multipart/related codec) unblocked CreateSMContext, which is
// multipart/related-ONLY per spec (no application/json alternative exists for its request body).
//
// Deliberately deferred, not dropped:
// - The /pdu-sessions collection (PostPduSessions/UpdatePduSession/ReleasePduSession/
//   RetrievePduSession) -- the I-SMF/inter-SMF roaming scenario, not the standard AMF-triggered
//   flow this turn targets.
// - SendMoData/TransferMoData -- small-data-over-NAS operations, multipart-only, peripheral to
//   the core session lifecycle.
// - Nsmf_EventExposure.yaml and Nsmf_NIDD.yaml -- separate SMF services, out of scope for this
//   turn's procedure list.
//
// Disclosed simplifications, real and not hidden:
// - SMF has no real UPF (N4/PFCP is Phase 3), no real PCF (SM Policy Association -- PCF doesn't
//   exist yet in this build order), and no real UDM (subscription data retrieval). CreateSMContext
//   therefore does real request validation and real store bookkeeping but cannot perform the real
//   procedure's PCF/UDM/UPF interactions -- it always succeeds (201) rather than modeling failure
//   modes that would come from those absent dependencies.
// - UpdateSMContext acknowledges (204) without fabricating SmContextUpdatedData content (EBI
//   allocation, N1/N2 info, ...) -- there is nothing real behind those fields yet.
// - Error responses use the generic ProblemDetails shape (sbi_core::http2::problem_response,
//   application/problem+json) rather than each operation's bespoke *Error schema
//   (SmContextCreateError, SmContextUpdateError) -- same simplification NRF/AMF already use.

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
#include "sm_context_store.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/smf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7779;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9466";
constexpr const char* kNfType = "SMF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kApiRoot = "/nsmf-pdusession/v1";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// Same pattern as nfs/nrf and nfs/amf's check_bearer -- see those files' comments for why a
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
void run_nrf_lifecycle(const std::string& smf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", smf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", smf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("smf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + smf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("smf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("smf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("smf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + smf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("smf: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("smf");
    sbi_core::init_tracing("smf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string smf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("smf: starting, nfInstanceId={}", smf_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/smf/cert.pem",
        .key_path = CERTS_DIR "/smf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    smf::SmContextStore sm_contexts;

    auto meter = sbi_core::get_meter("smf");
    auto create_counter =
        meter->CreateUInt64Counter("smf_create_sm_context_total", "Total CreateSMContext calls");
    auto retrieve_counter = meter->CreateUInt64Counter("smf_retrieve_sm_context_total",
                                                       "Total RetrieveSMContext calls");
    auto update_counter =
        meter->CreateUInt64Counter("smf_update_sm_context_total", "Total UpdateSMContext calls");
    auto release_counter =
        meter->CreateUInt64Counter("smf_release_sm_context_total", "Total ReleaseSMContext calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts",
        [&verifier, &sm_contexts, &create_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_multipart_json_body<sbi_gen::SmContextCreateData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            // Disclosed simplification: stores only that a context now exists, not the full
            // SmContextCreateData -- there is no real PCF/UDM/UPF in this build for the stored
            // detail to feed into yet (see file header).
            const auto sm_context_ref = sm_contexts.create(json::object());
            create_counter->Add(1);

            sbi_gen::SmContextCreatedData resp_data;
            resp_data.pduSessionId = body->pduSessionId;
            resp_data.sNssai = body->sNssai;
            json j = resp_data;
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kApiRoot) + "/sm-contexts/" + sm_context_ref);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts/{smContextRef}/retrieve",
        [&verifier, &sm_contexts, &retrieve_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto sm_context_ref = req.path_params.at("smContextRef");
            if (!sm_contexts.get(sm_context_ref).has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM context with ref " + sm_context_ref);
            }
            // SmContextRetrieveData is optional per spec (required: false) -- an empty body is
            // valid, not a parse error.
            if (!req.body.empty()) {
                sbi_core::http2::Response err;
                auto body =
                    sbi_core::http2::parse_json_body<sbi_gen::SmContextRetrieveData>(req, err);
                if (!body.has_value()) {
                    return err;
                }
            }
            retrieve_counter->Add(1);
            // Disclosed simplification: ueEpsPdnConnection (mandatory per spec) is an opaque
            // base64 container with nothing real behind it in this build (no EPS interworking
            // state exists) -- emitted as an empty string, a schema-valid but empty value.
            sbi_gen::SmContextRetrievedData resp_data;
            resp_data.ueEpsPdnConnection = "";
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts/{smContextRef}/modify",
        [&verifier, &sm_contexts, &update_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmContextUpdateData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto sm_context_ref = req.path_params.at("smContextRef");
            if (!sm_contexts.get(sm_context_ref).has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM context with ref " + sm_context_ref);
            }
            update_counter->Add(1);
            // Disclosed simplification: acknowledges the update (204) rather than fabricating
            // SmContextUpdatedData content (EBI allocation, N1/N2 info, ...) with no real PCF/
            // UPF backing it yet (see file header).
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/sm-contexts/{smContextRef}/release",
        [&verifier, &sm_contexts, &release_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto sm_context_ref = req.path_params.at("smContextRef");
            if (!sm_contexts.get(sm_context_ref).has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM context with ref " + sm_context_ref);
            }
            // SmContextReleaseData is optional per spec (required: false).
            if (!req.body.empty()) {
                sbi_core::http2::Response err;
                auto body =
                    sbi_core::http2::parse_json_body<sbi_gen::SmContextReleaseData>(req, err);
                if (!body.has_value()) {
                    return err;
                }
            }
            sm_contexts.remove(sm_context_ref);
            release_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, smf_instance_id).detach();

    server.start();
    spdlog::info("smf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("smf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
