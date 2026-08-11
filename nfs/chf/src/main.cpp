// nfs/chf: CHF (Charging Function), Nchf_ConvergedCharging service (TS 32.291).
// Source: specs/5G_APIs-REL-19/TS32291_Nchf_ConvergedCharging.yaml
// (commit bca84b60a37773133bcae97e5c6c0d10a93b47b6). Phase 4's first NF (CLAUDE.md's charging
// domain).
//
// Scope, in three approved stages:
// - Stage 0/1 (ADR-0044): Nchf_ConvergedCharging_Create -- POST /chargingdata. Real request
//   parsing, real ChargingDataRef allocation, a schema-valid ChargingDataResponse. Also builds and
//   logs a TM Forum SID Individual for the subscriber (docs/CHARGING_MAPPING.md, ADR-0045).
// - ADR-0046: Nchf_ConvergedCharging_Release -- POST /chargingdata/{ChargingDataRef}/
//   release. Validates the ref is a real, still-active one (404 if not), returns 204 per spec.
// - This turn (ADR-0048): a real rating engine. CHF is now a real HTTP client of
//   bss/product-catalog (ADR-0047) -- when a request's multipleUnitUsage carries a ratingGroup,
//   CHF fetches the first Active/isSellable ProductOffering's first referenced
//   ProductOfferingPrice, converts its unitOfMeasure into a real GrantedUnit, and returns it in
//   multipleUnitInformation. See build_rating_grant's own comment for the real conversion/
//   simplification details.
//
// - ADR-0050 Stage 4: Nchf_ConvergedCharging_Update -- POST /chargingdata/{ChargingDataRef}/
//   update. Validates the ref is still active (404 if not, same convention as Release), logs the
//   real reported usage (multipleUnitUsage[].usedUnitContainer[]) SMF's Stage 3 code now genuinely
//   sends, and re-authorizes: issues a fresh GrantedUnit from the same catalog-lookup rating engine
//   Create already uses. Disclosed, real simplifications, not silently different from a real OCS:
//   no balance/wallet deduction against what was already consumed (no such store exists yet, see
//   docs/CHARGING_MAPPING.md's TMF654 gap note); does not differentiate a Volume-Threshold report
//   from a Volume-Quota-exhaustion report (SMF's own Stage 3 code doesn't yet forward that
//   distinction as a real Trigger in the request body either -- a real, disclosed gap on the SMF
//   side, not fixed by this stage).
//
// - P4.2 (CHARGING_PROMPT.md/ADR-0055), starting this turn: two real 3GPP-defined sibling
//   services, hosted under this same CHF binary --
//     * Nchf_OfflineOnlyCharging (TS 32.291): Create/Update/Release, mirroring
//       ConvergedCharging's own shape but with NO rating-engine call (its ChargingDataResponse
//       schema carries no multipleUnitInformation/grantedUnit field at all -- confirmed directly
//       against the vendored YAML, not assumed).
//     * Nchf_SpendingLimitControl (TS 29.594): Subscribe/Update/Unsubscribe
//       (POST/PUT/DELETE /subscriptions) -- CHF is the real SERVER here (PCF subscribes TO CHF,
//       confirmed directly against the YAML, correcting an initial assumption that this would be
//       a CHF-as-PCF-client integration). The real statusNotification/subscriptionTermination
//       callbacks (CHF as client, POSTing to the subscriber's notifUri) are NOT implemented --
//       no real policy-counter-breach-detection engine exists yet to trigger them from, same
//       deferred-not-dropped category as ConvergedCharging's own chargingNotification below.
//   Real, disclosed rename that came with adding OfflineOnlyCharging's YAML: it independently
//   defines its own `ChargingDataRequest`/`ChargingDataResponse`/`MultipleUnitUsage`/
//   `UsedUnitContainer`/`NFIdentification`/`NodeFunctionality` schema names, colliding with
//   ConvergedCharging's own (two real, independent 3GPP services that happen to reuse type names
//   for different shapes) -- sbi-codegen's existing collision-disambiguation (ADR-0010) suffixed
//   BOTH sides with their source service name, so every reference in this file and
//   nfs/smf/src/main.cpp changed from e.g. `sbi_gen::ChargingDataRequest` to
//   `sbi_gen::ChargingDataRequest_Nchf_ConvergedCharging`. Mechanical, verified (full rebuild +
//   146/146 tests, including the real SMF<->CHF integration test), not a functional change.
//
// Deliberately still deferred, not dropped: the Nchf_ConvergedCharging chargingNotification
// callback (real trigger condition -- e.g. server-initiated reauthorization -- doesn't exist yet),
// Nchf_SpendingLimitControl's own notify/terminate callbacks (same reason), and N41/N42 (AMF)
// wiring (AMF has no real UE Registration procedure in this codebase yet to trigger a charging
// call from -- no NGAP/NAS stack exists; a separate, much larger plan for that is drafted but not
// started). See docs/DECISIONS.md ADR-0044/ADR-0046/ADR-0048/ADR-0050/ADR-0055.
//
// Disclosed simplifications, stated up front:
// - No real subscriber-to-product assignment: the rating engine grants from whichever
//   ProductOffering happens to be first (Active+isSellable) in the catalog, not a real per-
//   subscriber rate plan lookup (no customer/subscription store exists) -- same category of
//   simplification as PCF's fixed-default policy, ADR-0028. If the catalog has no matching
//   offering (e.g. nothing seeded yet), Create still succeeds with an empty grant, same as before
//   this turn -- schema-valid, not a real charging decision, disclosed not hidden.
// - ChargingDataResponse's invocationSequenceNumber is set by echoing the request's own value.
//   TS32291_Nchf_ConvergedCharging.yaml carries no per-field description text distinguishing
//   "echo the request's sequence" from "CHF assigns its own independent sequence" for this field,
//   and no normative TS 32.291 prose is vendored in this repo to check -- echoing the request's
//   value is the least-invented choice (no independent CHF-side sequencing semantics assumed) and
//   disclosed here rather than picked silently.
// - Real Redis/Valkey persistence (CHARGING_PROMPT.md entity E3's "recoverable across restarts"
//   requirement, docs/DATA_MODEL.md's own persistence assignment). `OfflineChargingDataStore`
//   still only tracks active-ref existence (real, disclosed, unchanged gap: no genuine offline-
//   session content is stored). `ChargingDataStore` now DOES hold real content (P4.3/ADR-0057):
//   the session's SUPI and running reserved-balance total, both needed for the real ABMF
//   integration below -- see stores.hpp's own header comment.
// - P4.3 (ADR-0056/0057): CHF's rating engine now makes real Nchf-ConvergedCharging grants
//   contingent on a real balance reservation against bss/balance-management (ADR-0056) --
//   closing the "no balance/wallet deduction against what was already consumed" gap disclosed
//   since ADR-0048/0050. Create/Update reserve the granted price's real monetary cost against a
//   Bucket keyed by the request's SUPI (this project's own disclosed convention -- no real
//   customer-to-bucket provisioning system exists); a grant is only included in the response if
//   the reservation succeeds. Release finalizes the session's full reserved total as a real
//   permanent debit and unreserves the same amount. Disclosed, real simplification: finalization
//   is for the FULL session total, not proportional to SMF's actually-reported usage
//   (usedUnitContainer) -- a real per-usage proportional refund is deferred, not fabricated as
//   more sophisticated than it is. See build_rating_grant/reserve_subscriber_balance/
//   finalize_subscriber_balance's own comments for the full reasoning.

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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>

