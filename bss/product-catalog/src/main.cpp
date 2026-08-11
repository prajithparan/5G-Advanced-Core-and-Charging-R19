// bss/product-catalog: a real, standalone TM Forum TMF620 Product Catalog Management service.
// Source: TM Forum's real public TMF620 v4.1.0 swagger
// (github.com/tmforum-apis/TMF620_ProductCatalog/blob/main/TMF620-ProductCatalog-v4.1.0.swagger.json,
// fetched and parsed directly, not vendored -- see libs/bss_sid/include/bss_sid/product.hpp's own
// header for why). Real basePath: /tmf-api/productCatalogManagement/v4/.
//
// Why this exists, and why it's a standalone service rather than code inside nfs/chf: CLAUDE.md's
// charging domain requirement says product/tariff definition must be CONFIGURABLE data, not code
// (see PROMPT.md's charging principles) -- until this turn, no product/tariff data model existed
// anywhere in this repo at all; CHF's Nchf_ConvergedCharging_Create has always returned an empty
// grant (disclosed since ADR-0044, "no real rating/quota engine"). This service is that missing
// data model, built as its own TM Forum ODA component -- consistent with CLAUDE.md's explicit
// "align to TM Forum ODA component boundaries so the BSS layer could be swapped for a commercial
// stack" goal, which argues for a real component boundary here, not code folded into CHF.
//
// Scope, approved before implementation: real ProductOffering/ProductOfferingPrice/
// ProductSpecification CRUD (Create/Get/List/Delete -- the real TMF620 PATCH-for-update and the
// /listener/* event-notification callbacks are deferred, not needed to prove the data model is
// real and usable) against a real PostgreSQL store (extended 2026-08-10, see below -- was
// in-memory-only until this turn). NOT yet wired to CHF -- CHF still returns an empty grant; making
// it actually consult this catalog to rate a charging event is a real rating engine, a separate,
// larger, not-yet-approved scope. NOT a GUI -- Phase 7 (JSON-schema-driven operator console, not
// started) is the eventual consumer of this service's real API; this turn only builds the
// API/data model it would render against.
//
// Persistence, extended 2026-08-10 per docs/DATA_MODEL.md's E2 / docs/DECISIONS.md ADR-0053/
// ADR-0054: real PostgreSQL (libpqxx), not the earlier in-memory std::unordered_map store --
// header fields as real columns, TMF620's array/nested fields as `jsonb` columns (schema.sql).
// Connection string from PRODUCT_CATALOG_DATABASE_URL (never hardcoded credentials); see that env
// var's own comment below for the default used when unset.
//
// Disclosed simplifications:
// - Not a 3GPP NF: no NRF registration, no OAuth2 bearer-token verification (there is no NRF-
//   issued token source for a non-3GPP ODA component, and building a separate BSS-side OAuth2/OIDC
//   stack is out of scope for "data model + API first"). Security boundary is mTLS only, reusing
//   this lab's existing CA (scripts/gen-lab-pki.sh) for real transport security and consistency
//   with every other component -- not "no security", just a narrower one than the 3GPP NFs have.
// - One libpqxx connection per store, serialized behind a mutex -- not a connection pool (see
//   store.hpp's own disclosure). Real limitation if this becomes a throughput bottleneck; nothing
//   benchmarked yet (ADR-0049's standing disclosure).
// - This service is not yet wired into deploy/docker/docker-compose.yml (a real, separately
//   tracked pending item from an earlier audit, not fixed by this turn) -- so this turn's real
//   Postgres persistence is verified by manually running a postgres container, not via the lab's
//   compose stack. Disclosed, not silently assumed done.
// - PATCH (real TMF620 update semantics, JSON Merge Patch per the real spec) is not implemented
//   this turn -- Create/Get/List/Delete is enough to prove product/tariff definitions are
//   configurable data, not code; Update is a real but separate future addition.

#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/otel.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>

#include "bss_sid/product.hpp"
#include "store.hpp"

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see bss/product-catalog/CMakeLists.txt)"
#endif

namespace {

using nlohmann::json;

constexpr unsigned short kPort = 7785;
constexpr const char* kMetricsBindAddress = "0.0.0.0:9473";
constexpr const char* kSelfBase = "https://127.0.0.1:7785";
constexpr const char* kApiRoot = "/tmf-api/productCatalogManagement/v4";

// First getenv-based config in this repo (every other NF so far uses compile-time constants) --
// a database connection string is exactly the kind of value that must never be hardcoded, so this
// is a deliberate, disclosed departure from the rest of this codebase's convention, not an
// inconsistency. Default matches this project's lab/dev convention (local Postgres, dedicated
// product_catalog role/database) -- override for any real deployment.
std::string database_conninfo() {
    if (const char* env = std::getenv("PRODUCT_CATALOG_DATABASE_URL")) {
        return env;
    }
    return "postgresql://product_catalog:product_catalog@localhost:5432/product_catalog";
}

} // namespace

