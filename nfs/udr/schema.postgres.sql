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
--
-- Gap-closure (task #106, ADR-0125): `sms_mng_data` column added -- the real, sibling
-- `.../provisioned-data/sms-mng-data` resource (real schema `SmsManagementSubscriptionData`,
-- QuerySmsMngData, same real GET-only shape, same (ue_id, serving_plmn_id) key) -- a genuinely
-- distinct real sub-resource under this same group, not a rename.
--
-- Gap-closure (task #106, ADR-0126): `sms_data` column added -- the real, sibling
-- `.../provisioned-data/sms-data` resource (real schema `SmsSubscriptionData`, QuerySmsData,
-- same real GET-only shape, same (ue_id, serving_plmn_id) key) -- a genuinely distinct real
-- sub-resource under this same group, not a rename, and NOT the same as UDM's own
-- `sms-subscription-data` naming elsewhere -- this is the real Nudr_DataRepository resource name.
--
-- Gap-closure (task #106, ADR-0127): `trace_data` column added -- the real, sibling
-- `.../provisioned-data/trace-data` resource (real response schema
-- `TraceDataOrSharedTraceDataId`, a real `oneOf` of the full `TraceData` object
-- (TS29571_CommonData.yaml) or a bare `SharedDataId` string reference, QueryTraceData, same real
-- GET-only shape, same (ue_id, serving_plmn_id) key). This project's stores persist/return raw
-- opaque JSON for every provisioned-data sub-resource (no strong DTO layer at this level), so the
-- real `oneOf` union needs no special handling -- whichever real shape is seeded is returned
-- verbatim.
CREATE TABLE IF NOT EXISTS udr_provisioned_data (
    ue_id            TEXT NOT NULL,
    serving_plmn_id  TEXT NOT NULL,
    am_data          JSONB,
    smf_sel_data     JSONB,
    sm_data          JSONB,
    lcs_bca_data     JSONB,
    sms_mng_data     JSONB,
    sms_data         JSONB,
    trace_data       JSONB,
    PRIMARY KEY (ue_id, serving_plmn_id)
);

-- `CREATE TABLE IF NOT EXISTS` above is a no-op against an already-existing table from a prior
-- run (real persistence property this whole schema relies on) -- these ALTERs are what actually
-- add the new columns to an existing real database.
ALTER TABLE udr_provisioned_data ADD COLUMN IF NOT EXISTS lcs_bca_data JSONB;
ALTER TABLE udr_provisioned_data ADD COLUMN IF NOT EXISTS sms_mng_data JSONB;
ALTER TABLE udr_provisioned_data ADD COLUMN IF NOT EXISTS sms_data JSONB;
ALTER TABLE udr_provisioned_data ADD COLUMN IF NOT EXISTS trace_data JSONB;

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

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0110). Real Nudr_DataRepository
-- individual Shared Data resource (/subscription-data/shared-data/{sharedDataId}, real schema
-- SharedData -- TS29503_Nudm_SDM.yaml, mandatory sharedDataId + optional sharedAmData/
-- sharedSmsSubsData/sharedSmsMngSubsData/sharedDnnConfigurations and others). Confirmed by
-- grepping every operationId under the real /subscription-data/shared-data* prefix: genuinely
-- GET-only (GetIndividualSharedData), same real "provisioned out-of-band, seeded at startup"
-- shape as the other GET-only UDR resources. Genuinely NOT per-UE -- keyed by shared_data_id alone
-- (real 3GPP concept: operator-shared default profile data reused across many UEs), unlike every
-- other UDR resource closed so far. Real, disclosed scope narrowing: the real sibling collection
-- resource (/subscription-data/shared-data, GetSharedData, a required comma-separated
-- shared-data-ids array query parameter) is deferred -- this project has no existing precedent
-- anywhere yet for parsing array-shaped query parameters, and building that real capability
-- belongs in its own scoped turn, not bundled into this one.
CREATE TABLE IF NOT EXISTS udr_shared_data (
    shared_data_id TEXT PRIMARY KEY,
    data           JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0111). Real Nudr_DataRepository
-- Operator-Specific Data Container (Document) resource
-- (/subscription-data/{ueId}/operator-specific-data, real response shape: a map keyed by operator
-- specific data element name, values real schema OperatorSpecificDataContainer -- mandatory
-- dataType+value each -- TS29505_Subscription_Data.yaml, no top-level wrapper struct). Confirmed
-- by direct YAML read: real GET (QueryOperSpecData) + real PATCH (ModifyOperSpecData,
-- application/json-patch+json -- RFC 6902, same standard as udr_pp_data's own patch) -- no
-- PUT/DELETE exists for this resource. No POST/create operation exists either, so -- same
-- disclosed, deliberate precedent already established for udr_pp_data -- apply_patch is
-- upsert-capable (missing ueId = start from an empty document).
CREATE TABLE IF NOT EXISTS udr_operator_specific_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0112). Real Nudr_DataRepository
-- Event Exposure Data (Document) resource (/subscription-data/{ueId}/ee-profile-data, real schema
-- EeProfileData -- restrictedEventTypes (array of real EventType enum values,
-- TS29503_Nudm_EE.yaml), allowedMtcProvider, iwkEpcRestricted, every field optional). Confirmed by
-- grepping every operationId referencing this exact path (only one): genuinely GET-only
-- (QueryEEData), same real "provisioned out-of-band, seeded at startup" shape as the other
-- GET-only UDR resources. Real, distinct UDR-side resource from this project's own UDM-side
-- Nudm_EE work (task #105) -- this is the real Nudr_DataRepository backing document, not the
-- UDM service surface.
CREATE TABLE IF NOT EXISTS udr_ee_profile_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0113). Real Nudr_DataRepository
-- `policy-data` group's UE Policy Set resource (/policy-data/ues/{ueId}/ue-policy-set, real
-- schema UePolicySet -- TS29519_Policy_Data.yaml, praInfos/subscCats/uePolicySections, every
-- field optional). Confirmed by direct YAML read: real GET (ReadUEPolicySet) + real PUT
-- (CreateOrReplaceUEPolicySet, real distinct 201-vs-204 response codes) + real PATCH
-- (UpdateUEPolicySet, application/merge-patch+json -- RFC 7396, same standard as
-- udr_am_policy_data's own patch, NOT udr_authentication_subscription's RFC 6902 -- and, unlike
-- udr_am_policy_data's own PATCH, the real spec here only documents 204 as the success response,
-- no 200-with-body option). No DELETE exists for this resource in the spec.
CREATE TABLE IF NOT EXISTS udr_ue_policy_set (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0114). Real Nudr_DataRepository
-- `policy-data` group's Operator-Specific Data resource
-- (/policy-data/ues/{ueId}/operator-specific-data, real response shape: a map keyed by operator
-- specific data element name, values the same real schema OperatorSpecificDataContainer already
-- used by udr_operator_specific_data above -- reused via a real cross-file $ref from
-- TS29519_Policy_Data.yaml into TS29505_Subscription_Data.yaml). Confirmed by grepping this exact
-- path (only one block, two operations): real GET (ReadOperatorSpecificData) + real PATCH
-- (UpdateOperatorSpecificData, application/json-patch+json -- RFC 6902) -- no PUT/DELETE. Real,
-- genuinely distinct resource from the subscription-data-scoped udr_operator_specific_data (real,
-- separate operationId pair, same "distinct resource, not a rename" precedent already established
-- for the AMF/SMSF 3GPP-vs-non-3GPP pairs). No POST/create operation exists either, so
-- apply_patch() is upsert-capable, same disclosed precedent already established.
CREATE TABLE IF NOT EXISTS udr_policy_operator_specific_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0115). Real Nudr_DataRepository
-- `policy-data` group's Sponsor Connectivity Data resource
-- (/policy-data/sponsor-connectivity-data/{sponsorId}, real schema SponsorConnectivityData --
-- mandatory aspIds, optional suppFeat). Confirmed by direct YAML read: genuinely GET-only
-- (ReadSponsorConnectivityData), no other operation exists for this path. Genuinely NOT per-UE --
-- keyed by sponsor_id alone (real 3GPP concept, TS 23.503, sponsored-data-connectivity policy
-- shared across the sponsor's own application service providers), same real "not every UDR
-- resource is UE-scoped" precedent already established for udr_shared_data. Real, disclosed
-- simplification: the real spec also documents a distinct `204` ("resource found but no data
-- available") separate from `404` ("not found at all") -- this project's simple existence-based
-- store model only distinguishes 200-with-data vs 404-not-provisioned, not the finer real
-- "provisioned but empty" case.
CREATE TABLE IF NOT EXISTS udr_sponsor_connectivity_data (
    sponsor_id TEXT PRIMARY KEY,
    data       JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0116). Real Nudr_DataRepository
-- `policy-data` group's individual BDT (Background Data Transfer) Data resource
-- (/policy-data/bdt-data/{bdtReferenceId}, real schema BdtData -- aspId/transPolicy/bdtRefId/
-- nwAreaInfo/numOfUes/volPerUe/dnn/snssai/trafficDes/bdtpStatus/warnNotifEnabled, every field
-- optional). Confirmed by direct YAML read: real GET (ReadIndividualBdtData) + real PUT
-- (CreateIndividualBdtData -- real, disclosed: the spec documents ONLY `201` as a success
-- response for this PUT, unlike ue-policy-set's own PUT which documents 201/200/204; this
-- project's own store is still upsert-capable internally for idempotent retries, but the route
-- always responds `201`, matching the real spec's own single documented status literally rather
-- than inventing an undocumented 204) + real PATCH (UpdateIndividualBdtData,
-- application/merge-patch+json -- RFC 7396, same standard as udr_ue_policy_set's own patch) +
-- real DELETE (DeleteIndividualBdtData). Keyed by bdt_ref_id (BdtReferenceId, a plain string per
-- TS 29.154 clause 5.3.3), genuinely NOT per-UE. Real sibling collection resource
-- (/policy-data/bdt-data, optional bdt-ref-ids array query filter) deferred -- same real
-- array-query-parameter parsing gap already disclosed for shared-data's own list sibling.
CREATE TABLE IF NOT EXISTS udr_bdt_data (
    bdt_ref_id TEXT PRIMARY KEY,
    data       JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0117). Real Nudr_DataRepository
-- PLMN UE Policy Set resource (/policy-data/plmns/{plmnId}/ue-policy-set, real schema
-- UePolicySet -- same type as udr_ue_policy_set's own per-UE resource, praInfos/subscCats/
-- uePolicySections/etc, every field optional). Confirmed by direct YAML read: this resource is
-- genuinely GET-only (ReadPlmnUePolicySet) -- no create/update operation exists at all, same real
-- "provisioned out-of-band, seeded at startup" shape as udr_coverage_restriction_data above. Keyed
-- by plmn_id (VarPlmnId, TS29505_Subscription_Data.yaml -- mcc+mnc concatenated), genuinely NOT
-- per-UE -- a distinct resource from udr_ue_policy_set even though it reuses the same schema type.
CREATE TABLE IF NOT EXISTS udr_plmn_ue_policy_set (
    plmn_id TEXT PRIMARY KEY,
    data    JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0118). Real Nudr_DataRepository
-- Slice-specific Policy Control Data resource (/policy-data/slice-control-data/{snssai}, real
-- schema SlicePolicyData -- mbrUl/mbrDl/remainMbrUl/remainMbrDl/suppFeat, every field optional;
-- PATCH request body is the narrower SlicePolicyDataPatch, remainMbrUl/remainMbrDl only).
-- Confirmed by direct YAML read: real GET (ReadSlicePolicyControlData) + real PATCH
-- (UpdateSlicePolicyControlData, application/merge-patch+json -- RFC 7396) -- no PUT/POST create
-- operation exists at all, so (same disclosed, deliberate precedent already established for
-- AmPolicyDataStore/SmPolicyDataStore) merge_patch is upsert-capable. Keyed by snssai: the real
-- YAML types this path parameter as the Snssai *object* schema with no documented string
-- encoding for a bare path segment (checked, not assumed -- genuinely different from every other
-- real 5G_APIs YAML use of Snssai as a query param, which always wraps it in a real
-- `content: application/json` parameter instead). This project reuses its own already-disclosed,
-- not-spec-mandated "sst + '-' + sd" string convention (ADR-0072/PCF's snssai_map_key) for
-- consistency rather than inventing a second answer to the same open question.
CREATE TABLE IF NOT EXISTS udr_slice_control_data (
    snssai TEXT PRIMARY KEY,
    data   JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0119). Real Nudr_DataRepository
-- group-specific Policy Control Data resource (/policy-data/group-control-data/{intGroupId}, real
-- schema GroupPolicyData -- maxGroupMbrUl/maxGroupMbrDl/remainGroupMbrUl/remainGroupMbrDl/
-- suppFeat, every field optional; PATCH request body is the narrower GroupPolicyDataPatch).
-- Confirmed by direct YAML read: real GET (ReadGroupPolCtrlData) + real PATCH
-- (ModifyGroupPolCtrlData, application/merge-patch+json -- RFC 7396) -- no PUT/POST create
-- operation exists at all, so (same disclosed, deliberate precedent already established for
-- AmPolicyDataStore/SlicePolicyDataStore) merge_patch is upsert-capable. Keyed by intGroupId
-- (real GroupId schema, TS29571_CommonData.yaml -- plain string, real pattern cited from
-- TS 23.003 clause 19.9, no encoding ambiguity unlike slice-control-data's own snssai key).
CREATE TABLE IF NOT EXISTS udr_group_control_data (
    int_group_id TEXT PRIMARY KEY,
    data         JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0120). Real GetRoutingIDs resource
-- (/routing-ids, real schema RoutingIdResult -- routingIndicators: array of strings, pattern
-- ^[0-9]{1,4}$, minItems 1). Genuinely DIFFERENT real Nudr API from every other table in this
-- file: TS29504_Nudr_GroupIDmap.yaml's Nudr_GroupIDmap service (real server base path
-- `/nudr-group-id-map/v1`, real OAuth2 scope `nudr-group-id-map`), not Nudr_DataRepository's own
-- `/nudr-dr/v2` -- does NOT count toward the "N of free5GC's ~42+ Nudr_DataRepository resources"
-- metric tracked elsewhere in this file's own comments. Confirmed by direct YAML read: real
-- GET-only (GetRoutingIDs), no create/update operation exists at all, same "provisioned
-- out-of-band, seeded at startup" shape as every other GET-only resource in this project.
-- Composite-keyed by (nf_type, nf_group_id) per the real spec's own two required query
-- parameters (`nf-type`: real NFType enum string, `nf-group-id`: real NfGroupId plain string).
CREATE TABLE IF NOT EXISTS udr_routing_ids (
    nf_type      TEXT NOT NULL,
    nf_group_id  TEXT NOT NULL,
    data         JSONB NOT NULL,
    PRIMARY KEY (nf_type, nf_group_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0121). Real Nudr_DataRepository NIDD
-- Authorization Info context-data resource (/subscription-data/{ueId}/context-data/
-- nidd-authorizations, real schema NiddAuthorizationInfo -- niddAuthorizationList: required array
-- of AuthorizationInfo, TS29122_CommonData_grp.hpp-generated per sbi-codegen's own real grouping).
-- Real, disclosed correction: this project's own header comments previously lumped this resource
-- in with ee-subscriptions/sdm-subscriptions as a deferred "deeply nested sub-subscription" shape
-- without individually checking the real YAML -- it is genuinely a flat per-UE document. Real
-- CreateNIDDAuthorizationInfo/GetNiddAuthorizationInfo/ModifyNiddAuthorizationInfo/
-- RemoveNiddAuthorizationInfo: PUT+GET+PATCH+DELETE, real distinct 201-vs-204 PUT response codes
-- (same shape as AmfContextStore's own real context-data resource), real RFC 6902
-- application/json-patch+json PATCH, real DELETE (which amf-3gpp-access's own resource lacks).
CREATE TABLE IF NOT EXISTS udr_nidd_authorization_info (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0122). Real Query/Modify Identity
-- Data by SUPI or GPSI resource (/subscription-data/{ueId}/identity-data, real schema
-- IdentityData -- supiList/gpsiList/allowedAfIds, all optional arrays). Confirmed by direct YAML
-- read: real GET (GetIdentityData) + real PATCH (ModifyIdentityData, real RFC 6902
-- application/json-patch+json, NOT RFC 7396 merge-patch like slice-control-data/
-- group-control-data's own PATCH standard) -- no PUT/POST create operation exists at all, so
-- (same disclosed, deliberate precedent already established for PpDataStore/
-- OperatorSpecificDataStore) apply_patch is upsert-capable. Real, disclosed simplification: the
-- real spec's optional `app-port-id` query param and conditional-request headers
-- (If-None-Match/If-Modified-Since, Cache-Control/ETag/Last-Modified) are not implemented -- same
-- "no conditional-GET semantics anywhere in this project yet" gap as every other GET route.
CREATE TABLE IF NOT EXISTS udr_identity_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0123). Real Query ODB Data by SUPI
-- or GPSI resource (/subscription-data/{ueId}/operator-determined-barring-data, real schema
-- OdbData -- roamingOdb: optional RoamingOdb enum). Confirmed by direct YAML read: this resource
-- is genuinely GET-only (GetOdbData) -- no create/update operation exists at all, same real
-- "provisioned out-of-band, seeded at startup" shape as udr_coverage_restriction_data above.
CREATE TABLE IF NOT EXISTS udr_odb_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0128). Real V2X Subscription Data
-- resource (/subscription-data/{ueId}/v2x-data, real schema V2xSubscriptionData --
-- nrV2xServicesAuth/lteV2xServicesAuth/nrUePc5Ambr/ltePc5Ambr, every field optional). Confirmed by
-- direct YAML read: this resource is genuinely GET-only (QueryV2xData) -- no create/update
-- operation exists at all, same real "provisioned out-of-band, seeded at startup" shape as
-- udr_coverage_restriction_data above. Keyed by ue_id alone, genuinely NOT part of the
-- provisioned-data group's own (ue_id, serving_plmn_id) composite key shape.
CREATE TABLE IF NOT EXISTS udr_v2x_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0129). Real ProSe Service
-- Subscription Data resource (/subscription-data/{ueId}/prose-data, real schema
-- ProseSubscriptionData -- proseServiceAuth/nrUePc5Ambr/proseAllowedPlmn, every field optional).
-- Confirmed by direct YAML read: this resource is genuinely GET-only (real spec operationId
-- `QueryPorseData` -- a real, literal typo in TS29505_Subscription_Data.yaml itself, cited as-is,
-- not corrected) -- no create/update operation exists at all, same real "provisioned out-of-band,
-- seeded at startup" shape as udr_v2x_data above. Keyed by ue_id alone.
CREATE TABLE IF NOT EXISTS udr_prose_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0130). Real User Consent
-- Subscription Data resource (/subscription-data/{ueId}/uc-data, real schema UcSubscriptionData
-- (TS29503_Nudm_SDM.yaml) -- a single optional userConsentPerPurposeList map, no required fields
-- at all). Confirmed by direct YAML read: this resource is genuinely GET-only (real spec
-- operationId QueryUserConsentData) -- no create/update operation exists at all, same real
-- "provisioned out-of-band, seeded at startup" shape as udr_prose_data above. Keyed by ue_id alone.
CREATE TABLE IF NOT EXISTS udr_uc_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0131). Real Time Synchronization
-- Subscription Data resource (/subscription-data/{ueId}/time-sync-data, real schema
-- TimeSyncSubscriptionData (TS29503_Nudm_SDM.yaml) -- required afReqAuthorizations (oneOf
-- gptpAllowedInfoList/astiAllowedInfo) + required serviceIds array of TimeSyncServiceId, each
-- requiring a `reference` string). Confirmed by direct YAML read: this resource is genuinely
-- GET-only (real spec operationId QueryTimeSyncSubscriptionData) -- no create/update operation
-- exists at all, same real "provisioned out-of-band, seeded at startup" shape as udr_uc_data
-- above. Keyed by ue_id alone.
CREATE TABLE IF NOT EXISTS udr_time_sync_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0133). Real UE's Location
-- Information (Document) resource (/subscription-data/{ueId}/context-data/location, real schema
-- LocationInfo (TS29503_Nudm_UECM.yaml) -- required registrationLocationInfoList array of
-- RegistrationLocationInfo, each requiring amfInstanceId + accessTypeList). Confirmed by direct
-- YAML read: this resource is genuinely GET-only (real spec operationId QueryUeLocation), no
-- required/complex query parameters (unlike the sibling nidd-authorization-data resource, which
-- is genuinely blocked on real complex-object query-param parsing this project has no precedent
-- for -- deliberately skipped, not attempted) -- no create/update operation exists at all, same
-- real "provisioned out-of-band, seeded at startup" shape as udr_time_sync_data above. Keyed by
-- ue_id alone.
CREATE TABLE IF NOT EXISTS udr_location_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0134). Real A2X Subscription Data
-- resource (/subscription-data/{ueId}/a2x-data, real schema A2xSubscriptionData
-- (TS29503_Nudm_SDM.yaml) -- nrA2xServicesAuth/lteA2xServicesAuth/nrUePc5Ambr/ltePc5Ambr, every
-- field optional). Confirmed by direct YAML read: this resource is genuinely GET-only (real spec
-- operationId QueryA2xData) -- no create/update operation exists at all, same real "provisioned
-- out-of-band, seeded at startup" shape as udr_v2x_data/udr_prose_data. Keyed by ue_id alone.
CREATE TABLE IF NOT EXISTS udr_a2x_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0135). Real Ranging and Sidelink
-- Positioning Privacy Subscription Data resource (/subscription-data/{ueId}/rangingsl-privacy-data,
-- real schema RangingSlPrivacyData (TS29503_Nudm_SDM.yaml) -- rslppi/rangingSlUnrelatedClass/
-- rangingSlPlmnOperatorClasses/rangingSlEvtRptExpectedArea, every top-level field optional).
-- Confirmed by direct YAML read: this resource is genuinely GET-only (real spec operationId
-- QueryRangingSlPrivacyData) -- no create/update operation exists at all, same real "provisioned
-- out-of-band, seeded at startup" shape as udr_a2x_data. Real, disclosed simplification: the
-- spec's own optional (not required) array-style `fields` query parameter (RFC 6570 form-style,
-- explode=false) for field-selection filtering is not honored -- the full stored document is
-- always returned, same disclosed precedent as other GET-only resources in this project. Keyed by
-- ue_id alone.
CREATE TABLE IF NOT EXISTS udr_rangingsl_privacy_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0136). Real Ranging and Sidelink
-- Positioning Service Subscription Data resource (/subscription-data/{ueId}/ranging-slpos-data,
-- real schema RangingSlPosSubscriptionData (TS29503_Nudm_SDM.yaml) -- rangingSlPosAuth/
-- rangingSlPosPlmn/rangingSlPosQos, every top-level field optional). Confirmed by direct YAML
-- read: this resource is genuinely GET-only (real spec operationId QueryRangingSlPosData) -- no
-- create/update operation exists at all, same real "provisioned out-of-band, seeded at startup"
-- shape as udr_rangingsl_privacy_data above. Keyed by ue_id alone.
CREATE TABLE IF NOT EXISTS udr_ranging_slpos_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0137). Real 5MBS Subscription Data
-- (Document) resource (/subscription-data/{ueId}/5mbs-data, real schema MbsSubscriptionData
-- (TS29503_Nudm_SDM.yaml) -- mbsAllowed/mbsSessionIdList/ueMbsAssistanceInfo, every field
-- optional). Confirmed by direct YAML read: this resource is genuinely GET-only (real spec
-- operationId Query5mbsData) -- no create/update operation exists at all, same real "provisioned
-- out-of-band, seeded at startup" shape as udr_ranging_slpos_data above. Keyed by ue_id alone.
CREATE TABLE IF NOT EXISTS udr_5mbs_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0139). Real Service Specific
-- Authorization Info (Document) context-data resource
-- (/subscription-data/{ueId}/context-data/service-specific-authorizations/{serviceType}, real
-- schema ServiceSpecificAuthorizationInfo -- required serviceSpecificAuthorizationList, a map of
-- AuthorizationInfo keyed by authId). Real CreateServiceSpecificAuthorizationInfo/
-- GetServiceSpecificAuthorizationInfo/ModifyServiceSpecificAuthorizationInfo/
-- RemoveServiceSpecificAuthorizationInfo: PUT+GET+PATCH+DELETE, real distinct 201-vs-204 PUT
-- response codes, real RFC 6902 application/json-patch+json PATCH (same shape as
-- udr_nidd_authorization_info's own resource, ADR-0121). Composite key (ue_id, service_type)
-- matches PpDataEntryStore's own precedent (ADR-0109) -- serviceType is a real plain-string enum
-- (TS29503_Nudm_SSAU.yaml), no path-segment encoding ambiguity. Real, disclosed: the sibling
-- GET-only resource at /subscription-data/{ueId}/service-specific-authorization-data/{serviceType}
-- (GetSSAuData) is genuinely blocked, not attempted -- its spec requires a complex-object query
-- parameter (single-nssai via content: application/json) this project has no parsing precedent
-- for, same class of gap already disclosed for nidd-authorization-data.
CREATE TABLE IF NOT EXISTS udr_service_specific_auth_info (
    ue_id        TEXT NOT NULL,
    service_type TEXT NOT NULL,
    data         JSONB NOT NULL,
    PRIMARY KEY (ue_id, service_type)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0140). Real Group Identifiers
-- mapping resource (/subscription-data/group-data/group-identifiers, real schema
-- GroupIdentifiers -- extGroupId/intGroupId/ueIdList/allowedAfIds, every field optional).
-- Confirmed by direct YAML read: this resource is genuinely GET-only (real spec operationId
-- GetGroupIdentifiers), genuinely NOT per-UE and has no path parameters at all -- real, optional
-- query parameters ext-group-id and int-group-id (both plain strings, no encoding ambiguity)
-- select which group to look up. Real, disclosed simplification: since the spec marks both
-- filters optional with no defined "list all groups" behavior this project has any precedent for
-- returning, at least one of ext-group-id/int-group-id is required by this implementation (400
-- otherwise) -- same "no unfiltered collection scan" precedent as this project's other resources.
-- The real ue-id-ind query parameter (controls whether ueIdList is included in the response) is
-- not honored -- ueIdList is always included regardless, a disclosed simplification. Both
-- ext_group_id and int_group_id are real alternate lookup keys for the same seeded record.
CREATE TABLE IF NOT EXISTS udr_group_identifiers (
    ext_group_id TEXT PRIMARY KEY,
    int_group_id TEXT NOT NULL UNIQUE,
    data         JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0141). Real NSSAI update ack
-- (Document) resource (/subscription-data/{ueId}/ue-update-confirmation-data/subscribed-snssais,
-- real schema NssaiAckData -- required provisioningTime (DateTime) + ueUpdateStatus (real
-- UeUpdateStatus enum)). Real CreateOrUpdateNssaiAck/QueryNssaiAck: real PUT+GET, no PATCH/DELETE
-- operation exists in the spec at all. Real, disclosed: unlike this project's other PUT
-- resources, the spec documents only a single `204` response for this PUT (no `201`) -- no
-- create-vs-update distinction exists for this resource, so put() always returns void, not a
-- bool. Keyed by ue_id (real path schema is Supi, not the more general VarUeId used elsewhere).
CREATE TABLE IF NOT EXISTS udr_nssai_ack_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0142). Real CAG update ack
-- (Document) resource (/subscription-data/{ueId}/ue-update-confirmation-data/subscribed-cag,
-- real schema CagAckData -- required provisioningTime (DateTime) + ueUpdateStatus (real
-- UeUpdateStatus enum), identical shape to NssaiAckData). Real CreateCagUpdateAck/QueryCagAck:
-- real PUT+GET, no PATCH/DELETE operation exists in the spec at all. Real, disclosed: same as
-- udr_nssai_ack_data's own resource above, the spec documents only a single `204` response for
-- this PUT (no `201`) -- no create-vs-update distinction exists, so put() returns void. Keyed by
-- ue_id (real path schema is Supi).
CREATE TABLE IF NOT EXISTS udr_cag_ack_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0143). Real Authentication SoR
-- (Document) resource (/subscription-data/{ueId}/ue-update-confirmation-data/sor-data, real
-- schema SorData -- required provisioningTime (DateTime) + ueUpdateStatus, plus optional
-- sorXmacIue/sorMacIue/meSupportOfSorCmci/meSupportOfSorSnpnSi/meSupportOfSorSnpnSiLs). Real
-- CreateAuthenticationSoR/QueryAuthSoR/UpdateAuthenticationSoR: real PUT+GET+PATCH. Real,
-- disclosed: same as udr_nssai_ack_data/udr_cag_ack_data, the spec documents only a single `204`
-- response for PUT (no `201`) -- no create-vs-update distinction exists, so put() returns void.
-- Unlike either ack resource, a real RFC 6902 application/json-patch+json PATCH also exists here
-- (apply_patch, NOT upsert-capable -- requires a prior PUT). Keyed by ue_id (real path schema is
-- Supi).
CREATE TABLE IF NOT EXISTS udr_sor_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0143). Real Authentication UPU
-- (Document) resource (/subscription-data/{ueId}/ue-update-confirmation-data/upu-data, real
-- schema UpuData -- required provisioningTime (DateTime) + ueUpdateStatus, plus optional
-- upuXmacIue/upuMacIue/meSupportUHP). Real CreateAuthenticationUPU/QueryAuthUPU: real PUT+GET
-- only, no PATCH/DELETE operation exists in the spec at all -- genuinely narrower than
-- udr_sor_data above despite sharing the same UeUpdateStatus-based schema shape. Real, disclosed:
-- same 204-only PUT, no create-vs-update distinction. Keyed by ue_id (real path schema is Supi).
CREATE TABLE IF NOT EXISTS udr_upu_data (
    ue_id TEXT PRIMARY KEY,
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0144). Real group-data individual
-- 5G VN Group Configuration resource
-- (/subscription-data/group-data/5g-vn-groups/{externalGroupId}, real schema
-- 5GVnGroupConfiguration generated as sbi_gen::N5GVnGroupConfiguration -- an optional
-- N5GVnGroupData wrapping required dnn/sNssai plus several optional fields). Real
-- Create5GVnGroup/Get5GVnGroupConfiguration/Modify5GVnGroup/Delete5GVnGroup: real
-- GET+PUT+PATCH+DELETE. Real, disclosed: same as udr_bdt_data, the real PUT documents ONLY `201`
-- (operationId literally "Create...", no update-via-PUT status), so this project's own route
-- always responds 201, not 204, even though put() is internally upsert-capable. PATCH is real RFC
-- 6902 application/json-patch+json (confirmed by direct YAML read -- NOT the RFC 7396 merge-patch
-- udr_bdt_data itself uses), apply_patch NOT upsert-capable (PUT is the real create path). Keyed
-- by ext_group_id (real path schema is ExtGroupId, a plain string). First real group-data
-- sub-resource closed since group-identifiers (ADR-0140).
CREATE TABLE IF NOT EXISTS udr_5g_vn_groups (
    ext_group_id TEXT PRIMARY KEY,
    data         JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0145). Real group-data individual