#include "TS29122_CommonData_grp.hpp"
#include "TS29594_Nchf_SpendingLimitControl.hpp"
#include "TS32291_Nchf_OfflineOnlyCharging.hpp"
#include "bss_sid/balance.hpp"
#include "bss_sid/party.hpp"
#include "bss_sid/product.hpp"
#include "cdr.hpp"
#include "diameter_core/header.hpp"
#include "diameter_server.hpp"
#include "stores.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/chf/CMakeLists.txt)"
#endif

constexpr unsigned short kPort = 7784;
// P4.5/ADR-0059 Stage 2: real Diameter (Gy) listener, real IANA-assigned port (RFC 6733 §2.1,
// diameter_core::kDiameterTcpPort). Lab-internal identity, disclosed -- no real registered DNS
// realm/enterprise number, matching the same per-NF naming convention already used for TLS cert
// CNs (scripts/gen-lab-pki.sh).
constexpr unsigned short kDiameterPort = diameter_core::kDiameterTcpPort;
constexpr const char* kDiameterOriginHost = "chf.5gc-r19.local";
constexpr const char* kDiameterOriginRealm = "5gc-r19.local";
constexpr const char* kMetricsBindAddress = "0.0.0.0:9472";
constexpr const char* kNfType = "CHF";
constexpr const char* kNrfBase = "https://127.0.0.1:7777";
constexpr const char* kApiRoot = "/nchf-convergedcharging/v3";
// Real basePath confirmed directly from TS32291_Nchf_OfflineOnlyCharging.yaml's own `servers`
// block (ADR-0055) -- P4.2.
constexpr const char* kOfflineApiRoot = "/nchf-offlineonlycharging/v1";
// Real basePath confirmed directly from TS29594_Nchf_SpendingLimitControl.yaml's own `servers`
// block (ADR-0055) -- P4.2.
constexpr const char* kSpendingLimitApiRoot = "/nchf-spendinglimitcontrol/v1";
constexpr const char* kProductCatalogBase = "https://127.0.0.1:7785";
constexpr const char* kProductCatalogApiRoot = "/tmf-api/productCatalogManagement/v4";
// P4.3 (ADR-0056/0057): CHF's real client to bss/balance-management (ABMF).
constexpr const char* kBalanceManagementBase = "https://127.0.0.1:7786";
constexpr const char* kBalanceManagementApiRoot = "/tmf-api/prepayBalanceManagement/v4";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// Redis/Valkey connection string for CHF's stores (E3 persistence, see stores.hpp) -- same
// getenv-based-config precedent bss/product-catalog's PRODUCT_CATALOG_DATABASE_URL already
// established (ADR-0054) for exactly this "never hardcode a connection string" reason. Default
// matches this project's lab/dev convention.
std::string chf_redis_conninfo() {
    if (const char* env = std::getenv("CHF_REDIS_URL")) {
        return env;
    }
    return "tcp://127.0.0.1:6379";
}

