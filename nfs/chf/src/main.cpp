// nfs/chf: CHF (Charging Function), Nchf_ConvergedCharging service (TS 32.291).
// Source: specs/5G_APIs-REL-19/TS32291_Nchf_ConvergedCharging.yaml
// (commit bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 4's first NF (CLAUDE.md's charging
// domain), approved scope for this turn:
//
// Nchf_ConvergedCharging_Create only -- POST /chargingdata. Real request parsing, real
// ChargingDataRef allocation, a schema-valid ChargingDataResponse.
//
// Deliberately deferred, not dropped (separate approved future turns): Update
// (POST /chargingdata/{ChargingDataRef}/update) and Release
// (POST /chargingdata/{ChargingDataRef}/release), the chargingNotification callback,
// Nchf_OfflineOnlyCharging, Nchf_SpendingLimitControl, and the TM Forum SID/BSS mapping layer
// (docs/CHARGING_MAPPING.md, required by CLAUDE.md before any mapping code -- not needed for this
// turn's pure-3GPP-side scope). See docs/DECISIONS.md ADR-0044.
//
// Disclosed simplifications, stated up front:
// - No real rating/quota engine. multipleUnitInformation is never populated (no online/quota
//   charging decision, since there's no rate table or subscriber balance anywhere in this build --
//   same category of gap as PCF's fixed-default policy, ADR-0028). This Create always succeeds
//   with an empty grant, which is schema-valid (multipleUnitInformation is optional) but not a
//   real charging decision.
// - ChargingDataResponse's invocationSequenceNumber is set by echoing the request's own value.
//   TS32291_Nchf_ConvergedCharging.yaml carries no per-field description text distinguishing
//   "echo the request's sequence" from "CHF assigns its own independent sequence" for this field,
//   and no normative TS 32.291 prose is vendored in this repo to check -- echoing the request's
//   value is the least-invented choice (no independent CHF-side sequencing semantics assumed) and
//   disclosed here rather than picked silently.
// - No persistence across restarts (in-memory ChargingDataRefAllocator only) -- same disclosed
//   simplification as every other NF's store so far.

#include "bss_sid/party.hpp"
#include "sbi_core/datetime.hpp"
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
#error "CERTS_DIR must be defined by CMake (see nfs/chf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7784;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9472";
constexpr const char* kNfType = "CHF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kApiRoot = "/nchf-convergedcharging/v3";

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

// Runs on a dedicated thread, never on the server's io_context -- same reasoning as every other
// NF's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& chf_instance_id) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/chf/cert.pem",
        .key_path = CERTS_DIR "/chf/key.pem",
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
        http_client, std::string(kNrfBase) + "/oauth2/token", chf_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", chf_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("chf: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + chf_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("chf: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("chf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("chf: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = std::string(kNrfBase) + "/nnrf-nfm/v1/nf-instances/" + chf_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("chf: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("chf");
    sbi_core::init_tracing("chf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string chf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("chf: starting, nfInstanceId={}", chf_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/chf/cert.pem",
        .key_path = CERTS_DIR "/chf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    chf::ChargingDataRefAllocator ref_allocator;

    auto meter = sbi_core::get_meter("chf");
    auto create_counter = meter->CreateUInt64Counter(
        "chf_charging_data_create_total", "Total Nchf_ConvergedCharging_Create calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    // --- Nchf_ConvergedCharging ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/chargingdata",
        [&verifier, &ref_allocator, &create_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::ChargingDataRequest>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto ref = ref_allocator.allocate();

            // docs/CHARGING_MAPPING.md's resolved mapping: build the TM Forum SID record for the
            // subscriber this charging event is for. Logged, not yet persisted or exposed via a
            // real TMF632 REST surface (no Party-management store exists in this codebase yet) --
            // this demonstrates CHF's internal charging record is genuinely SID-shaped, which is
            // as much of the mapping as has a real, unambiguous 3GPP field to build it from today
            // (see the mapping doc's own scope section for why every other field is deferred).
            if (body->subscriberIdentifier.has_value()) {
                const auto individual = bss_sid::map_supi_to_individual(*body->subscriberIdentifier);
                spdlog::info("chf: mapped subscriberIdentifier to TM Forum SID Individual: {}",
                            nlohmann::json(individual).dump());
            }

            sbi_gen::ChargingDataResponse response{};
            response.invocationTimeStamp = sbi_core::format_rfc3339(std::chrono::system_clock::now());
            // See file header for why this echoes the request's value rather than assigning an
            // independent CHF-side sequence.
            response.invocationSequenceNumber = body->invocationSequenceNumber;
            create_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/chargingdata/" + ref);
            json j = response;
            resp.body = j.dump();
            return resp;
        });

    std::thread(run_nrf_lifecycle, chf_instance_id).detach();

    server.start();
    spdlog::info("chf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("chf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
