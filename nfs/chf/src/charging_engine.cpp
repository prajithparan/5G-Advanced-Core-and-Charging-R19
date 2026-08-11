#include "charging_engine.hpp"

#include "sbi_core/datetime.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace chf {

namespace {

using nlohmann::json;

} // namespace

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
    return {grant, price.price, price.id, price.version, offering_it->name, price.name};
}

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

void write_rating_decision(chf::RatingDecisionStore& rating_decision_store,
                           const std::string& ref,
                           const sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging& usage,
                           const RatingResult& rating,
                           bool reserved) {
    if (!rating.tariffId.has_value()) {
        return;
    }
    chf::RatingDecisionRecord decision{};
    decision.tariffId = *rating.tariffId;
    decision.tariffVersion = rating.tariffVersion;
    decision.ratingGroup = static_cast<std::int64_t>(usage.ratingGroup);
    // Real, disclosed gap (schema.postgres.sql's own header): balance-at-decision-time is not
    // captured here -- would need an extra bss/balance-management call on every rating decision,
    // not added this pass.
    decision.inputSnapshot = {
        {"chargingDataRef", ref},
        {"ratingGroup", usage.ratingGroup},
        {"offeringName", rating.offeringName.value_or("")},
        {"priceName", rating.priceName.value_or("")},
        {"reserved", reserved},
        {"timestamp", sbi_core::format_rfc3339(std::chrono::system_clock::now())},
    };
    if (reserved && rating.cost.has_value()) {
        decision.ratedAmount = rating.cost->value;
        decision.currency = rating.cost->unit;
    }
    decision.ruleFiredId = *rating.tariffId;
    decision.acbrType = "appliedBillingCharge";
    rating_decision_store.record(decision);
}

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
                 const sbi_gen::MultipleUnitUsage_Nchf_ConvergedCharging& usage) {
    ChargeUsageResult result;
    result.rating = build_rating_grant(catalog_client);

    if (result.rating.cost.has_value() && !supi.empty()) {
        result.reserved =
            reserve_subscriber_balance(balance_client,
                                       supi,
                                       *result.rating.cost,
                                       "Nchf_ConvergedCharging_" + operation + " " + ref);
        if (result.reserved) {
            charging_data_store.add_reserved(ref, *result.rating.cost->value);
        } else if (reserve_rejected_counter != nullptr) {
            reserve_rejected_counter->Add(1);
        }
    }
    if (result.reserved && result.rating.grant.has_value() && grant_counter != nullptr) {
        grant_counter->Add(1);
    }

    write_converged_charging_cdr(cdr_writer,
                                 ref,
                                 operation,
                                 supi,
                                 node_functionality,
                                 invocation_sequence_number,
                                 usage,
                                 result.rating,
                                 result.reserved);
    write_rating_decision(rating_decision_store, ref, usage, result.rating, result.reserved);
    return result;
}

} // namespace chf