-- 5G MBS Group Membership resource
-- (/subscription-data/group-data/mbs-group-membership/{externalGroupId}, real schema
-- MulticastMbsGroupMemb -- required multicastGroupMemb (array of Gpsi) plus optional
-- afInstanceId/internalGroupIdentifier). Real Create5GmbsGroup/GetMulticastMbsGroupMemb/
-- Modify5GmbsGroup/Delete5GmbsGroup: real GET+PUT+PATCH+DELETE, structurally an exact twin of
-- udr_5g_vn_groups above (real PUT documents ONLY `201`, PATCH real RFC 6902
-- application/json-patch+json, NOT upsert-capable). Keyed by ext_group_id (real path schema is
-- ExtGroupId, a plain string). Second real group-data sub-resource closed after
-- 5g-vn-groups/{externalGroupId} (ADR-0144).
CREATE TABLE IF NOT EXISTS udr_mbs_group_membership (
    ext_group_id TEXT PRIMARY KEY,
    data         JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0146). Real group-data Event
-- Exposure Data for a group resource (/subscription-data/group-data/{ueGroupId}/ee-profile-data,
-- real schema EeGroupProfileData -- every field optional: restrictedEventTypes/
-- allowedMtcProvider/supportedFeatures/iwkEpcRestricted/extGroupId/hssGroupId). Real
-- QueryGroupEEData: real GET-only, no create/update operation exists in the spec at all, genuinely
-- NOT per-UE -- keyed by ueGroupId (real path schema VarUeGroupId, a plain string matching
-- `^(extgroupid-[^@]+@[^@]+|anyUE)$`, no encoding ambiguity), a real, distinct sibling of the
-- already-closed per-UE `ee-profile-data` resource. Seeded at startup, same "surface first, wire
-- consumers later" precedent as other GET-only UDR resources.
CREATE TABLE IF NOT EXISTS udr_group_ee_profile_data (
    ue_group_id TEXT PRIMARY KEY,
    data        JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0148). Real Event Exposure
