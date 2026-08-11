-- bss/roaming-interconnect PostgreSQL schema.
--
-- P4.1/P4.11's E7 (Roaming and Interconnect Agreements) persistence, per docs/DATA_MODEL.md's own
-- schema sketch. Real, disclosed scoping decision (2026-08-11, docs/DECISIONS.md ADR-0060): this
-- turn builds the schema and a real PostgreSQL-backed store library (src/store.hpp/.cpp), proven
-- with real live verification -- same rigor as E1/E10/E2/E5/E6 -- but deliberately does NOT add a
-- new HTTP/REST service (main.cpp) yet. CHARGING_PROMPT.md's own phase sequence assigns "Roaming
-- and interconnect settlement (E7)" to P4.11, a later, not-yet-reached phase.
--
-- `interconnect_agreement` is project-internal (docs/DATA_MODEL.md's own explicit "not itself a
-- spec-mandated shape" disclosure), realized as a real TMF651 Agreement -- header scalar fields
-- become real columns, the real Agreement's own array/nested fields (agreementAuthorization,
-- agreementItem, associatedAgreement, characteristic, engagedParty) are `jsonb` columns on the
-- same row, same convention as E6's Bucket-wraps-TMF654 pattern.
--
-- Real, disclosed limit (unchanged from P4.1, restated here): TAP3/RAP/NRTRDE (the real GSMA
-- roaming-settlement CDR file formats) are GSMA documents behind membership -- not quoted from
-- memory, not fabricated. `roaming_cdr_file.format` includes a real `STUB` value for exactly this
-- reason; `raw_payload` is stored opaquely (`bytea`) until a real GSMA spec is supplied to build a
-- real codec against.

CREATE SEQUENCE IF NOT EXISTS interconnect_agreement_id_seq;
CREATE SEQUENCE IF NOT EXISTS roaming_cdr_file_id_seq;

CREATE TABLE IF NOT EXISTS interconnect_agreement (
    id                          TEXT PRIMARY KEY,
    href                        TEXT,
    partner_operator_plmn_id    TEXT,            -- TS 23.501 PLMN ID, project-internal field
                                                 -- (docs/DATA_MODEL.md's own E7 sketch) -- no real
                                                 -- TMF651 field carries a PLMN id directly
    agreement_type              TEXT,            -- real TMF651 Agreement.agreementType
    description                 TEXT,
    document_number             INTEGER,         -- real TMF651 field (integer, not string --
                                                 -- confirmed from the real swagger, not assumed)
    initial_date                TEXT,
    name                        TEXT,
    statement_of_intent          TEXT,
    status                      TEXT,
    version                     TEXT,
    agreement_authorization      JSONB NOT NULL DEFAULT '[]',
    agreement_item                JSONB NOT NULL DEFAULT '[]',
    agreement_period_from          TEXT,
    agreement_period_to            TEXT,
    agreement_specification         JSONB,
    associated_agreement            JSONB NOT NULL DEFAULT '[]',
    characteristic                 JSONB NOT NULL DEFAULT '[]',
    completion_date_from            TEXT,
    completion_date_to              TEXT,
    engaged_party                  JSONB NOT NULL DEFAULT '[]',
    rate_terms                    JSONB,           -- project-internal (docs/DATA_MODEL.md's own
                                                   -- sketch: "partner-specific rating; shape TBD,
                                                   -- not guessed") -- deliberately opaque jsonb,
                                                   -- no fixed shape asserted
    valid_from                   TEXT,
    valid_to                     TEXT,
    created_at                   TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS roaming_cdr_file (
    id              TEXT PRIMARY KEY,
    agreement_id    TEXT REFERENCES interconnect_agreement(id),
    format          TEXT NOT NULL DEFAULT 'STUB', -- TAP3 | RAP | NRTRDE | STUB
    raw_payload     BYTEA,                        -- opaque until a real GSMA codec exists
    received_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at    TIMESTAMPTZ
);

-- P4.5/ADR-0060 (E8, Security): real audit trail, same transaction as the mutation it records --
-- see bss/product-catalog's own schema.sql header for the full "local per-service table" real
-- architectural disclosure.
CREATE SEQUENCE IF NOT EXISTS audit_record_id_seq;

CREATE TABLE IF NOT EXISTS audit_record (
    id                TEXT PRIMARY KEY,
    entity_type       TEXT NOT NULL,   -- INTERCONNECT_AGREEMENT, ROAMING_CDR_FILE
    entity_id         TEXT NOT NULL,
    action            TEXT NOT NULL,
    actor             TEXT NOT NULL,
    before_snapshot   JSONB,
    after_snapshot    JSONB,
    ai_advisory_ref   TEXT,
    recorded_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);
