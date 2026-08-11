-- nfs/chf's real ClickHouse CDR schema (P4.4/ADR-0058: CDF, TS 32.240/32.296).
--
-- IMPORTANT, disclosed explicitly: this is NOT a conformant TS 32.298 CDR. TS 32.298 (CDR
-- parameter description) is not vendored in this repo -- flagged as an open question back in
-- P4.1 (docs/DATA_MODEL.md), resolution on file: proceed with TS 32.291's already-vendored,
-- already-used field shape instead of inventing TS 32.298's real (different, ASN.1-based) field
-- taxonomy (real 32.298 record types like a "5GSChargingDataRecord" have their own real field
-- names this project has no access to and will not guess). Every column below is a real,
-- already-confirmed TS 32.291 field (ChargingDataRequest/Response, already vendored and used
-- throughout nfs/chf/src/main.cpp) or a real, disclosed project-internal addition (recorded_at,
-- service_type, operation) -- not a fabricated 32.298 field name.
--
-- ReplacingMergeTree: real, native ClickHouse duplicate-detection mechanism, not a project
-- invention -- rows sharing the same ORDER BY key are deduplicated (the row with the highest
-- `version` column, here `recorded_at`, wins) during background merges. Disclosed, real
-- eventual-consistency characteristic: deduplication is NOT immediate at insert time, only
-- during ClickHouse's own background merge cycles (or when a query uses FINAL) -- a real
-- ClickHouse behavior, not a simplification this project chose.
--
-- TTL: real, native ClickHouse retention-driven auto-archival (PROMPT.md principle P14) for the
-- CDR/usage-analytics tier specifically (docs/DATA_MODEL.md's E4 ClickHouse assignment). A
-- separate cold-archive-to-object-store tier (E4's other assignment, for long-term retention
-- beyond this table's own TTL window) is NOT implemented this pass -- disclosed, deferred.

CREATE TABLE IF NOT EXISTS cdr (
    charging_data_ref              String,
    invocation_sequence_number     Int64,
    service_type                   LowCardinality(String), -- project-internal: 'ConvergedCharging' | 'OfflineOnlyCharging'
    operation                      LowCardinality(String), -- project-internal: 'Create' | 'Update' | 'Release'
    subscriber_identifier          String,                 -- real TS 32.291 field: subscriberIdentifier (SUPI)
    nf_consumer_node_functionality LowCardinality(String), -- real TS 32.291 field: nfConsumerIdentification.nodeFunctionality
    rating_group                   Nullable(Int64),        -- real TS 32.291 field: MultipleUnitUsage.ratingGroup
    granted_total_volume           Nullable(UInt64),       -- real TS 32.291 field: GrantedUnit.totalVolume (octets)
    granted_service_specific_units Nullable(UInt64),       -- real TS 32.291 field: GrantedUnit.serviceSpecificUnits
    used_total_volume              Nullable(UInt64),       -- real TS 32.291 field: UsedUnitContainer.totalVolume (octets)
    reserved_cost                  Nullable(Float64),       -- real ABMF amount reserved this event (ADR-0057), project-internal join field
    reserved_cost_currency         Nullable(String),
    invocation_time_stamp          DateTime,               -- real TS 32.291 field: invocationTimeStamp
    recorded_at                    DateTime DEFAULT now()  -- project-internal: when CHF wrote this CDR row
)
ENGINE = ReplacingMergeTree(recorded_at)
ORDER BY (charging_data_ref, invocation_sequence_number, service_type)
TTL recorded_at + INTERVAL 90 DAY DELETE;
