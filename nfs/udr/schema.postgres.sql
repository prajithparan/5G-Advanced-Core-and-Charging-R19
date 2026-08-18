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

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0093): a real, distinct resource
-- from udr_amf_context above -- TS29505_Subscription_Data.yaml's own
-- /subscription-data/{ueId}/context-data/amf-non-3gpp-access is a separate real path/schema
-- (AmfNon3GppAccessRegistration), not the same document reused.
CREATE TABLE IF NOT EXISTS udr_amf_non3gpp_context (
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
--
-- Gap-closure (task #106, ADR-0106): `lcs_bca_data` column added -- the real, sibling
-- `.../provisioned-data/lcs-bca-data` resource (real schema `LcsBroadcastAssistanceTypesData`,
-- QueryLcsBcaData, same real GET-only shape, same (ue_id, serving_plmn_id) key) -- a genuinely
-- distinct real sub-resource under this same group, not a rename.
CREATE TABLE IF NOT EXISTS udr_provisioned_data (
    ue_id            TEXT NOT NULL,
    serving_plmn_id  TEXT NOT NULL,
    am_data          JSONB,
    smf_sel_data     JSONB,
    sm_data          JSONB,
    lcs_bca_data     JSONB,
    PRIMARY KEY (ue_id, serving_plmn_id)
);

-- `CREATE TABLE IF NOT EXISTS` above is a no-op against an already-existing table from a prior
-- run (real persistence property this whole schema relies on) -- this ALTER is what actually adds
-- the new column to an existing real database.
ALTER TABLE udr_provisioned_data ADD COLUMN IF NOT EXISTS lcs_bca_data JSONB;

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

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0083). Real Nudr_DataRepository
-- Authentication Data group (TS29505_Subscription_Data.yaml,
-- /subscription-data/{ueId}/authentication-data/authentication-subscription, real schema
-- AuthenticationSubscription) -- genuinely distinct from udm's own in-process
-- AuthenticationSubscriptionStore (which independently holds this project's own seeded 5G-AKA
-- test-subscriber K/OPc/SQN for the real authentication VECTOR-GENERATION crypto path); this
-- table is the real Nudr_DR-exposed COPY of that same conceptual data, per spec, not yet wired as
-- UDM's actual source of truth for GenerateAuthData -- see this task's own ADR for the real,
-- disclosed architectural note on why the two aren't merged in this pass. Real GET (query) + real
-- PATCH (RFC 6902 JSON Patch, ModifyAuthenticationSubscription -- confirmed by reading the YAML's
-- own requestBody content type, same standard as udr_amf_context's own AmfContextStore above, NOT
-- udr_sm_policy_data's RFC 7396 merge-patch).
CREATE TABLE IF NOT EXISTS udr_authentication_subscription (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Real Nudr_DataRepository Authentication Status document
-- (/subscription-data/{ueId}/authentication-data/authentication-status, real schema AuthEvent --
-- TS29503_Nudm_UEAU.yaml's own AuthEvent, reused directly since the real spec cites it verbatim
-- for this resource, not a distinct type). Real PUT (create/replace) + GET + DELETE, unlike
-- authentication-subscription's own GET+PATCH shape -- confirmed per-operation from the YAML, not
-- assumed uniform across the Authentication Data group.
CREATE TABLE IF NOT EXISTS udr_authentication_status (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Real Nudr_DataRepository `policy-data` group's AM Policy resource
-- (TS29519_Policy_Data.yaml's /policy-data/ues/{ueId}/am-data, real schema AmPolicyData) --
-- genuinely DIFFERENT real resource from udr_provisioned_data's own `am_data` column (that one is
-- AccessAndMobilitySubscriptionData, TS29503_Nudm_SDM/TS29505_Subscription_Data, GET-only; this
-- one is AmPolicyData, the real UDR-side backing for PCF's own Npcf_AMPolicyControl, TS 29.519,
-- real GET+PATCH). Same real RFC 7396 merge-patch + upsert-on-PATCH precedent as
-- udr_sm_policy_data above, confirmed by reading this resource's own YAML directly
-- (application/merge-patch+json, AmPolicyDataPatch).
CREATE TABLE IF NOT EXISTS udr_am_policy_data (
    ue_id       TEXT PRIMARY KEY,
    policy_data JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0097). Real Nudr_DataRepository
-- SMSF Registration context-data group (TS29505_Subscription_Data.yaml's
-- /subscription-data/{ueId}/context-data/smsf-3gpp-access and .../smsf-non-3gpp-access) --
-- both real, distinct resources per spec (two separate real paths/operationIds
-- CreateSmsfContext3gpp/QuerySmsfContext3gpp/DeleteSmsfContext3gpp vs.
-- CreateSmsfContextNon3gpp/QuerySmsfContextNon3gpp/DeleteSmsfContextNon3gpp), even though both
-- happen to share the identical real schema `SmsfRegistration` -- same "real, distinct resource,
-- not a rename" reasoning udr_amf_non3gpp_context's own comment already established for the
-- AMF-side pair, kept as two separate tables/stores here too rather than merged. Real GET+PUT+
-- DELETE (confirmed per-operation from the YAML, matching udr_authentication_status's own shape).
CREATE TABLE IF NOT EXISTS udr_smsf_3gpp_context (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

CREATE TABLE IF NOT EXISTS udr_smsf_non3gpp_context (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0098). Real Nudr_DataRepository
-- IP-SM-GW Registration context-data resource
-- (/subscription-data/{ueId}/context-data/ip-sm-gw, real schema IpSmGwRegistration). Real, richer
-- operation set than the SMSF pair above: PUT (CreateIpSmGwContext) + GET (QueryIpSmGwContext) +
-- PATCH (ModifyIpSmGwContext, real application/json-patch+json -- RFC 6902, confirmed by reading
-- the YAML directly, same standard as udr_amf_context's own patch, NOT the RFC 7396 merge-patch
-- style) + DELETE (DeleteIpSmGwContext), confirmed per-operation from the YAML, not assumed
-- uniform across the context-data group.
CREATE TABLE IF NOT EXISTS udr_ip_sm_gw_context (
    ue_id   TEXT PRIMARY KEY,
    context JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0099). Real Nudr_DataRepository
-- Message Waiting Data (Document) resource (/subscription-data/{ueId}/context-data/mwd, real
-- schema MessageWaitingData -- a `mwdList` of `SmscData` entries, each a real SMSC MAP or Diameter
-- address holding SMS awaiting delivery to the UE). Real PUT (CreateMessageWaitingData, genuinely
-- distinguishes 201-Created from 204-updated per the YAML, unlike IpSmGwContextStore's own
-- always-204 PUT above -- confirmed per-operation, not assumed uniform) + GET
-- (QueryMessageWaitingData) + PATCH (ModifyMessageWaitingData, real application/json-patch+json --
-- RFC 6902, same standard as udr_ip_sm_gw_context's own patch) + DELETE
-- (DeleteMessageWaitingData).
CREATE TABLE IF NOT EXISTS udr_mwd (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0100). Real Nudr_DataRepository
-- Roaming Information (Document) resource (/subscription-data/{ueId}/context-data/
-- roaming-information, real schema RoamingInfoUpdate -- TS29503_Nudm_UECM.yaml, `roaming` bool +
-- mandatory `servingPlmn` + optional `contextInfo`). Real, simple operation set, confirmed by
-- direct YAML read: PUT (UpdateRoamingInformation, real distinct 201-vs-204 response codes, same
-- shape as udr_amf_non3gpp_context's own PUT) + GET (QueryRoamingInformation) only -- no
-- PATCH/DELETE exists for this resource in the spec.
CREATE TABLE IF NOT EXISTS udr_roaming_information (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0101). Real Nudr_DataRepository
-- PEI Information (Document) resource (/subscription-data/{ueId}/context-data/pei-info, real
-- schema PeiUpdateInfo -- an allOf composition of TS29503_Nudm_UECM.yaml's own base PeiUpdateInfo
-- (mandatory `pei`) plus this file's own PeiUpdateInfoExt (lastPeiChangeTimestamp/
-- lastImeiChangeTimestamp/previousPei/previousPeiTimestamp), flattened by sbi-codegen into
-- PeiUpdateInfo_Subscription_Data to disambiguate from the base type's own PeiUpdateInfo_Nudm_UECM
-- name). Real, simple operation set, confirmed by direct YAML read: PUT
-- (CreateOrUpdatePeiInformation, real distinct 201-vs-204 response codes) + GET
-- (QueryPeiInformation) only -- no PATCH/DELETE exists for this resource in the spec, same shape
-- as udr_roaming_information above.
CREATE TABLE IF NOT EXISTS udr_pei_info (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0102). Real Nudr_DataRepository
-- Enhanced Coverage Restriction Data resource (/subscription-data/{ueId}/coverage-restriction-data,
-- real schema EnhancedCoverageRestrictionData -- TS29503_Nudm_SDM.yaml, a `plmnEcInfoList` of
-- `PlmnEcInfo` entries). Confirmed by direct YAML read: this resource is genuinely GET-only
-- (QueryCoverageRestrictionData) -- no create/update operation exists at all, same real
-- "provisioned out-of-band, seeded at startup" precedent already established for
-- udr_provisioned_data above (ADR-0069).
CREATE TABLE IF NOT EXISTS udr_coverage_restriction_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0103). Real Nudr_DataRepository LCS
-- Privacy Subscription Data resource (/subscription-data/{ueId}/lcs-privacy-data, real schema
-- LcsPrivacyData -- $ref'd verbatim from TS29503_Nudm_SDM.yaml). Confirmed by grepping every
-- operationId referencing this path: genuinely GET-only (QueryLcsPrivacyData), same real
-- "provisioned out-of-band, seeded at startup" shape as udr_coverage_restriction_data/
-- udr_provisioned_data above.
CREATE TABLE IF NOT EXISTS udr_lcs_privacy_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0104). Real Nudr_DataRepository LCS
-- Subscription Data resource (/subscription-data/{ueId}/lcs-subscription-data, real schema
-- LcsSubscriptionData -- $ref'd verbatim from TS29503_Nudm_SDM.yaml, all fields optional:
-- configuredLmfId, pruInd, lpHapType, userPlanePosIndLmf). Confirmed by grepping every operationId
-- referencing this path: genuinely GET-only (QueryLcsSubscriptionData), same real "provisioned
-- out-of-band, seeded at startup" shape as udr_lcs_privacy_data above.
CREATE TABLE IF NOT EXISTS udr_lcs_subscription_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0105). Real Nudr_DataRepository LCS
-- Mobile Originated Subscription Data resource (/subscription-data/{ueId}/lcs-mo-data, real schema
-- LcsMoData -- $ref'd verbatim from TS29503_Nudm_SDM.yaml, mandatory `allowedServiceClasses`
-- (minItems 1) + optional `moAssistanceDataTypes`). Confirmed by grepping every operationId
-- referencing this path: genuinely GET-only (QueryLcsMoData), same real "provisioned out-of-band,
-- seeded at startup" shape as udr_lcs_subscription_data above.
CREATE TABLE IF NOT EXISTS udr_lcs_mo_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0107). Real Nudr_DataRepository
-- Parameter Provision (Document) resource (/subscription-data/{ueId}/pp-data, real schema PpData
-- -- $ref'd verbatim from TS29503_Nudm_PP.yaml, all fields optional). Confirmed by direct YAML
-- read: real GET (GetppData) + real PATCH (ModifyPpData, application/json-patch+json -- RFC 6902,
-- same standard as udr_authentication_subscription's own patch) -- no PUT/DELETE exists for this
-- resource in the spec. No POST/create operation exists either, so -- same disclosed, deliberate
-- precedent already established for udr_authentication_subscription/udr_sm_policy_data --
-- apply_patch is upsert-capable (missing ueId = start from an empty document).
CREATE TABLE IF NOT EXISTS udr_pp_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0108). Real Nudr_DataRepository
-- Parameter Provision profile Data (Document) resource (/subscription-data/{ueId}/pp-profile-data,
-- real schema PpProfileData -- an `allowedMtcProviders` map keyed by PpDataType (or the special
-- key "ALL"), every field optional). Confirmed by grepping every operationId referencing this
-- path: genuinely GET-only (QueryPPData), same real "provisioned out-of-band, seeded at startup"
-- shape as the other GET-only UDR resources already closed (ADR-0102/0103/0104/0105).
CREATE TABLE IF NOT EXISTS udr_pp_profile_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0109). Real Nudr_DataRepository
-- Provisioned Parameter Data Entry resource
-- (/subscription-data/{ueId}/pp-data-store/{afInstanceId}, real schema PpDataEntry --
-- TS29503_Nudm_PP.yaml, every field optional) -- confirmed by direct YAML read: real
-- PUT (Create PP Data Entry) + GET (Get PP Data Entry) + DELETE (Delete PP Data Entry), plus a
-- real sibling collection resource (/subscription-data/{ueId}/pp-data-store, Get Multiple PP Data
-- Entries, real schema PpDataEntryList). Composite key (ue_id, af_instance_id) matches the real
-- spec resource path exactly -- same real "one row per UE+afInstanceId" shape already established
-- for udr_smf_registration's own (ue_id, pdu_session_id) key.
CREATE TABLE IF NOT EXISTS udr_pp_data_entry (
    ue_id          TEXT NOT NULL,
    af_instance_id TEXT NOT NULL,
    data           JSONB NOT NULL,
    PRIMARY KEY (ue_id, af_instance_id)
);
