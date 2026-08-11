-- bss/product-catalog PostgreSQL schema.
--
-- Design per docs/DATA_MODEL.md's E2 (Service Catalog + Charging Plans) persistence decision and
-- ADR-0053/ADR-0054 in docs/DECISIONS.md: real TMF620 top-level scalar fields become real columns;
-- TMF620's array/nested-object fields (productOfferingPrice refs, category, channel, marketSegment,
-- prodSpecCharValueUse, productSpecification ref, resourceCandidate, serviceCandidate,
-- serviceLevelAgreement, agreement, bundledProductOffering, productSpecCharacteristic) are stored as
-- `jsonb` columns on the same row -- one database technology (PostgreSQL's native jsonb) satisfies
-- both TMF620's relationally-shaped header fields and its variable-shape nested fields, rather than
-- introducing a second NoSQL engine purely for this resource (see ADR-0053's E2 reasoning).
--
-- id columns are server-assigned, generated from a per-table sequence and cast to text -- matching
-- the real REST semantics the in-memory store this replaces already had (server always assigns a
-- fresh id/href on create, overwriting any client-supplied value).

CREATE SEQUENCE IF NOT EXISTS product_offering_id_seq;
CREATE SEQUENCE IF NOT EXISTS product_offering_price_id_seq;
CREATE SEQUENCE IF NOT EXISTS product_specification_id_seq;

CREATE TABLE IF NOT EXISTS product_offering (
    id                          TEXT PRIMARY KEY,
    href                        TEXT,
    name                        TEXT,
    description                 TEXT,
    lifecycle_status            TEXT,
    is_bundle                   BOOLEAN,
    is_sellable                 BOOLEAN,
    version                     TEXT,
    valid_from                  TEXT,
    valid_to                    TEXT,
    product_offering_price      JSONB NOT NULL DEFAULT '[]',
    category                    JSONB NOT NULL DEFAULT '[]',
    channel                     JSONB NOT NULL DEFAULT '[]',
    market_segment              JSONB NOT NULL DEFAULT '[]',
    prod_spec_char_value_use    JSONB NOT NULL DEFAULT '[]',
    product_specification       JSONB,
    resource_candidate          JSONB,
    service_candidate           JSONB,
    service_level_agreement     JSONB,
    agreement                   JSONB NOT NULL DEFAULT '[]',
    bundled_product_offering    JSONB NOT NULL DEFAULT '[]',
    created_at                  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS product_offering_price (
    id                          TEXT PRIMARY KEY,
    href                        TEXT,
    name                        TEXT,
    description                 TEXT,
    lifecycle_status            TEXT,
    price_type                  TEXT,
    price                       JSONB,
    recurring_charge_period_length  INTEGER,
    recurring_charge_period_type    TEXT,
    unit_of_measure              JSONB,
    prod_spec_char_value_use     JSONB NOT NULL DEFAULT '[]',
    valid_from                   TEXT,
    valid_to                     TEXT,
    created_at                   TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS product_specification (
    id                          TEXT PRIMARY KEY,
    href                        TEXT,
    brand                        TEXT,
    description                  TEXT,
    is_bundle                    BOOLEAN,
    lifecycle_status             TEXT,
    name                         TEXT,
    product_number                TEXT,
    version                       TEXT,
    product_spec_characteristic   JSONB NOT NULL DEFAULT '[]',
    valid_from                    TEXT,
    valid_to                      TEXT,
    created_at                    TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                    TIMESTAMPTZ NOT NULL DEFAULT now()
);
