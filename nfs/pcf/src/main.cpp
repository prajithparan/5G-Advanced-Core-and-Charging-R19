// nfs/pcf: PCF (Policy Control Function), Npcf_AMPolicyControl + Npcf_SMPolicyControl services.
// Source: specs/5G_APIs-REL-19/TS29507_Npcf_AMPolicyControl.yaml,
// TS29512_Npcf_SMPolicyControl.yaml (commit bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 2's
// seventh and final originally-scoped NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope, agreed with the user before implementation -- the two services CLAUDE.md's Phase 2
// end goal actually needs (UE registration -> AMF gets AM policy from PCF; PDU session
// establishment -> SMF gets SM policy from PCF):
// Npcf_AMPolicyControl -- CreateIndividualAMPolicyAssociation, ReadIndividualAMPolicyAssociation,
// DeleteIndividualAMPolicyAssociation, ReportObservedEventTriggersForIndividualAMPolicyAssociation.
// Npcf_SMPolicyControl -- CreateSMPolicy, GetSMPolicy, UpdateSMPolicy, DeleteSMPolicy.
//
// Deliberately deferred, not dropped: both services' callback notifications (PolicyUpdate/
// TerminationNotification pushed BY PCF TO the notificationUri AMF/SMF supplied) -- neither AMF
// nor SMF has a receiver for these yet, same shape as every other proactive/callback flow this
// build has deferred so far. Npcf_PolicyAuthorization (AF/Rx-style), Npcf_UEPolicyControl (URSP),
// Npcf_EventExposure, Npcf_BDTPolicyControl, Npcf_PDTQPolicyControl, Npcf_AMPolicyAuthorization,
// Npcf_MBSPolicyControl/Authorization -- separate PCF sub-services, not needed for the core
// registration/PDU-session flows. Also deferred: actually wiring AMF/SMF to call this PCF -- this
// turn stands up PCF's own API surface + tests standalone, same precedent as UDR's turn
// (ADR-0025) and UDM's Nudm_UEAU turn (ADR-0026) before AUSF called it -- a deliberate future turn
// touching already-committed AMF/SMF code, reviewable on its own. See ADR-0028.
//
// Disclosed simplification, stated up front: real PCF policy decisions are computed from
// subscriber data UDR would hold (Npcf's own UDR client for the policy-data group), which UDR's
// turn (ADR-0025) never implemented (UDR's provisioned-data group is GET-only with nothing to
// provision anyway). So PCF's policy responses here are schema-valid, real objects built from the
// request plus a small fixed default policy (5QI 9 non-GBR, a placeholder ARP priority level not
// sourced from any TS 23.501 table, a fixed 1 Gbps/1 Gbps session AMBR when the request doesn't
// supply one) -- not real subscriber-specific decisioning. Same category of gap as UDM's SDM stub.

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
#error "CERTS_DIR must be defined by CMake (see nfs/pcf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7783;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9470";
constexpr const char* kNfType = "PCF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kAmApiRoot = "/npcf-am-policy-control/v1";
constexpr const char* kSmApiRoot = "/npcf-smpolicycontrol/v1";

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

// Builds a default AuthorizedDefaultQos: 5QI 9 (non-GBR, per TS 23.501's 5QI table -- the one
// piece of this sourced from a real spec table) with a fixed ARP. The ARP priorityLevel (8) is an
// arbitrary placeholder, NOT sourced from any TS 23.501 table -- disclosed here, not just in the
// file header, since it's the one field in this default with no spec backing at all.
sbi_gen::AuthorizedDefaultQos default_authorized_qos() {
    sbi_gen::AuthorizedDefaultQos qos{};
    qos.n5qi = 9;
    sbi_gen::Arp arp{};
    arp.priorityLevel = 8;
    arp.preemptCap.value = sbi_gen::PreemptionCapability::NOT_PREEMPT;
    arp.preemptVuln.value = sbi_gen::PreemptionVulnerability::NOT_PREEMPTABLE;
    qos.arp = arp;
    return qos;
}

sbi_gen::Ambr default_session_ambr() {
    sbi_gen::Ambr ambr{};
    ambr.uplink = "1 Gbps";
    ambr.downlink = "1 Gbps";
    return ambr;
}

