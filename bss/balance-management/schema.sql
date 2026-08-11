-- bss/balance-management PostgreSQL schema.
--
-- Design per docs/DATA_MODEL.md's E6 (Balance Management/ABMF) persistence decision and
-- docs/DECISIONS.md's P4.3 ADR: real TMF654 header fields become real columns.
--
-- Disclosed deviation from docs/DATA_MODEL.md's original E6 sketch (Redis hot balance +
-- PostgreSQL ledger, two stores): this schema uses PostgreSQL ALONE as Bucket's authoritative
-- store. Reasoning: CHARGING_PROMPT.md's P4.3 explicitly requires "strong consistency on balance
-- mutation -- prove it under concurrent debit tests" -- a single-statement atomic
-- `UPDATE ... WHERE remaining_value >= $amount` already gives genuine, provable strong
-- consistency via PostgreSQL's own row-level locking and MVCC, with no risk of the two stores
-- (Redis hot value + PostgreSQL ledger) drifting out of sync under a crash between the two
-- writes. Adding a Redis hot-path cache on top would be a real, valid future optimization once
-- real throughput numbers justify it (nothing benchmarked yet, ADR-0049's own standing
-- disclosure) -- not adding speculative complexity now for a correctness property PostgreSQL
-- alone already provides.
--
-- Extended 2026-08-11 (user directive: "no compromise on data model", docs/DECISIONS.md ADR-0060):
-- full real TMF654 field fidelity, re-confirmed by re-fetching the real swagger directly. Real,
-- concrete bug found and fixed by this pass: `product` is `array<ProductRef>` in the real spec on
-- EVERY one of these resources (Bucket, TopupBalance, AdjustBalance, ReserveBalance), not a single
-- ref -- the old `product_id`/`product_name` scalar column pair could only ever represent one
-- product, silently wrong for the real multi-product case. Replaced with a `product` jsonb array
-- column, matching this project's own established convention for array fields (see
-- bss/product-catalog/schema.sql). Also fixes a real, live data-loss bug: `Bucket.logicalResource`/
-- `Bucket.relatedParty` were already modeled in `bss_sid::Bucket` (the C++ struct) but had NO
-- columns at all -- silently dropped on every write. New jsonb columns for the previously-missing
-- real fields: `logical_resource`, `related_party`, `channel`, `payment_method`, `requestor`,
-- `balance_topup`.

CREATE SEQUENCE IF NOT EXISTS bucket_id_seq;
CREATE SEQUENCE IF NOT EXISTS topup_balance_id_seq;
CREATE SEQUENCE IF NOT EXISTS adjust_balance_id_seq;
CREATE SEQUENCE IF NOT EXISTS reserve_balance_id_seq;

-- The balance resource itself. remaining_value/reserved_value are the authoritative, strongly-
-- consistent values every mutation below acts on atomically.
CREATE TABLE IF NOT EXISTS bucket (
    id                     TEXT PRIMARY KEY,
    href                   TEXT,
    confirmation_date      TEXT,
    description            TEXT,
    is_shared              BOOLEAN,
    name                   TEXT,
    remaining_value_name   TEXT,
    requested_date         TEXT,
    party_account_id       TEXT,
    party_account_name     TEXT,
    product                JSONB NOT NULL DEFAULT '[]',
    logical_resource       JSONB NOT NULL DEFAULT '[]',
    related_party          JSONB NOT NULL DEFAULT '[]',
    remaining_value_unit   TEXT,
    remaining_value        NUMERIC(18, 6) NOT NULL DEFAULT 0,
    reserved_value_unit    TEXT,
    reserved_value         NUMERIC(18, 6) NOT NULL DEFAULT 0,
    status                 TEXT NOT NULL DEFAULT 'active',
    usage_type             TEXT,
    valid_from             TEXT,
    valid_to               TEXT,
    created_at             TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at             TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Real, durable audit ledger for every TopupBalance/AdjustBalance/ReserveBalance action -- entity
-- E8 (Security)'s "full audit trail on every balance... mutation" requirement, and P4.3's own
-- "every rating decision emits an audit record sufficient to reconstruct the charge".

CREATE TABLE IF NOT EXISTS topup_balance (
    id                  TEXT PRIMARY KEY,
    href                TEXT,
    confirmation_date   TEXT,
    description         TEXT,
    is_auto_topup       BOOLEAN,
    number_of_periods   INTEGER,
    reason              TEXT,
    requested_date      TEXT,
    voucher             TEXT,
    bucket_id           TEXT NOT NULL,
    amount              NUMERIC(18, 6) NOT NULL,
    amount_units        TEXT,
    balance_topup       JSONB,
    channel             JSONB,
    logical_resource    JSONB NOT NULL DEFAULT '[]',
    party_account_id    TEXT,
    payment_method      JSONB,
    product              JSONB NOT NULL DEFAULT '[]',
    recurring_period    TEXT,
    related_party       JSONB NOT NULL DEFAULT '[]',
    requestor           JSONB,
    status              TEXT NOT NULL,
    usage_type          TEXT,
    valid_from          TEXT,
    valid_to            TEXT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS adjust_balance (
    id                  TEXT PRIMARY KEY,
    href                TEXT,
    confirmation_date   TEXT,
    description         TEXT,
    reason              TEXT,
    requested_date      TEXT,
    adjust_type         TEXT,
    bucket_id           TEXT NOT NULL,
    amount              NUMERIC(18, 6) NOT NULL,
    amount_units        TEXT,
    channel             JSONB,
    logical_resource    JSONB NOT NULL DEFAULT '[]',
    party_account_id    TEXT,
    product              JSONB NOT NULL DEFAULT '[]',
    related_party       JSONB NOT NULL DEFAULT '[]',
    requestor           JSONB,
    status              TEXT NOT NULL,
    usage_type          TEXT,
    valid_from          TEXT,
    valid_to            TEXT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS reserve_balance (
    id                  TEXT PRIMARY KEY,
    href                TEXT,
    confirmation_date   TEXT,
    description         TEXT,
    reason              TEXT,
    requested_date      TEXT,
    bucket_id           TEXT NOT NULL,
    amount              NUMERIC(18, 6) NOT NULL,
    amount_units        TEXT,
    channel             JSONB,
    logical_resource    JSONB NOT NULL DEFAULT '[]',
    party_account_id    TEXT,
    product              JSONB NOT NULL DEFAULT '[]',
    related_party       JSONB NOT NULL DEFAULT '[]',
    requestor           JSONB,
    status              TEXT NOT NULL,
    usage_type          TEXT,
    valid_from          TEXT,
    valid_to            TEXT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);
