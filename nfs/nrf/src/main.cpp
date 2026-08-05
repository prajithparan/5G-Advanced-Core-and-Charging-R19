// nfs/nrf: the real NRF (NF Repository Function), TS 29.510 Nnrf_NFManagement +
// Nnrf_NFDiscovery + Nnrf_AccessToken (specs/5G_APIs-REL-19/, commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6). Replaces the Phase 0 nfs/stub-nrf throwaway.
// Phase 2's first NF, dependency root for every NF that follows (PROMPT.md order: NRF -> AMF ->
// SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope (see the procedure list agreed before this NF was built):
//   RegisterNFInstance, GetNFInstance, UpdateNFInstance (real RFC 6902 patch application),
//   DeregisterNFInstance, GetNFInstances, CreateSubscription, UpdateSubscription,
//   RemoveSubscription (all NFManagement.yaml), SearchNFInstances (NFDiscovery.yaml),
//   real signed-JWT OAuth2 token issuance (AccessToken.yaml).
// Out of scope, deferred not dropped: /shared-data* (multi-NRF federation -- no second NRF
// instance exists in this lab) and /scp-domain-routing-info* (SCP isn't built yet).
//
// Disclosed simplifications (see docs/DECISIONS.md for the full ADRs):
//   - In-memory storage only, no persistence across restarts.
//   - SearchNFInstances filters on target-nf-type only, not the full query parameter set.
//   - Subscription notification fan-out ignores subscrCond (delivers to every active subscriber
//     on NF_REGISTERED/NF_DEREGISTERED, not just matching ones).
//   - Notification delivery is synchronous best-effort (log on failure, do not retry) --
//     consistent with ADR-0006's synchronous HTTP/2 client.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/jwt.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/problem_details.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <sstream>

#include "registry.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/nrf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7777;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9464";  // see the SBI server bind for why
constexpr const char* kNfType = "NRF";

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