// P4.4/ADR-0058: real ClickHouse connection options for CdrWriter -- same never-hardcode-
// credentials, getenv-based-config precedent as chf_redis_conninfo above. Defaults match this
// project's lab/dev convention (a real ClickHouse instance's default user has no password).
clickhouse::ClientOptions chf_clickhouse_options() {
    clickhouse::ClientOptions options;
    options.SetHost(std::getenv("CHF_CLICKHOUSE_HOST") ? std::getenv("CHF_CLICKHOUSE_HOST")
                                                       : "127.0.0.1");
    options.SetPort(std::getenv("CHF_CLICKHOUSE_PORT")
                        ? static_cast<std::uint16_t>(std::stoi(std::getenv("CHF_CLICKHOUSE_PORT")))
                        : 9000);
    options.SetUser(std::getenv("CHF_CLICKHOUSE_USER") ? std::getenv("CHF_CLICKHOUSE_USER")
                                                       : "default");
    options.SetPassword(
        std::getenv("CHF_CLICKHOUSE_PASSWORD") ? std::getenv("CHF_CLICKHOUSE_PASSWORD") : "");
    options.SetDefaultDatabase(std::getenv("CHF_CLICKHOUSE_DATABASE")
                                   ? std::getenv("CHF_CLICKHOUSE_DATABASE")
                                   : "default");
    return options;
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

// Real rating engine (ADR-0048), CHF's side. Fetches the first Active/isSellable ProductOffering
// from bss/product-catalog (ADR-0047), then its first referenced ProductOfferingPrice, and
// converts that price's unitOfMeasure into a real GrantedUnit -- CHF's actual charging decision,
// not a fabricated placeholder. No OAuth2 token needed: product-catalog is mTLS-only (ADR-0047),
// same trust boundary the client cert already provides. Returns nullopt if the catalog is
// unreachable or has no matching offering (schema-valid empty grant, same fallback this build has
// always had, see file header).
//
// Unit conversion is real but deliberately narrow: TS 32.291's GrantedUnit has no generic "amount
// + unit string" field the way TMF620's Quantity does -- totalVolume/uplinkVolume/downlinkVolume
// are raw octet counts (TS 32.298's own CDR volume fields are octet-counted, confirmed by their
// Uint64 typing with no separate unit field), time is raw seconds. Only "GB"/"MB" (decimal,
// matching 3GPP's own octet-counting convention, not binary GiB/MiB) convert to totalVolume; any
// other unit string falls back to serviceSpecificUnits carrying the raw amount unconverted --
// disclosed as a real but narrow conversion, not a general unit-aware rating engine.

// Nchf_SpendingLimitControl (TS 29.594, P4.2/ADR-0055): builds the real SpendingLimitStatus both
// Subscribe (201) and Update (200) return, per the real confirmed schema. `statusInfos` is a
// map<policyCounterId, PolicyCounterInfo> -- the generated type falls back to opaque
// nlohmann::json for it (additionalProperties-only schema, no fixed key set), so it's built by
// hand here rather than through a generated struct.
//
// Disclosed, real simplification: `currentStatus` is a fixed placeholder ("unknown") for every
// policy counter -- no real policy-counter engine exists in this codebase to report a genuine
// status from (same category of gap as ADR-0028's PCF fixed-default policy, or this same file's
// own rating-engine "whichever catalog offering is first" simplification). The real spec text
// itself says the status values "are not specified... out of scope of 3GPP", so any string is
// schema-conformant; "unknown" is the least-invented choice, not a guess at real semantics.
sbi_gen::SpendingLimitStatus
build_spending_limit_status(const sbi_gen::SpendingLimitContext& context) {
    sbi_gen::SpendingLimitStatus status{};
    status.supi = context.supi;
    status.notifId = context.notifId;
    status.expiry = context.expiry;
    status.supportedFeatures = context.supportedFeatures;

    json status_infos = json::object();
    if (context.policyCounterIds.has_value()) {
        for (const auto& counter_id : *context.policyCounterIds) {
            sbi_gen::PolicyCounterInfo info{};
            info.policyCounterId = counter_id;
            info.currentStatus = "unknown";
            status_infos[counter_id] = info;
        }
    }
    status.statusInfos = status_infos;
    return status;
}

// P4.3 (ADR-0057): a rating decision is now a real (GrantedUnit, cost) pair -- the quantity of
// service granted (e.g. 5GB) AND its real monetary cost (e.g. $20), confirmed as two genuinely
// separate real TMF620 fields on ProductOfferingPrice (`unitOfMeasure` vs `price`), not something
// this project invented a split for. `cost` is nullopt when the matched price has no `price`
// field set (a real, valid TMF620 state -- not every price is monetary), in which case no real
// balance reservation is attempted for this grant (same as before this ADR).
struct RatingResult {
    std::optional<sbi_gen::GrantedUnit> grant;
    std::optional<bss_sid::Money> cost;
};

RatingResult build_rating_grant(sbi_core::http2::Client& catalog_client) {
    sbi_core::http2::ClientRequest offerings_req;
    offerings_req.method = "GET";
    offerings_req.url =
        std::string(kProductCatalogBase) + kProductCatalogApiRoot + "/productOffering";
    auto offerings_resp = catalog_client.send(offerings_req);
    if (!offerings_resp.has_value() || offerings_resp->status != 200) {
        spdlog::warn("chf: could not reach bss/product-catalog for rating, granting nothing");
        return {};
    }

    std::vector<bss_sid::ProductOffering> offerings;
    try {
        offerings = json::parse(offerings_resp->body).get<std::vector<bss_sid::ProductOffering>>();
    } catch (const json::exception& e) {
        spdlog::warn("chf: malformed ProductOffering list from product-catalog: {}", e.what());
        return {};
    }

    const auto offering_it = std::find_if(offerings.begin(), offerings.end(), [](const auto& o) {
        return o.isSellable.value_or(false) && o.lifecycleStatus.value_or("") == "Active" &&
               !o.productOfferingPrice.empty();
    });
    if (offering_it == offerings.end()) {
        spdlog::info("chf: no Active/isSellable ProductOffering with a price found, granting "
                     "nothing this call");
        return {};
    }

    sbi_core::http2::ClientRequest price_req;
    price_req.method = "GET";
    price_req.url = std::string(kProductCatalogBase) + kProductCatalogApiRoot +
                    "/productOfferingPrice/" + offering_it->productOfferingPrice.front().id;
    auto price_resp = catalog_client.send(price_req);
    if (!price_resp.has_value() || price_resp->status != 200) {
        spdlog::warn("chf: could not fetch ProductOfferingPrice {}, granting nothing",
                     offering_it->productOfferingPrice.front().id);
        return {};
    }

    bss_sid::ProductOfferingPrice price;
    try {
        price = json::parse(price_resp->body).get<bss_sid::ProductOfferingPrice>();
    } catch (const json::exception& e) {
        spdlog::warn("chf: malformed ProductOfferingPrice from product-catalog: {}", e.what());
        return {};
    }
    if (!price.unitOfMeasure.has_value() || !price.unitOfMeasure->amount.has_value()) {
        spdlog::info("chf: ProductOfferingPrice {} has no unitOfMeasure, granting nothing",
                     *price.id);
        return {};
    }

    sbi_gen::GrantedUnit grant{};
    const auto amount = *price.unitOfMeasure->amount;
    const auto units = price.unitOfMeasure->units.value_or("");
    if (units == "GB") {
        grant.totalVolume = static_cast<std::uint64_t>(amount * 1'000'000'000.0);
    } else if (units == "MB") {
        grant.totalVolume = static_cast<std::uint64_t>(amount * 1'000'000.0);
    } else {
        grant.serviceSpecificUnits = static_cast<std::uint64_t>(amount);
    }
    spdlog::info("chf: rating engine granted {} from ProductOffering '{}' / ProductOfferingPrice "
                 "'{}'",
                 units == "GB" || units == "MB"
                     ? std::to_string(*grant.totalVolume) + " octets"
                     : std::to_string(*grant.serviceSpecificUnits) + " service-specific units",
                 offering_it->name.value_or(""),
                 price.name.value_or(""));
    return {grant, price.price};
}

// P4.3 (ADR-0057): CHF as a real HTTP client of bss/balance-management (ADR-0056) -- reserves
// `cost` against the real per-subscriber Bucket keyed by SUPI (this project's own disclosed
// convention: no real customer-to-bucket provisioning system exists, see
// bss/balance-management's own file header). Returns false if the reservation was rejected
// (insufficient balance, or no bucket exists yet for this SUPI -- same real business outcome
// either way) -- callers must not grant units when this returns false, the real prepaid-
// enforcement point this ADR closes (ADR-0048/0050's own disclosed "no balance/wallet deduction"
// gap).
bool reserve_subscriber_balance(sbi_core::http2::Client& balance_client,
                                const std::string& supi,
                                const bss_sid::Money& cost,
                                const std::string& description) {
    if (!cost.value.has_value() || *cost.value <= 0.0) {
        // A real, valid TMF620 state (price with no monetary value, or a zero-cost promotional
        // price) -- nothing to reserve, not a failure.
        return true;
    }

    bss_sid::ReserveBalance reserve_req{};
    bss_sid::Quantity amount{};
    amount.amount = *cost.value;
    amount.units = cost.unit;
    reserve_req.amount = amount;
    bss_sid::BucketRef bucket{};
    bucket.id = supi;
    reserve_req.bucket = bucket;
    reserve_req.description = description;
    reserve_req.usageType = "monetary";

    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = std::string(kBalanceManagementBase) + kBalanceManagementApiRoot + "/reserveBalance";
    req.headers.emplace("content-type", "application/json");
    req.body = json(reserve_req).dump();

    auto resp = balance_client.send(req);
    if (!resp.has_value() || resp->status != 201) {
        spdlog::warn("chf: reserveBalance call to bss/balance-management failed (SUPI={})", supi);
        return false;
    }
    try {
        const auto result = json::parse(resp->body).get<bss_sid::ReserveBalance>();
        return result.status.value_or("") == "completed";
    } catch (const json::exception& e) {
        spdlog::warn("chf: malformed ReserveBalance response: {}", e.what());
        return false;
    }
}

// P4.3 (ADR-0057): Release-time finalization -- converts everything reserved for this session
// (ChargingDataStore::get_reserved_total) into a real, permanent debit. Disclosed simplification:
// this finalizes the FULL reserved total, not a proportional amount based on SMF's actually-
// reported usage (usedUnitContainer) -- a real per-usage proportional refund is deferred, not
// fabricated as more sophisticated than it is (see this file's own header).
//
// Real bug found and fixed via live verification, not caught by unit-level reasoning alone:
// bss/balance-management's `ReserveBalance` (positive amount) moves money remainingValue ->
// reservedValue; `AdjustBalance` only ever touches remainingValue directly and its own atomic
// floor check (`remaining_value + amount >= 0`) has no visibility into reservedValue at all.
// Neither operation alone can "commit" a reservation (decrease reservedValue permanently WITHOUT
// crediting remainingValue back first) -- TMF654's own modeled resource set (this project's
// scope, see bss_sid/balance.hpp) has no dedicated "commit" action. The correct way to compose
// the two real primitives into a net "commit" is ORDER-DEPENDENT: unreserve FIRST (ReserveBalance,
// negative amount -- returns the money to remainingValue, always succeeds since exactly this much
// is known to be reserved), THEN debit (AdjustBalance, negative amount -- now succeeds, since the
// money is back in remainingValue) -- net effect: reservedValue permanently decreases by the full
// amount, remainingValue is unchanged (temporarily credited then immediately re-debited the same
// amount). Doing this in the OPPOSITE order (debit before unreserve, this function's first,
// buggy version) makes the debit's own atomic floor check fail (the money is still sitting in
// reservedValue, not yet in remainingValue), so only the unreserve half succeeds -- silently
// refunding the entire reservation instead of committing it. Caught by this ADR's own real
// end-to-end live verification (a real $50 topup, $40 total reserved across Create+Update, then
// Release incorrectly returned the bucket to $50/$0 instead of the correct $10/$0) -- exactly the
// kind of bug this project's "live-verify over self-consistency" discipline exists to catch.
void finalize_subscriber_balance(sbi_core::http2::Client& balance_client,
                                 const std::string& supi,
                                 double total_reserved,
                                 const std::string& description) {
    if (total_reserved <= 0.0) {
        return;
    }

    bss_sid::BucketRef bucket{};
    bucket.id = supi;

    bss_sid::ReserveBalance unreserve_req{};
    bss_sid::Quantity unreserve_amount{};
    unreserve_amount.amount = -total_reserved;
    unreserve_req.amount = unreserve_amount;
    unreserve_req.bucket = bucket;
    unreserve_req.description = description + " (unreserve before commit)";
    unreserve_req.usageType = "monetary";

    sbi_core::http2::ClientRequest unreserve_http_req;
    unreserve_http_req.method = "POST";
    unreserve_http_req.url =
        std::string(kBalanceManagementBase) + kBalanceManagementApiRoot + "/reserveBalance";
    unreserve_http_req.headers.emplace("content-type", "application/json");
    unreserve_http_req.body = json(unreserve_req).dump();
    auto unreserve_resp = balance_client.send(unreserve_http_req);
    if (!unreserve_resp.has_value() || unreserve_resp->status != 201) {
        spdlog::warn("chf: finalize unreserve call failed (SUPI={})", supi);
        return;
    }

    bss_sid::AdjustBalance adjust_req{};
    bss_sid::Quantity debit{};
    debit.amount = -total_reserved;
    adjust_req.amount = debit;
    adjust_req.bucket = bucket;
    adjust_req.adjustType = "oneTime";
    adjust_req.description = description + " (commit)";
    adjust_req.usageType = "monetary";

    sbi_core::http2::ClientRequest adjust_http_req;
    adjust_http_req.method = "POST";
    adjust_http_req.url =
        std::string(kBalanceManagementBase) + kBalanceManagementApiRoot + "/adjustBalance";
    adjust_http_req.headers.emplace("content-type", "application/json");
    adjust_http_req.body = json(adjust_req).dump();
    auto adjust_resp = balance_client.send(adjust_http_req);
    if (!adjust_resp.has_value() || adjust_resp->status != 201) {
        // Real, disclosed gap: the unreserve above already succeeded, so if this commit debit
        // fails (e.g. balance-management becomes unreachable mid-finalize), the money is left
        // sitting back in remainingValue, uncommitted -- not a lost-money bug (the ledger still
        // has both real action records to reconcile from), but not a fully atomic two-call
        // sequence either. A real distributed-transaction/outbox mechanism would close this;
        // not built here, disclosed rather than silently assumed safe.
        spdlog::warn("chf: finalize commit AdjustBalance call failed (SUPI={})", supi);
    }
}

// P4.4/ADR-0058: writes one real CDR row (chf::CdrRecord, see cdr.hpp) per MultipleUnitUsage
// entry -- every field here is either a real TS 32.291 value already flowing through this
// handler, or a real ADR-0057 rating-engine output (grant/cost), not fabricated. Shared between
// the Create and Update route handlers below to avoid duplicating this construction twice.
void write_converged_charging_cdr(chf::CdrWriter& cdr_writer,
                                  const std::string& ref,
                                  const std::string& operation,
                                  const std::string& supi,
                                  const std::string& node_functionality,
                                  std::int64_t invocation_sequence_number,
                                  const sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging& usage,
                                  const RatingResult& rating,
                                  bool reserved) {
    chf::CdrRecord cdr{};
    cdr.charging_data_ref = ref;
    cdr.invocation_sequence_number = invocation_sequence_number;
    cdr.service_type = "ConvergedCharging";
    cdr.operation = operation;
    cdr.subscriber_identifier = supi;
    cdr.nf_consumer_node_functionality = node_functionality;
    cdr.rating_group = static_cast<std::int64_t>(usage.ratingGroup);
    if (reserved && rating.grant.has_value()) {
        cdr.granted_total_volume = rating.grant->totalVolume;
        cdr.granted_service_specific_units = rating.grant->serviceSpecificUnits;
    }
    if (usage.usedUnitContainer.has_value() && !usage.usedUnitContainer->empty()) {
        cdr.used_total_volume = usage.usedUnitContainer->front().totalVolume;
    }
    if (reserved && rating.cost.has_value()) {
        cdr.reserved_cost = rating.cost->value;
        cdr.reserved_cost_currency = rating.cost->unit;
    }
    cdr.invocation_time_stamp =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    try {
        cdr_writer.write(cdr);
    } catch (const std::exception& e) {
        // Real, disclosed gap: a ClickHouse write failure does not block or fail the real
        // charging response CHF already committed to (the balance reservation above already
        // happened) -- CDR generation is best-effort in this build, matching this project's
        // existing "no real distributed-transaction guarantee across CHF's several real
        // dependencies" disclosure (see finalize_subscriber_balance's own comment for the
        // balance-management analogue).
        spdlog::warn(
            "chf: CDR write to ClickHouse failed for ChargingDataRef={}: {}", ref, e.what());
    }
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

    // Real Redis/Valkey persistence for CHF's stores (E3's "recoverable across restarts" -- see
    // stores.hpp's own header comment). One shared client: sw::redis::Redis pools connections
    // internally and is genuinely thread-safe, confirmed by reading its own header, not the
    // per-store single-connection-behind-a-mutex pattern bss/product-catalog uses for libpqxx
    // (ADR-0054), since libpqxx::connection has no such built-in pooling.
    auto redis = std::make_shared<sw::redis::Redis>(chf_redis_conninfo());
    // sw::redis::Redis's connection pool connects lazily on first command (confirmed: pool size
    // defaults to 1, no eager-connect option used here) -- a real PING here, not assumed
    // connectivity, gives the same fail-fast-at-startup behavior every other NF's real dependency
    // check already has (e.g. bss/product-catalog's libpqxx::connection, which throws immediately
    // in its own constructor if unreachable).
    redis->ping();
    spdlog::info("chf: connected to Redis/Valkey");
    chf::ChargingDataStore charging_data_store(redis);
    chf::OfflineChargingDataStore offline_charging_data_store(redis);
    chf::SpendingLimitSubscriptionStore spending_limit_store(redis);

    // P4.4/ADR-0058: real CDF (CDR generation, TS 32.240/32.296) -- see cdr.hpp's own header for
    // the full disclosure of what this real CDR record is (and is not: not a conformant TS 32.298
    // CDR, that spec isn't vendored -- schema.clickhouse.sql explains why).
    chf::CdrWriter cdr_writer(chf_clickhouse_options());
    if (cdr_writer.is_connected()) {
        spdlog::info("chf: connected to ClickHouse (CDF)");
    } else {
        spdlog::warn("chf: ClickHouse unavailable, CDF/CDR generation disabled for this process");
    }

    // P4.5/ADR-0059 Stage 2: real Diameter server (CER/CEA capability exchange only -- Stage 3
    // adds real CCR/CCA Gy handling on the same listener, per the ADR's staged plan).
    chf::DiameterServer diameter_server(kDiameterPort, kDiameterOriginHost, kDiameterOriginRealm);
    spdlog::info("chf: Diameter (Gy) listening on tcp://0.0.0.0:{}", kDiameterPort);

    // CHF's own client to bss/product-catalog (ADR-0048) -- mTLS only, no OAuth2 (product-catalog
    // has no NRF-issued token source, see ADR-0047). Only ever touched from route handlers, which
    // all run on ioc's single thread -- same "second client safe on the shared ioc thread" pattern
    // ADR-0027 established.
    sbi_core::http2::TlsConfig catalog_client_tls{
        .cert_path = CERTS_DIR "/chf/cert.pem",
        .key_path = CERTS_DIR "/chf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client catalog_client(std::move(catalog_client_tls));

    // P4.3 (ADR-0056/0057): CHF's own client to bss/balance-management -- same mTLS-only, no-OAuth2
    // reasoning as catalog_client above (balance-management, like product-catalog, has no
    // NRF-issued token source).
    sbi_core::http2::TlsConfig balance_client_tls{
        .cert_path = CERTS_DIR "/chf/cert.pem",
        .key_path = CERTS_DIR "/chf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client balance_client(std::move(balance_client_tls));

    auto meter = sbi_core::get_meter("chf");
    auto create_counter = meter->CreateUInt64Counter("chf_charging_data_create_total",
                                                     "Total Nchf_ConvergedCharging_Create calls");
    auto release_counter = meter->CreateUInt64Counter("chf_charging_data_release_total",
                                                      "Total Nchf_ConvergedCharging_Release calls");
    auto update_counter = meter->CreateUInt64Counter("chf_charging_data_update_total",
                                                     "Total Nchf_ConvergedCharging_Update calls");
    auto grant_counter = meter->CreateUInt64Counter(
        "chf_rating_grant_total", "Total real GrantedUnit rating decisions issued");
    auto reserve_rejected_counter = meter->CreateUInt64Counter(
        "chf_balance_reserve_rejected_total",
        "Total grants withheld because the real balance reservation was rejected (P4.3/ADR-0057)");
    auto offline_create_counter = meter->CreateUInt64Counter(
        "chf_offline_charging_data_create_total", "Total Nchf_OfflineOnlyCharging_Create calls");
    auto offline_update_counter = meter->CreateUInt64Counter(
        "chf_offline_charging_data_update_total", "Total Nchf_OfflineOnlyCharging_Update calls");
    auto offline_release_counter = meter->CreateUInt64Counter(
        "chf_offline_charging_data_release_total", "Total Nchf_OfflineOnlyCharging_Release calls");
    auto spending_limit_subscribe_counter = meter->CreateUInt64Counter(
        "chf_spending_limit_subscribe_total", "Total Nchf_SpendingLimitControl Subscribe calls");
    auto spending_limit_update_counter = meter->CreateUInt64Counter(
        "chf_spending_limit_update_total", "Total Nchf_SpendingLimitControl subscription updates");
    auto spending_limit_unsubscribe_counter =
        meter->CreateUInt64Counter("chf_spending_limit_unsubscribe_total",
                                   "Total Nchf_SpendingLimitControl Unsubscribe calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", kPort, server_tls);

    // --- Nchf_ConvergedCharging ---

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/chargingdata",
        [&verifier,
         &charging_data_store,
         &create_counter,
         &catalog_client,
         &balance_client,
         &grant_counter,
         &reserve_rejected_counter,
         &cdr_writer](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<
                sbi_gen::ChargingDataRequest_Nchf_ConvergedCharging>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto supi = body->subscriberIdentifier.value_or("");
            const auto ref = charging_data_store.create(supi);

            // docs/CHARGING_MAPPING.md's resolved mapping: build the TM Forum SID record for the
            // subscriber this charging event is for. Logged, not yet persisted or exposed via a
            // real TMF632 REST surface (no Party-management store exists in this codebase yet) --
            // this demonstrates CHF's internal charging record is genuinely SID-shaped, which is
            // as much of the mapping as has a real, unambiguous 3GPP field to build it from today
            // (see the mapping doc's own scope section for why every other field is deferred).
            if (body->subscriberIdentifier.has_value()) {
                const auto individual =
                    bss_sid::map_supi_to_individual(*body->subscriberIdentifier);
                spdlog::info("chf: mapped subscriberIdentifier to TM Forum SID Individual: {}",
                             nlohmann::json(individual).dump());
            }

            sbi_gen::ChargingDataResponse_Nchf_ConvergedCharging response{};
            response.invocationTimeStamp =
                sbi_core::format_rfc3339(std::chrono::system_clock::now());
            // See file header for why this echoes the request's value rather than assigning an
            // independent CHF-side sequence.
            response.invocationSequenceNumber = body->invocationSequenceNumber;

            // ADR-0048/ADR-0057: the real rating engine. Only runs if the request actually asked
            // for units (a real MultipleUnitUsage entry, mandatory ratingGroup) -- SMF's own call
            // always sends exactly one (see nfs/smf/src/main.cpp), but this handler doesn't
            // assume that, it reads what's actually there. ADR-0057: a grant is only actually
            // included in the response if its real monetary cost was successfully reserved
            // against the subscriber's real balance (bss/balance-management) -- real prepaid
            // enforcement, closing this project's long-disclosed "no balance/wallet deduction"
            // gap.
            if (body->multipleUnitUsage.has_value()) {
                std::vector<sbi_gen::MultipleUnitInformation> granted;
                for (const auto& usage : *body->multipleUnitUsage) {
                    sbi_gen::MultipleUnitInformation info{};
                    info.ratingGroup = usage.ratingGroup;
                    const auto rating = build_rating_grant(catalog_client);
                    bool reserved = true;
                    if (rating.cost.has_value() && !supi.empty()) {
                        reserved =
                            reserve_subscriber_balance(balance_client,
                                                       supi,
                                                       *rating.cost,
                                                       "Nchf_ConvergedCharging_Create " + ref);
                        if (reserved) {
                            charging_data_store.add_reserved(ref, *rating.cost->value);
                        } else {
                            reserve_rejected_counter->Add(1);
                        }
                    }
                    if (reserved && rating.grant.has_value()) {
                        info.grantedUnit = rating.grant;
                        grant_counter->Add(1);
                    }
                    write_converged_charging_cdr(
                        cdr_writer,
                        ref,
                        "Create",
                        supi,
                        body->nfConsumerIdentification.nodeFunctionality.value,
                        body->invocationSequenceNumber,
                        usage,
                        rating,
                        reserved);
                    granted.push_back(info);
                }
                response.multipleUnitInformation = std::move(granted);
            }

            create_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", std::string(kApiRoot) + "/chargingdata/" + ref);
            json j = response;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/chargingdata/{ChargingDataRef}/update",
        [&verifier,
         &charging_data_store,
         &update_counter,
         &catalog_client,
         &balance_client,
         &grant_counter,
         &reserve_rejected_counter,
         &cdr_writer](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<
                sbi_gen::ChargingDataRequest_Nchf_ConvergedCharging>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto ref = req.path_params.at("ChargingDataRef");
            if (!charging_data_store.is_active(ref)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No active charging data resource " + ref);
            }
            const auto supi = charging_data_store.get_supi(ref).value_or("");

            // ADR-0050 Stage 4: log the real reported usage this call carries -- SMF's Stage 3
            // Nchf_ConvergedCharging_Update, itself built from UPF's real Stage 2 Usage Report.
            // This is CHF's real evidence that consumption tracking closed the loop, not a
            // fabricated placeholder.
            if (body->multipleUnitUsage.has_value()) {
                for (const auto& usage : *body->multipleUnitUsage) {
                    if (usage.usedUnitContainer.has_value()) {
                        for (const auto& used : *usage.usedUnitContainer) {
                            spdlog::info(
                                "chf: Update for ChargingDataRef={} reports ratingGroup={} "
                                "used {} octets (localSequenceNumber={})",
                                ref,
                                usage.ratingGroup,
                                used.totalVolume.value_or(0),
                                used.localSequenceNumber);
                        }
                    }
                }
            }

            sbi_gen::ChargingDataResponse_Nchf_ConvergedCharging response{};
            response.invocationTimeStamp =
                sbi_core::format_rfc3339(std::chrono::system_clock::now());
            response.invocationSequenceNumber = body->invocationSequenceNumber;

            // Real re-authorization: a fresh grant for continued usage, from the same rating
            // engine Create already uses -- ADR-0057: a real reservation against the subscriber's
            // balance is attempted for each fresh grant too, same prepaid-enforcement point as
            // Create. Still disclosed, real simplification carried forward: no differentiation
            // between a Volume-Threshold report and a Volume-Quota-exhaustion one (see this
            // file's own header comment for why), and no proportional finalize against what was
            // actually reported used this call (see finalize_subscriber_balance's own comment --
            // Release finalizes the full session total, not incrementally per Update).
            if (body->multipleUnitUsage.has_value()) {
                std::vector<sbi_gen::MultipleUnitInformation> granted;
                for (const auto& usage : *body->multipleUnitUsage) {
                    sbi_gen::MultipleUnitInformation info{};
                    info.ratingGroup = usage.ratingGroup;
                    const auto rating = build_rating_grant(catalog_client);
                    bool reserved = true;
                    if (rating.cost.has_value() && !supi.empty()) {
                        reserved =
                            reserve_subscriber_balance(balance_client,
                                                       supi,
                                                       *rating.cost,
                                                       "Nchf_ConvergedCharging_Update " + ref);
                        if (reserved) {
                            charging_data_store.add_reserved(ref, *rating.cost->value);
                        } else {
                            reserve_rejected_counter->Add(1);
                        }
                    }
                    if (reserved && rating.grant.has_value()) {
                        info.grantedUnit = rating.grant;
                        grant_counter->Add(1);
                    }
                    write_converged_charging_cdr(
                        cdr_writer,
                        ref,
                        "Update",
                        supi,
                        body->nfConsumerIdentification.nodeFunctionality.value,
                        body->invocationSequenceNumber,
                        usage,
                        rating,
                        reserved);
                    granted.push_back(info);
                }
                response.multipleUnitInformation = std::move(granted);
            }

            // CHARGING_PROMPT.md's own explicit P4.4 requirement: real gap detection. Checked on
            // every Update (not just Release) so a missing invocationSequenceNumber is surfaced
            // as close to real time as this build's synchronous request handling allows, not only
            // discovered at session end.
            try {
                const auto gaps = cdr_writer.detect_gaps(ref);
                if (!gaps.empty()) {
                    std::string gap_list;
                    for (const auto& gap : gaps) {
                        if (!gap_list.empty()) {
                            gap_list += ", ";
                        }
                        gap_list += std::to_string(gap);
                    }
                    spdlog::warn("chf: CDR sequence gap detected for ChargingDataRef={} -- missing "
                                 "invocationSequenceNumber(s): {}",
                                 ref,
                                 gap_list);
                }
            } catch (const std::exception& e) {
                spdlog::warn("chf: CDR gap-detection query failed for ChargingDataRef={}: {}",
                             ref,
                             e.what());
            }

            update_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 200;
            resp.headers.emplace("content-type", "application/json");
            json j = response;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kApiRoot) + "/chargingdata/{ChargingDataRef}/release",
        [&verifier, &charging_data_store, &release_counter, &balance_client, &cdr_writer](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            // Real spec shape (requestBody required: true, schema ChargingDataRequest) -- parsed
            // for validation/mandatory-field-checking parity with Create.
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<
                sbi_gen::ChargingDataRequest_Nchf_ConvergedCharging>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto ref = req.path_params.at("ChargingDataRef");
            // ADR-0057: finalize (real permanent debit + unreserve) whatever this session
            // reserved, BEFORE releasing the ref -- get_supi/get_reserved_total read
            // chf:cdr:content:{ref}, which release() deliberately does not erase (see
            // ChargingDataStore::release's own comment), but reading before releasing keeps the
            // real order-of-operations obviously correct rather than relying on that.
            const auto supi = charging_data_store.get_supi(ref);
            const auto reserved_total = charging_data_store.get_reserved_total(ref);
            if (!charging_data_store.release(ref)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No active charging data resource " + ref);
            }
            if (supi.has_value() && !supi->empty()) {
                finalize_subscriber_balance(
                    balance_client, *supi, reserved_total, "Nchf_ConvergedCharging_Release " + ref);
            }

            // P4.4/ADR-0058: a real, final CDR row for this session -- reserved_cost here is the
            // session's TOTAL committed cost (finalize_subscriber_balance's own real amount), not
            // a per-rating-group figure the way Create/Update's rows are.
            try {
                chf::CdrRecord cdr{};
                cdr.charging_data_ref = ref;
                cdr.invocation_sequence_number = body->invocationSequenceNumber;
                cdr.service_type = "ConvergedCharging";
                cdr.operation = "Release";
                cdr.subscriber_identifier = supi.value_or("");
                cdr.nf_consumer_node_functionality =
                    body->nfConsumerIdentification.nodeFunctionality.value;
                if (reserved_total > 0.0) {
                    cdr.reserved_cost = reserved_total;
                }
                cdr.invocation_time_stamp =
                    std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                cdr_writer.write(cdr);
            } catch (const std::exception& e) {
                spdlog::warn("chf: CDR write to ClickHouse failed for ChargingDataRef={}: {}",
                             ref,
                             e.what());
            }

            release_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nchf_OfflineOnlyCharging (TS 32.291, P4.2/ADR-0055) ---
    //
    // Real spec shape confirmed directly against TS32291_Nchf_OfflineOnlyCharging.yaml: unlike
    // ConvergedCharging, ChargingDataResponse_Nchf_OfflineOnlyCharging carries no
    // multipleUnitInformation/grantedUnit field at all -- offline charging (e.g. bulk SMS,
    // delayed-CDR events) records usage for later billing, it does not grant real-time online
    // quota. So this handler set does NOT call the rating engine (build_rating_grant) at all,
    // unlike Create/Update above -- a real, spec-driven difference, not an oversight.

    server.add_route(
        "POST",
        std::string(kOfflineApiRoot) + "/offlinechargingdata",
        [&verifier, &offline_charging_data_store, &offline_create_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<
                sbi_gen::ChargingDataRequest_Nchf_OfflineOnlyCharging>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto ref = offline_charging_data_store.create();

            sbi_gen::ChargingDataResponse_Nchf_OfflineOnlyCharging response{};
            response.invocationTimeStamp =
                sbi_core::format_rfc3339(std::chrono::system_clock::now());
            response.invocationSequenceNumber = body->invocationSequenceNumber;

            offline_create_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kOfflineApiRoot) + "/offlinechargingdata/" + ref);
            json j = response;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kOfflineApiRoot) + "/offlinechargingdata/{OfflineChargingDataRef}/update",
        [&verifier, &offline_charging_data_store, &offline_update_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<
                sbi_gen::ChargingDataRequest_Nchf_OfflineOnlyCharging>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto ref = req.path_params.at("OfflineChargingDataRef");
            if (!offline_charging_data_store.is_active(ref)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No active offline charging data resource " + ref);
            }

            sbi_gen::ChargingDataResponse_Nchf_OfflineOnlyCharging response{};
            response.invocationTimeStamp =
                sbi_core::format_rfc3339(std::chrono::system_clock::now());
            response.invocationSequenceNumber = body->invocationSequenceNumber;

            offline_update_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 200;
            resp.headers.emplace("content-type", "application/json");
            json j = response;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "POST",
        std::string(kOfflineApiRoot) + "/offlinechargingdata/{OfflineChargingDataRef}/release",
        [&verifier, &offline_charging_data_store, &offline_release_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<
                sbi_gen::ChargingDataRequest_Nchf_OfflineOnlyCharging>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto ref = req.path_params.at("OfflineChargingDataRef");
            if (!offline_charging_data_store.release(ref)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No active offline charging data resource " + ref);
            }
            offline_release_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nchf_SpendingLimitControl (TS 29.594, P4.2/ADR-0055) ---
    //
    // Real spec shape confirmed directly against TS29594_Nchf_SpendingLimitControl.yaml: CHF is
    // the SERVER for this service (PCF subscribes TO CHF), unlike the "N28 wiring" phrase in
    // CHARGING_PROMPT.md's P4.2 prompt might suggest at a glance -- see ADR-0055 for the full
    // finding. The real statusNotification/subscriptionTermination callbacks (CHF as client,
    // POSTing to the subscriber's notifUri) are NOT implemented this turn -- no real policy-
    // counter-breach-detection engine exists yet to trigger them from (same category of
    // deliberately-deferred gap as Nchf_ConvergedCharging's own chargingNotification, this file's
    // header comment). Subscribe/Update/Unsubscribe below are real, live resource CRUD.

    server.add_route(
        "POST",
        std::string(kSpendingLimitApiRoot) + "/subscriptions",
        [&verifier, &spending_limit_store, &spending_limit_subscribe_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SpendingLimitContext>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto status = build_spending_limit_status(*body);
            const auto id = spending_limit_store.create(*body);
            spending_limit_subscribe_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 std::string(kSpendingLimitApiRoot) + "/subscriptions/" + id);
            json j = status;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PUT",
        std::string(kSpendingLimitApiRoot) + "/subscriptions/{subscriptionId}",
        [&verifier, &spending_limit_store, &spending_limit_update_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SpendingLimitContext>(req, err);
            if (!body.has_value()) {
                return err;
            }

            const auto id = req.path_params.at("subscriptionId");
            if (!spending_limit_store.update(id, *body)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No active spending limit subscription " + id);
            }
            spending_limit_update_counter->Add(1);

            const auto status = build_spending_limit_status(*body);
            sbi_core::http2::Response resp;
            resp.status = 200;
            resp.headers.emplace("content-type", "application/json");
            json j = status;
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "DELETE",
        std::string(kSpendingLimitApiRoot) + "/subscriptions/{subscriptionId}",
        [&verifier, &spending_limit_store, &spending_limit_unsubscribe_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto id = req.path_params.at("subscriptionId");
            if (!spending_limit_store.remove(id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No active spending limit subscription " + id);
            }
            spending_limit_unsubscribe_counter->Add(1);

            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    std::thread(run_nrf_lifecycle, chf_instance_id).detach();

    server.start();
    spdlog::info("chf: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", kPort);
    spdlog::info("chf: Prometheus metrics at http://{}/metrics", kMetricsBindAddress);
    ioc.run();
    return 0;
}
