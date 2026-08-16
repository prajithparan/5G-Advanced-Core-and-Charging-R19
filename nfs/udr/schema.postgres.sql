-- nfs/udr PostgreSQL schema (ADR-0068 -- gap-closure: real UDR persistence, Tier 1a of the
-- free5GC/open5gs source comparison). Both real references (free5gc/udr's real MongoDB backend,
-- open5gs's lib/dbi/ogs-mongoc.c) treat UDR as a genuinely persistent repository -- the whole
-- reason a UDR exists as a separate NF from UDM. This project's own mandated storage stack
-- (CLAUDE.md) is PostgreSQL for exactly this kind of state, not MongoDB specifically; the real
-- persistence property (survives restart) is what matters, not the vendor.
--
-- Both tables store the real Nudr_DataRepository context-data resources as opaque JSONB --
-- matching bss/product-catalog's own established "PostgreSQL jsonb for variable-shape nested
-- fields" pattern (ADR-0053) -- since these resources (Amf3GppAccessRegistration, SmfRegistration)
-- are already carried through this NF as generated OpenAPI JSON, same as the in-memory store this
-- replaces did (nfs/udr/src/stores.hpp's own std::unordered_map<std::string, nlohmann::json>).

CREATE TABLE IF NOT EXISTS udr_amf_context (
    ue_id   TEXT PRIMARY KEY,
    context JSONB NOT NULL
);

-- Composite primary key (ue_id, pdu_session_id) matches the real spec resource path
-- /subscription-data/{ueId}/context-data/smf-registrations/{pduSessionId} exactly -- one row per
-- UE+PDU-session, same shape QuerySmfRegList's real "list every registration for a given ueId"
-- semantics already required of the in-memory store this replaces.
CREATE TABLE IF NOT EXISTS udr_smf_registration (
    ue_id          TEXT NOT NULL,
    pdu_session_id TEXT NOT NULL,
    registration   JSONB NOT NULL,
    PRIMARY KEY (ue_id, pdu_session_id)
);

-- ADR-0069 (gap-closure Tier 1b): the real Nudr_DataRepository `provisioned-data` group
-- (TS29505_Subscription_Data.yaml's /subscription-data/{ueId}/{servingPlmnId}/provisioned-data/
-- am-data|smf-selection-subscription-data|sm-data) -- confirmed by direct read of the real YAML
-- this resource group is GET-only, no create/update operation exists in the spec at all (the real
-- provisioning path for this data in a production deployment is an out-of-band OSS/BSS tool
-- writing directly into UDR's backing store, not this public API) -- same real reasoning
-- nfs/udr/src/main.cpp's own file header already gave for deferring this group originally. Row
-- keyed by (ue_id, serving_plmn_id) per the real path shape; one JSONB column per real sub-
-- resource since all three are always read/seeded together per UE in this slice.
CREATE TABLE IF NOT EXISTS udr_provisioned_data (
    ue_id            TEXT NOT NULL,
    serving_plmn_id  TEXT NOT NULL,
    am_data          JSONB,
    smf_sel_data     JSONB,
    sm_data          JSONB,
    PRIMARY KEY (ue_id, serving_plmn_id)
);

-- ADR-0072 (gap-closure: real N28 end-to-end): the real Nudr_DataRepository `policy-data` group's
-- SM policy resource (TS29519_Policy_Data.yaml's /policy-data/ues/{ueId}/sm-data, real schema
-- SmPolicyData) -- genuinely DIFFERENT real resource from udr_provisioned_data's own `sm_data`
-- column above (that one is SessionManagementSubscriptionData, TS29503_Nudm_SDM/
-- TS29505_Subscription_Data, keyed by ueId+servingPlmnId, GET-only; this one is SmPolicyData,
-- TS29519_Policy_Data, keyed by ueId alone, real GET+PATCH). Confirmed by direct read of the real
-- YAML: unlike provisioned-data, this resource DOES support PATCH (application/merge-patch+json,
-- SmPolicyDataPatch) -- but the real spec still defines no POST/create operation for it at all
-- (real 3GPP assumption: an out-of-band OSS/BSS tool creates the initial document; PATCH only
-- ever modifies an existing one). This project's own store deliberately treats PATCH as
-- upsert-capable (missing ueId = start from an empty document) so this resource can genuinely be
-- created from a future GUI, per explicit user direction -- a disclosed, deliberate divergence
-- from the real spec's own implicit assumption, not a misreading of it.
CREATE TABLE IF NOT EXISTS udr_sm_policy_data (
    ue_id       TEXT PRIMARY KEY,
    policy_data JSONB NOT NULL
);