// application/x-www-form-urlencoded parsing for POST /oauth2/token. Minimal: splits on '&'/'=',
// percent-decodes values. Good enough for the fixed field set AccessTokenReq actually needs here.
std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            const std::string hex = in.substr(i + 1, 2);
            out += static_cast<char>(std::stoi(hex, nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_form_urlencoded(const std::string& body) {
    std::unordered_map<std::string, std::string> fields;
    std::stringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        fields[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
    }
    return fields;
}

// Returns nullopt if no Authorization header is present at all -- RegisterNFInstance/
// GetNFInstance/etc. all declare `security: [{}, oAuth2ClientCredentials:[...]]` in the YAML,
// i.e. an anonymous alternative is explicitly schema-permitted (bootstrap: an NF may not have a
// token yet before its first registration). If a header IS present, it must verify -- callers
// that want to reject anonymous access entirely can check has_value() themselves.
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

} // namespace

int main() {
    sbi_core::init_logging("nrf");
    sbi_core::init_tracing("nrf");
    sbi_core::init_metrics(kMetricsBindAddress);

    const std::string certs_dir = CERTS_DIR;
    const sbi_core::http2::TlsConfig server_tls{
        .cert_path = certs_dir + "/nrf/cert.pem",
        .key_path = certs_dir + "/nrf/key.pem",
        .ca_path = certs_dir + "/ca/ca.crt",
    };
    const sbi_core::http2::TlsConfig client_tls = server_tls; // NRF is also a client for
                                                              // notification delivery.

    const std::string nrf_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("nrf: starting, nfInstanceId={}", nrf_instance_id);

    sbi_core::jwt::Issuer issuer(certs_dir + "/nrf-jwt/private.pem", nrf_instance_id);
    sbi_core::jwt::Verifier verifier(certs_dir + "/nrf-jwt/public.pem", nrf_instance_id);

    auto nf_registry = std::make_shared<nrf::NfRegistry>();
    auto sub_registry = std::make_shared<nrf::SubscriptionRegistry>();
    auto notify_client = std::make_shared<sbi_core::http2::Client>(client_tls);

    auto meter = sbi_core::get_meter("nrf");
    auto registrations_counter = meter->CreateUInt64Counter(
        "nrf_registrations_total", "Total successful RegisterNFInstance calls");
    auto deregistrations_counter = meter->CreateUInt64Counter(
        "nrf_deregistrations_total", "Total successful DeregisterNFInstance calls");
    auto heartbeats_counter = meter->CreateUInt64Counter("nrf_heartbeats_total",
                                                         "Total successful UpdateNFInstance calls");
    auto tokens_counter =
        meter->CreateUInt64Counter("nrf_tokens_issued_total", "Total OAuth2 access tokens issued");
    auto registered_gauge = meter->CreateInt64ObservableGauge(
        "nrf_registered_nf_count", "Number of NF instances currently registered");
    registered_gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult observer_result, void* state) {
            auto* registry = static_cast<nrf::NfRegistry*>(state);
            const auto count = static_cast<std::int64_t>(registry->list_all().size());
            if (auto obs = opentelemetry::nostd::get_if<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(&observer_result)) {
                (*obs)->Observe(count);
            }
        },
        nf_registry.get());

    auto notify =
        [notify_client, sub_registry, nrf_instance_id](const std::string& event,
                                                       const std::string& nf_instance_id,
                                                       const std::optional<json>& profile) {
            json body{
                {"event", event},
                {"nfInstanceUri", "/nnrf-nfm/v1/nf-instances/" + nf_instance_id},
            };
            if (profile.has_value()) {
                body["nfProfile"] = *profile;
            }
            const std::string body_str = body.dump();
            for (const auto& uri : sub_registry->all_notification_uris()) {
                sbi_core::http2::ClientRequest req;
                req.method = "POST";
                req.url = uri;
                req.headers.emplace("content-type", "application/json");
                req.body = body_str;
                auto resp = notify_client->send(req);
                if (!resp.has_value()) {
                    spdlog::warn("nrf: notification delivery to {} failed: {}", uri, resp.error());
                }
            }
        };

    boost::asio::io_context ioc;
    // 0.0.0.0, not 127.0.0.1: found via actual Docker verification, not assumed -- a
    // loopback-only bind is unreachable from outside the container even with `docker run -p`,
    // since Docker's port mapping targets the container's external interface, not its loopback.
    // See docs/DECISIONS.md ADR-0014. Still reachable at 127.0.0.1 for anything running on the
    // same host/network namespace (hello-nf's local dev usage is unaffected).
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    server.add_route(
        "POST", "/oauth2/token", [&issuer, &tokens_counter](const sbi_core::http2::Request& req) {
            auto fields = parse_form_urlencoded(req.body);
            if (fields.count("grant_type") == 0 || fields["grant_type"] != "client_credentials") {
                return problem_response(400,
                                        "Unsupported grant_type",
                                        "Only grant_type=client_credentials is supported");
            }
            if (fields.count("nfInstanceId") == 0 || fields.count("scope") == 0) {
                return problem_response(
                    400, "Missing mandatory IE", "AccessTokenReq requires nfInstanceId and scope");
            }
            const std::string target_nf_type =
                fields.count("targetNfType") != 0 ? fields["targetNfType"] : kNfType;

            auto issued = issuer.issue(fields["nfInstanceId"],
                                       target_nf_type,
                                       fields["scope"],
                                       std::chrono::seconds(3600));
            tokens_counter->Add(1);

            json resp{
                {"access_token", issued.token},
                {"token_type", issued.token_type},
                {"expires_in", issued.expires_in.count()},
                {"scope", fields["scope"]},
            };
            return sbi_core::http2::Response::json(200, resp.dump());
        });

    server.add_route(
        "PUT",
        "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
        [&verifier, nf_registry, &notify, &registrations_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto path_id = req.path_params.at("nfInstanceID");
            json profile;
            try {
                profile = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return problem_response(400, "Malformed JSON", e.what());
            }
            for (const char* required : {"nfInstanceId", "nfType", "nfStatus"}) {
                if (!profile.contains(required)) {
                    return problem_response(400,
                                            "Missing mandatory IE",
                                            std::string("NFProfile missing '") + required + "'");
                }
            }
            const bool is_new = nf_registry->put(path_id, profile);
            spdlog::info("nrf: {} NF instance {} (type={}, status={})",
                         is_new ? "registered new" : "replaced",
                         path_id,
                         profile.value("nfType", "?"),
                         profile.value("nfStatus", "?"));
            registrations_counter->Add(1);
            notify(is_new ? "NF_REGISTERED" : "NF_PROFILE_CHANGED",
                   path_id,
                   std::make_optional(profile));

            sbi_core::http2::Response resp;
            resp.status = is_new ? 201 : 200;
            resp.headers.emplace("content-type", "application/json");
            if (is_new) {
                resp.headers.emplace("location", "/nnrf-nfm/v1/nf-instances/" + path_id);
            }
            resp.body = profile.dump();
            return resp;
        });

    server.add_route(
        "GET",
        "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
        [&verifier, nf_registry](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto path_id = req.path_params.at("nfInstanceID");
            auto profile = nf_registry->get(path_id);
            if (!profile.has_value()) {
                return problem_response(404, "Not Found", "No NF instance with id " + path_id);
            }
            return sbi_core::http2::Response::json(200, profile->dump());
        });

    server.add_route("GET",
                     "/nnrf-nfm/v1/nf-instances",
                     [&verifier, nf_registry](const sbi_core::http2::Request& req) {
                         if (auto auth = check_bearer(req, verifier);
                             auth.has_value() && !auth->valid) {
                             return problem_response(401, "Unauthorized", auth->error);
                         }
                         json arr = json::array();
                         for (const auto& profile : nf_registry->list_all()) {
                             arr.push_back(profile);
                         }
                         return sbi_core::http2::Response::json(200, arr.dump());
                     });

    server.add_route(
        "PATCH",
        "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
        [&verifier, nf_registry, &notify, &heartbeats_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto path_id = req.path_params.at("nfInstanceID");
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return problem_response(400, "Malformed JSON", e.what());
            }
            std::optional<json> patched;
            try {
                patched = nf_registry->apply_patch(path_id, patch_ops);
            } catch (const json::exception& e) {
                return problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return problem_response(404, "Not Found", "No NF instance with id " + path_id);
            }
            spdlog::info("nrf: heartbeat/update for {}", path_id);
            heartbeats_counter->Add(1);
            notify("NF_PROFILE_CHANGED", path_id, patched);
            return sbi_core::http2::Response::json(200, patched->dump());
        });

    server.add_route(
        "DELETE",
        "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
        [&verifier, nf_registry, &notify, &deregistrations_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto path_id = req.path_params.at("nfInstanceID");
            const bool existed = nf_registry->remove(path_id);
            spdlog::info(
                "nrf: deregister {} ({})", path_id, existed ? "removed" : "was not registered");
            if (existed) {
                deregistrations_counter->Add(1);
                notify("NF_DEREGISTERED", path_id, std::nullopt);
            }
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // NFDiscovery.yaml's SearchNFInstances -- distinct base path (/nnrf-disc/v1) from the
    // NFManagement routes above (/nnrf-nfm/v1), both served by this same NRF process.
    server.add_route(
        "GET",
        "/nnrf-disc/v1/nf-instances",
        [&verifier, nf_registry](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            // Query string parsing: http2::Request doesn't split query params out (Phase 0's
            // router never needed to), so parse target-nf-type directly out of req.path here.
            const auto qpos = req.path.find('?');
            std::string target_type;
            if (qpos != std::string::npos) {
                auto qs = parse_form_urlencoded(req.path.substr(qpos + 1));
                if (auto it = qs.find("target-nf-type"); it != qs.end()) {
                    target_type = it->second;
                }
            }
            if (target_type.empty()) {
                return problem_response(
                    400, "Missing mandatory query parameter", "target-nf-type is required");
            }
            json instances = json::array();
            for (const auto& profile : nf_registry->search_by_type(target_type)) {
                instances.push_back(profile);
            }
            json result{
                {"validityPeriod", 60},
                {"nfInstances", instances},
            };
            return sbi_core::http2::Response::json(200, result.dump());
        });

    server.add_route(
        "POST",
        "/nnrf-nfm/v1/subscriptions",
        [&verifier, sub_registry](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            json sub_data;
            try {
                sub_data = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return problem_response(400, "Malformed JSON", e.what());
            }
            if (!sub_data.contains("nfStatusNotificationUri")) {
                return problem_response(400,
                                        "Missing mandatory IE",
                                        "SubscriptionData requires nfStatusNotificationUri");
            }
            auto created = sub_registry->create(sub_data);
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 "/nnrf-nfm/v1/subscriptions/" +
                                     created.at("subscriptionId").get<std::string>());
            resp.body = created.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        "/nnrf-nfm/v1/subscriptions/{subscriptionID}",
        [&verifier, sub_registry](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return problem_response(401, "Unauthorized", auth->error);
            }
            const auto sub_id = req.path_params.at("subscriptionID");
            json sub_data;
            try {
                sub_data = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return problem_response(400, "Malformed JSON", e.what());
            }
            auto updated = sub_registry->update(sub_id, sub_data);
            if (!updated.has_value()) {
                return problem_response(404, "Not Found", "No subscription with id " + sub_id);
            }
            return sbi_core::http2::Response::json(200, updated->dump());
        });

    server.add_route("DELETE",
                     "/nnrf-nfm/v1/subscriptions/{subscriptionID}",
                     [&verifier, sub_registry](const sbi_core::http2::Request& req) {
                         if (auto auth = check_bearer(req, verifier);
                             auth.has_value() && !auth->valid) {
                             return problem_response(401, "Unauthorized", auth->error);
                         }
                         const auto sub_id = req.path_params.at("subscriptionID");
                         sub_registry->remove(sub_id);
                         sbi_core::http2::Response resp;
                         resp.status = 204;
                         return resp;
                     });

    server.start();
    spdlog::info("nrf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("nrf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