-- Subscriptions resource -- collection
-- (/subscription-data/{ueId}/context-data/ee-subscriptions, real spec operations
-- Queryeesubscriptions/CreateEeSubscriptions) and individual document
-- (/subscription-data/{ueId}/context-data/ee-subscriptions/{subsId}, real spec operations
-- QueryeeSubscription/UpdateEesubscriptions/ModifyEesubscription/RemoveeeSubscriptions), real
-- schema EeSubscription -- required callbackReference (Uri) + monitoringConfigurations (opaque
-- JSON), plus a real dozen-plus optional fields. Real, disclosed: subsId is genuinely
-- server-generated on POST (real UUID v4 via sbi_core::generate_uuid_v4(), same generator this
-- project's own NF instance IDs use) -- the real spec's own Location header format confirms this
-- ("...ee-subscriptions/{subsId}"), no client-supplied ID exists for this resource. PUT
-- (UpdateEesubscriptions) is genuinely update-only, never create -- the spec's own 404 response
-- explicitly documents "update of non-existing resource is rejected", a real, new departure from
-- every other single-key PUT resource this project has closed (all of which are either
-- create-capable or upsert-capable). PATCH is real RFC 6902 application/json-patch+json, NOT
-- upsert-capable (same precedent as sor-data/nidd-authorizations). Real, disclosed simplification:
-- the collection GET's own optional event-types/nf-identifiers array query-param filters (both
-- genuinely optional, unlike every other array-query-param this project has found and left
-- blocked) are not honored -- always returns the full, unfiltered list for that UE, same
-- "optional filter not honored" precedent already used for e.g. rangingsl-privacy-data's own
-- `fields` parameter (ADR-0135). Scope, disclosed: only the collection + individual document are
-- implemented -- the deeper amf-subscriptions/smf-subscriptions/hss-subscriptions nested
-- sub-collections under each {subsId} remain genuinely deferred, not built. Composite key
-- (ue_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_ee_subscriptions (
    ue_id   TEXT NOT NULL,
    subs_id TEXT NOT NULL,
    data    JSONB NOT NULL,
    PRIMARY KEY (ue_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0149). Real Subs To Notify
-- collection (/subscription-data/subs-to-notify, real spec operations
-- SubscriptionDataSubscriptions [POST]/QuerySubsToNotify [GET]) and individual document
-- (/subscription-data/subs-to-notify/{subsId}, real spec operations
-- QuerySubscriptionDataSubscriptions/ModifysubscriptionDataSubscription/
-- RemovesubscriptionDataSubscriptions -- GET+PATCH+DELETE, genuinely no PUT operation exists at
-- all for this resource), real schema SubscriptionDataSubscriptions -- required
-- callbackReference (Uri) + monitoredResourceUris (array of Uri), plus a real dozen-plus optional
-- fields including its own optional ueId. Real, disclosed: subsId is server-generated (real UUID
-- v4, same precedent as ee-subscriptions, ADR-0148). This collection is genuinely NOT scoped
-- under {ueId} in its own path (unlike ee-subscriptions) -- QuerySubsToNotify's own real,
-- required `ue-id` query parameter is a plain string (VarUeId, not an array), so this project
-- stores the POST body's own optional `ueId` field (empty string if the caller omits it) as a
-- real, queryable column to back that real filter. PATCH is real RFC 6902
-- application/json-patch+json, NOT upsert-capable.
CREATE TABLE IF NOT EXISTS udr_subs_to_notify (
    subs_id TEXT PRIMARY KEY,
    ue_id   TEXT NOT NULL,
    data    JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0151). Real SDM Subscriptions
-- collection (/subscription-data/{ueId}/context-data/sdm-subscriptions, real spec operations
-- Querysdmsubscriptions [GET]/CreateSdmSubscriptions [POST]) and individual document
-- (/subscription-data/{ueId}/context-data/sdm-subscriptions/{subsId}, real spec operations
-- QuerysdmSubscription/Updatesdmsubscriptions/ModifysdmSubscription/RemovesdmSubscriptions --
-- GET+PUT+PATCH+DELETE), real schema SdmSubscription -- required nfInstanceId + callbackReference
-- (Uri) + monitoredResourceUris (array of Uri), plus a real dozen-plus optional fields. Real,
-- disclosed: structurally identical to udr_ee_subscriptions (ADR-0148) -- server-generated
-- subsId (real UUID v4), PUT genuinely update-only (real spec 404 "update of non-existing
-- resource is rejected"). Corrects ADR-0122's own blanket "genuinely deeply-nested"
-- characterization of ee-subscriptions/sdm-subscriptions together -- on direct, individual read,
-- only the deeper hss-sdm-subscriptions nested sub-collection under each subsId is genuinely
-- deferred; the collection + individual document themselves are buildable. Composite key
-- (ue_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_sdm_subscriptions (
    ue_id   TEXT NOT NULL,
    subs_id TEXT NOT NULL,
    data    JSONB NOT NULL,
    PRIMARY KEY (ue_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0152). Real AMF Subscription Info
-- (Document), nested under an individual ee-subscription
-- (/subscription-data/{ueId}/context-data/ee-subscriptions/{subsId}/amf-subscriptions, real spec
-- operations "Create AMF Subscriptions" [PUT]/GetAmfSubscriptionInfo [GET]/
-- ModifyAmfSubscriptionInfo [PATCH]/RemoveAmfSubscriptionsInfo [DELETE]). Real, disclosed: the
-- real document body is a JSON ARRAY of AmfSubscriptionInfo (minItems 1), not a single object --
-- schema AmfSubscriptionInfo: required amfInstanceId + subscriptionId (Uri), plus optional
-- subsChangeNotifyCorrelationId/contextInfo. Real, distinct 201-vs-204 PUT (same
-- is-new-tracking precedent as AmfContextStore). Real, disclosed simplification: no referential
-- integrity is enforced against udr_ee_subscriptions (this project's own established precedent
-- elsewhere, e.g. pp-data-store, of not enforcing cross-resource existence checks) -- this
-- resource is addressable by any (ue_id, subs_id) pair regardless of whether that ee-subscription
-- was itself ever created. First of ee-subscriptions' own nested sub-collections surveyed
-- directly rather than left blanket-deferred (smf-subscriptions/hss-subscriptions and
-- sdm-subscriptions' own hss-sdm-subscriptions remain deferred). Composite key (ue_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_ee_amf_subscription_info (
    ue_id   TEXT NOT NULL,
    subs_id TEXT NOT NULL,
    data    JSONB NOT NULL,
    PRIMARY KEY (ue_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0153). Real SMF Event Subscription
-- Info (Document), nested under an individual ee-subscription
-- (/subscription-data/{ueId}/context-data/ee-subscriptions/{subsId}/smf-subscriptions, real spec
-- operations "Create SMF Subscriptions" [PUT]/GetSmfSubscriptionInfo [GET]/
-- ModifySmfSubscriptionInfo [PATCH]/RemoveSmfSubscriptionsInfo [DELETE]). Real, disclosed,
-- genuinely different shape from its sibling amf-subscriptions (ADR-0152): the document body is a
-- SINGLE SmfSubscriptionInfo object (required smfSubscriptionList, an array of
-- SmfSubscriptionItem), not a bare array. Same real, distinct 201-vs-204 PUT (is-new-tracking).
-- Same disclosed no-referential-integrity-against-parent precedent as amf-subscriptions. Second of
-- ee-subscriptions' own nested sub-collections closed (hss-subscriptions and
-- sdm-subscriptions' own hss-sdm-subscriptions remain deferred). Composite key (ue_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_ee_smf_subscription_info (
    ue_id   TEXT NOT NULL,
    subs_id TEXT NOT NULL,
    data    JSONB NOT NULL,
    PRIMARY KEY (ue_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0154). Real HSS Event Subscription
-- Info (Document), nested under an individual ee-subscription
-- (/subscription-data/{ueId}/context-data/ee-subscriptions/{subsId}/hss-subscriptions, real spec
-- operations "Create HSS Subscriptions" [PUT]/GetHssSubscriptionInfo [GET]/
-- ModifyHssSubscriptionInfo [PATCH]/RemoveHssSubscriptionsInfo [DELETE]). Real, disclosed spec
-- inconsistency, asked and confirmed rather than silently assumed: GetHssSubscriptionInfo's own
-- 200 response literally cites schema SmfSubscriptionInfo, not HssSubscriptionInfo -- even though
-- PUT/PATCH/DELETE on this identical resource all use HssSubscriptionInfo (a real, distinct,
-- already-generated schema wrapping hssSubscriptionList, an array of HssSubscriptionItem), and
-- every analogous sibling (amf-/smf-subscriptions) returns its own PUT-body type on GET. Treated
-- as a real spec typo (same precedent as ADR-0129's QueryPorseData typo) -- GET returns
-- HssSubscriptionInfo, matching this resource's own PUT/PATCH/DELETE and every sibling's
-- internally-consistent pattern. Same real, distinct 201-vs-204 PUT and
-- no-referential-integrity-against-parent precedent as amf-/smf-subscriptions. Third and final of
-- ee-subscriptions' own nested sub-collections closed. Composite key (ue_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_ee_hss_subscription_info (
    ue_id   TEXT NOT NULL,
    subs_id TEXT NOT NULL,
    data    JSONB NOT NULL,
    PRIMARY KEY (ue_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0155). Real HSS SDM Subscription
-- Info (Document), nested under an individual sdm-subscription
-- (/subscription-data/{ueId}/context-data/sdm-subscriptions/{subsId}/hss-sdm-subscriptions, real
-- spec operations "Create HSS SDM Subscriptions" [PUT]/GetHssSDMSubscriptionInfo [GET]/
-- ModifyHssSDMSubscriptionInfo [PATCH]/RemoveHssSDMSubscriptionsInfo [DELETE]). Reuses the same
-- HssSubscriptionInfo schema as ee-subscriptions' own hss-subscriptions sibling (ADR-0154).
-- Two real, disclosed findings from direct read: (1) the real spec's own PUT response list
-- documents ONLY 204 -- no 201 anywhere -- genuinely unlike amf-/smf-/hss-subscriptions under
-- ee-subscriptions (which all document a real distinct 201-vs-204); this matches the existing
-- sor-data/upu-data precedent (ADR-0143) of a single-response-code (204-only) upsert PUT, not
-- invented, confirmed by direct read of the real spec text. (2) GetHssSDMSubscriptionInfo's own
-- 200 response again literally cites SmfSubscriptionInfo, not HssSubscriptionInfo -- the same
-- typo class just resolved in ADR-0154 for the sibling resource under ee-subscriptions; applying
-- the same, already-user-confirmed resolution (return HssSubscriptionInfo) rather than re-asking
-- for the identical schema-citation error on a structurally parallel resource. No referential
-- integrity enforced against the parent sdm-subscriptions resource (established project
-- precedent). This is sdm-subscriptions' own final deferred nested sub-collection. Composite key
-- (ue_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_sdm_hss_subscription_info (
    ue_id   TEXT NOT NULL,
    subs_id TEXT NOT NULL,
    data    JSONB NOT NULL,
    PRIMARY KEY (ue_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0156). Real Event Exposure Group
-- Subscriptions collection (/subscription-data/group-data/{ueGroupId}/ee-subscriptions, real spec
-- operations QueryEeGroupSubscriptions/CreateEeGroupSubscriptions) and individual document
-- (/subscription-data/group-data/{ueGroupId}/ee-subscriptions/{subsId}, real spec operations
-- QueryEeGroupSubscription/UpdateEeGroupSubscriptions/ModifyEeGroupSubscription/
-- RemoveEeGroupSubscriptions -- GET+PUT+PATCH+DELETE), the group-data-scoped sibling of
-- ee-subscriptions (ADR-0148), structurally identical but keyed by ueGroupId instead of ueId:
-- same EeSubscription schema, server-generated subsId (real UUID v4, same generator), PUT
-- genuinely update-only (real spec 404 text "update of non-existing resource is rejected", same
-- as ADR-0148). The individual GET response schema has the same real `items:` (no `type: array`)
-- authoring artifact already found and resolved in ADR-0148's own QueryeeSubscription -- treated
-- identically, returns a single EeSubscription object. Real, disclosed scope narrowing kept from
-- ADR-0148's own precedent: only the collection + individual document are implemented -- the
-- deeper group-data-scoped amf-subscriptions/smf-subscriptions/hss-subscriptions nested
-- sub-collections under each {subsId} remain genuinely deferred, not built. Composite key
-- (ue_group_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_group_ee_subscriptions (
    ue_group_id TEXT NOT NULL,
    subs_id     TEXT NOT NULL,
    data        JSONB NOT NULL,
    PRIMARY KEY (ue_group_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0157). Real AMF Group Subscription
-- Info (Document), nested under an individual group-data-scoped ee-subscription
-- (/subscription-data/group-data/{ueGroupId}/ee-subscriptions/{subsId}/amf-subscriptions, real
-- spec operations CreateAmfGroupSubscriptions [PUT]/GetAmfGroupSubscriptions [GET]/
-- ModifyAmfGroupSubscriptions [PATCH]/RemoveAmfGroupSubscriptions [DELETE]) -- the group-data-
-- scoped sibling of ee-subscriptions/{subsId}/amf-subscriptions (ADR-0152), structurally
-- identical but keyed by ueGroupId instead of ueId: same array-valued AmfSubscriptionInfo[]
-- document body, real distinct 201-vs-204 PUT. This same ADR also fixes a real, disclosed
-- pre-existing RFC 9110 Location-header bug found live-verifying this resource -- see the
-- resolved_location() helper added to nfs/udr/src/main.cpp and ADR-0157's own DECISIONS.md entry
-- for the full, wide-reaching disclosure (fixed in ~20 routes project-wide, not just this one).
-- Composite key (ue_group_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_group_amf_subscription_info (
    ue_group_id TEXT NOT NULL,
    subs_id     TEXT NOT NULL,
    data        JSONB NOT NULL,
    PRIMARY KEY (ue_group_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0158). Real SMF Event Group
-- Subscription Info (Document), nested under an individual group-data-scoped ee-subscription
-- (/subscription-data/group-data/{ueGroupId}/ee-subscriptions/{subsId}/smf-subscriptions, real
-- spec operations CreateSmfGroupSubscriptions [PUT]/GetSmfGroupSubscriptions [GET]/
-- ModifySmfGroupSubscriptions [PATCH]/RemoveSmfGroupSubscriptions [DELETE]) -- the group-data-
-- scoped sibling of ee-subscriptions/{subsId}/smf-subscriptions (ADR-0153), structurally
-- identical but keyed by ueGroupId instead of ueId: same single-object SmfSubscriptionInfo
-- document body (unlike its array-valued amf-subscriptions sibling), real distinct 201-vs-204
-- PUT. Composite key (ue_group_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_group_smf_subscription_info (
    ue_group_id TEXT NOT NULL,
    subs_id     TEXT NOT NULL,
    data        JSONB NOT NULL,
    PRIMARY KEY (ue_group_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0159). Real HSS Event Group
-- Subscription Info (Document), nested under an individual group-data-scoped ee-subscription
-- (/subscription-data/group-data/{ueGroupId}/ee-subscriptions/{subsId}/hss-subscriptions, real
-- spec operations CreateHssGroupSubscriptions [PUT]/GetHssGroupSubscriptions [GET]/
-- ModifyHssGroupSubscriptions [PATCH]/RemoveHssGroupSubscriptions [DELETE]) -- the group-data-
-- scoped sibling of ee-subscriptions/{subsId}/hss-subscriptions (ADR-0154), structurally
-- identical but keyed by ueGroupId instead of ueId: same single-object HssSubscriptionInfo
-- document body, real distinct 201-vs-204 PUT. GetHssGroupSubscriptions correctly cites
-- HssSubscriptionInfo -- no response-schema typo this time (unlike ADR-0154/ADR-0155's
-- ueId-scoped and sdm-subscriptions-scoped hss-subscriptions siblings).
--
-- Real, disclosed spec inconsistency found on direct read: the real spec's own DELETE/PATCH/GET
-- operations on this path declare a parameter named `externalGroupId`
-- ($ref: TS29503_Nudm_SDM.yaml#/components/schemas/ExtGroupId), but the actual URL path template
-- for this resource is literally `.../group-data/{ueGroupId}/ee-subscriptions/{subsId}/
-- hss-subscriptions` -- there is no `{externalGroupId}` placeholder anywhere in the path, only
-- `{ueGroupId}` and `{subsId}`. PUT on this same path correctly declares `ueGroupId`
-- ($ref: #/components/schemas/VarUeGroupId), matching the real path template and every sibling
-- resource in this family. Since the router binds strictly to the real, literal path template
-- (the only thing that determines what's actually capturable from a request URL), this is not a
-- genuine design ambiguity requiring a choice between two real behaviors -- `externalGroupId` is
-- simply not a bindable value here at all, and `ueGroupId` is the only value consistent with the
-- resource's own PUT operation and its actual URL. Implemented using `ueGroupId` throughout.
-- Completes all three of group-data's own ee-subscriptions/{subsId}/... nested sub-collections
-- (siblings amf-/smf-subscriptions closed in ADR-0157/ADR-0158). Composite key
-- (ue_group_id, subs_id).
CREATE TABLE IF NOT EXISTS udr_group_hss_subscription_info (
    ue_group_id TEXT NOT NULL,
    subs_id     TEXT NOT NULL,
    data        JSONB NOT NULL,
    PRIMARY KEY (ue_group_id, subs_id)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0162). Real PDTQ Data (Planned
-- Data Transfer with QoS requirements) collection (/policy-data/pdtq-data, real spec operation
-- ReadPdtqData) and individual document (/policy-data/pdtq-data/{pdtqReferenceId}, real spec
-- operations ReadIndividualPdtqData/CreateIndividualPdtqData/UpdateIndividualPdtqData/
-- DeleteIndividualPdtqData), TS29519_Policy_Data.yaml. The first real UDR resource closed using
-- the new sbi_core::http2::split_form_array() infra (ADR-0161) -- confirmed genuinely unblocked
-- by it, not merely candidate. Real, disclosed: `pdtqReferenceId` is client-supplied (a real path
-- parameter on the individual document, not server-generated) -- no UUID generation needed. Real,
-- disclosed: CreateIndividualPdtqData documents ONLY 201 as a success response (no update-via-PUT
-- status) -- matches the existing bdt-data (ADR-0117-era) precedent exactly, this route always
-- responds 201 with the stored resource, matching the real spec literally rather than inventing
-- an undocumented 204. Real RFC 7396 JSON Merge Patch (application/merge-patch+json,
-- PdtqDataPatch), same idiom as bdt-data/UDM's own AMF-registration merge-patch: validated
-- against the real generated PdtqDataPatch shape first, then applied via nlohmann::json's
-- .merge_patch() on the raw parsed body (not round-tripped through the DTO) to preserve RFC 7396's
-- own absent-vs-null field semantics. Real, disclosed scope choice: the collection GET's own
-- optional `pdtq-ref-ids` array query-param filter (`style: form, explode: false`, confirmed
-- real, genuinely optional, not required) is NOT honored -- same "optional filter not honored"
-- precedent as `ee-subscriptions`' own `event-types`/`nf-identifiers`, kept for consistency with
-- that established precedent rather than starting to honor it here just because the new parsing
-- infra now makes that technically possible; retrofitting is a separate, disclosed decision left
-- for its own turn. Single-key on pdtq_ref_id (the resource's own real identity).
CREATE TABLE IF NOT EXISTS udr_pdtq_data (
    pdtq_ref_id TEXT PRIMARY KEY,
    data        JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0164). Real GetNfGroupIDs resource
-- (/nf-group-ids, TS29504_Nudr_GroupIDmap.yaml -- the same Nudr_GroupIDmap service as
-- udr_routing_ids above, NOT Nudr_DataRepository; server base path /nudr-group-id-map/v1). Real,
-- confirmed by direct YAML read: GET-only, real REQUIRED `nf-type` array query param
-- (style: form, explode: false, the second real UDR resource closed using the
-- sbi_core::http2::split_form_array() infra from ADR-0161/ADR-0162) and real REQUIRED
-- `subscriberId` (plain string). Response is a real map {NFType: NfGroupId}, `minProperties: 1`
-- -- unlike the aggregate resources (ue-update-confirmation-data/context-data, ADR-0147/ADR-0161)
-- which deliberately always return 200 since those are live views with no independent existence,
-- this resource's own response schema genuinely cannot be empty, and a real 404 is documented, so
-- an empty result (no requested (subscriberId, nfType) pair found) returns 404 here -- honoring
-- the real spec literally, not a deviation. No create/update/delete operation exists anywhere in
-- this service for the underlying mapping data itself (the file's only other write path,
-- /nf-group-ids/subscriptions, creates a subscription to be NOTIFIED of future changes, not a way
-- to SET the mapping -- confirmed by direct read, deliberately not implemented in this ADR, real
-- webhook delivery has no project precedent, same disclosed gap class as subs-to-notify) -- same
-- "provisioned out-of-band, seeded at startup" precedent as udr_routing_ids/udr_group_identifiers
-- above. Composite-keyed by (subscriber_id, nf_type) per the resource's own two real required
-- query parameters.
CREATE TABLE IF NOT EXISTS udr_nf_group_ids (
    subscriber_id TEXT NOT NULL,
    nf_type       TEXT NOT NULL,
    group_id      TEXT NOT NULL,
    PRIMARY KEY (subscriber_id, nf_type)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0165). Real GetNiddAuData resource
-- (/subscription-data/{ueId}/nidd-authorization-data, TS29505_Subscription_Data.yaml). Real,
-- confirmed by direct YAML read: GET-only, real REQUIRED `single-nssai` query param -- genuinely a
-- different parsing shape than ADR-0161's array-style params: it uses `content: application/json`
-- (a JSON-encoded query value, decomposed here to its own `sst`/`sd` fields), plus real REQUIRED
-- `dnn`/`mtc-provider-information` (plain strings) and an optional `af-id` (deliberately not
-- honored, matching this project's own established "optional filter not honored" precedent).
-- Response schema `AuthorizationData` -- real, disclosed: this same schema is also cited (per
-- ADR-0160's own earlier finding) by the deliberately-deferred `GetSSAuData`; reading this
-- resource's own real spec text confirms `AuthorizationData`'s real, unambiguous home is here
-- (`GetNiddAuData`, "NIDD Authorization Information") rather than there. No create/update/delete
-- operation exists anywhere in this project's real, in-scope APIs for this document (the real
-- provisioning path is UDM's own Nudm_NIDDAU service, out of scope here) -- seeded at startup,
-- same "no live provisioning path yet" precedent as udr_routing_ids/udr_nf_group_ids above.
-- Composite-keyed by (ue_id, sst, sd, dnn, mtc_provider_information); `sd` stored as an empty
-- string when absent (real, disclosed: `Snssai`'s own schema only requires `sst`, and PostgreSQL
-- primary-key columns cannot be NULL).
CREATE TABLE IF NOT EXISTS udr_nidd_authorization_data (
    ue_id                     TEXT    NOT NULL,
    sst                       INTEGER NOT NULL,
    sd                        TEXT    NOT NULL DEFAULT '',
    dnn                       TEXT    NOT NULL,
    mtc_provider_information  TEXT    NOT NULL,
    data                      JSONB   NOT NULL,
    PRIMARY KEY (ue_id, sst, sd, dnn, mtc_provider_information)
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0169). Real Query5GVnGroupPPData /
-- Query5GMbsGroupPPData resources (`/subscription-data/group-data/5g-vn-groups/pp-profile-data`,
-- `.../mbs-group-membership/pp-profile-data`, TS29505_Subscription_Data.yaml). Real, confirmed by
-- direct YAML read: GET-only, real OPTIONAL `ext-group-ids` array filter and OPTIONAL
-- `supported-features` (both deliberately not honored, matching this project's own established
-- "optional filter not honored" precedent). Real, disclosed: unlike every other `group-data`
-- sub-resource closed so far, the response schemas (`Pp5gVnGroupProfileData`/
-- `Pp5gMbsGroupProfileData`) are genuinely NOT per-group documents -- each is a single, global
-- document whose own internal `allowedMtcProviders`/`allowedMbsInfos` field is itself a map
-- keyed by ExtGroupId (or the literal "ALL"). A truly keyless singleton, a real, new storage
-- shape for this project (every prior "non-per-UE" UDR resource, e.g. `group_identifiers`/
-- `routing_ids`, is still keyed by some real identifier) -- modeled here as a fixed single-row
-- table (`id` pinned to `1`) rather than inventing a key the spec doesn't have. No
-- create/update/delete operation exists anywhere in the spec for either document -- seeded at
-- startup with a minimal, real-shaped empty document, same "GET-only, seeded at startup"
-- precedent as `udr_routing_ids`/`udr_nf_group_ids` above.
CREATE TABLE IF NOT EXISTS udr_5g_vn_group_pp_profile_data (
    id    INTEGER PRIMARY KEY DEFAULT 1 CHECK (id = 1),
    data  JSONB NOT NULL
);

CREATE TABLE IF NOT EXISTS udr_mbs_group_pp_profile_data (
    id    INTEGER PRIMARY KEY DEFAULT 1 CHECK (id = 1),
    data  JSONB NOT NULL
);

-- Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0170). Real Nudr_GroupIDmap
-- subscription-management family (CreateGroupIdSubscription/QueryGroupIdSubscription/
-- ModifyGroupIdSubscription/RemoveGroupIdSubscription, TS29504_Nudr_GroupIDmap.yaml
-- /nf-group-ids/subscriptions[/{subscriptionId}]). Real POST+GET+PATCH+DELETE, real required
-- `SubscriptionData` fields (notificationUri/nfType/nfGroupId), server-generated
-- `subscriptionId`. Real, disclosed: the spec's own `onGroupIdMapChange` webhook callback is NOT
-- implemented (no real outbound HTTP delivery to `notificationUri`) -- same disclosed gap class
-- as `subs-to-notify`'s own lack of real webhook delivery (ADR-0149).
CREATE TABLE IF NOT EXISTS udr_nf_group_id_subscriptions (
    subscription_id  TEXT   PRIMARY KEY,
    data             JSONB  NOT NULL
);