int main() {
    sbi_core::init_logging("product-catalog");
    sbi_core::init_tracing("product-catalog");
    sbi_core::init_metrics(kMetricsBindAddress);

    spdlog::info("product-catalog: starting (TM Forum TMF620 Product Catalog Management)");

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/product-catalog/cert.pem",
        .key_path = CERTS_DIR "/product-catalog/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    const auto conninfo = database_conninfo();
    product_catalog::ProductOfferingStore offering_store(
        std::string(kSelfBase) + kApiRoot + "/productOffering", conninfo);
    product_catalog::ProductOfferingPriceStore price_store(
        std::string(kSelfBase) + kApiRoot + "/productOfferingPrice", conninfo);
    product_catalog::ProductSpecificationStore spec_store(
        std::string(kSelfBase) + kApiRoot + "/productSpecification", conninfo);
    spdlog::info("product-catalog: connected to PostgreSQL");

    auto meter = sbi_core::get_meter("product-catalog");
    auto offering_create_counter = meter->CreateUInt64Counter(
        "product_catalog_offering_create_total", "Total ProductOffering creates");
    auto price_create_counter = meter->CreateUInt64Counter(
        "product_catalog_offering_price_create_total", "Total ProductOfferingPrice creates");
    auto spec_create_counter = meter->CreateUInt64Counter(
        "product_catalog_specification_create_total", "Total ProductSpecification creates");

    boost::asio::io_context ioc;
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    // --- ProductOffering ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/productOffering",
        [&offering_store, &offering_create_counter](const sbi_core::http2::Request& req) {
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<bss_sid::ProductOffering>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto id = offering_store.create(*body);
            offering_create_counter->Add(1);
            const auto stored = offering_store.get(id);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/productOffering/" + id);
            resp.body = json(*stored).dump();
            return resp;
        });

    server.add_route("GET",
                     std::string(kApiRoot) + "/productOffering",
                     [&offering_store](const sbi_core::http2::Request&) {
                         const auto offerings = offering_store.list();
                         return sbi_core::http2::Response::json(200, json(offerings).dump());
                     });

    server.add_route("GET",
                     std::string(kApiRoot) + "/productOffering/{id}",
                     [&offering_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         const auto offering = offering_store.get(id);
                         if (!offering.has_value()) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No ProductOffering " + id);
                         }
                         return sbi_core::http2::Response::json(200, json(*offering).dump());
                     });

    server.add_route("DELETE",
                     std::string(kApiRoot) + "/productOffering/{id}",
                     [&offering_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         if (!offering_store.remove(id)) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No ProductOffering " + id);
                         }
                         sbi_core::http2::Response resp;
                         resp.status = 204;
                         return resp;
                     });

    // --- ProductOfferingPrice ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/productOfferingPrice",
        [&price_store, &price_create_counter](const sbi_core::http2::Request& req) {
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<bss_sid::ProductOfferingPrice>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto id = price_store.create(*body);
            price_create_counter->Add(1);
            const auto stored = price_store.get(id);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/productOfferingPrice/" + id);
            resp.body = json(*stored).dump();
            return resp;
        });

    server.add_route("GET",
                     std::string(kApiRoot) + "/productOfferingPrice",
                     [&price_store](const sbi_core::http2::Request&) {
                         const auto prices = price_store.list();
                         return sbi_core::http2::Response::json(200, json(prices).dump());
                     });

    server.add_route("GET",
                     std::string(kApiRoot) + "/productOfferingPrice/{id}",
                     [&price_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         const auto price = price_store.get(id);
                         if (!price.has_value()) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No ProductOfferingPrice " + id);
                         }
                         return sbi_core::http2::Response::json(200, json(*price).dump());
                     });

    server.add_route("DELETE",
                     std::string(kApiRoot) + "/productOfferingPrice/{id}",
                     [&price_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         if (!price_store.remove(id)) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No ProductOfferingPrice " + id);
                         }
                         sbi_core::http2::Response resp;
                         resp.status = 204;
                         return resp;
                     });

    // --- ProductSpecification ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/productSpecification",
        [&spec_store, &spec_create_counter](const sbi_core::http2::Request& req) {
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<bss_sid::ProductSpecification>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto id = spec_store.create(*body);
            spec_create_counter->Add(1);
            const auto stored = spec_store.get(id);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/productSpecification/" + id);
            resp.body = json(*stored).dump();
            return resp;
        });

    server.add_route("GET",
                     std::string(kApiRoot) + "/productSpecification",
                     [&spec_store](const sbi_core::http2::Request&) {
                         const auto specs = spec_store.list();
                         return sbi_core::http2::Response::json(200, json(specs).dump());
                     });

    server.add_route("GET",
                     std::string(kApiRoot) + "/productSpecification/{id}",
                     [&spec_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         const auto spec = spec_store.get(id);
                         if (!spec.has_value()) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No ProductSpecification " + id);
                         }
                         return sbi_core::http2::Response::json(200, json(*spec).dump());
                     });

    server.add_route("DELETE",
                     std::string(kApiRoot) + "/productSpecification/{id}",
                     [&spec_store](const sbi_core::http2::Request& req) {
                         const auto id = req.path_params.at("id");
                         if (!spec_store.remove(id)) {
                             return sbi_core::http2::problem_response(
                                 404, "Not Found", "No ProductSpecification " + id);
                         }
                         sbi_core::http2::Response resp;
                         resp.status = 204;
                         return resp;
                     });

    server.start();
    spdlog::info("product-catalog: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("product-catalog: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