// Builds the SmPolicyDecision this PCF returns for both CreateSMPolicy and UpdateSMPolicy: one
// default SessionRule, using the request's own subsSessAmbr/subsDefQos when supplied so the
// decision at least reflects what the request actually said, falling back to the fixed defaults
// above otherwise. See file header for the disclosed simplification this represents.
sbi_gen::SmPolicyDecision build_default_decision(const sbi_gen::SmPolicyContextData& context) {
    sbi_gen::SessionRule rule{};
    rule.sessRuleId = "default";
    rule.authSessAmbr = context.subsSessAmbr.value_or(default_session_ambr());
    if (context.subsDefQos.has_value()) {
        sbi_gen::AuthorizedDefaultQos qos{};
        qos.n5qi = context.subsDefQos->n5qi;
        qos.arp = context.subsDefQos->arp;
        rule.authDefQos = qos;
    } else {
        rule.authDefQos = default_authorized_qos();
    }

    sbi_gen::SmPolicyDecision decision{};
    // sessRules is an opaque map (TS29512's additionalProperties-keyed-by-sessRuleId shape isn't
    // representable as a typed map by tools/sbi-codegen -- see nfs/pcf/src/stores.hpp), built by
    // hand as {sessRuleId: SessionRule}.
    decision.sessRules = json{{rule.sessRuleId, json(rule)}};
    decision.online = context.online;
    decision.offline = context.offline;
    decision.suppFeat = context.suppFeat.value_or("");
    return decision;
}

