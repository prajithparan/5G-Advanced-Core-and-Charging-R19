-- bss/product-catalog PostgreSQL schema.
--
-- Design per docs/DATA_MODEL.md's E2 (Service Catalog + Charging Plans) persistence decision and
-- ADR-0053/ADR-0054 in docs/DECISIONS.md: real TMF620 top-level scalar fields become real columns;
-- TMF620's array/nested-object fields (productOfferingPrice refs, category, channel, marketSegment,
-- prodSpecCharValueUse, productSpecification ref, resourceCandidate, serviceCandidate,
-- serviceLevelAgreement, agreement, bundledProductOffering, productSpecCharacteristic, and the
-- 2026-08-11 "no compromise on data model" extension's attachment, place,
-- productOfferingRelationship, productOfferingTerm, bundledPopRelationship, constraint,
-- popRelationship, pricingLogicAlgorithm, tax, bundledProductSpecification,
-- productSpecificationRelationship, relatedParty, resourceSpecification, serviceSpecification,
-- targetProductSchema) are stored as `jsonb` columns on the same row -- one database technology
-- (PostgreSQL's native jsonb) satisfies both TMF620's relationally-shaped header fields and its
-- variable-shape nested fields, rather than introducing a second NoSQL engine purely for this
-- resource (see ADR-0053's E2 reasoning).
--
-- id columns are server-assigned, generated from a per-table sequence and cast to text -- matching
-- the real REST semantics the in-memory store this replaces already had (server always assigns a
-- fresh id/href on create, overwriting any client-supplied value).
--
-- Extended 2026-08-11 (user directive: "no compromise on data model") with every remaining real
-- TMF620 v4.1.0 field on ProductOffering/ProductOfferingPrice/ProductSpecification, confirmed by
-- re-fetching the real swagger directly (see libs/bss-sid/include/bss_sid/product.hpp's own header
-- for the full citation and the one genuine gap this re-check found: `percentage` on
-- ProductOfferingPrice had not previously been disclosed as missing at all).

CREATE SEQUENCE IF NOT EXISTS product_offering_id_seq;
CREATE SEQUENCE IF NOT EXISTS product_offering_price_id_seq;
CREATE SEQUENCE IF NOT EXISTS product_specification_id_seq;

CREATE TABLE IF NOT EXISTS product_offering (
    id                              TEXT PRIMARY KEY,
    href                            TEXT,
    name                            TEXT,
    description                     TEXT,
    lifecycle_status                TEXT,
    last_update                     TEXT,
    status_reason                   TEXT,
    is_bundle                       BOOLEAN,
    is_sellable                     BOOLEAN,
    version                         TEXT,
    valid_from                      TEXT,
    valid_to                        TEXT,
    product_offering_price          JSONB NOT NULL DEFAULT '[]',
    category                        JSONB NOT NULL DEFAULT '[]',
    channel                         JSONB NOT NULL DEFAULT '[]',
    market_segment                  JSONB NOT NULL DEFAULT '[]',
    prod_spec_char_value_use        JSONB NOT NULL DEFAULT '[]',
    product_specification           JSONB,
    resource_candidate              JSONB,
    service_candidate               JSONB,
    service_level_agreement         JSONB,
    agreement                       JSONB NOT NULL DEFAULT '[]',
    bundled_product_offering        JSONB NOT NULL DEFAULT '[]',
    attachment                      JSONB NOT NULL DEFAULT '[]',
    place                           JSONB NOT NULL DEFAULT '[]',
    product_offering_relationship   JSONB NOT NULL DEFAULT '[]',
    product_offering_term           JSONB NOT NULL DEFAULT '[]',
    created_at                      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS product_offering_price (
    id                              TEXT PRIMARY KEY,
    href                            TEXT,
    name                            TEXT,
    description                     TEXT,
    lifecycle_status                TEXT,
    last_update                     TEXT,
    price_type                      TEXT,
    percentage                      DOUBLE PRECISION,
    price                           JSONB,
    recurring_charge_period_length  INTEGER,
    recurring_charge_period_type    TEXT,
    unit_of_measure                 JSONB,
    prod_spec_char_value_use        JSONB NOT NULL DEFAULT '[]',
    bundled_pop_relationship        JSONB NOT NULL DEFAULT '[]',
    constraint_ref                  JSONB NOT NULL DEFAULT '[]',
    place                           JSONB NOT NULL DEFAULT '[]',
    pop_relationship                JSONB NOT NULL DEFAULT '[]',
    pricing_logic_algorithm         JSONB NOT NULL DEFAULT '[]',
    product_offering_term           JSONB NOT NULL DEFAULT '[]',
    tax                             JSONB NOT NULL DEFAULT '[]',
    valid_from                      TEXT,
    valid_to                        TEXT,
    created_at                      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS product_specification (
    id                                  TEXT PRIMARY KEY,
    href                                TEXT,
    brand                               TEXT,
    description                         TEXT,
    is_bundle                           BOOLEAN,
    lifecycle_status                    TEXT,
    last_update                         TEXT,
    name                                TEXT,
    product_number                      TEXT,
    version                             TEXT,
    product_spec_characteristic         JSONB NOT NULL DEFAULT '[]',
    attachment                          JSONB NOT NULL DEFAULT '[]',
    bundled_product_specification       JSONB NOT NULL DEFAULT '[]',
    product_specification_relationship  JSONB NOT NULL DEFAULT '[]',
    related_party                       JSONB NOT NULL DEFAULT '[]',
    resource_specification              JSONB NOT NULL DEFAULT '[]',
    service_specification               JSONB NOT NULL DEFAULT '[]',
    target_product_schema               JSONB,
    valid_from                          TEXT,
    valid_to                            TEXT,
    created_at                          TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                          TIMESTAMPTZ NOT NULL DEFAULT now()
);
