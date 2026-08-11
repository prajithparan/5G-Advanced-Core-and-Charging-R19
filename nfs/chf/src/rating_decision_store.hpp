#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <pqxx/pqxx>
#include <string>

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule.
//
// P4.5/ADR-0060 (E5, Rating Function): CHF's first real PostgreSQL connection (schema:
// ../schema.postgres.sql, real TMF678 AppliedCustomerBillingRate mapping). Same "graceful
// degradation, never crash the higher-priority real-time charging path" design principle already
// established for CdrWriter (ADR-0058's own real eager-connect crash bug and fix) -- a
// rating-decision audit-write failure must never block or crash the real charging response CHF
// has already committed to.

namespace chf {

struct RatingDecisionRecord {
    std::string tariffId;
    std::optional<std::string> tariffVersion;
    std::optional<std::int64_t> ratingGroup;
    nlohmann::json inputSnapshot = nlohmann::json::object();
    std::optional<double> ratedAmount;
    std::optional<std::string> currency;
    std::string ruleFiredId;
    std::optional<std::string>
        acbrType; // appliedBillingCharge | appliedBillingCredit | appliedPenaltyCharge
};

class RatingDecisionStore {
public:
    // conninfo: a libpq connection string, sourced by the caller (env var, never hardcoded
    // credentials -- same precedent as bss/product-catalog's PRODUCT_CATALOG_DATABASE_URL,
    // ADR-0054). Does not throw on connection failure -- catches it internally and degrades to a
    // logged no-op state, same real-bug-driven design as CdrWriter (ADR-0058).
    explicit RatingDecisionStore(const std::string& conninfo);

    // Real INSERT into `rating_decision` (best-effort -- a write failure is logged and swallowed,
    // never propagated to the caller, same "does not block the real charging response" discipline
    // CdrWriter::write already established).
    void record(const RatingDecisionRecord& decision);

    bool is_connected() const { return client_ != nullptr; }

private:
    std::unique_ptr<pqxx::connection> client_;
};

} // namespace chf
