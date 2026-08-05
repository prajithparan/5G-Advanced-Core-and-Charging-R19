// nfs/amf: AMF (Access and Mobility Management Function), Namf_Communication surface.
// Source: specs/5G_APIs-REL-19/TS29518_Namf_Communication.yaml (commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 2's second NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope (see the procedure list agreed before this NF was built): ReleaseUEContext,
// EBIAssignment, UEContextTransfer, RegistrationStatusUpdate, N1N2MessageTransfer,
// N1N2MessageSubscribe, N1N2MessageUnSubscribe, NonUeN2MessageTransfer, NonUeN2InfoSubscribe,
// NonUeN2InfoUnSubscribe, AMFStatusChangeSubscribe, AMFStatusChangeUnSubscribe,
// AMFStatusChangeSubscribeModfy -- every Namf_Communication operationId that has a real
// application/json request-body alternative in the YAML.
//
// Deliberately deferred, not dropped: CreateUEContext, RelocateUEContext, CancelRelocateUEContext
// are multipart/related-ONLY per spec (no application/json alternative exists) and
// libs/sbi-core has no multipart/related support yet -- see docs/DECISIONS.md ADR-0016's
// discussion. Consequence: nothing in this process ever populates UeContextStore, so
// ReleaseUEContext/EBIAssignment/UEContextTransfer/RegistrationStatusUpdate can only be exercised
// on their "no such UE context" (404) branch until CreateUEContext lands in a later turn -- their
// "found" branches are still implemented for real, just unreachable/unverified for now (disclosed,
// not hidden).
//
// Also disclosed: subscriptions (N1N2Message*, NonUeN2Info*, AMFStatusChange*) are created/removed
// for real, but notification DELIVERY is not implemented -- there is no trigger path yet (no
// NGAP/N2, no real UE, no multi-AMF deployment) that would ever fire one. Error responses use the
// generic ProblemDetails shape (sbi_core::ProblemDetails, application/problem+json) rather than
// each operation's bespoke *Error schema (AssignEbiError, N1N2MessageTransferError,
// N2InformationTransferError) -- same simplification NRF already uses for its own error paths.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/jwt.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/problem_details.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <thread>

#include "TS29122_CommonData_grp.hpp"
#include "subscriptions.hpp"
#include "ue_context_store.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/amf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7778;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9465";
constexpr const char* kNfType = "AMF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kApiRoot = "/namf-comm/v1";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018 for
// why NRF's identity is a fixed constant rather than randomly generated per run (any NF other than
// NRF itself needs to know NRF's nfInstanceId in advance to construct a working
// sbi_core::jwt::Verifier, since the `iss` claim on every token NRF issues is NRF's own
// nfInstanceId).
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

sbi_core::http2::Response
problem_response(int status, const std::string& title, const std::string& detail) {
    auto pd = sbi_core::make_problem_details(status, title, detail);
    json j = pd;
    sbi_core::http2::Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/problem+json");
    r.body = j.dump();
    return r;
}

// Same pattern as nfs/nrf/src/main.cpp's check_bearer -- see that file's comment for why a missing
// Authorization header is not itself a 401 (RegisterNFInstance-equivalent bootstrap security
// alternative: `security: [{}, oAuth2ClientCredentials:[...]]` in the YAML).
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

// Parses req.body as JSON into T (a generated sbi_gen DTO). On success returns the value; on
// failure writes a 400 ProblemDetails into err_out and returns nullopt. Shared across all 13
// routes below rather than repeating the same try/catch 13 times.
template <typename T>
std::optional<T> parse_json_body(const sbi_core::http2::Request& req,
                                 sbi_core::http2::Response& err_out) {
    try {
        return json::parse(req.body).get<T>();
    } catch (const json::parse_error& e) {
        err_out = problem_response(400, "Malformed JSON", e.what());
    } catch (const json::exception& e) {
        err_out = problem_response(400, "Missing or invalid mandatory IE", e.what());
    }
    return std::nullopt;
}