// Runs on a dedicated thread, never on the server's io_context -- same reasoning as
// nfs/udr/src/main.cpp's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& pcf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/pcf/cert.pem",
        .key_path = CERTS_DIR "/pcf/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", pcf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", pcf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("pcf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + pcf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("pcf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("pcf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("pcf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + pcf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("pcf: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("pcf");
    sbi_core::init_tracing("pcf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string pcf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("pcf: starting, nfInstanceId={}", pcf_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/pcf/cert.pem",
        .key_path = CERTS_DIR "/pcf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    pcf::AmPolicyStore am_policies;
    pcf::SmPolicyStore sm_policies;

    auto meter = sbi_core::get_meter("pcf");
    auto am_create_counter = meter->CreateUInt64Counter(
        "pcf_am_policy_create_total", "Total CreateIndividualAMPolicyAssociation calls");
    auto am_update_counter = meter->CreateUInt64Counter(
        "pcf_am_policy_update_total",
        "Total ReportObservedEventTriggersForIndividualAMPolicyAssociation calls");
    auto am_delete_counter = meter->CreateUInt64Counter(
        "pcf_am_policy_delete_total", "Total DeleteIndividualAMPolicyAssociation calls");
    auto sm_create_counter =
        meter->CreateUInt64Counter("pcf_sm_policy_create_total", "Total CreateSMPolicy calls");
    auto sm_update_counter =
        meter->CreateUInt64Counter("pcf_sm_policy_update_total", "Total UpdateSMPolicy calls");
    auto sm_delete_counter =
        meter->CreateUInt64Counter("pcf_sm_policy_delete_total", "Total DeleteSMPolicy calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    // --- Npcf_AMPolicyControl ---

    server.add_route(
        "POST",
        std::string(kAmApiRoot) + "/policies",
        [&verifier, &am_policies, &am_create_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<sbi_gen::PolicyAssociationRequest>(req, err);
            if (!body.has_value()) {
                return err;
            }

            sbi_gen::PolicyAssociation association{};
            association.request = *body;
            association.ueAmbr = body->ueAmbr.value_or(default_session_ambr());
            association.suppFeat = body->suppFeat;
            json j = association;
            const auto id = am_policies.create(j);
            am_create_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kAmApiRoot) + "/policies/" + id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        std::string(kAmApiRoot) + "/policies/{polAssoId}",
        [&verifier, &am_policies](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto pol_asso_id = req.path_params.at("polAssoId");
            auto association = am_policies.get(pol_asso_id);
            if (!association.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AM policy association " + pol_asso_id);
            }
            return sbi_core::http2::Response::json(200, association->dump());
        });

    server.add_route(
        "DELETE",
        std::string(kAmApiRoot) + "/policies/{polAssoId}",
        [&verifier, &am_policies, &am_delete_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto pol_asso_id = req.path_params.at("polAssoId");
            if (!am_policies.remove(pol_asso_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AM policy association " + pol_asso_id);
            }
            am_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kAmApiRoot) + "/policies/{polAssoId}/update",
        [&verifier, &am_policies, &am_update_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<sbi_gen::PolicyAssociationUpdateRequest>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto pol_asso_id = req.path_params.at("polAssoId");
            auto stored = am_policies.get(pol_asso_id);
            if (!stored.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AM policy association " + pol_asso_id);
            }
            auto association = stored->get<sbi_gen::PolicyAssociation>();
            if (body->ueAmbr.has_value()) {
                association.ueAmbr = body->ueAmbr;
            }
            if (body->servAreaRes.has_value()) {
                association.servAreaRes = body->servAreaRes;
            }
            if (body->triggers.has_value()) {
                association.triggers = body->triggers;
            }
            am_policies.put(pol_asso_id, json(association));

            sbi_gen::PolicyUpdate update{};
            update.resourceUri = std::string(kAmApiRoot) + "/policies/" + pol_asso_id;
            update.triggers = body->triggers;
            update.servAreaRes = association.servAreaRes;
            update.ueAmbr = association.ueAmbr;
            am_update_counter->Add(1);
            json j = update;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    // --- Npcf_SMPolicyControl ---

    server.add_route(
        "POST",
        std::string(kSmApiRoot) + "/sm-policies",
        [&verifier, &sm_policies, &sm_create_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmPolicyContextData>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto decision = build_default_decision(*body);
            sbi_gen::SmPolicyControl control{};
            control.context = *body;
            control.policy = decision;
            const auto id = sm_policies.create(json(control));
            sm_create_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kSmApiRoot) + "/sm-policies/" + id);
            json j = decision;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        std::string(kSmApiRoot) + "/sm-policies/{smPolicyId}",
        [&verifier, &sm_policies](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto sm_policy_id = req.path_params.at("smPolicyId");
            auto control = sm_policies.get(sm_policy_id);
            if (!control.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM policy " + sm_policy_id);
            }
            return sbi_core::http2::Response::json(200, control->dump());
        });

    server.add_route(
        "POST",
        std::string(kSmApiRoot) + "/sm-policies/{smPolicyId}/update",
        [&verifier, &sm_policies, &sm_update_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            // SmPolicyUpdateContextData is an opaque fallback in the generated DTOs (an `allOf`
            // shape tools/sbi-codegen doesn't model -- see nfs/pcf/src/stores.hpp's header),
            // parsed here as raw JSON rather than a typed struct.
            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto sm_policy_id = req.path_params.at("smPolicyId");
            auto stored = sm_policies.get(sm_policy_id);
            if (!stored.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM policy " + sm_policy_id);
            }
            auto control = stored->get<sbi_gen::SmPolicyControl>();
            // This build's decisioning is entirely a function of the ORIGINAL context (see
            // build_default_decision) -- there is no real trigger-driven re-evaluation, so an
            // UpdateSMPolicy call re-derives the same decision from the stored context rather than
            // reading the report itself. Disclosed, not silently assumed: matches the file
            // header's stated simplification.
            control.policy = build_default_decision(control.context);
            sm_policies.put(sm_policy_id, json(control));
            sm_update_counter->Add(1);
            json j = control.policy;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "POST",
        std::string(kSmApiRoot) + "/sm-policies/{smPolicyId}/delete",
        [&verifier, &sm_policies, &sm_delete_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            // SmPolicyDeleteData has no mandatory fields (TS29512) -- accept whatever body is
            // sent without requiring it to parse as a specific typed DTO.
            const auto sm_policy_id = req.path_params.at("smPolicyId");
            if (!sm_policies.remove(sm_policy_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM policy " + sm_policy_id);
            }
            sm_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, pcf_instance_id).detach();

    server.start();
    spdlog::info("pcf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("pcf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
