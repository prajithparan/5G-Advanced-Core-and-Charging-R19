// nfs/nef: NEF (Network Exposure Function), Nnef_PFDmanagement service (one of NEF's real
// services -- see this file's own "In scope" section for why only this one, not all 14 real NEF
// YAML files, is built this turn). Source:
// specs/5G_APIs-REL-19/TS29551_Nnef_PFDmanagement.yaml (v1.4.0), commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6. This project's eleventh NF, third built under the
// continuous move-to-next-NF process (docs/DECISIONS.md ADR-0184).
//
// In scope, agreed with the user before implementation: `docs/CAPABILITY_GAP_ANALYSIS.md`'s "still
// not done" list names NEF as one of the two remaining unbuilt Tier 1 NFs (with SCP). NEF's own
// real spec surface is 14 separate YAML files, ~52 operations total -- too large for one turn as a
// whole (Nnef_EventExposure, Nnef_SMContext, Nnef_SMService, Nnef_UEId, Nnef_Authentication,
// Nnef_TrafficInfluenceData, Nnef_DNAIMapping, Nnef_ECSAddress, Nnef_EASDeployment,
// Nnef_Inference/Training/VFLInference/VFLTraining all remain unbuilt after this turn, a real,
// disclosed deferral, not a claim of NEF completeness). `Nnef_PFDmanagement` (6 real top-level
// operations, the largest single well-defined NEF service) chosen as this turn's slice: it is the
// real, spec-documented consumer-facing counterpart to this project's own already-built UPF-side
// PFCP PFD Management (task #107/ADR-0086, TS 29.244 -- the N4 side SMF already uses to push PFDs
// to UPF), even though the two aren't wired together this turn (real, disclosed, deferred --
// SMF doesn't yet call this NEF API to source the PFDs it pushes onward).
//
// All 6 real top-level operations implemented:
//   GET    {apiRoot}/applications                  Nnef_PFDmanagement_AllFetch
//   POST   {apiRoot}/applications/partialpull       Nnef_PFDmanagement_AppFetchPartialUpdate
//   GET    {apiRoot}/applications/{appId}           Nnef_PFDmanagement_IndAppFetch
//   POST   {apiRoot}/subscriptions                  Nnef_PFDmanagement_CreateSubscr
//   PUT    {apiRoot}/subscriptions/{subscriptionId} Nnef_PFDmanagement_ModifySubscr
//   DELETE {apiRoot}/subscriptions/{subscriptionId} Nnef_PFDmanagement_Unsubscribe
//
// Real, disclosed simplifications/gaps -- stated up front, not discovered in review:
// 1. This YAML has NO operation anywhere that lets a caller WRITE PFD content into NEF -- the real
//    3GPP AF-to-NEF PFD provisioning path is genuinely out of 3GPP's own standardized SBI
//    framework scope (typically OAM/vendor-specific), not just unbuilt here (confirmed by direct
//    read of the full YAML, not assumed). `PfdCatalogStore` is therefore seed()-only, same
//    precedent as several of this project's own other "no live write path exists" stores.
// 2. Because of (1), the real `PfdChangeNotification`/`NotificationPush` callback delivery this
//    API declares (`CreateSubscr`'s own POST callbacks) has NO real trigger this project can ever
//    fire -- PFD content never changes after startup seeding, so there is no real "PFD changed"
//    event to notify about. This project does NOT build a notification-delivery function with no
//    real caller (dead code presented as if it were live infrastructure would be worse than
//    disclosing the gap plainly): `CreateSubscr`/`ModifySubscr`/`Unsubscribe` are real, live,
//    tested CRUD on the subscription resource itself, but no notification is ever sent. A real,
//    disclosed structural gap, not an oversight.
// 3. `AppFetchPartialUpdate`'s real "changed since" comparison
//    (`PfdCatalogStore::get_if_changed_since`) uses lexicographic string comparison of ISO8601 UTC
//    timestamps -- correct for this project's own generated `DateTime` string format (always
//    zero-padded, always UTC/`Z`-suffixed), a real, disclosed narrower assumption than a full
//    calendar-aware datetime comparison would need for arbitrary input formats.

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
#include <string>
#include <thread>
#include <vector>