// Runs on a dedicated thread, never on the server's io_context -- libs/sbi-core's http2::Client is
// synchronous (docs/DECISIONS.md ADR-0006, disclosed debt), so driving NRF registration/heartbeat
// from the same io_context that serves inbound Namf_Communication requests would stall the server
// while a heartbeat call is in flight. A dedicated thread with its own Client instance is a
// minimal, disclosed resolution -- not the full curl_multi/Asio integration ADR-0006 names as the
// eventual real fix, which remains future work.
void run_nrf_lifecycle(const std::string& amf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/amf/cert.pem",
        .key_path = CERTS_DIR "/amf/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", amf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", amf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("amf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + amf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("amf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("amf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("amf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + amf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("amf: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("amf");
    sbi_core::init_tracing("amf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string amf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("amf: starting, nfInstanceId={}", amf_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/amf/cert.pem",
        .key_path = CERTS_DIR "/amf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    amf::UeContextStore ue_contexts;
    amf::UeN1N2SubscriptionStore ue_n1n2_subs("n1n2sub-");
    amf::NonUeN2SubscriptionStore non_ue_n2_subs("nonuen2sub-");
    amf::AmfStatusSubscriptionStore amf_status_subs("amfstatussub-");

    auto meter = sbi_core::get_meter("amf");
    auto release_counter =
        meter->CreateUInt64Counter("amf_release_ue_context_total", "Total ReleaseUEContext calls");
    auto ebi_counter =
        meter->CreateUInt64Counter("amf_ebi_assignment_total", "Total EBIAssignment calls");
    auto transfer_counter = meter->CreateUInt64Counter("amf_ue_context_transfer_total",
                                                       "Total UEContextTransfer calls");
    auto reg_status_counter = meter->CreateUInt64Counter("amf_registration_status_update_total",
                                                         "Total RegistrationStatusUpdate calls");
    auto n1n2_counter = meter->CreateUInt64Counter("amf_n1n2_message_transfer_total",
                                                   "Total N1N2MessageTransfer calls");
    auto non_ue_n2_counter = meter->CreateUInt64Counter("amf_non_ue_n2_message_transfer_total",
                                                        "Total NonUeN2MessageTransfer calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-contexts/{ueContextId}/release",
        [&verifier, &ue_contexts, &release_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::UEContextRelease>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_context_id = req.path_params.at("ueContextId");
            if (!ue_contexts.get(ue_context_id).has_value()) {
                return problem_response(404, "Not Found", "No UE context with id " + ue_context_id);
            }
            ue_contexts.remove(ue_context_id);
            release_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-contexts/{ueContextId}/assign-ebi",
        [&verifier, &ue_contexts, &ebi_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::AssignEbiData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_context_id = req.path_params.at("ueContextId");
            if (!ue_contexts.get(ue_context_id).has_value()) {
                return problem_response(404, "Not Found", "No UE context with id " + ue_context_id);
            }
            ebi_counter->Add(1);
            // Disclosed simplification: no real EPS Bearer Id pool -- always reports zero newly
            // assigned bearers rather than fabricating EBI values with no real bearer behind them.
            sbi_gen::AssignedEbiData resp_data;
            resp_data.pduSessionId = body->pduSessionId;
            resp_data.assignedEbiList = {};
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-contexts/{ueContextId}/transfer",
        [&verifier, &ue_contexts, &transfer_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::UeContextTransferReqData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_context_id = req.path_params.at("ueContextId");
            if (!ue_contexts.get(ue_context_id).has_value()) {
                return problem_response(404, "Not Found", "No UE context with id " + ue_context_id);
            }
            transfer_counter->Add(1);
            // Unreachable today (see file header): if it ever runs, only the mandatory
            // ueContext field is populated (all-optional-empty is schema-valid) -- there is no
            // real stored UE context content yet to transfer.
            sbi_gen::UeContextTransferRspData resp_data;
            resp_data.ueContext = sbi_gen::UeContext{};
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-contexts/{ueContextId}/transfer-update",
        [&verifier, &ue_contexts, &reg_status_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::UeRegStatusUpdateReqData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_context_id = req.path_params.at("ueContextId");
            if (!ue_contexts.get(ue_context_id).has_value()) {
                return problem_response(404, "Not Found", "No UE context with id " + ue_context_id);
            }
            reg_status_counter->Add(1);
            sbi_gen::UeRegStatusUpdateRspData resp_data;
            resp_data.regStatusTransferComplete = true;
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-contexts/{ueContextId}/n1-n2-messages",
        [&verifier, &ue_contexts, &n1n2_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::N1N2MessageTransferReqData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_context_id = req.path_params.at("ueContextId");
            if (!ue_contexts.get(ue_context_id).has_value()) {
                return problem_response(404, "Not Found", "No UE context with id " + ue_context_id);
            }
            n1n2_counter->Add(1);
            // No real N1/N2 delivery pipeline exists yet (no NGAP/N2, no NAS) -- this is a
            // bookkeeping acknowledgment only, always reporting immediate synchronous "initiated"
            // rather than the async 202+callback flow, since there is nothing downstream to
            // actually deliver a NAS/N2 payload through yet.
            sbi_gen::N1N2MessageTransferRspData resp_data;
            resp_data.cause.value = sbi_gen::N1N2MessageTransferCause::N1_N2_TRANSFER_INITIATED;
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-contexts/{ueContextId}/n1-n2-messages/subscriptions",
        [&verifier, &ue_n1n2_subs](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::UeN1N2InfoSubscriptionCreateData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_context_id = req.path_params.at("ueContextId");
            // Implementation choice, disclosed: NOT gated on the UE context already existing --
            // the schema doesn't mandate that precondition, and gating on it would make this
            // operation permanently untestable until CreateUEContext lands (see file header).
            const auto id = ue_n1n2_subs.create(
                amf::UeN1N2Subscription{.ue_context_id = ue_context_id, .data = *body});
            sbi_gen::UeN1N2InfoSubscriptionCreatedData resp_data;
            resp_data.n1n2NotifySubscriptionId = id;
            json j = resp_data;
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kApiRoot) + "/ue-contexts/" + ue_context_id +
                                     "/n1-n2-messages/subscriptions/" + id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route("DELETE",
                     std::string(kApiRoot) +
                         "/ue-contexts/{ueContextId}/n1-n2-messages/subscriptions/{subscriptionId}",
                     [&verifier, &ue_n1n2_subs](const sbi_core::http2::Request& req) {
                         if (auto auth = check_bearer(req, verifier);
                             auth.has_value() && !auth->valid) {
                             return problem_response(401, "Unauthorized", auth->error);
                         }
                         const auto ue_context_id = req.path_params.at("ueContextId");
                         const auto sub_id = req.path_params.at("subscriptionId");
                         auto existing = ue_n1n2_subs.get(sub_id);
                         if (!existing.has_value() || existing->ue_context_id != ue_context_id) {
                             return problem_response(404, "Not Found", "No such N1N2 subscription");
                         }
                         ue_n1n2_subs.remove(sub_id);
                         sbi_core::http2::Response resp;
                         resp.status = 204;
                         return resp;
                     });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/non-ue-n2-messages/transfer",
        [&verifier, &non_ue_n2_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::N2InformationTransferReqData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            non_ue_n2_counter->Add(1);
            // Same "no real N2 delivery pipeline yet" disclosed simplification as
            // N1N2MessageTransfer above.
            sbi_gen::N2InformationTransferRspData resp_data;
            resp_data.result.value =
                sbi_gen::N2InformationTransferResult::N2_INFO_TRANSFER_INITIATED;
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/non-ue-n2-messages/subscriptions",
        [&verifier, &non_ue_n2_subs](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::NonUeN2InfoSubscriptionCreateData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto id = non_ue_n2_subs.create(*body);
            sbi_gen::NonUeN2InfoSubscriptionCreatedData resp_data;
            resp_data.n2NotifySubscriptionId = id;
            resp_data.n2InformationClass = body->n2InformationClass;
            json j = resp_data;
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kApiRoot) + "/non-ue-n2-messages/subscriptions/" + id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "DELETE",
        std::string(kApiRoot) + "/non-ue-n2-messages/subscriptions/{n2NotifySubscriptionId}",
        [&verifier, &non_ue_n2_subs](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto sub_id = req.path_params.at("n2NotifySubscriptionId");
            if (!non_ue_n2_subs.get(sub_id).has_value()) {
                return problem_response(404, "Not Found", "No such non-UE N2 subscription");
            }
            non_ue_n2_subs.remove(sub_id);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/subscriptions",
        [&verifier, &amf_status_subs](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::SubscriptionData_Namf_Communication>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto id = amf_status_subs.create(*body);
            json j = *body; // Response body schema is SubscriptionData itself -- no id field in
                            // it (unlike NRF's SubscriptionData); id travels only via Location.
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/subscriptions/" + id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "DELETE",
        std::string(kApiRoot) + "/subscriptions/{subscriptionId}",
        [&verifier, &amf_status_subs](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto sub_id = req.path_params.at("subscriptionId");
            if (!amf_status_subs.get(sub_id).has_value()) {
                return problem_response(404, "Not Found", "No such AMF status subscription");
            }
            amf_status_subs.remove(sub_id);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "PUT",
        std::string(kApiRoot) + "/subscriptions/{subscriptionId}",
        [&verifier, &amf_status_subs](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = parse_json_body<sbi_gen::SubscriptionData_Namf_Communication>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto sub_id = req.path_params.at("subscriptionId");
            if (!amf_status_subs.update(sub_id, *body)) {
                return problem_response(404, "Not Found", "No such AMF status subscription");
            }
            json j = *body;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    std::thread(run_nrf_lifecycle, amf_instance_id).detach();

    server.start();
    spdlog::info("amf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("amf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
