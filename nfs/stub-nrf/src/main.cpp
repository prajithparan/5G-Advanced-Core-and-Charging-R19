// stub-nrf: a minimal in-memory NF registry, Phase 0 throwaway.
//
// This is NOT the real NRF (that's Phase 2, built from generated TS29510_Nnrf_NFManagement
// DTOs against libs/sbi-core). It exists only to give hello-nf something real to register
// against over actual HTTP/2, so Phase 0 proves the transport/SBI plumbing works end-to-end
// before any business logic exists. Paths and field names match
// specs/5G_APIs-REL-19/TS29510_Nnrf_NFManagement.yaml and TS29510_Nnrf_AccessToken.yaml, but the
// behaviour behind them is deliberately naive:
//   - PATCH (heartbeat) does not apply the JSON Patch body, it just checks the record exists.
//   - The OAuth2 token returned is JWT-shaped but unsigned -- no real TS 33.501 validation.
// Both are called out again in docs/DECISIONS.md.

#include "sbi_core/http2_server.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/problem_details.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace {

using nlohmann::json;

class Registry {
public:
    // Returns true if this was a new registration (caller should respond 201), false if it
    // replaced an existing profile (caller should respond 200).
    bool put(const std::string& nf_instance_id, json profile) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool is_new = !profiles_.contains(nf_instance_id);
        profiles_[nf_instance_id] = std::move(profile);
        return is_new;
    }

    std::optional<json> get(const std::string& nf_instance_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(nf_instance_id);
        if (it == profiles_.end()) {
            return std::nullopt;
        }
        return std::make_optional(it->second);
    }

    bool touch(const std::string& nf_instance_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return profiles_.contains(nf_instance_id);
    }

    bool remove(const std::string& nf_instance_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return profiles_.erase(nf_instance_id) > 0;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, json> profiles_;
};

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

} // namespace

int main() {
    sbi_core::init_logging("stub-nrf");
    sbi_core::init_tracing("stub-nrf");

    constexpr unsigned short kPort = 7777;
    boost::asio::io_context ioc;
    sbi_core::http2::Server server(ioc, "127.0.0.1", kPort);

    auto registry = std::make_shared<Registry>();

    server.add_route("POST", "/oauth2/token", [](const sbi_core::http2::Request& req) {
        spdlog::info("stub-nrf: POST /oauth2/token ({} bytes body)", req.body.size());
        // Deliberately not conformant: unsigned, JWT-shaped placeholder only. See file header.
        json resp{
            {"access_token", "stub-nrf.unsigned-placeholder-token.not-for-real-security"},
            {"token_type", "Bearer"},
            {"expires_in", 3600},
        };
        return sbi_core::http2::Response::json(200, resp.dump());
    });

    server.add_route(
        "PUT",
        "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
        [registry](const sbi_core::http2::Request& req) {
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
            const bool is_new = registry->put(path_id, profile);
            spdlog::info("stub-nrf: {} NF instance {} (type={}, status={})",
                         is_new ? "registered new" : "replaced",
                         path_id,
                         profile.value("nfType", "?"),
                         profile.value("nfStatus", "?"));

            sbi_core::http2::Response resp;
            resp.status = is_new ? 201 : 200;
            resp.headers.emplace("content-type", "application/json");
            if (is_new) {
                resp.headers.emplace("location", "/nnrf-nfm/v1/nf-instances/" + path_id);
            }
            resp.body = profile.dump();
            return resp;
        });

    server.add_route("GET",
                     "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
                     [registry](const sbi_core::http2::Request& req) {
                         const auto path_id = req.path_params.at("nfInstanceID");
                         auto profile = registry->get(path_id);
                         if (!profile.has_value()) {
                             return problem_response(
                                 404, "Not Found", "No NF instance with id " + path_id);
                         }
                         return sbi_core::http2::Response::json(200, profile->dump());
                     });

    server.add_route(
        "PATCH",
        "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
        [registry](const sbi_core::http2::Request& req) {
            const auto path_id = req.path_params.at("nfInstanceID");
            if (!registry->touch(path_id)) {
                return problem_response(404, "Not Found", "No NF instance with id " + path_id);
            }
            spdlog::info("stub-nrf: heartbeat for {} (patch body not applied -- see file header)",
                         path_id);
            auto profile = registry->get(path_id);
            return sbi_core::http2::Response::json(200, profile->dump());
        });

    server.add_route("DELETE",
                     "/nnrf-nfm/v1/nf-instances/{nfInstanceID}",
                     [registry](const sbi_core::http2::Request& req) {
                         const auto path_id = req.path_params.at("nfInstanceID");
                         const bool existed = registry->remove(path_id);
                         spdlog::info("stub-nrf: deregister {} ({})",
                                      path_id,
                                      existed ? "removed" : "was not registered");
                         sbi_core::http2::Response resp;
                         resp.status = 204;
                         return resp;
                     });

    server.start();
    spdlog::info("stub-nrf: listening on http://127.0.0.1:{} (h2c, Phase 0 throwaway)", kPort);
    ioc.run();
    return 0;
}