#include "TS29551_Nnef_PFDmanagement.hpp"
#include "nf_config/nf_config.hpp"
#include "stores.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/nef/CMakeLists.txt)"
#endif
#ifndef CONFIG_DIR
#error "CONFIG_DIR must be defined by CMake (see nfs/nef/CMakeLists.txt)"
#endif

constexpr const char* kNfType = "NEF";
constexpr const char* kApiRoot = "/nnef-pfdmanagement/v1";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// Real, illustrative seed data -- see this file's own top comment, simplification 1, for why no
// live write path exists to populate this instead.
void seed_pfd_catalog(nef::PfdCatalogStore& store) {
    sbi_gen::PfdContent content;
    content.pfdId = "pfd1";
    content.urls = std::vector<std::string>{"^https://video\\.example\\.com/.*$"};

    sbi_gen::PfdDataForApp app1;
    app1.applicationId = "app-video-streaming";
    app1.pfds = std::vector<sbi_gen::PfdContent>{content};
    app1.pfdTimestamp = "2026-01-01T00:00:00Z";
    store.seed("app-video-streaming", json(app1));
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
// nfs/ausf/src/main.cpp's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& nef_instance_id, const std::string& nrf_base) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/nef/cert.pem",
        .key_path = CERTS_DIR "/nef/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client http_client(std::move(client_tls));

    for (int attempt = 0; attempt < 300; ++attempt) {
        sbi_core::http2::ClientRequest probe;
        probe.method = "GET";
        probe.url = nrf_base + "/nnrf-nfm/v1/nf-instances/00000000-0000-4000-8000-000000000000";
        if (http_client.send(probe).has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sbi_core::OAuth2Client oauth(
        http_client, nrf_base + "/oauth2/token", nef_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", nef_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("nef: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + nef_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();
        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("nef: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("nef: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("nef: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + nef_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("nef: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("nef");
    sbi_core::init_tracing("nef");

    // ADR-0077 (user-directed, mandatory, project-wide): no DB URL/connection/deployment
    // parameter may be a hardcoded literal default in source -- real values live in the
    // checked-in config/nef.json, with an env var override per key still available.
    const auto config = nf_config::load("nef", CONFIG_DIR);
    const auto port = nf_config::require<unsigned short>(config, "port");
    const auto metrics_bind_address =
        nf_config::require<std::string>(config, "metrics_bind_address");
    const auto nrf_base =
        nf_config::require<std::string>(config, "nrf_base_url", "NEF_NRF_BASE_URL");

    sbi_core::init_metrics(metrics_bind_address);

    const std::string nef_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("nef: starting, nfInstanceId={}", nef_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/nef/cert.pem",
        .key_path = CERTS_DIR "/nef/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    nef::PfdCatalogStore pfd_catalog;
    seed_pfd_catalog(pfd_catalog);
    nef::PfdSubscriptionStore subscriptions;

    auto meter = sbi_core::get_meter("nef");
    auto all_fetch_counter = meter->CreateUInt64Counter("nef_pfd_all_fetch_total",
                                                        "Total Nnef_PFDmanagement_AllFetch calls");
    auto partial_fetch_counter = meter->CreateUInt64Counter(
        "nef_pfd_partial_fetch_total", "Total Nnef_PFDmanagement_AppFetchPartialUpdate calls");
    auto ind_fetch_counter = meter->CreateUInt64Counter(
        "nef_pfd_ind_fetch_total", "Total Nnef_PFDmanagement_IndAppFetch calls");
    auto sub_create_counter = meter->CreateUInt64Counter(
        "nef_pfd_sub_create_total", "Total Nnef_PFDmanagement_CreateSubscr calls");
    auto sub_modify_counter = meter->CreateUInt64Counter(
        "nef_pfd_sub_modify_total", "Total Nnef_PFDmanagement_ModifySubscr calls");
    auto sub_delete_counter = meter->CreateUInt64Counter(
        "nef_pfd_sub_delete_total", "Total Nnef_PFDmanagement_Unsubscribe calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", port, server_tls);

    // --- Nnef_PFDmanagement: applications ---

    server.add_route(
        "GET",
        std::string(kApiRoot) + "/applications",
        [&verifier, &pfd_catalog, &all_fetch_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            auto ids_it = req.query_params.find("application-ids");
            if (ids_it == req.query_params.end()) {
                return sbi_core::http2::problem_response(
                    400, "Missing mandatory query parameter", "application-ids is required");
            }
            all_fetch_counter->Add(1);
            const auto ids = sbi_core::http2::split_form_array(ids_it->second);
            json results = json::array();
            for (const auto& pfd_data : pfd_catalog.get_many(ids)) {
                results.push_back(pfd_data);
            }
            return sbi_core::http2::Response::json(200, results.dump());
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/applications/partialpull",
        [&verifier, &pfd_catalog, &partial_fetch_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<std::vector<sbi_gen::ApplicationForPfdRequest>>(
                    req, err);
            if (!body.has_value()) {
                return err;
            }
            partial_fetch_counter->Add(1);

            json changed = json::array();
            for (const auto& request_item : *body) {
                std::optional<std::string> since;
                if (request_item.pfdTimestamp.has_value()) {
                    since = *request_item.pfdTimestamp;
                }
                if (auto pfd_data =
                        pfd_catalog.get_if_changed_since(request_item.applicationId, since);
                    pfd_data.has_value()) {
                    changed.push_back(*pfd_data);
                }
            }
            if (changed.empty()) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            return sbi_core::http2::Response::json(200, changed.dump());
        });

    server.add_route(
        "GET",
        std::string(kApiRoot) + "/applications/{appId}",
        [&verifier, &pfd_catalog, &ind_fetch_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto app_id = req.path_params.at("appId");
            auto pfd_data = pfd_catalog.get(app_id);
            if (!pfd_data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No PFDs for application " + app_id);
            }
            ind_fetch_counter->Add(1);
            return sbi_core::http2::Response::json(200, pfd_data->dump());
        });

    // --- Nnef_PFDmanagement: subscriptions ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/subscriptions",
        [&verifier, &subscriptions, &sub_create_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::PfdSubscription>(req, err);
            if (!body.has_value()) {
                return err;
            }

            sub_create_counter->Add(1);
            json j = *body;
            const auto id = subscriptions.create(j);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/subscriptions/" + id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PUT",
        std::string(kApiRoot) + "/subscriptions/{subscriptionId}",
        [&verifier, &subscriptions, &sub_modify_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto id = req.path_params.at("subscriptionId");
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::PfdSubscription>(req, err);
            if (!body.has_value()) {
                return err;
            }
            json j = *body;
            if (!subscriptions.put(id, j)) {
                return sbi_core::http2::problem_response(404, "Not Found", "No subscription " + id);
            }
            sub_modify_counter->Add(1);
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "DELETE",
        std::string(kApiRoot) + "/subscriptions/{subscriptionId}",
        [&verifier, &subscriptions, &sub_delete_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto id = req.path_params.at("subscriptionId");
            if (!subscriptions.remove(id)) {
                return sbi_core::http2::problem_response(404, "Not Found", "No subscription " + id);
            }
            sub_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, nef_instance_id, nrf_base).detach();

    server.start();
    spdlog::info("nef: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", port);
    spdlog::info("nef: Prometheus metrics at http://{}/metrics", metrics_bind_address);
    ioc.run();
    return 0;
}
