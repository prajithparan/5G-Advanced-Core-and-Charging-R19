// nfs/ausf: AUSF (Authentication Server Function), Nausf_UEAuthentication service.
// Source: specs/5G_APIs-REL-19/TS29509_Nausf_UEAuthentication.yaml (commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 2's sixth NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF). Real MILENAGE/TS 33.501/EAP-AKA' crypto via
// libs/aka-crypto (built last turn, see docs/DECISIONS.md ADR-0026); this is the first NF whose
// own request handlers make a real synchronous SBI client call to another NF (UDM's
// Nudm_UEAU_GenerateAuthData) rather than only to NRF -- see ADR-0027.
//
// In scope, agreed with the user before implementation -- the `ue-authentications` resource
// group only (5G-AKA and EAP-AKA' for a normal UE, TS 33.501 clause 6.1.3):
//   POST   /ue-authentications                                (initiate; calls UDM)
//   PUT    /ue-authentications/{authCtxId}/5g-aka-confirmation (Confirm5gAkaAuthentication)
//   DELETE /ue-authentications/{authCtxId}/5g-aka-confirmation (Delete5gAkaAuthenticationResult)
//   POST   /ue-authentications/{authCtxId}/eap-session          (EapAuthMethod)
//   DELETE /ue-authentications/{authCtxId}/eap-session          (DeleteEapAuthenticationResult)
//   POST   /ue-authentications/deregister                       (UEAuthenticationsDeregister)
//
// Deliberately deferred, not dropped: /rg-authentications (5G-RG) and /prose-authentications +
// /prose-authentications/{authCtxId}/prose-auth (ProSe) -- same Tier-1 5G-AKA-for-a-normal-UE
// boundary ADR-0026 already drew for UDM's Nudm_UEAU turn.
//
// Disclosed simplification, stated up front rather than discovered in review: AUSF does NOT call
// UDM's ConfirmAuth/DeleteAuth (Nudm_UEAuthentication_ResultConfirmation) after an authentication
// completes, even though UDM's ConfirmAuth/DeleteAuth were built in ADR-0026 anticipating exactly
// this caller. That wiring is a real design decision of its own (sync-in-the-response-path vs.
// fire-and-forget, what to do if UDM is unreachable at that point) that wasn't part of the scope
// agreed for this turn -- left for a dedicated future turn, not silently done or silently skipped.
// SUCI de-concealment is also NOT implemented, same disclosed gap as UDM's GenerateAuthData
// (ADR-0026): AuthenticationInfo.supiOrSuci is passed straight through to UDM, so a real
// SUCI-formatted id 404s exactly like it does calling UDM directly.

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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include "TS29509_Nausf_UEAuthentication.hpp"
#include "aka_crypto/eap_aka_prime.hpp"
#include "aka_crypto/hex.hpp"
#include "aka_crypto/kdf.hpp"
#include "aka_crypto/milenage.hpp"
#include "stores.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/ausf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7782;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9469";
constexpr const char* kNfType = "AUSF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kUdmBase = "https://127.0.0.1:7780";
constexpr const char* kApiRoot = "/nausf-auth/v1";

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
// nfs/udm/src/main.cpp's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& ausf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/ausf/cert.pem",
        .key_path = CERTS_DIR "/ausf/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", ausf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", ausf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("ausf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + ausf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("ausf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("ausf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("ausf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + ausf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("ausf: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("ausf");
    sbi_core::init_tracing("ausf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string ausf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("ausf: starting, nfInstanceId={}", ausf_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/ausf/cert.pem",
        .key_path = CERTS_DIR "/ausf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    // AUSF's own client identity + token source for calling UDM -- separate http2::Client/
    // OAuth2Client from run_nrf_lifecycle's (which runs on its own thread; these two are only ever
    // touched from route handlers, which all run on ioc's single thread -- see http2_server.hpp).
    sbi_core::http2::TlsConfig udm_client_tls{
        .cert_path = CERTS_DIR "/ausf/cert.pem",
        .key_path = CERTS_DIR "/ausf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client udm_client(std::move(udm_client_tls));
    sbi_core::OAuth2Client udm_oauth(
        udm_client, std::string(kNrfBase) + "/oauth2/token", ausf_instance_id, "nudm-ueau", "UDM");

    ausf::AuthContextStore auth_contexts;

    auto meter = sbi_core::get_meter("ausf");
    auto ue_auth_counter = meter->CreateUInt64Counter("ausf_ue_authentications_total",
                                                      "Total POST /ue-authentications calls");
    auto confirm_5g_aka_counter = meter->CreateUInt64Counter(
        "ausf_5g_aka_confirmation_total", "Total Confirm5gAkaAuthentication calls");
    auto eap_session_counter =
        meter->CreateUInt64Counter("ausf_eap_session_total", "Total EapAuthMethod calls");
    auto deregister_counter = meter->CreateUInt64Counter("ausf_deregister_total",
                                                         "Total UEAuthenticationsDeregister calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    // --- Nausf_UEAuthentication: ue-authentications ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-authentications",
        [&verifier, &udm_client, &udm_oauth, &auth_contexts, &ausf_instance_id, &ue_auth_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::AuthenticationInfo>(req, err);
            if (!body.has_value()) {
                return err;
            }

            auto token = udm_oauth.get_bearer_token();
            if (!token.has_value()) {
                return sbi_core::http2::problem_response(500,
                                                         "Internal Server Error",
                                                         "AUSF could not obtain a token for UDM: " +
                                                             token.error());
            }

            sbi_gen::AuthenticationInfoRequest udm_req{};
            udm_req.servingNetworkName = body->servingNetworkName;
            udm_req.ausfInstanceId = ausf_instance_id;
            // SQN resynchronisation (TS 33.102 §6.3.3, ADR-0037): both AuthenticationInfo (this
            // handler's own request) and AuthenticationInfoRequest (UDM's) declare the identical
            // ResynchronizationInfo_Nudm_UEAU{rand,auts} type for this field -- AUSF is a pure
            // passthrough here, UDM is the one that actually verifies AUTS and resets SQN.
            udm_req.resynchronizationInfo = body->resynchronizationInfo;

            sbi_core::http2::ClientRequest udm_http_req;
            udm_http_req.method = "POST";
            udm_http_req.url = std::string(kUdmBase) + "/nudm-ueau/v1/" + body->supiOrSuci +
                               "/security-information/generate-auth-data";
            udm_http_req.headers.emplace("content-type", "application/json");
            udm_http_req.headers.emplace("authorization", "Bearer " + *token);
            udm_http_req.body = json(udm_req).dump();

            auto udm_resp = udm_client.send(udm_http_req);
            if (!udm_resp.has_value()) {
                return sbi_core::http2::problem_response(
                    500, "Internal Server Error", "AUSF could not reach UDM: " + udm_resp.error());
            }
            if (udm_resp->status == 404) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "User does not exist in the HPLMN");
            }
            if (udm_resp->status != 200) {
                return sbi_core::http2::problem_response(
                    500,
                    "Internal Server Error",
                    "UDM GenerateAuthData returned unexpected status " +
                        std::to_string(udm_resp->status));
            }

            sbi_gen::AuthenticationInfoResult udm_result;
            try {
                udm_result = json::parse(udm_resp->body).get<sbi_gen::AuthenticationInfoResult>();
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(
                    500,
                    "Internal Server Error",
                    "UDM returned a malformed AuthenticationInfoResult: " + std::string(e.what()));
            }
            if (!udm_result.authenticationVector.has_value()) {
                return sbi_core::http2::problem_response(
                    500, "Internal Server Error", "UDM returned no authenticationVector");
            }
            const std::string supi = udm_result.supi.value_or(body->supiOrSuci);

            sbi_gen::UEAuthenticationCtx ctx{};
            std::string auth_ctx_id;

            if (udm_result.authType.value == sbi_gen::AuthType_Nudm_UEAU::EAP_AKA_PRIME) {
                sbi_gen::AvEapAkaPrime av;
                try {
                    av = udm_result.authenticationVector->get<sbi_gen::AvEapAkaPrime>();
                } catch (const json::exception& e) {
                    return sbi_core::http2::problem_response(
                        500, "Internal Server Error", "UDM returned a malformed AvEapAkaPrime");
                }
                const auto rand = aka_crypto::from_hex<16>(av.rand);
                const auto autn = aka_crypto::from_hex<16>(av.autn);
                const auto xres = aka_crypto::from_hex<8>(av.xres);
                const auto ck_prime = aka_crypto::from_hex<16>(av.ckPrime);
                const auto ik_prime = aka_crypto::from_hex<16>(av.ikPrime);
                if (!rand || !autn || !xres || !ck_prime || !ik_prime) {
                    return sbi_core::http2::problem_response(
                        500,
                        "Internal Server Error",
                        "UDM returned malformed hex in AvEapAkaPrime");
                }

                const auto keys = aka_crypto::eap::derive_keys(*ck_prime, *ik_prime, supi);
                // KAUSF for EAP-AKA': the same Annex A.2 KDF as 5G-AKA's (FC=0x6A,
                // aka_crypto::derive_kausf), but keyed on CK'/IK' instead of CK/IK -- NOT RFC
                // 5448's EMSK (dimensionally inconsistent with every 32-byte Kausf field in the
                // YAML; confirmed with the user rather than assumed, see docs/DECISIONS.md
                // ADR-0027). SQN xor AK is AUTN's own first 6 bytes -- AUSF never sees SQN
                // directly either, same as a real UE/USIM.
                aka_crypto::Ak48 sqn_xor_ak{};
                std::copy(autn->begin(), autn->begin() + 6, sqn_xor_ak.begin());
                const auto kausf_eap = aka_crypto::derive_kausf(
                    *ck_prime, *ik_prime, body->servingNetworkName, sqn_xor_ak);
                // Identifier: derived from the AV's own RAND rather than a separate counter/RNG --
                // deterministic given this context, and RFC 3748 only requires it be distinct
                // across concurrent exchanges, not globally unique.
                const uint8_t identifier = (*rand)[0];
                const auto packet = aka_crypto::eap::build_challenge_request(
                    identifier, *rand, *autn, body->servingNetworkName, keys.k_aut);
                const auto eap_payload_b64 = aka_crypto::eap::base64_encode(packet);

                ausf::AuthContext store_ctx{};
                store_ctx.supi = supi;
                store_ctx.serving_network_name = body->servingNetworkName;
                store_ctx.auth_type = "EAP_AKA_PRIME";
                store_ctx.k_aut = keys.k_aut;
                store_ctx.xres = *xres;
                store_ctx.kausf_eap = kausf_eap;
                store_ctx.msk = keys.msk;
                auth_ctx_id = auth_contexts.create(std::move(store_ctx));

                ctx.authType.value = sbi_gen::AuthType_Nausf_UEAuthentication::EAP_AKA_PRIME;
                ctx.n5gAuthData = json(eap_payload_b64);
                ctx._links = json{{"eap-session",
                                   json{{"href",
                                         std::string(kApiRoot) + "/ue-authentications/" +
                                             auth_ctx_id + "/eap-session"}}}};
            } else {
                sbi_gen::Av5GHeAka av;
                try {
                    av = udm_result.authenticationVector->get<sbi_gen::Av5GHeAka>();
                } catch (const json::exception& e) {
                    return sbi_core::http2::problem_response(
                        500, "Internal Server Error", "UDM returned a malformed Av5GHeAka");
                }
                const auto rand = aka_crypto::from_hex<16>(av.rand);
                const auto autn = aka_crypto::from_hex<16>(av.autn);
                const auto xres_star = aka_crypto::from_hex<16>(av.xresStar);
                const auto kausf = aka_crypto::from_hex<32>(av.kausf);
                if (!rand || !autn || !xres_star || !kausf) {
                    return sbi_core::http2::problem_response(
                        500, "Internal Server Error", "UDM returned malformed hex in Av5GHeAka");
                }
                const auto hxres_star = aka_crypto::derive_hxres_star(*rand, *xres_star);

                ausf::AuthContext store_ctx{};
                store_ctx.supi = supi;
                store_ctx.serving_network_name = body->servingNetworkName;
                store_ctx.auth_type = "5G_AKA";
                store_ctx.xres_star = *xres_star;
                store_ctx.kausf_5g_aka = *kausf;
                auth_ctx_id = auth_contexts.create(std::move(store_ctx));

                sbi_gen::Av5gAka ausf_av{};
                ausf_av.rand = av.rand;
                ausf_av.hxresStar = aka_crypto::to_hex(hxres_star);
                ausf_av.autn = av.autn;

                ctx.authType.value = sbi_gen::AuthType_Nausf_UEAuthentication::V5G_AKA;
                ctx.n5gAuthData = json(ausf_av);
                ctx._links = json{{"5g-aka",
                                   json{{"href",
                                         std::string(kApiRoot) + "/ue-authentications/" +
                                             auth_ctx_id + "/5g-aka-confirmation"}}}};
            }
            ctx.servingNetworkName = body->servingNetworkName;

            ue_auth_counter->Add(1);
            json j = ctx;
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kApiRoot) + "/ue-authentications/" + auth_ctx_id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PUT",
        std::string(kApiRoot) + "/ue-authentications/{authCtxId}/5g-aka-confirmation",
        [&verifier, &auth_contexts, &confirm_5g_aka_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::ConfirmationData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto auth_ctx_id = req.path_params.at("authCtxId");
            auto ctx = auth_contexts.get(auth_ctx_id);
            if (!ctx.has_value() || ctx->auth_type != "5G_AKA") {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No 5G-AKA authentication context " + auth_ctx_id);
            }
            const auto res_star = aka_crypto::from_hex<16>(body->resStar);
            if (!res_star) {
                return sbi_core::http2::problem_response(
                    400, "Bad Request", "resStar is not valid 32-hex-char data");
            }

            confirm_5g_aka_counter->Add(1);
            sbi_gen::ConfirmationDataResponse resp_data{};
            if (*res_star == ctx->xres_star) {
                resp_data.authResult.value = sbi_gen::AuthResult::AUTHENTICATION_SUCCESS;
                resp_data.supi = ctx->supi;
                resp_data.kseaf = aka_crypto::to_hex(
                    aka_crypto::derive_kseaf(ctx->kausf_5g_aka, ctx->serving_network_name));
            } else {
                resp_data.authResult.value = sbi_gen::AuthResult::AUTHENTICATION_FAILURE;
            }
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "DELETE",
        std::string(kApiRoot) + "/ue-authentications/{authCtxId}/5g-aka-confirmation",
        [&verifier, &auth_contexts](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto auth_ctx_id = req.path_params.at("authCtxId");
            auto ctx = auth_contexts.get(auth_ctx_id);
            if (!ctx.has_value() || ctx->auth_type != "5G_AKA" ||
                !auth_contexts.remove(auth_ctx_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No 5G-AKA authentication context " + auth_ctx_id);
            }
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-authentications/{authCtxId}/eap-session",
        [&verifier, &auth_contexts, &eap_session_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::EapSession>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto auth_ctx_id = req.path_params.at("authCtxId");
            auto ctx = auth_contexts.get(auth_ctx_id);
            if (!ctx.has_value() || ctx->auth_type != "EAP_AKA_PRIME") {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No EAP-AKA' authentication context " + auth_ctx_id);
            }
            auto packet = aka_crypto::eap::base64_decode(body->eapPayload);
            if (!packet.has_value() || packet->size() < 2) {
                return sbi_core::http2::problem_response(
                    400, "Bad Request", "eapPayload is not valid base64 EAP data");
            }
            const uint8_t identifier = (*packet)[1];

            eap_session_counter->Add(1);
            sbi_gen::EapSession resp_data{};

            const bool mac_ok = aka_crypto::eap::verify_mac(*packet, ctx->k_aut);
            const auto parsed = aka_crypto::eap::parse_challenge_response(*packet);
            const bool res_ok =
                mac_ok && parsed.has_value() && parsed->res.size() == ctx->xres.size() &&
                std::equal(parsed->res.begin(), parsed->res.end(), ctx->xres.begin());

            if (res_ok) {
                const auto kseaf =
                    aka_crypto::derive_kseaf(ctx->kausf_eap, ctx->serving_network_name);
                resp_data.eapPayload =
                    aka_crypto::eap::base64_encode(aka_crypto::eap::build_success(identifier));
                resp_data.authResult = sbi_gen::AuthResult{};
                resp_data.authResult->value = sbi_gen::AuthResult::AUTHENTICATION_SUCCESS;
                resp_data.supi = ctx->supi;
                resp_data.kSeaf = aka_crypto::to_hex(kseaf);
                resp_data.msk =
                    aka_crypto::to_hex(std::vector<uint8_t>(ctx->msk.begin(), ctx->msk.end()));
            } else {
                resp_data.eapPayload =
                    aka_crypto::eap::base64_encode(aka_crypto::eap::build_failure(identifier));
                resp_data.authResult = sbi_gen::AuthResult{};
                resp_data.authResult->value = sbi_gen::AuthResult::AUTHENTICATION_FAILURE;
            }
            json j = resp_data;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "DELETE",
        std::string(kApiRoot) + "/ue-authentications/{authCtxId}/eap-session",
        [&verifier, &auth_contexts](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto auth_ctx_id = req.path_params.at("authCtxId");
            auto ctx = auth_contexts.get(auth_ctx_id);
            if (!ctx.has_value() || ctx->auth_type != "EAP_AKA_PRIME" ||
                !auth_contexts.remove(auth_ctx_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No EAP-AKA' authentication context " + auth_ctx_id);
            }
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/ue-authentications/deregister",
        [&verifier, &auth_contexts, &deregister_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::DeregistrationInfo>(req, err);
            if (!body.has_value()) {
                return err;
            }
            if (!auth_contexts.remove_by_supi(body->supi)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No authentication context for supi " + body->supi);
            }
            deregister_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, ausf_instance_id).detach();

    server.start();
    spdlog::info("ausf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("ausf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
