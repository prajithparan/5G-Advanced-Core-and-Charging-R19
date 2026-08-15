// bss/roaming-interconnect: a real, standalone TM Forum TMF651 Agreement Management service --
// InterconnectAgreement (E7) only, CHARGING_PROMPT.md's P4.7 ("BSS layer + master/consumer/
// enterprise model") scope.
//
// Real basePath, confirmed directly against TM Forum's own public swagger (fetched live, same
// sourcing discipline as bss/product-catalog's own TMF620 citation --
// github.com/tmforum-apis/TMF651_AgreementManagement, TMF651-Agreement-v4.0.0.swagger.json):
// `/tmf-api/agreementManagement/v4/` with a real `/agreement` collection.
//
// Real, disclosed scope split (a deliberate refinement of this project's own earlier phase
// assignment -- see schema.sql's own updated header comment for the full disclosure): this
// project's earlier E7 schema work (docs/DECISIONS.md ADR-0060) originally assigned E7's ENTIRE
// HTTP service, InterconnectAgreement AND RoamingCdrFile together, to P4.11 (Roaming and
// interconnect SETTLEMENT), since P4.11 is genuinely blocked on real GSMA TAP3/RAP/NRTRDE spec
// text CLAUDE.md forbids fabricating. On review, `InterconnectAgreement` itself -- WHO the partner
// operator is and what a real TMF651 Agreement between the two operators looks like -- is not
// GSMA-blocked at all; it's ordinary master data, squarely part of P4.7's own "master model" layer
// (docs/DATA_MODEL.md's E10 framing already treats Agreement as part of the enterprise/SLA
// hierarchy). `RoamingCdrFile` (real GSMA-formatted CDR ingestion, the actually-blocked piece)
// stays unexposed here -- P4.11's own real scope, still deferred pending the user supplying real
// GSMA spec text, not worked around.
//
// Scope, approved before implementation: real Agreement Create/Get/List (matching
// product-catalog's own disclosed CRUD bar; PATCH/DELETE deferred, same as product-catalog).
//
// Disclosed simplifications (same class as product-catalog's own):
// - Not a 3GPP NF: mTLS-only security boundary, no NRF registration, no OAuth2.
// - One libpqxx connection per store, serialized behind a mutex -- not a connection pool.
// - PATCH/DELETE not implemented this turn.
// - RoamingCdrFileStore exists (store.hpp) but has no HTTP route here -- real, disclosed, P4.11's
//   own scope, not silently narrowed away.

#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/otel.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>

#include "bss_sid/agreement.hpp"
#include "store.hpp"

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see bss/roaming-interconnect/CMakeLists.txt)"
#endif

namespace {

using nlohmann::json;

constexpr unsigned short kPort = 7788;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9476";
constexpr const char* kSelfBase = "https://127.0.0.1:7788";
constexpr const char* kApiRoot = "/tmf-api/agreementManagement/v4";

std::string database_conninfo() {
    if (const char* env = std::getenv("ROAMING_INTERCONNECT_DATABASE_URL")) {
        return env;
    }
    return "postgresql://roaming_interconnect:roaming_interconnect@localhost:5432/"
           "roaming_interconnect";
}

} // namespace

int main() {
    sbi_core::init_logging("roaming-interconnect");
    sbi_core::init_tracing("roaming-interconnect");
    sbi_core::init_metrics(kMetricsBindAddress);

    spdlog::info("roaming-interconnect: starting (TM Forum TMF651 Agreement Management, E7 "
                 "InterconnectAgreement)");

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/roaming-interconnect/cert.pem",
        .key_path = CERTS_DIR "/roaming-interconnect/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    const auto conninfo = database_conninfo();
    roaming_interconnect::InterconnectAgreementStore agreement_store(
        std::string(kSelfBase) + kApiRoot + "/agreement", conninfo);
    spdlog::info("roaming-interconnect: connected to PostgreSQL");

    auto meter = sbi_core::get_meter("roaming-interconnect");
    auto agreement_create_counter =
        meter->CreateUInt64Counter("roaming_interconnect_agreement_create_total",
                                   "Total TMF651 InterconnectAgreement creates");

    boost::asio::io_context ioc;
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/agreement",
        [&agreement_store, &agreement_create_counter](const sbi_core::http2::Request& req) {
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<roaming_interconnect::InterconnectAgreement>(req,
                                                                                              err);
            if (!body.has_value()) {
                return err;
            }
            const auto id = agreement_store.create(*body);
            agreement_create_counter->Add(1);
            const auto stored = agreement_store.get(id);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/agreement/" + id);
            resp.body = json(*stored).dump();
            return resp;
        });

    server.add_route("GET",
                     std::string(kApiRoot) + "/agreement",
                     [&agreement_store](const sbi_core::http2::Request&) {
                         const auto agreements = agreement_store.list();
                         return sbi_core::http2::Response::json(200, json(agreements).dump());
                     });

    server.add_route("GET",
                     std::string(kApiRoot) + "/agreement/{id}",
                     [&agreement_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         const auto agreement = agreement_store.get(id);
                         if (!agreement.has_value()) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No InterconnectAgreement " + id);
                         }
                         return sbi_core::http2::Response::json(200, json(*agreement).dump());
                     });

    server.start();
    spdlog::info("roaming-interconnect: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("roaming-interconnect: Prometheus metrics at http://{}/metrics",
                 kMetricsBindAddress);
    ioc.run();
    return 0;
}
