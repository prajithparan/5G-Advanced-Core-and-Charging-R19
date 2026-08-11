-- nfs/chf's real PostgreSQL schema -- P4.5/ADR-0060 (E5, Rating Function): CHF's first PostgreSQL
-- connection (previously Redis/Valkey for E3 session state and ClickHouse for E4 CDRs only).
--
-- Real TMF678 AppliedCustomerBillingRate is E5's own real SID mapping (docs/DATA_MODEL.md), the
-- "rated" representation of a charging decision. `rating_decision` combines this project's own
-- disclosed project-internal audit fields (CHARGING_PROMPT.md principle 1: "same inputs -> same
-- charge, forever, provably" -- input_snapshot/rule_fired_id/ai_advisory) with the real TMF678
-- fields the decision is realized as (acbr_type/acbr_is_billed/acbr_tax_excluded/
-- acbr_tax_included) on the SAME row, same "project-internal wrapper around a real TM Forum
-- resource" pattern this project already used for E6's Bucket.
--
-- Wired into CHF's real rating engine (build_rating_grant, ADR-0048/0057, nfs/chf/src/main.cpp):
-- every real rating decision (a grant issued, or explicitly withheld) writes one row here.

CREATE SEQUENCE IF NOT EXISTS rating_decision_id_seq;

CREATE TABLE IF NOT EXISTS rating_decision (
    id                  TEXT PRIMARY KEY,
    usage_record_id     TEXT,           -- FK -> a future E4 UsageRecord row; nullable, no such
                                        -- table exists yet (E4 is ClickHouse CDRs, not this table)
    tariff_id           TEXT,           -- ProductOfferingPrice.id (E2), the tariff that fired
    tariff_version      TEXT,           -- ProductOfferingPrice.version, pinned per principle 1
    rating_group        BIGINT,
    input_snapshot       JSONB NOT NULL DEFAULT '{}', -- usage + offering/price ids + timestamp;
                                        -- disclosed real gap: balance-at-decision-time is NOT
                                        -- captured (would need an extra bss/balance-management
                                        -- call on every rating decision -- not added this pass)
    rated_amount         NUMERIC(18, 6),
    currency             TEXT,
    rule_fired_id         TEXT,          -- which ProductOffering/ProductOfferingPrice fired
                                        -- (principle 2: "every charge is explainable")
    ai_advisory           JSONB,          -- nullable; not populated until P4.8 (AI layer)
    decided_at            TIMESTAMPTZ NOT NULL DEFAULT now(),
    -- Real TMF678 AppliedCustomerBillingRate fields (E5's own real SID mapping):
    acbr_type             TEXT,          -- appliedBillingCharge | appliedBillingCredit | appliedPenaltyCharge
    acbr_is_billed         BOOLEAN,       -- always false at write time -- this project has no real
                                        -- billing-cycle/invoice-generation process yet to flip it
    acbr_tax_excluded      NUMERIC(18, 6),
    acbr_tax_included      NUMERIC(18, 6)
);
