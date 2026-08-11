#pragma once

// P4.5/ADR-0060 (Stage 3): the single, shared charging code path CHARGING_PROMPT.md's own P4.5
// explicitly demands ("normalising to the SAME internal JSON representation used by the 5G
// path... prove the single-code-path property with a test that charges an identical usage event
// arriving via Gy and via Nchf and asserts an identical rated result"). Every real decision this
// module makes (rating, balance reservation, CDR write, RatingDecision audit) is IDENTICAL
// regardless of whether the caller is `Nchf_ConvergedCharging`'s real HTTP handler (main.cpp) or
// the real Diameter Gy CCR handler (diameter_server.cpp) -- both call these same functions, not
// two parallel implementations that happen to look similar. `MultipleUnitUsage`/
// `MultipleUnitInformation` (TS 32.291's own real types) are themselves already protocol-neutral --
// Gy's own CCR/CCA AVPs are translated into/out of these same types at the Diameter boundary
// (diameter_server.cpp), not given a second, separate internal representation.
//
// Extracted from main.cpp (previously anonymous-namespace-local, HTTP-handler-only) so
// diameter_server.cpp can call the identical functions.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/metrics.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "TS29122_CommonData_grp.hpp"
#include "bss_sid/balance.hpp"
#include "bss_sid/product.hpp"
#include "cdr.hpp"
#include "rating_decision_store.hpp"
#include "stores.hpp"

namespace chf {

// CHF's own client base URLs/paths for its two real BSS-layer dependencies (ADR-0047/ADR-0056).
constexpr const char* kProductCatalogBase = "https://127.0.0.1:7785";
constexpr const char* kProductCatalogApiRoot = "/tmf-api/productCatalogManagement/v4";
constexpr const char* kBalanceManagementBase = "https://127.0.0.1:7786";
constexpr const char* kBalanceManagementApiRoot = "/tmf-api/prepayBalanceManagement/v4";

// P4.3 (ADR-0057): a rating decision is now a real (GrantedUnit, cost) pair -- the quantity of
// service granted (e.g. 5GB) AND its real monetary cost (e.g. $20), confirmed as two genuinely
// separate real TMF620 fields on ProductOfferingPrice (`unitOfMeasure` vs `price`), not something
// this project invented a split for. `cost` is nullopt when the matched price has no `price`
// field set (a real, valid TMF620 state -- not every price is monetary), in which case no real
// balance reservation is attempted for this grant (same as before this ADR).
// P4.5/ADR-0060 (E5): tariffId/tariffVersion/offeringName/priceName added so callers can write a
// real RatingDecision audit row (principle 2: "every charge is explainable") without a second
// lookup -- empty when no offering/price was matched (nothing was rated, nothing to audit).
struct RatingResult {
    std::optional<sbi_gen::GrantedUnit> grant;
    std::optional<bss_sid::Money> cost;
    std::optional<std::string> tariffId;
    std::optional<std::string> tariffVersion;
    std::optional<std::string> offeringName;
    std::optional<std::string> priceName;
};

// Real rating engine (ADR-0048). Fetches the first Active/isSellable ProductOffering from
// bss/product-catalog (ADR-0047), then its first referenced ProductOfferingPrice, and converts
// that price's unitOfMeasure into a real GrantedUnit -- the actual charging decision, not a
// fabricated placeholder. No OAuth2 token needed: product-catalog is mTLS-only (ADR-0047), same
// trust boundary the client cert already provides. Returns an empty result if the catalog is
// unreachable or has no matching offering (schema-valid empty grant, same fallback this build has
// always had).
//
// Unit conversion is real but deliberately narrow: TS 32.291's GrantedUnit has no generic "amount
// + unit string" field the way TMF620's Quantity does -- totalVolume/uplinkVolume/downlinkVolume
// are raw octet counts (TS 32.298's own CDR volume fields are octet-counted, confirmed by their
// Uint64 typing with no separate unit field), time is raw seconds. Only "GB"/"MB" (decimal,
// matching 3GPP's own octet-counting convention, not binary GiB/MiB) convert to totalVolume; any
// other unit string falls back to serviceSpecificUnits carrying the raw amount unconverted --
// disclosed as a real but narrow conversion, not a general unit-aware rating engine.
RatingResult build_rating_grant(sbi_core::http2::Client& catalog_client);

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
                                const std::string& description);

// P4.3 (ADR-0057): Release-time finalization -- converts everything reserved for this session
// (ChargingDataStore::get_reserved_total) into a real, permanent debit. Disclosed simplification:
// this finalizes the FULL reserved total, not a proportional amount based on SMF's actually-
// reported usage (usedUnitContainer) -- a real per-usage proportional refund is deferred, not
// fabricated as more sophisticated than it is. See charging_engine.cpp's own comment for the real
// order-dependent unreserve-then-debit bug this function's implementation was fixed for.
void finalize_subscriber_balance(sbi_core::http2::Client& balance_client,
                                 const std::string& supi,
                                 double total_reserved,
                                 const std::string& description);

// P4.4/ADR-0058: writes one real CDR row (chf::CdrRecord, see cdr.hpp) per MultipleUnitUsage
// entry -- every field here is either a real TS 32.291 value already flowing through the caller,
// or a real ADR-0057 rating-engine output (grant/cost), not fabricated. Shared between every
// caller of charge_one_usage below.
void write_converged_charging_cdr(chf::CdrWriter& cdr_writer,
                                  const std::string& ref,
                                  const std::string& operation,
                                  const std::string& supi,
                                  const std::string& node_functionality,
                                  std::int64_t invocation_sequence_number,
                                  const sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging& usage,
                                  const RatingResult& rating,
                                  bool reserved);

// P4.5/ADR-0060 (E5): writes one real RatingDecision audit row per MultipleUnitUsage entry that
// was actually rated (rating.tariffId has a value) -- an unmatched offering/price is not a
// "decision" worth auditing (nothing was explained because nothing was rated). Shared between
// every caller of charge_one_usage below.
void write_rating_decision(chf::RatingDecisionStore& rating_decision_store,
                           const std::string& ref,
                           const sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging& usage,
                           const RatingResult& rating,
                           bool reserved);

struct ChargeUsageResult {
    RatingResult rating;
    bool reserved = true;
};

// The single, shared code path: rating, balance reservation, CDR write, RatingDecision audit --
// called identically by Nchf_ConvergedCharging's real HTTP Create/Update handlers (main.cpp) and
// the real Diameter Gy CCR handler (diameter_server.cpp).
ChargeUsageResult
charge_one_usage(sbi_core::http2::Client& catalog_client,
                 sbi_core::http2::Client& balance_client,
                 chf::CdrWriter& cdr_writer,
                 chf::RatingDecisionStore& rating_decision_store,
                 chf::ChargingDataStore& charging_data_store,
                 opentelemetry::metrics::Counter<std::uint64_t>* grant_counter,
                 opentelemetry::metrics::Counter<std::uint64_t>* reserve_rejected_counter,
                 const std::string& ref,
                 const std::string& operation,
                 const std::string& supi,
                 const std::string& node_functionality,
                 std::int64_t invocation_sequence_number,
                 const sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging& usage);

} // namespace chf
