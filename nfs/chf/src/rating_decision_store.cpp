#include "rating_decision_store.hpp"

#include <spdlog/spdlog.h>

namespace chf {

RatingDecisionStore::RatingDecisionStore(const std::string& conninfo) {
    try {
        client_ = std::make_unique<pqxx::connection>(conninfo);
    } catch (const std::exception& e) {
        spdlog::warn("chf: could not connect to PostgreSQL (RatingDecision audit disabled): {}",
                     e.what());
        client_.reset();
    }
}

void RatingDecisionStore::record(const RatingDecisionRecord& decision) {
    if (!client_) {
        spdlog::warn(
            "chf: RatingDecision write skipped for tariffId={} -- PostgreSQL not connected",
            decision.tariffId);
        return;
    }

    try {
        pqxx::work txn(*client_);
        const auto id = txn.exec("SELECT nextval('rating_decision_id_seq')::text AS id")
                            .one_row()["id"]
                            .as<std::string>();
        // Disclosed simplification: acbr_tax_excluded and acbr_tax_included both bind to the same
        // $10 (decision.ratedAmount) -- this project has no real tax-computation subsystem yet
        // (see rating.hpp's own AppliedBillingTaxRate, modeled but not populated from any real
        // rate table), so the two real TMF678 fields are not yet meaningfully distinguished, not
        // fabricated as more sophisticated than they are.
        txn.exec("INSERT INTO rating_decision "
                 "(id, tariff_id, tariff_version, rating_group, input_snapshot, rated_amount, "
                 "currency, rule_fired_id, acbr_type, acbr_is_billed, acbr_tax_excluded, "
                 "acbr_tax_included) "
                 "VALUES ($1,$2,$3,$4,$5::jsonb,$6,$7,$8,$9,false,$10,$10)",
                 pqxx::params{id,
                              decision.tariffId,
                              decision.tariffVersion,
                              decision.ratingGroup,
                              decision.inputSnapshot.dump(),
                              decision.ratedAmount,
                              decision.currency,
                              decision.ruleFiredId,
                              decision.acbrType,
                              decision.ratedAmount});

        // P4.5/ADR-0060 (E8, Security): real audit trail, same transaction as the mutation it
        // records -- same "local per-service table" architectural disclosure as
        // bss/product-catalog's own schema.sql header.
        const auto audit_id = txn.exec("SELECT nextval('audit_record_id_seq')::text AS id")
                                  .one_row()["id"]
                                  .as<std::string>();
        txn.exec("INSERT INTO audit_record (id, entity_type, entity_id, action, actor, "
                 "after_snapshot) VALUES ($1,'RATING_DECISION',$2,'ratingDecision.record',"
                 "'chf',$3::jsonb)",
                 pqxx::params{audit_id, id, decision.inputSnapshot.dump()});

        txn.commit();
    } catch (const std::exception& e) {
        // Best-effort, same discipline as CdrWriter::write -- a rating-decision audit-write
        // failure must never block or fail the real charging response CHF already committed to.
        spdlog::warn(
            "chf: RatingDecision write failed for tariffId={}: {}", decision.tariffId, e.what());
    }
}

} // namespace chf
