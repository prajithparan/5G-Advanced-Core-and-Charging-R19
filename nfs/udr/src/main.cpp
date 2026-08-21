// nfs/udr: UDR (Unified Data Repository), Nudr_DataRepository context-data group.
// Source: specs/5G_APIs-REL-19/TS29505_Subscription_Data.yaml (the file TS29504_Nudr_DR.yaml's
// paths $ref into -- TS29504 itself defines almost no schemas of its own), commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6. Phase 2's fifth NF (PROMPT.md/CLAUDE.md order:
// NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF).
//
// In scope, agreed with the user before implementation: the `context-data` group --
// QueryAmfContext3gpp, CreateAmfContext3gpp, AmfContext3gpp (AMF 3GPP-access context, singular
// per UE -- no delete operation exists for this resource in the spec, checked not assumed) and
// QuerySmfRegList, QuerySmfRegistration, CreateOrUpdateSmfRegistration, UpdateSmfContext,
// DeleteSmfRegistration (SMF registration context, one per UE+pduSessionId). These mirror
// nfs/udm's UECM registration groups almost exactly -- 3GPP's real intended backing store for
// that data -- but per explicit user decision this turn, UDM's own AmfRegistrationStore/
// SmfRegistrationStore are NOT wired to call UDR yet; that remains a separate, deliberate future
// turn touching already-committed UDM code, not silently done here.
//
// UPDATE (ADR-0069, gap-closure Tier 1b): the `provisioned-data` group (am-data/smf-selection-
// subscription-data/sm-data) is now implemented -- real GET routes, keyed by (ueId,
// servingPlmnId) per the real path shape. Still real, disclosed: this group is genuinely GET-only
// in the spec (no create/update operation exists at all), so there is no live provisioning path;
// data is seeded at startup instead (see main() below), and nfs/udm's own GetAmData/GetSmfSelData/
// GetSmData now call these real routes instead of returning the old permanently-empty stub.
//
// UPDATE (ADR-0072, gap-closure: real N28 end-to-end): the `policy-data` group's SM policy
// resource (/policy-data/ues/{ueId}/sm-data, real schema SmPolicyData -- source
// TS29519_Policy_Data.yaml) is now implemented, real GET+PATCH, keyed by ueId alone. Unlike
// provisioned-data above, this real resource DOES support PATCH (application/merge-patch+json) --
// but the real spec still has no POST/create operation for it, so this project's own store treats
// PATCH as upsert-capable (a disclosed, deliberate choice enabling GUI-driven creation -- see
// stores.hpp's own comment on SmPolicyDataStore). PCF is the real consumer, fetching a
// subscriber's subscSpendingLimits/policyCounterIds per DNN to decide whether to subscribe to
// CHF's Nchf_SpendingLimitControl.
//
// UPDATE (ADR-0083, gap-closure task #106): the Authentication Data group's
// authentication-subscription (real GET+PATCH, RFC 6902) and authentication-status (real
// PUT+GET+DELETE) documents, and the policy-data group's AM policy resource (real GET+PATCH, RFC
// 7396 -- the real UDR-side backing for PCF's own Npcf_AMPolicyControl) are now implemented.
// Real, disclosed architectural note: neither AUSF's own AuthContextStore/KausfStore nor UDM's
// own AuthenticationSubscriptionStore, nor PCF's own AmPolicyStore, were migrated to call these
// new UDR routes in this pass -- that would be a real, separate architectural decision (each of
// those NFs already has its own working, tested store; switching them to be UDR-backed is a
// cross-cutting change touching already-committed code, not a "stand up the resource" change) --
// same "build the surface first, wire consumers in a dedicated later turn" precedent this
// project already used for UDR's own provisioned-data group (ADR-0069) and PCF itself (ADR-0028).
//
// UPDATE (ADR-0093, gap-closure task #106): the AMF non-3GPP-access context group
// (QueryAmfContextNon3gpp/CreateAmfContextNon3gpp, real GET+PUT) is now implemented -- a real,
// distinct resource/table from the 3GPP one above (schema `AmfNon3GppAccessRegistration`, not
// `Amf3GppAccessRegistration`), same "no PATCH/DELETE exists for this resource" scope its 3GPP
// sibling already has.
//
// UPDATE (ADR-0097, gap-closure task #106): the SMSF Registration context-data group
// (CreateSmsfContext3gpp/QuerySmsfContext3gpp/DeleteSmsfContext3gpp and their non-3GPP
// counterparts, real GET+PUT+DELETE) is now implemented -- two real, distinct resources sharing
// the identical `SmsfRegistration` schema, same "no PATCH exists for this resource" scope.
//
// UPDATE (ADR-0098, gap-closure task #106): the IP-SM-GW Registration context-data resource
// (CreateIpSmGwContext/QueryIpSmGwContext/ModifyIpSmGwContext/DeleteIpSmGwContext, real
// PUT+GET+PATCH+DELETE, RFC 6902 JSON Patch) is now implemented -- the richest operation set of
// any context-data resource closed so far.
//
// UPDATE (ADR-0099, gap-closure task #106): the Message Waiting Data (Document) resource
// (CreateMessageWaitingData/QueryMessageWaitingData/ModifyMessageWaitingData/
// DeleteMessageWaitingData, real PUT+GET+PATCH+DELETE, RFC 6902 JSON Patch) is now implemented --
// unlike ip-sm-gw's own always-204 PUT, this one's real PUT genuinely distinguishes 201-Created
// from 204-updated per the YAML.
//
// UPDATE (ADR-0100, gap-closure task #106): the Roaming Information (Document) resource
// (UpdateRoamingInformation/QueryRoamingInformation, real GET+PUT, real distinct 201-vs-204 PUT
// response codes) is now implemented -- same "no PATCH/DELETE exists for this resource" scope as
// the AMF non-3GPP-access context resource.
//
// UPDATE (ADR-0101, gap-closure task #106): the PEI Information (Document) resource
// (CreateOrUpdatePeiInformation/QueryPeiInformation, real GET+PUT, real distinct 201-vs-204 PUT
// response codes) is now implemented -- real schema is an allOf composition
// (TS29503_Nudm_UECM.yaml's base PeiUpdateInfo + this file's own PeiUpdateInfoExt), generated as
// `sbi_gen::PeiUpdateInfo_Subscription_Data` to disambiguate from the base type's own
// `PeiUpdateInfo_Nudm_UECM`.
//
// UPDATE (ADR-0102, gap-closure task #106): the Enhanced Coverage Restriction Data resource
// (QueryCoverageRestrictionData, real GET-only) is now implemented -- same real "no create/update
// operation exists at all, seeded at startup" shape as the provisioned-data group (ADR-0069).
//
// UPDATE (ADR-0103, gap-closure task #106): the LCS Privacy Subscription Data resource
// (QueryLcsPrivacyData, real GET-only) is now implemented -- same real "seeded at startup" shape.
//
// UPDATE (ADR-0104, gap-closure task #106): the LCS Subscription Data resource
// (QueryLcsSubscriptionData, real GET-only) is now implemented -- same real "seeded at startup"
// shape.
//
// UPDATE (ADR-0105, gap-closure task #106): the LCS Mobile Originated Subscription Data resource
// (QueryLcsMoData, real GET-only) is now implemented -- same real "seeded at startup" shape.
//
// UPDATE (ADR-0106, gap-closure task #106): the provisioned-data group's `lcs-bca-data`
// sub-resource (QueryLcsBcaData, real GET-only) is now implemented as a 4th column on
// ProvisionedDataStore -- same real (ueId, servingPlmnId) key shape as am-data/
// smf-selection-subscription-data/sm-data.
//
// UPDATE (ADR-0107, gap-closure task #106): the Parameter Provision (Document) resource
// (GetppData/ModifyPpData, real GET+PATCH, RFC 6902) is now implemented -- same
// "no PUT/DELETE, apply_patch upsert-capable" shape as authentication-subscription.
//
// UPDATE (ADR-0108, gap-closure task #106): the Parameter Provision profile Data (Document)
// resource (QueryPPData, real GET-only) is now implemented -- same real "seeded at startup" shape.
//
// UPDATE (ADR-0109, gap-closure task #106): the Provisioned Parameter Data Entry resource
// (Create/Get/Delete PP Data Entry, real PUT+GET+DELETE) and its real sibling collection resource
// (Get Multiple PP Data Entries) are now implemented -- composite (ueId, afInstanceId) key, same
// real shape as SmfRegistrationStore's own (ueId, pduSessionId).
//
// UPDATE (ADR-0110, gap-closure task #106): the individual Shared Data resource
// (GetIndividualSharedData, real GET-only) is now implemented -- genuinely NOT per-UE, keyed by
// shared_data_id alone. Real, disclosed scope narrowing: the real sibling collection resource
// (GetSharedData, a required comma-separated shared-data-ids array query parameter) remains
// deferred -- this project has no existing precedent for parsing array-shaped query parameters
// yet, a real capability that belongs in its own scoped turn.
//
// UPDATE (ADR-0111, gap-closure task #106): the Operator-Specific Data Container (Document)
// resource (QueryOperSpecData/ModifyOperSpecData, real GET+PATCH, RFC 6902) is now implemented --
// same "no PUT/DELETE, apply_patch upsert-capable" shape as pp-data.
//
// UPDATE (ADR-0112, gap-closure task #106): the Event Exposure Data (Document) resource
// (QueryEEData, real GET-only) is now implemented -- same real "seeded at startup" shape. Real,
// distinct UDR-side resource from this project's own UDM-side Nudm_EE work (task #105).
//
// UPDATE (ADR-0113, gap-closure task #106): the `policy-data` group's UE Policy Set resource
// (ReadUEPolicySet/CreateOrReplaceUEPolicySet/UpdateUEPolicySet, real GET+PUT+PATCH RFC 7396
// merge-patch) is now implemented -- real distinct 201-vs-204 PUT codes; unlike am-data's own
// PATCH, the real spec here only documents 204 (no 200-with-body option).
//
// UPDATE (ADR-0114, gap-closure task #106): the `policy-data` group's Operator-Specific Data
// resource (ReadOperatorSpecificData/UpdateOperatorSpecificData, real GET+PATCH RFC 6902) is now
// implemented -- real, distinct resource from the subscription-data-scoped
// operator-specific-data (ADR-0111), reusing the same real OperatorSpecificDataContainer schema
// via a cross-file $ref.
//
// UPDATE (ADR-0115, gap-closure task #106): the `policy-data` group's Sponsor Connectivity Data
// resource (ReadSponsorConnectivityData, real GET-only) is now implemented -- genuinely NOT
// per-UE, keyed by sponsorId alone. Real, disclosed simplification: the real spec's own distinct
// `204` ("found but no data") vs `404` ("not found") is not modeled -- this store's simple
// existence-based model only distinguishes 200-with-data vs 404-not-provisioned.
//
// UPDATE (ADR-0116, gap-closure task #106): the `policy-data` group's individual BDT
// (Background Data Transfer) Data resource (ReadIndividualBdtData/CreateIndividualBdtData/
// UpdateIndividualBdtData/DeleteIndividualBdtData, real GET+PUT+PATCH+DELETE) is now implemented
// -- real, disclosed: PUT only documents `201` (no update-via-PUT status), so the route always
// responds 201; PATCH is NOT upsert-capable (real 404 if the resource doesn't already exist,
// unlike am-data/ue-policy-set's own upsert-capable PATCH).
//
// UPDATE (ADR-0117, gap-closure task #106): the `policy-data` group's PLMN UE Policy Set resource
// (ReadPlmnUePolicySet, real GET-only, no create/update operation exists for this resource at
// all) is now implemented -- reuses the real UePolicySet schema (same type as the per-UE
// ue-policy-set resource) but keyed by plmn_id, a genuinely distinct resource, not a UE-scoped
// alias.
//
// UPDATE (ADR-0118, gap-closure task #106): the `policy-data` group's Slice-specific Policy
// Control Data resource (ReadSlicePolicyControlData/UpdateSlicePolicyControlData, real
// GET+PATCH-only, no PUT/POST create operation exists at all) is now implemented -- merge_patch
// is upsert-capable, same disclosed precedent as AmPolicyDataStore/SmPolicyDataStore. Real,
// disclosed: the YAML types the {snssai} path parameter as the Snssai object schema with no
// documented bare-path-segment string encoding; this project reuses its own already-disclosed
// "sst + '-' + sd" convention (ADR-0072/PCF's snssai_map_key) rather than inventing a new one.
//
// UPDATE (ADR-0119, gap-closure task #106): the `policy-data` group's group-specific Policy
// Control Data resource (ReadGroupPolCtrlData/ModifyGroupPolCtrlData, real GET+PATCH-only, no
// PUT/POST create operation exists at all) is now implemented -- merge_patch is upsert-capable,
// same precedent as slice-control-data above. Keyed by the real GroupId schema (plain string, no
// encoding ambiguity, unlike slice-control-data's own snssai key).
//
// UPDATE (ADR-0120, gap-closure task #106): the real GetRoutingIDs resource (/routing-ids) is now
// implemented -- real, disclosed: this is a genuinely DIFFERENT real Nudr API
// (TS29504_Nudr_GroupIDmap.yaml's Nudr_GroupIDmap service, server base path
// `/nudr-group-id-map/v1`, OAuth2 scope `nudr-group-id-map`), not Nudr_DataRepository
// (`/nudr-dr/v2`) like every other resource in this file -- does NOT count toward the "N of
// free5GC's ~42+ Nudr_DataRepository resources" metric tracked in this file's own UPDATE entries
// above. Real GET-only, composite-keyed by the two real required query parameters (nf-type,
// nf-group-id), no PUT/POST/PATCH exists at all.
//
// UPDATE (ADR-0121, gap-closure task #106): the NIDD Authorization Info context-data resource
// (CreateNIDDAuthorizationInfo/GetNiddAuthorizationInfo/ModifyNiddAuthorizationInfo/
// RemoveNiddAuthorizationInfo, real PUT+GET+PATCH+DELETE) is now implemented. Real, disclosed
// correction: earlier UPDATE entries above lumped `nidd-authorizations` in with
// `ee-subscriptions`/`sdm-subscriptions` as a deferred deeply-nested sub-subscription shape
// without individually checking the real YAML -- it is genuinely a flat per-UE document, same
// shape as amf-3gpp-access's own real distinct-201-vs-204 PUT + RFC 6902 PATCH, plus a real
// DELETE (which amf-3gpp-access's own resource lacks).
//
// UPDATE (ADR-0122, gap-closure task #106): the real Query/Modify Identity Data by SUPI or GPSI
// resource (GetIdentityData/ModifyIdentityData, real GET+PATCH, no PUT/POST create operation
// exists at all) is now implemented -- apply_patch is upsert-capable, same precedent as
// pp-data/operator-specific-data. Real RFC 6902 JSON Patch, not merge-patch. Confirmed while
// surveying: `ee-subscriptions`/`sdm-subscriptions` ARE genuinely deeply-nested
// subscription-lifecycle resources (collection -> individual -> amf-/smf-/hss-subscriptions
// sub-collections, plus a parallel group-data-scoped tree, server-generated subsId via POST) --
// the original deferral for those two was correct, unlike nidd-authorizations above.
// `subs-to-notify` also confirmed genuinely deferred: real POST-based collection with a
// server-generated Location header and real webhook callback registration
// (`{$request.body#/notificationUri}`), no existing project precedent for either.
//
// UPDATE (ADR-0123, gap-closure task #106): the real Query ODB Data by SUPI or GPSI resource
// (GetOdbData -- real GET-only, no create/update operation exists at all) is now implemented,
// seeded at startup, same shape as coverage-restriction-data.
//
// UPDATE (ADR-0125, gap-closure task #106): the real SMS Management Subscription Data resource
// (QuerySmsMngData -- real GET-only, no create/update operation exists at all) is now implemented
// as a new `sms_mng_data` column on `udr_provisioned_data`, same real sibling-column precedent
// ADR-0106 already established for lcs-bca-data (same provisioned-data group,
// same (ueId, servingPlmnId) key).
//
// UPDATE (ADR-0126, gap-closure task #106): the real SMS Subscription Data resource (QuerySmsData
// -- real GET-only, no create/update operation exists at all) is now implemented as a new
// `sms_data` column on `udr_provisioned_data`, same precedent, genuinely distinct from
// sms-mng-data above (real, separate schema, separate operationId).
//
// UPDATE (ADR-0127, gap-closure task #106): the real Trace Data resource (QueryTraceData -- real
// GET-only, no create/update operation exists at all) is now implemented as a new `trace_data`
// column on `udr_provisioned_data`, same precedent. Real response schema is a `oneOf` (full
// `TraceData` object or a bare `SharedDataId` string reference) -- handled as opaque JSON, no
// special-casing needed since this store never strongly types sub-resource bodies.
//
// UPDATE (ADR-0128, gap-closure task #106): the real V2X Subscription Data resource (QueryV2xData
// -- real GET-only, no create/update operation exists at all) is now implemented, seeded at
// startup, same shape as coverage-restriction-data. Keyed by ueId alone, a genuinely new key
// shape (not part of the provisioned-data group's own composite key), so a new
// store/table rather than a new column.
//
// UPDATE (ADR-0129, gap-closure task #106): the real ProSe Service Subscription Data resource
// (real spec operationId `QueryPorseData` -- a literal typo in the spec itself, cited as-is, not
// corrected -- real GET-only, no create/update operation exists at all) is now implemented,
// seeded at startup, same shape as v2x-data.
//
// UPDATE (ADR-0130, gap-closure task #106): the real User Consent Subscription Data resource
// (real spec operationId `QueryUserConsentData`, schema `UcSubscriptionData` --
// TS29503_Nudm_SDM.yaml -- a single optional userConsentPerPurposeList map, no required fields at
// all -- real GET-only, no create/update operation exists at all) is now implemented, seeded at
// startup, same shape as prose-data. Takes UDR's real Nudr_DataRepository resource-type coverage
// to 43 of free5GC's ~42+ real resources -- past the free5GC-comparison baseline; real remaining
// work is the not-yet-surveyed remainder of TS29505_Subscription_Data.yaml and the genuinely
// deferred subsystems below.
//
// UPDATE (ADR-0131, gap-closure task #106): the real Time Synchronization Subscription Data
// resource (real spec operationId `QueryTimeSyncSubscriptionData`, schema
// `TimeSyncSubscriptionData` -- TS29503_Nudm_SDM.yaml -- unlike the last several GET-only
// resources closed, this one has real required fields (`afReqAuthorizations`, `serviceIds`) --
// real GET-only, no create/update operation exists at all) is now implemented, seeded at startup
// with a minimal real-shaped body, same shape as uc-data.
//
// UPDATE (ADR-0133, gap-closure task #106): the real UE's Location Information (Document)
// resource (real spec operationId `QueryUeLocation`, schema `LocationInfo` --
// TS29503_Nudm_UECM.yaml -- requires a non-empty registrationLocationInfoList -- real GET-only,
// no create/update operation exists at all) is now implemented, seeded at startup, same shape as
// time-sync-data. Its real sibling `nidd-authorization-data` was surveyed in the same pass and is
// genuinely blocked (not attempted): its spec requires real complex-object query parameters
// (`single-nssai` passed as `content: application/json` in the query string) this project has no
// precedent for parsing, same class of gap already disclosed for `pdtq-data`.
//
// UPDATE (ADR-0134, gap-closure task #106): the real A2X Subscription Data resource (real spec
// operationId `QueryA2xData`, schema `A2xSubscriptionData` -- TS29503_Nudm_SDM.yaml -- every field
// optional, same shape as v2x-data/prose-data -- real GET-only, no create/update operation exists
// at all) is now implemented, seeded at startup.
//
// UPDATE (ADR-0135, gap-closure task #106): the real Ranging and Sidelink Positioning Privacy
// Subscription Data resource (real spec operationId `QueryRangingSlPrivacyData`, schema
// `RangingSlPrivacyData` -- TS29503_Nudm_SDM.yaml -- every top-level field optional -- real
// GET-only, no create/update operation exists at all) is now implemented, seeded at startup. Real,
// disclosed: the spec's own optional `fields` query parameter (RFC 6570 form-style array,
// explode=false) for field-selection filtering is not honored -- the full stored document is
// always returned.
//
// UPDATE (ADR-0136, gap-closure task #106): the real Ranging and Sidelink Positioning Service
// Subscription Data resource (real spec operationId `QueryRangingSlPosData`, schema
// `RangingSlPosSubscriptionData` -- TS29503_Nudm_SDM.yaml -- every top-level field optional --
// real GET-only, no create/update operation exists at all) is now implemented, seeded at startup.
//
// UPDATE (ADR-0137, gap-closure task #106): the real 5MBS Subscription Data (Document) resource
// (real spec operationId `Query5mbsData`, schema `MbsSubscriptionData` -- TS29503_Nudm_SDM.yaml
// -- every field optional -- real GET-only, no create/update operation exists at all) is now
// implemented, seeded at startup.
//
// UPDATE (ADR-0139, gap-closure task #106): the real Service Specific Authorization Info
// (Document) context-data resource (real PUT+GET+PATCH+DELETE, schema
// `ServiceSpecificAuthorizationInfo` -- TS29505_Subscription_Data.yaml -- required
// serviceSpecificAuthorizationList, a map of AuthorizationInfo keyed by authId, real distinct
// 201-vs-204 PUT response codes, real RFC 6902 JSON Patch, same shape as nidd-authorizations's
// own resource) is now implemented. Composite (ueId, serviceType) key, same precedent as
// pp-data-store. Its real sibling GET-only resource at
// /subscription-data/{ueId}/service-specific-authorization-data/{serviceType} was surveyed in
// the same pass and confirmed genuinely blocked (not attempted): real required complex-object
// query parameters this project has no parsing precedent for, same class of gap already
// disclosed for nidd-authorization-data.
//
// UPDATE (ADR-0140, gap-closure task #106): the real Group Identifiers mapping resource (real
// spec operationId `GetGroupIdentifiers`, schema `GroupIdentifiers` -- every field optional --
// real GET-only, no path parameters, genuinely NOT per-UE) is now implemented, seeded at
// startup. Real, disclosed: `extGroupId`/`intGroupId` are alternate lookup keys for the same
// seeded record (at least one required, no unfiltered "list all" behavior implemented); the real
// `ue-id-ind` query parameter is not honored -- `ueIdList` is always included. This is the first
// real `group-data` sub-resource closed -- the rest of `group-data` (`5g-vn-groups`,
// `mbs-group-membership`, `ee-profile-data`'s own group-keyed sibling, and their own
// `/internal`/`/pp-profile-data` variants) remains genuinely deferred, not dropped.
//
// UPDATE (ADR-0141, gap-closure task #106): the real NSSAI update ack (Document) resource (real
// spec operations `CreateOrUpdateNssaiAck`/`QueryNssaiAck`, schema `NssaiAckData` -- required
// `provisioningTime`/`ueUpdateStatus` -- real PUT+GET, no PATCH/DELETE operation exists at all)
// is now implemented. Real, disclosed: unlike this project's other PUT resources, the spec
// documents only a single `204` response for this PUT (no `201`) -- no create-vs-update
// distinction exists for this resource. This is the first real
// `ue-update-confirmation-data` sub-resource closed -- its siblings (`sor-data`, `upu-data`,
// `subscribed-cag`) remain genuinely deferred, not dropped.
//
// UPDATE (ADR-0142, gap-closure task #106): the real CAG update ack (Document) resource (real
// spec operations `CreateCagUpdateAck`/`QueryCagAck`, schema `CagAckData` -- required
// `provisioningTime`/`ueUpdateStatus`, identical shape to `NssaiAckData` -- real PUT+GET, no
// PATCH/DELETE operation exists at all, same real 204-only-PUT shape) is now implemented. Its
// `sor-data`/`upu-data` siblings remain genuinely deferred, not dropped.
//
// UPDATE (ADR-0143, gap-closure task #106): the real Authentication SoR (Document) and
// Authentication UPU (Document) resources (real spec operations
// `CreateAuthenticationSoR`/`QueryAuthSoR`/`UpdateAuthenticationSoR` for `sor-data`, schema
// `SorData`; `CreateAuthenticationUPU`/`QueryAuthUPU` for `upu-data`, schema `UpuData` -- both
// required `provisioningTime`/`ueUpdateStatus`, real PUT+GET, same 204-only-PUT shape as
// `subscribed-snssais`/`subscribed-cag` above) are now implemented. Real, disclosed asymmetry:
// `sor-data` genuinely also has a real RFC 6902 `application/json-patch+json` PATCH (`apply_patch`
// NOT upsert-capable -- requires a prior PUT, same precedent as `nidd-authorizations`); `upu-data`
// has no PATCH/DELETE at all per the spec, a genuine difference despite both resources sharing the
// same `UeUpdateStatus`-based schema shape. This closes all four `ue-update-confirmation-data`
// sub-resources this project has surveyed.
//
// Deliberately still deferred, not dropped:
// context-data's other sub-resources (
// ee-subscriptions, sdm-subscriptions -- confirmed genuinely deeply-nested, see ADR-0122);
// the remainder of group-data (5g-vn-groups, mbs-group-membership, ee-profile-data's own
// group-keyed sibling, and their own /internal and /pp-profile-data variants -- group-identifiers
// itself closed, see ADR-0140 above);
// subs-to-notify; policy-data's
// own other resources (mbs-session-pol-data -- real, disclosed: its MbsSessPolDataId key is a
// deeply nested oneOf/anyOf object (mbsSessionId -> tmgi/ssm/nid, or afAppId) with no documented
// bare-path-segment string encoding at all, genuinely more ambiguous than snssai's own flat
// two-field shape, so left deferred rather than inventing a serialization; pdtq-data, and
// others); all of TS29504_Nudr_GroupIDmap.yaml; nidd-authorization-data (query-parameter-keyed,
// not ueId-alone -- a genuinely different resource shape, deferred to its own scoped turn).
//
// RFC 6902 JSON Patch, not RFC 7396 Merge Patch: AmfContext3gpp and UpdateSmfContext both use
// application/json-patch+json (confirmed by reading the YAML directly), unlike UDM's
// Update3GppRegistration/UpdateSmfRegistration which use application/merge-patch+json. Applied
// via nlohmann::json::patch() (matching nfs/nrf's own UpdateNFInstance), not .merge_patch(). Both
// patch responses always return 204 here (spec permits either 204-no-body or 200 with a
// PatchResult report body listing per-operation outcomes; 204 is simpler and doesn't require
// fabricating report items with no real per-op tracking behind them -- a disclosed, deliberate
// choice, not an oversight).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"
#include "sbi_core/jwt.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <thread>

// docs/DECISIONS.md ADR-0077 -- no hardcoded DB URL/deployment literal in source, see
// nf_config.hpp's own comment.
#include "nf_config/nf_config.hpp"

// TS29505_Subscription_Data's own types now live in TS29122_CommonData_grp.hpp -- see
// nfs/chf/src/stores.hpp's own comment (ADR-0072).
#include "TS29122_CommonData_grp.hpp"
// AuthEvent (real Authentication Status resource schema, TS29503_Nudm_UEAU.yaml, reused verbatim
// per TS29505_Subscription_Data.yaml's own $ref -- ADR-0083, gap-closure task #106) lives in its
// own generated group file, not TS29122_CommonData_grp.hpp.
#include "TS29503_Nudm_UEAU_grp.hpp"
#include "stores.hpp"

namespace {

using nlohmann::json;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/udr/CMakeLists.txt)"
#endif
#ifndef CONFIG_DIR
#error "CONFIG_DIR must be defined by CMake (see nfs/udr/CMakeLists.txt)"
#endif

constexpr const char* kNfType = "UDR";
constexpr const char* kApiRoot = "/nudr-dr/v2";
// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0120). Real, distinct server base
// path for TS29504_Nudr_GroupIDmap.yaml's own Nudr_GroupIDmap service -- a genuinely different
// real Nudr API from Nudr_DataRepository above, not a typo/duplicate of kApiRoot.
constexpr const char* kGroupIdMapApiRoot = "/nudr-group-id-map/v1";

// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

// Same pattern as every other NF's check_bearer -- see nfs/nrf/src/main.cpp's comment for why a
// missing Authorization header is not itself a 401 (bootstrap security alternative:
// `security: [{}, oAuth2ClientCredentials:[...]]` in the YAML).
std::optional<sbi_core::jwt::VerifyResult> check_bearer(const sbi_core::http2::Request& req,
                                                        sbi_core::jwt::Verifier& verifier) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) {
        return std::nullopt;
    }
    const std::string& value = it->second;
    constexpr std::string_view kPrefix = "Bearer ";
    if (value.size() <= kPrefix.size() || value.compare(0, kPrefix.size(), kPrefix) != 0) {
        sbi_core::jwt::VerifyResult r;
        r.valid = false;
        r.error = "Authorization header present but not a Bearer token";
        return r;
    }
    return verifier.verify(value.substr(kPrefix.size()));
}

// Runs on a dedicated thread, never on the server's io_context -- same reasoning as
// nfs/amf/src/main.cpp's run_nrf_lifecycle (docs/DECISIONS.md ADR-0006/ADR-0019).
void run_nrf_lifecycle(const std::string& udr_instance_id, const std::string& nrf_base) {
    sbi_core::http2::TlsConfig client_tls{
        .cert_path = CERTS_DIR "/udr/cert.pem",
        .key_path = CERTS_DIR "/udr/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client http_client(std::move(client_tls));

    for (int attempt = 0; attempt < 300; ++attempt) {
        sbi_core::http2::ClientRequest probe;
        probe.method = "GET";
        probe.url = nrf_base + "/nnrf-nfm/v1/nf-instances/00000000-0000-4000-8000-000000000000";
        if (http_client.send(probe).has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sbi_core::OAuth2Client oauth(
        http_client, nrf_base + "/oauth2/token", udr_instance_id, "nnrf-nfm", "NRF");

    constexpr int kHeartbeatSeconds = 30;
    json profile{
        {"nfInstanceId", udr_instance_id},
        {"nfType", kNfType},
        {"nfStatus", "REGISTERED"},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };

    while (true) {
        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("udr: OAuth2 token fetch failed: {}", token.error());
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        sbi_core::http2::ClientRequest put_req;
        put_req.method = "PUT";
        put_req.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + udr_instance_id;
        put_req.headers.emplace("content-type", "application/json");
        put_req.headers.emplace("authorization", "Bearer " + *token);
        put_req.headers.emplace(
            sbi_core::headers::kSenderTimestamp,
            sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
        put_req.body = profile.dump();

        auto put_resp = http_client.send(put_req);
        if (put_resp.has_value() && (put_resp->status == 200 || put_resp->status == 201)) {
            spdlog::info("udr: registered with NRF (HTTP {})", put_resp->status);
            break;
        }
        spdlog::warn("udr: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));

        auto token = oauth.get_bearer_token();
        if (!token.has_value()) {
            spdlog::error("udr: OAuth2 token fetch failed for heartbeat: {}", token.error());
            continue;
        }

        sbi_core::http2::ClientRequest patch_req;
        patch_req.method = "PATCH";
        patch_req.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + udr_instance_id;
        patch_req.headers.emplace("content-type", "application/json-patch+json");
        patch_req.headers.emplace("authorization", "Bearer " + *token);
        patch_req.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();

        auto patch_resp = http_client.send(patch_req);
        if (!patch_resp.has_value() || patch_resp->status != 200) {
            spdlog::warn("udr: heartbeat failed");
        }
    }
}

} // namespace

int main() {
    const auto config = nf_config::load("udr", CONFIG_DIR);
    const auto port = nf_config::require<unsigned short>(config, "port");
    const auto metrics_bind_address =
        nf_config::require<std::string>(config, "metrics_bind_address");
    const auto nrf_base_url =
        nf_config::require<std::string>(config, "nrf_base_url", "UDR_NRF_BASE_URL");
    const auto conninfo =
        nf_config::require<std::string>(config, "database_url", "UDR_DATABASE_URL");

    sbi_core::init_logging("udr");
    sbi_core::init_tracing("udr");
    sbi_core::init_metrics(metrics_bind_address);

    const std::string udr_instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("udr: starting, nfInstanceId={}", udr_instance_id);

    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/udr/cert.pem",
        .key_path = CERTS_DIR "/udr/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };

    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);

    udr::AmfContextStore amf_contexts(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0093).
    udr::AmfNon3GppContextStore amf_non3gpp_contexts(conninfo);
    udr::SmfRegistrationStore smf_registrations(conninfo);
    udr::ProvisionedDataStore provisioned_data(conninfo);
    udr::SmPolicyDataStore sm_policy_data(conninfo);
    udr::AuthenticationSubscriptionDataStore auth_subscription_data(conninfo);
    udr::AuthenticationStatusStore auth_status(conninfo);
    udr::AmPolicyDataStore am_policy_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0097).
    udr::SmsfContext3gppStore smsf_3gpp_context(conninfo);
    udr::SmsfNon3GppContextStore smsf_non3gpp_context(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0098).
    udr::IpSmGwContextStore ip_sm_gw_context(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0099).
    udr::MessageWaitingDataStore mwd(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0100).
    udr::RoamingInformationStore roaming_information(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0101).
    udr::PeiInfoStore pei_info(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0102).
    udr::CoverageRestrictionDataStore coverage_restriction_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0103).
    udr::LcsPrivacyDataStore lcs_privacy_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0104).
    udr::LcsSubscriptionDataStore lcs_subscription_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0105).
    udr::LcsMoDataStore lcs_mo_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0107).
    udr::PpDataStore pp_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0108).
    udr::PpProfileDataStore pp_profile_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0109).
    udr::PpDataEntryStore pp_data_entry(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0110).
    udr::SharedDataStore shared_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0111).
    udr::OperatorSpecificDataStore operator_specific_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0112).
    udr::EeProfileDataStore ee_profile_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0113).
    udr::UePolicySetStore ue_policy_set(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0114).
    udr::PolicyOperatorSpecificDataStore policy_operator_specific_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0115).
    udr::SponsorConnectivityDataStore sponsor_connectivity_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0116).
    udr::BdtDataStore bdt_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0117).
    udr::PlmnUePolicySetStore plmn_ue_policy_set(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0118).
    udr::SlicePolicyDataStore slice_control_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0119).
    udr::GroupPolicyDataStore group_control_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0120).
    udr::RoutingIdStore routing_ids(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0121).
    udr::NiddAuthorizationInfoStore nidd_authorization_info(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0122).
    udr::IdentityDataStore identity_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0123).
    udr::OdbDataStore odb_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0128).
    udr::V2xDataStore v2x_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0129).
    udr::ProseDataStore prose_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0130).
    udr::UcDataStore uc_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0131).
    udr::TimeSyncDataStore time_sync_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0133).
    udr::LocationDataStore location_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0134).
    udr::A2xDataStore a2x_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0135).
    udr::RangingSlPrivacyDataStore rangingsl_privacy_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0136).
    udr::RangingSlPosDataStore ranging_slpos_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0137).
    udr::MbsDataStore mbs_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0139).
    udr::ServiceSpecificAuthorizationInfoStore service_specific_authorization_info(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0140).
    udr::GroupIdentifiersStore group_identifiers(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0141).
    udr::NssaiAckDataStore nssai_ack_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0142).
    udr::CagAckDataStore cag_ack_data(conninfo);
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0143).
    udr::SorDataStore sor_data(conninfo);
    udr::UpuDataStore upu_data(conninfo);

    // Real seed data (ADR-0069, gap-closure Tier 1b) -- the real provisioned-data group is
    // GET-only per spec (no create/update operation exists at all, see schema.postgres.sql's own
    // header), so there is no live provisioning path yet; seeded here for the same two real test
    // SUPIs nfs/udm/src/main.cpp's own AuthenticationSubscriptionStore already seeds
    // ("imsi-999700000000001"/"...002", UERANSIM's own real test values), so a real end-to-end
    // AUSF->UDM->UDR chain has real, non-empty data to return for at least these subscribers.
    // servingPlmnId "99970" = this project's own real lab PLMN, mcc=999/mnc=70 (ADR-0016),
    // VarPlmnId's real format per TS29505_Subscription_Data.yaml (mcc+mnc concatenated).
    // sst=1/sd="000001" matches that same ADR-0016 lab S-NSSAI. dnn="internet" is the real,
    // standard default DNN/APN name used industry-wide (free5gc/open5gs both default to it too),
    // not invented for this project. SmfSelectionSubscriptionData.subscribedSnssaiInfos and
    // SessionManagementSubscriptionData.dnnConfigurations are real, cited, opaque-JSON fields this
    // codegen couldn't strongly type (OPAQUE FALLBACK) -- left unpopulated here, a real, disclosed
    // gap rather than a guessed nested shape. lcs_bca_data.locationAssistanceType is a real `Bytes`
    // field (TS29571_CommonData.yaml, base64) -- "dGVzdA==" (base64 of "test") is this project's
    // own arbitrary representative test payload, not real 3GPP assistance-data content (ADR-0106,
    // gap-closure task #106). sms_mng_data.mtSmsSubscribed is a real optional boolean field
    // (TS29503_Nudm_SDM.yaml's SmsManagementSubscriptionData) -- true is this project's own
    // representative test choice (ADR-0125, gap-closure task #106). sms_data.smsSubscribed is a
    // real optional boolean field (TS29503_Nudm_SDM.yaml's SmsSubscriptionData, genuinely
    // distinct from SmsManagementSubscriptionData's own mtSmsSubscribed above) -- true is this
    // project's own representative test choice (ADR-0126, gap-closure task #106).
    // trace_data.traceRef/traceDepth are real fields (TS29571_CommonData.yaml's TraceData) --
    // "99970-A1B2C3" matches the real cited traceRef pattern (MCC+MNC + "-" + 3-octet hex Trace
    // ID per TS 32.422) for this project's own real lab PLMN, and "MEDIUM" is a real TraceDepth
    // enum value, this project's own representative test choice (ADR-0127, gap-closure task
    // #106).
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json am_data;
        am_data["nssai"]["defaultSingleNssais"] = json::array({json{{"sst", 1}, {"sd", "000001"}}});
        json sm_data;
        sm_data["singleNssai"] = json{{"sst", 1}, {"sd", "000001"}};
        json lcs_bca_data;
        lcs_bca_data["locationAssistanceType"] = "dGVzdA==";
        json sms_mng_data;
        sms_mng_data["mtSmsSubscribed"] = true;
        json sms_data;
        sms_data["smsSubscribed"] = true;
        json trace_data;
        trace_data["traceRef"] = "99970-A1B2C3";
        trace_data["traceDepth"] = "MEDIUM";
        provisioned_data.seed(supi,
                              "99970",
                              std::make_optional(am_data),
                              std::make_optional(json::object()),
                              std::make_optional(sm_data),
                              std::make_optional(lcs_bca_data),
                              std::make_optional(sms_mng_data),
                              std::make_optional(sms_data),
                              std::make_optional(trace_data));
    }

    // Real seed data (ADR-0102, gap-closure task #106) -- the real Enhanced Coverage Restriction
    // Data resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning
    // as provisioned-data above. Seeded for the same two real test SUPIs, real lab PLMN
    // (mcc=999/mnc=70, ADR-0016); ecRestrictionDataNb explicitly false is this project's own
    // representative test value, not a spec default beyond the schema's own documented
    // `default: false`.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json coverage_data;
        coverage_data["plmnEcInfoList"] = json::array({json{
            {"plmnId", json{{"mcc", "999"}, {"mnc", "70"}}}, {"ecRestrictionDataNb", false}}});
        coverage_restriction_data.seed(supi, coverage_data);
    }

    // Real seed data (ADR-0103, gap-closure task #106) -- the real LCS Privacy Subscription Data
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. `locationPrivacyInd` is a real enum value from LocationPrivacyInd
    // (TS29503_Nudm_SDM.yaml) -- LOCATION_ALLOWED is this project's own representative test
    // choice, not a spec default.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json lcs_privacy;
        lcs_privacy["lpi"]["locationPrivacyInd"] = "LOCATION_ALLOWED";
        lcs_privacy_data.seed(supi, lcs_privacy);
    }

    // Real seed data (ADR-0104, gap-closure task #106) -- the real LCS Subscription Data resource
    // is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as above.
    // `pruInd: "NON_PRU"` is a real enum value from PruInd (TS29503_Nudm_SDM.yaml), this project's
    // own representative test choice; `userPlanePosIndLmf: false` matches the schema's own
    // documented `default: false`.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json lcs_subscription;
        lcs_subscription["pruInd"] = "NON_PRU";
        lcs_subscription["userPlanePosIndLmf"] = false;
        lcs_subscription_data.seed(supi, lcs_subscription);
    }

    // Real seed data (ADR-0105, gap-closure task #106) -- the real LCS Mobile Originated
    // Subscription Data resource is genuinely GET-only per spec, same "no live provisioning path
    // yet" reasoning as above. `BASIC_SELF_LOCATION` is a real enum value from LcsMoServiceClass
    // (TS29503_Nudm_SDM.yaml), this project's own representative test choice for the mandatory
    // `allowedServiceClasses` field.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json lcs_mo;
        lcs_mo["allowedServiceClasses"] = json::array({"BASIC_SELF_LOCATION"});
        lcs_mo_data.seed(supi, lcs_mo);
    }

    // Real seed data (ADR-0108, gap-closure task #106) -- the real Parameter Provision profile
    // Data resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning
    // as above. `"ALL"` is a real, documented special key for `allowedMtcProviders` (per this
    // schema's own description text, not fabricated); `afId: "af1"` is this project's own
    // arbitrary representative test value.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json pp_profile;
        pp_profile["allowedMtcProviders"]["ALL"] = json::array({json{{"afId", "af1"}}});
        pp_profile_data.seed(supi, pp_profile);
    }

    // Real seed data (ADR-0110, gap-closure task #106) -- the real individual Shared Data
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. Genuinely NOT per-UE, so seeded once (not looped over the two test SUPIs).
    // "10000-default" matches SharedDataId's own real pattern (^[0-9]{5,6}-.+$), this project's
    // own representative test identifier, not fabricated spec content.
    {
        json shared;
        shared["sharedDataId"] = "10000-default";
        shared_data.seed("10000-default", shared);
    }

    // Real seed data (ADR-0112, gap-closure task #106) -- the real Event Exposure Data resource
    // is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as above.
    // `LOSS_OF_CONNECTIVITY` is a real enum value from EventType (TS29503_Nudm_EE.yaml), this
    // project's own representative test choice for the optional `restrictedEventTypes` field.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json ee_profile;
        ee_profile["restrictedEventTypes"] = json::array({"LOSS_OF_CONNECTIVITY"});
        ee_profile_data.seed(supi, ee_profile);
    }

    // Real seed data (ADR-0115, gap-closure task #106) -- the real Sponsor Connectivity Data
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. Genuinely NOT per-UE, so seeded once. "sponsor1" is this project's own arbitrary
    // representative test sponsorId; `aspIds` is the real mandatory field.
    {
        json sponsor;
        sponsor["aspIds"] = json::array({"asp1"});
        sponsor_connectivity_data.seed("sponsor1", sponsor);
    }

    // Real seed data (ADR-0117, gap-closure task #106) -- the real PLMN UE Policy Set resource is
    // genuinely GET-only per spec, same "no live provisioning path yet" reasoning as above.
    // Genuinely NOT per-UE, so seeded once for this project's own real lab PLMN ("99970",
    // mcc=999/mnc=70, ADR-0016). `subscCats` is a real optional field (array of strings, no
    // further-typed enum in the spec); "cat1" is this project's own arbitrary representative test
    // value, not fabricated spec content.
    {
        json plmn_ue_policy;
        plmn_ue_policy["subscCats"] = json::array({"cat1"});
        plmn_ue_policy_set.seed("99970", plmn_ue_policy);
    }

    // Real seed data (ADR-0120, gap-closure task #106) -- the real GetRoutingIDs resource
    // (Nudr_GroupIDmap, genuinely distinct API from Nudr_DataRepository above) is genuinely
    // GET-only per spec, same "no live provisioning path yet" reasoning as above. Composite-keyed
    // by (nf_type, nf_group_id); "UDM" matches this project's own real, already-built NF type
    // (TS29510_Nnrf_NFManagement.yaml's own NFType enum). "udm-group-1" is this project's own
    // arbitrary representative test NfGroupId, not fabricated spec content. "0001" is a real
    // RoutingIndicator per its own documented pattern (^[0-9]{1,4}$).
    {
        json routing_id;
        routing_id["routingIndicators"] = json::array({"0001"});
        routing_ids.seed("UDM", "udm-group-1", routing_id);
    }

    // Real seed data (ADR-0123, gap-closure task #106) -- the real ODB Data resource is genuinely
    // GET-only per spec, same "no live provisioning path yet" reasoning as above.
    // `roamingOdb: "OUTSIDE_HOME_PLMN"` is a real enum value from RoamingOdb
    // (TS29571_CommonData.yaml), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json odb;
        odb["roamingOdb"] = "OUTSIDE_HOME_PLMN";
        odb_data.seed(supi, odb);
    }

    // Real seed data (ADR-0128, gap-closure task #106) -- the real V2X Subscription Data resource
    // is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as above.
    // `nrV2xServicesAuth.vehicleUeAuth: "AUTHORIZED"` is a real enum value from UeAuth
    // (TS29571_CommonData.yaml), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json v2x;
        v2x["nrV2xServicesAuth"]["vehicleUeAuth"] = "AUTHORIZED";
        v2x_data.seed(supi, v2x);
    }

    // Real seed data (ADR-0129, gap-closure task #106) -- the real ProSe Service Subscription
    // Data resource is genuinely GET-only per spec, same "no live provisioning path yet"
    // reasoning as above. `proseServiceAuth.proseDirectDiscoveryAuth: "AUTHORIZED"` is a real
    // enum value from UeAuth (TS29571_CommonData.yaml), this project's own representative test
    // choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json prose;
        prose["proseServiceAuth"]["proseDirectDiscoveryAuth"] = "AUTHORIZED";
        prose_data.seed(supi, prose);
    }

    // Real seed data (ADR-0130, gap-closure task #106) -- the real User Consent Subscription Data
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. `ANALYTICS`/`CONSENT_GIVEN` are real enum values from UcPurpose/UserConsent
    // (TS29503_Nudm_SDM.yaml), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json uc;
        uc["userConsentPerPurposeList"]["ANALYTICS"] = "CONSENT_GIVEN";
        uc_data.seed(supi, uc);
    }

    // Real seed data (ADR-0131, gap-closure task #106) -- the real Time Synchronization
    // Subscription Data resource is genuinely GET-only per spec, same "no live provisioning path
    // yet" reasoning as above. Real schema TimeSyncSubscriptionData (TS29503_Nudm_SDM.yaml)
    // requires both afReqAuthorizations (oneOf gptpAllowedInfoList/astiAllowedInfo) and
    // serviceIds; the minimal real-shaped seed below uses gptpAllowedInfoList (an array of
    // GptpAllowedInfo, every field inside optional) and a single TimeSyncServiceId (its own
    // `reference` field is the only required one), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json time_sync;
        time_sync["afReqAuthorizations"]["gptpAllowedInfoList"] =
            json::array({json{{"dnn", "internet"}, {"gptpAllowed", true}}});
        time_sync["serviceIds"] = json::array({json{{"reference", "ts-service-1"}}});
        time_sync_data.seed(supi, time_sync);
    }

    // Real seed data (ADR-0133, gap-closure task #106) -- the real UE's Location Information
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. Real schema LocationInfo (TS29503_Nudm_UECM.yaml) requires a non-empty
    // registrationLocationInfoList; each RegistrationLocationInfo requires amfInstanceId (a real
    // UUID-format NfInstanceId) and accessTypeList (real AccessType enum,
    // TS29571_CommonData.yaml). Synthetic test AMF instance ID and `3GPP_ACCESS`, this project's
    // own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json location;
        location["registrationLocationInfoList"] =
            json::array({json{{"amfInstanceId", "00000000-0000-4000-8000-00000000a001"},
                              {"accessTypeList", json::array({"3GPP_ACCESS"})}}});
        location_data.seed(supi, location);
    }

    // Real seed data (ADR-0134, gap-closure task #106) -- the real A2X Subscription Data
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. `nrA2xServicesAuth.uavUeAuth: "AUTHORIZED"` is a real enum value from UeAuth
    // (TS29571_CommonData.yaml), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json a2x;
        a2x["nrA2xServicesAuth"]["uavUeAuth"] = "AUTHORIZED";
        a2x_data.seed(supi, a2x);
    }

    // Real seed data (ADR-0135, gap-closure task #106) -- the real Ranging and Sidelink
    // Positioning Privacy Subscription Data resource is genuinely GET-only per spec, same "no
    // live provisioning path yet" reasoning as above. `rslppi.rangingSlPrivacyInd:
    // "RANGINGSL_ALLOWED"` is a real enum value from RangingSlPrivacyInd
    // (TS29503_Nudm_SDM.yaml), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json rangingsl_privacy;
        rangingsl_privacy["rslppi"]["rangingSlPrivacyInd"] = "RANGINGSL_ALLOWED";
        rangingsl_privacy_data.seed(supi, rangingsl_privacy);
    }

    // Real seed data (ADR-0136, gap-closure task #106) -- the real Ranging and Sidelink
    // Positioning Service Subscription Data resource is genuinely GET-only per spec, same "no
    // live provisioning path yet" reasoning as above. `rangingSlPosAuth.rgSlPosPc5Auth:
    // "AUTHORIZED"` is a real enum value from UeAuth (TS29571_CommonData.yaml), this project's
    // own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json ranging_slpos;
        ranging_slpos["rangingSlPosAuth"]["rgSlPosPc5Auth"] = "AUTHORIZED";
        ranging_slpos_data.seed(supi, ranging_slpos);
    }

    // Real seed data (ADR-0137, gap-closure task #106) -- the real 5MBS Subscription Data
    // (Document) resource is genuinely GET-only per spec, same "no live provisioning path yet"
    // reasoning as above. `mbsAllowed: true` is a real, simple boolean field from
    // MbsSubscriptionData (TS29503_Nudm_SDM.yaml), this project's own representative test choice.
    for (const std::string& supi :
         {std::string("imsi-999700000000001"), std::string("imsi-999700000000002")}) {
        json mbs;
        mbs["mbsAllowed"] = true;
        mbs_data.seed(supi, mbs);
    }

    // Real seed data (ADR-0140, gap-closure task #106) -- the real Group Identifiers mapping
    // resource is genuinely GET-only per spec, same "no live provisioning path yet" reasoning as
    // above. Real, disclosed test values matching each field's own real pattern:
    // "extgroupid-group1@example.com" (ExtGroupId, ^extgroupid-[^@]+@[^@]+$),
    // "A1B2C3D4-001-01-AB" (GroupId/intGroupId,
    // ^[A-Fa-f0-9]{8}-[0-9]{3}-[0-9]{2,3}-([A-Fa-f0-9][A-Fa-f0-9]){1,10}$). ueIdList uses the same
    // two real test SUPIs every other seeded resource uses.
    {
        json group;
        group["extGroupId"] = "extgroupid-group1@example.com";
        group["intGroupId"] = "A1B2C3D4-001-01-AB";
        group["ueIdList"] = json::array(
            {json{{"supi", "imsi-999700000000001"}}, json{{"supi", "imsi-999700000000002"}}});
        group_identifiers.seed("extgroupid-group1@example.com", "A1B2C3D4-001-01-AB", group);
    }

    auto meter = sbi_core::get_meter("udr");
    auto amf_ctx_write_counter = meter->CreateUInt64Counter(
        "udr_amf_context_write_total", "Total CreateAmfContext3gpp/AmfContext3gpp calls");
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0093).
    auto amf_non3gpp_ctx_write_counter = meter->CreateUInt64Counter(
        "udr_amf_non3gpp_context_write_total", "Total CreateAmfContextNon3gpp calls");
    auto smf_reg_write_counter =
        meter->CreateUInt64Counter("udr_smf_registration_write_total",
                                   "Total CreateOrUpdateSmfRegistration/UpdateSmfContext calls");
    auto smf_reg_delete_counter = meter->CreateUInt64Counter("udr_smf_registration_delete_total",
                                                             "Total DeleteSmfRegistration calls");
    auto provisioned_data_get_counter = meter->CreateUInt64Counter(
        "udr_provisioned_data_get_total",
        "Total provisioned-data am-data/smf-selection-subscription-data/sm-data/lcs-bca-data GET "
        "calls");
    auto sm_policy_data_get_counter = meter->CreateUInt64Counter(
        "udr_sm_policy_data_get_total", "Total ReadSessionManagementPolicyData calls");
    auto sm_policy_data_patch_counter = meter->CreateUInt64Counter(
        "udr_sm_policy_data_patch_total", "Total UpdateSessionManagementPolicyData calls");
    auto auth_subscription_get_counter = meter->CreateUInt64Counter(
        "udr_auth_subscription_get_total", "Total QueryAuthSubsData calls");
    auto auth_subscription_patch_counter = meter->CreateUInt64Counter(
        "udr_auth_subscription_patch_total", "Total ModifyAuthenticationSubscription calls");
    auto auth_status_put_counter = meter->CreateUInt64Counter(
        "udr_auth_status_put_total", "Total CreateAuthenticationStatus calls");
    auto auth_status_get_counter = meter->CreateUInt64Counter(
        "udr_auth_status_get_total", "Total QueryAuthenticationStatus calls");
    auto auth_status_delete_counter = meter->CreateUInt64Counter(
        "udr_auth_status_delete_total", "Total DeleteAuthenticationStatus calls");
    auto am_policy_data_get_counter = meter->CreateUInt64Counter(
        "udr_am_policy_data_get_total", "Total ReadAccessAndMobilityPolicyData calls");
    auto am_policy_data_patch_counter = meter->CreateUInt64Counter(
        "udr_am_policy_data_patch_total", "Total UpdateAccessAndMobilityPolicyData calls");
    auto smsf_3gpp_write_counter = meter->CreateUInt64Counter("udr_smsf_3gpp_context_write_total",
                                                              "Total CreateSmsfContext3gpp calls");
    auto smsf_3gpp_delete_counter = meter->CreateUInt64Counter("udr_smsf_3gpp_context_delete_total",
                                                               "Total DeleteSmsfContext3gpp calls");
    auto smsf_non3gpp_write_counter = meter->CreateUInt64Counter(
        "udr_smsf_non3gpp_context_write_total", "Total CreateSmsfContextNon3gpp calls");
    auto smsf_non3gpp_delete_counter = meter->CreateUInt64Counter(
        "udr_smsf_non3gpp_context_delete_total", "Total DeleteSmsfContextNon3gpp calls");
    auto ip_sm_gw_write_counter = meter->CreateUInt64Counter(
        "udr_ip_sm_gw_context_write_total", "Total CreateIpSmGwContext/ModifyIpSmGwContext calls");
    auto ip_sm_gw_delete_counter = meter->CreateUInt64Counter("udr_ip_sm_gw_context_delete_total",
                                                              "Total DeleteIpSmGwContext calls");
    auto mwd_write_counter = meter->CreateUInt64Counter(
        "udr_mwd_write_total", "Total CreateMessageWaitingData/ModifyMessageWaitingData calls");
    auto mwd_delete_counter =
        meter->CreateUInt64Counter("udr_mwd_delete_total", "Total DeleteMessageWaitingData calls");
    auto roaming_information_write_counter = meter->CreateUInt64Counter(
        "udr_roaming_information_write_total", "Total UpdateRoamingInformation calls");
    auto pei_info_write_counter = meter->CreateUInt64Counter(
        "udr_pei_info_write_total", "Total CreateOrUpdatePeiInformation calls");
    auto coverage_restriction_data_get_counter = meter->CreateUInt64Counter(
        "udr_coverage_restriction_data_get_total", "Total QueryCoverageRestrictionData calls");
    auto lcs_privacy_data_get_counter = meter->CreateUInt64Counter(
        "udr_lcs_privacy_data_get_total", "Total QueryLcsPrivacyData calls");
    auto lcs_subscription_data_get_counter = meter->CreateUInt64Counter(
        "udr_lcs_subscription_data_get_total", "Total QueryLcsSubscriptionData calls");
    auto lcs_mo_data_get_counter =
        meter->CreateUInt64Counter("udr_lcs_mo_data_get_total", "Total QueryLcsMoData calls");
    auto pp_data_get_counter =
        meter->CreateUInt64Counter("udr_pp_data_get_total", "Total GetppData calls");
    auto pp_data_patch_counter =
        meter->CreateUInt64Counter("udr_pp_data_patch_total", "Total ModifyPpData calls");
    auto pp_profile_data_get_counter =
        meter->CreateUInt64Counter("udr_pp_profile_data_get_total", "Total QueryPPData calls");
    auto pp_data_entry_write_counter = meter->CreateUInt64Counter(
        "udr_pp_data_entry_write_total", "Total Create PP Data Entry calls");
    auto pp_data_entry_delete_counter = meter->CreateUInt64Counter(
        "udr_pp_data_entry_delete_total", "Total Delete PP Data Entry calls");
    auto shared_data_get_counter = meter->CreateUInt64Counter(
        "udr_shared_data_get_total", "Total GetIndividualSharedData calls");
    auto operator_specific_data_get_counter = meter->CreateUInt64Counter(
        "udr_operator_specific_data_get_total", "Total QueryOperSpecData calls");
    auto operator_specific_data_patch_counter = meter->CreateUInt64Counter(
        "udr_operator_specific_data_patch_total", "Total ModifyOperSpecData calls");
    auto ee_profile_data_get_counter =
        meter->CreateUInt64Counter("udr_ee_profile_data_get_total", "Total QueryEEData calls");
    auto ue_policy_set_write_counter = meter->CreateUInt64Counter(
        "udr_ue_policy_set_write_total", "Total CreateOrReplaceUEPolicySet calls");
    auto ue_policy_set_get_counter =
        meter->CreateUInt64Counter("udr_ue_policy_set_get_total", "Total ReadUEPolicySet calls");
    auto ue_policy_set_patch_counter = meter->CreateUInt64Counter("udr_ue_policy_set_patch_total",
                                                                  "Total UpdateUEPolicySet calls");
    auto policy_operator_specific_data_get_counter = meter->CreateUInt64Counter(
        "udr_policy_operator_specific_data_get_total", "Total ReadOperatorSpecificData calls");
    auto policy_operator_specific_data_patch_counter = meter->CreateUInt64Counter(
        "udr_policy_operator_specific_data_patch_total", "Total UpdateOperatorSpecificData calls");
    auto sponsor_connectivity_data_get_counter = meter->CreateUInt64Counter(
        "udr_sponsor_connectivity_data_get_total", "Total ReadSponsorConnectivityData calls");
    auto bdt_data_write_counter = meter->CreateUInt64Counter("udr_bdt_data_write_total",
                                                             "Total CreateIndividualBdtData calls");
    auto bdt_data_get_counter =
        meter->CreateUInt64Counter("udr_bdt_data_get_total", "Total ReadIndividualBdtData calls");
    auto bdt_data_patch_counter = meter->CreateUInt64Counter("udr_bdt_data_patch_total",
                                                             "Total UpdateIndividualBdtData calls");
    auto bdt_data_delete_counter = meter->CreateUInt64Counter(
        "udr_bdt_data_delete_total", "Total DeleteIndividualBdtData calls");
    auto plmn_ue_policy_set_get_counter = meter->CreateUInt64Counter(
        "udr_plmn_ue_policy_set_get_total", "Total ReadPlmnUePolicySet calls");
    auto slice_control_data_get_counter = meter->CreateUInt64Counter(
        "udr_slice_control_data_get_total", "Total ReadSlicePolicyControlData calls");
    auto slice_control_data_patch_counter = meter->CreateUInt64Counter(
        "udr_slice_control_data_patch_total", "Total UpdateSlicePolicyControlData calls");
    auto group_control_data_get_counter = meter->CreateUInt64Counter(
        "udr_group_control_data_get_total", "Total ReadGroupPolCtrlData calls");
    auto group_control_data_patch_counter = meter->CreateUInt64Counter(
        "udr_group_control_data_patch_total", "Total ModifyGroupPolCtrlData calls");
    auto routing_ids_get_counter =
        meter->CreateUInt64Counter("udr_routing_ids_get_total", "Total GetRoutingIDs calls");
    auto nidd_authorization_write_counter = meter->CreateUInt64Counter(
        "udr_nidd_authorization_write_total",
        "Total CreateNIDDAuthorizationInfo/ModifyNiddAuthorizationInfo calls");
    auto nidd_authorization_delete_counter = meter->CreateUInt64Counter(
        "udr_nidd_authorization_delete_total", "Total RemoveNiddAuthorizationInfo calls");
    auto identity_data_get_counter =
        meter->CreateUInt64Counter("udr_identity_data_get_total", "Total GetIdentityData calls");
    auto identity_data_patch_counter = meter->CreateUInt64Counter("udr_identity_data_patch_total",
                                                                  "Total ModifyIdentityData calls");
    auto odb_data_get_counter =
        meter->CreateUInt64Counter("udr_odb_data_get_total", "Total GetOdbData calls");
    auto v2x_data_get_counter =
        meter->CreateUInt64Counter("udr_v2x_data_get_total", "Total QueryV2xData calls");
    auto prose_data_get_counter =
        meter->CreateUInt64Counter("udr_prose_data_get_total", "Total QueryPorseData calls");
    auto uc_data_get_counter =
        meter->CreateUInt64Counter("udr_uc_data_get_total", "Total QueryUserConsentData calls");
    auto time_sync_data_get_counter = meter->CreateUInt64Counter(
        "udr_time_sync_data_get_total", "Total QueryTimeSyncSubscriptionData calls");
    auto location_data_get_counter =
        meter->CreateUInt64Counter("udr_location_data_get_total", "Total QueryUeLocation calls");
    auto a2x_data_get_counter =
        meter->CreateUInt64Counter("udr_a2x_data_get_total", "Total QueryA2xData calls");
    auto rangingsl_privacy_data_get_counter = meter->CreateUInt64Counter(
        "udr_rangingsl_privacy_data_get_total", "Total QueryRangingSlPrivacyData calls");
    auto ranging_slpos_data_get_counter = meter->CreateUInt64Counter(
        "udr_ranging_slpos_data_get_total", "Total QueryRangingSlPosData calls");
    auto mbs_data_get_counter =
        meter->CreateUInt64Counter("udr_5mbs_data_get_total", "Total Query5mbsData calls");
    auto service_specific_auth_write_counter = meter->CreateUInt64Counter(
        "udr_service_specific_auth_write_total",
        "Total CreateServiceSpecificAuthorizationInfo/ModifyServiceSpecificAuthorizationInfo "
        "calls");
    auto service_specific_auth_delete_counter =
        meter->CreateUInt64Counter("udr_service_specific_auth_delete_total",
                                   "Total RemoveServiceSpecificAuthorizationInfo calls");
    auto group_identifiers_get_counter = meter->CreateUInt64Counter(
        "udr_group_identifiers_get_total", "Total GetGroupIdentifiers calls");
    auto nssai_ack_data_write_counter = meter->CreateUInt64Counter(
        "udr_nssai_ack_data_write_total", "Total CreateOrUpdateNssaiAck calls");
    auto nssai_ack_data_get_counter =
        meter->CreateUInt64Counter("udr_nssai_ack_data_get_total", "Total QueryNssaiAck calls");
    auto cag_ack_data_write_counter = meter->CreateUInt64Counter("udr_cag_ack_data_write_total",
                                                                 "Total CreateCagUpdateAck calls");
    auto cag_ack_data_get_counter =
        meter->CreateUInt64Counter("udr_cag_ack_data_get_total", "Total QueryCagAck calls");
    auto sor_data_write_counter = meter->CreateUInt64Counter(
        "udr_sor_data_write_total", "Total CreateAuthenticationSoR/UpdateAuthenticationSoR calls");
    auto sor_data_get_counter =
        meter->CreateUInt64Counter("udr_sor_data_get_total", "Total QueryAuthSoR calls");
    auto upu_data_write_counter = meter->CreateUInt64Counter("udr_upu_data_write_total",
                                                             "Total CreateAuthenticationUPU calls");
    auto upu_data_get_counter =
        meter->CreateUInt64Counter("udr_upu_data_get_total", "Total QueryAuthUPU calls");

    boost::asio::io_context ioc;
    // 0.0.0.0: same Docker-reachability reasoning as NRF's bind -- see docs/DECISIONS.md ADR-0014.
    sbi_core::http2::Server server(ioc, "0.0.0.0", port, server_tls);

    const std::string amf_ctx_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/amf-3gpp-access";

    server.add_route(
        "GET",
        amf_ctx_path_pattern,
        [&verifier, &amf_contexts](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto context = amf_contexts.get(ue_id);
            if (!context.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF context for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, context->dump());
        });

    server.add_route(
        "PUT",
        amf_ctx_path_pattern,
        [&verifier, &amf_contexts, &amf_ctx_write_counter, amf_ctx_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<sbi_gen::Amf3GppAccessRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = amf_contexts.put(ue_id, j);
            amf_ctx_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", amf_ctx_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        amf_ctx_path_pattern,
        [&verifier, &amf_contexts, &amf_ctx_write_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            std::optional<json> patched;
            try {
                patched = amf_contexts.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF context for ueId " + ue_id);
            }
            amf_ctx_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #106, ADR-0093): real
    // QueryAmfContextNon3gpp/CreateAmfContextNon3gpp -- GET+PUT, mirrors the 3GPP-access group
    // above exactly (same real spec shape: no PATCH/DELETE for this resource either), backed by
    // its own distinct table/store (a real, separate resource, not a rename of the 3GPP one).
    const std::string amf_non3gpp_ctx_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/amf-non-3gpp-access";

    server.add_route(
        "GET",
        amf_non3gpp_ctx_path_pattern,
        [&verifier, &amf_non3gpp_contexts](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto context = amf_non3gpp_contexts.get(ue_id);
            if (!context.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AMF non-3GPP-access context for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, context->dump());
        });

    server.add_route(
        "PUT",
        amf_non3gpp_ctx_path_pattern,
        [&verifier,
         &amf_non3gpp_contexts,
         &amf_non3gpp_ctx_write_counter,
         amf_non3gpp_ctx_path_pattern](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<sbi_gen::AmfNon3GppAccessRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = amf_non3gpp_contexts.put(ue_id, j);
            amf_non3gpp_ctx_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", amf_non3gpp_ctx_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    const std::string smf_reg_list_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/smf-registrations";
    const std::string smf_reg_path_pattern = smf_reg_list_path_pattern + "/{pduSessionId}";

    server.add_route(
        "GET",
        smf_reg_list_path_pattern,
        [&verifier, &smf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            sbi_gen::SmfRegList list;
            for (const auto& registration : smf_registrations.list_for_ue(ue_id)) {
                list.push_back(registration.get<sbi_gen::SmfRegistration>());
            }
            json j = list;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "GET",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            auto registration = smf_registrations.get(ue_id, pdu_session_id);
            if (!registration.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            return sbi_core::http2::Response::json(200, registration->dump());
        });

    server.add_route(
        "PUT",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations, &smf_reg_write_counter, smf_reg_list_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmfRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            json j = *body;
            const bool is_new = smf_registrations.put(ue_id, pdu_session_id, j);
            smf_reg_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", smf_reg_list_path_pattern + "/" + pdu_session_id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations, &smf_reg_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            std::optional<json> patched;
            try {
                patched = smf_registrations.apply_patch(ue_id, pdu_session_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            smf_reg_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "DELETE",
        smf_reg_path_pattern,
        [&verifier, &smf_registrations, &smf_reg_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto pdu_session_id = req.path_params.at("pduSessionId");
            if (!smf_registrations.get(ue_id, pdu_session_id).has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No SMF registration for ueId/pduSessionId " + ue_id + "/" + pdu_session_id);
            }
            smf_registrations.remove(ue_id, pdu_session_id);
            smf_reg_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: provisioned-data group (ADR-0069, gap-closure Tier 1b) -- real,
    // GET-only per spec (see this file's own header), keyed by (ueId, servingPlmnId) per the real
    // path shape TS29505_Subscription_Data.yaml defines. ---

    const std::string provisioned_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/{servingPlmnId}/provisioned-data";

    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/am-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_am_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned am-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/smf-selection-subscription-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_smf_sel_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No provisioned smf-selection-subscription-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/sm-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_sm_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned sm-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // ADR-0106, gap-closure task #106: real LCS Broadcast Assistance Subscription Data
    // (QueryLcsBcaData), same real GET-only (ueId, servingPlmnId) shape as the three routes above.
    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/lcs-bca-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_lcs_bca_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned lcs-bca-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // ADR-0125, gap-closure task #106: real SMS Management Subscription Data
    // (QuerySmsMngData), same real GET-only (ueId, servingPlmnId) shape as the routes above.
    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/sms-mng-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_sms_mng_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned sms-mng-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // ADR-0126, gap-closure task #106: real SMS Subscription Data (QuerySmsData), same real
    // GET-only (ueId, servingPlmnId) shape as the routes above -- genuinely distinct from
    // sms-mng-data above, not a duplicate.
    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/sms-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_sms_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned sms-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // ADR-0127, gap-closure task #106: real Trace Data (QueryTraceData), same real GET-only
    // (ueId, servingPlmnId) shape as the routes above. Real response schema is a `oneOf` (full
    // `TraceData` object or a bare `SharedDataId` string) -- returned as opaque JSON.
    server.add_route(
        "GET",
        provisioned_data_path_pattern + "/trace-data",
        [&verifier, &provisioned_data, &provisioned_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto serving_plmn_id = req.path_params.at("servingPlmnId");
            auto data = provisioned_data.get_trace_data(ue_id, serving_plmn_id);
            provisioned_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No provisioned trace-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: policy-data group, SM policy resource (ADR-0072, gap-closure: real
    // N28 end-to-end) -- real GET+PATCH per TS29519_Policy_Data.yaml, keyed by ueId alone (no
    // servingPlmnId in the real path, unlike provisioned-data above -- genuinely different
    // resource, see schema.postgres.sql's own comment). ---

    const std::string sm_policy_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/ues/{ueId}/sm-data";

    server.add_route(
        "GET",
        sm_policy_data_path_pattern,
        [&verifier, &sm_policy_data, &sm_policy_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = sm_policy_data.get(ue_id);
            sm_policy_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SM policy data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        sm_policy_data_path_pattern,
        [&verifier, &sm_policy_data, &sm_policy_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto patched = sm_policy_data.merge_patch(ue_id, patch);
            sm_policy_data_patch_counter->Add(1);
            // Real spec: 204 (no body) or 200 (with the updated SmPolicyData) are both valid --
            // this project returns 200 with the real updated document, same real information a
            // future GUI editing this resource would want back without a second GET round-trip.
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: Authentication Data group (ADR-0083, gap-closure task #106) ---

    const std::string auth_subscription_path_pattern =
        std::string(kApiRoot) +
        "/subscription-data/{ueId}/authentication-data/authentication-subscription";

    server.add_route(
        "GET",
        auth_subscription_path_pattern,
        [&verifier, &auth_subscription_data, &auth_subscription_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = auth_subscription_data.get(ue_id);
            auth_subscription_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No authentication subscription data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        auth_subscription_path_pattern,
        [&verifier, &auth_subscription_data, &auth_subscription_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            // Real spec: application/json-patch+json (RFC 6902) -- same standard AmfContext3gpp
            // above uses, confirmed by reading the YAML directly.
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            json patched;
            try {
                patched = auth_subscription_data.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            auth_subscription_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    const std::string auth_status_path_pattern =
        std::string(kApiRoot) +
        "/subscription-data/{ueId}/authentication-data/authentication-status";

    server.add_route(
        "PUT",
        auth_status_path_pattern,
        [&verifier, &auth_status, &auth_status_put_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::AuthEvent>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            auth_status.put(ue_id, json(*body));
            auth_status_put_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        auth_status_path_pattern,
        [&verifier, &auth_status, &auth_status_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = auth_status.get(ue_id);
            auth_status_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No authentication status for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "DELETE",
        auth_status_path_pattern,
        [&verifier, &auth_status, &auth_status_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!auth_status.remove(ue_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No authentication status for ueId " + ue_id);
            }
            auth_status_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: policy-data group, AM policy resource (ADR-0083, gap-closure task
    // #106) -- real GET+PATCH per TS29519_Policy_Data.yaml, the real UDR-side backing for PCF's
    // own Npcf_AMPolicyControl. Genuinely distinct from provisioned-data's own `am_data` column
    // (AccessAndMobilitySubscriptionData) -- see schema.postgres.sql's own comment. ---

    const std::string am_policy_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/ues/{ueId}/am-data";

    server.add_route(
        "GET",
        am_policy_data_path_pattern,
        [&verifier, &am_policy_data, &am_policy_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = am_policy_data.get(ue_id);
            am_policy_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No AM policy data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        am_policy_data_path_pattern,
        [&verifier, &am_policy_data, &am_policy_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto patched = am_policy_data.merge_patch(ue_id, patch);
            am_policy_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: SMSF Registration context-data group (ADR-0097, gap-closure task
    // #106) -- real GET+PUT+DELETE per TS29505_Subscription_Data.yaml, two distinct real
    // resources (3GPP-access / non-3GPP-access) sharing the identical real `SmsfRegistration`
    // schema -- see schema.postgres.sql's own comment for why these stay two separate
    // tables/stores rather than merged. ---

    const std::string smsf_3gpp_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/smsf-3gpp-access";

    server.add_route(
        "PUT",
        smsf_3gpp_path_pattern,
        [&verifier, &smsf_3gpp_context, &smsf_3gpp_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmsfRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            smsf_3gpp_context.put(ue_id, json(*body));
            smsf_3gpp_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        smsf_3gpp_path_pattern,
        [&verifier, &smsf_3gpp_context](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = smsf_3gpp_context.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SMSF 3GPP-access context for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "DELETE",
        smsf_3gpp_path_pattern,
        [&verifier, &smsf_3gpp_context, &smsf_3gpp_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!smsf_3gpp_context.remove(ue_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SMSF 3GPP-access context for ueId " + ue_id);
            }
            smsf_3gpp_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    const std::string smsf_non3gpp_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/smsf-non-3gpp-access";

    server.add_route(
        "PUT",
        smsf_non3gpp_path_pattern,
        [&verifier, &smsf_non3gpp_context, &smsf_non3gpp_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SmsfRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            smsf_non3gpp_context.put(ue_id, json(*body));
            smsf_non3gpp_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        smsf_non3gpp_path_pattern,
        [&verifier, &smsf_non3gpp_context](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = smsf_non3gpp_context.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SMSF non-3GPP-access context for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "DELETE",
        smsf_non3gpp_path_pattern,
        [&verifier, &smsf_non3gpp_context, &smsf_non3gpp_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!smsf_non3gpp_context.remove(ue_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SMSF non-3GPP-access context for ueId " + ue_id);
            }
            smsf_non3gpp_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: IP-SM-GW Registration context-data resource (ADR-0098, gap-closure
    // task #106) -- real PUT+GET+PATCH(RFC 6902)+DELETE per TS29505_Subscription_Data.yaml, the
    // richest operation set of any context-data resource this project has closed so far. ---

    const std::string ip_sm_gw_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/ip-sm-gw";

    server.add_route(
        "PUT",
        ip_sm_gw_path_pattern,
        [&verifier, &ip_sm_gw_context, &ip_sm_gw_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::IpSmGwRegistration>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            ip_sm_gw_context.put(ue_id, json(*body));
            ip_sm_gw_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        ip_sm_gw_path_pattern,
        [&verifier, &ip_sm_gw_context](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = ip_sm_gw_context.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No IP-SM-GW context for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        ip_sm_gw_path_pattern,
        [&verifier, &ip_sm_gw_context, &ip_sm_gw_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            std::optional<json> patched;
            try {
                patched = ip_sm_gw_context.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No IP-SM-GW context for ueId " + ue_id);
            }
            ip_sm_gw_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "DELETE",
        ip_sm_gw_path_pattern,
        [&verifier, &ip_sm_gw_context, &ip_sm_gw_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!ip_sm_gw_context.remove(ue_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No IP-SM-GW context for ueId " + ue_id);
            }
            ip_sm_gw_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: Message Waiting Data (Document) resource (ADR-0099, gap-closure
    // task #106) -- real PUT+GET+PATCH(RFC 6902)+DELETE per TS29505_Subscription_Data.yaml. ---

    const std::string mwd_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/mwd";

    server.add_route(
        "PUT",
        mwd_path_pattern,
        [&verifier, &mwd, &mwd_write_counter, mwd_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::MessageWaitingData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = mwd.put(ue_id, j);
            mwd_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", mwd_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET", mwd_path_pattern, [&verifier, &mwd](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = mwd.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Message Waiting Data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        mwd_path_pattern,
        [&verifier, &mwd, &mwd_write_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            std::optional<json> patched;
            try {
                patched = mwd.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Message Waiting Data for ueId " + ue_id);
            }
            mwd_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "DELETE",
        mwd_path_pattern,
        [&verifier, &mwd, &mwd_delete_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!mwd.remove(ue_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Message Waiting Data for ueId " + ue_id);
            }
            mwd_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: Roaming Information (Document) resource (ADR-0100, gap-closure
    // task #106) -- real PUT+GET per TS29505_Subscription_Data.yaml, no PATCH/DELETE. ---

    const std::string roaming_information_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/roaming-information";

    server.add_route(
        "PUT",
        roaming_information_path_pattern,
        [&verifier,
         &roaming_information,
         &roaming_information_write_counter,
         roaming_information_path_pattern](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::RoamingInfoUpdate>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = roaming_information.put(ue_id, j);
            roaming_information_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", roaming_information_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        roaming_information_path_pattern,
        [&verifier, &roaming_information](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = roaming_information.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Roaming Information for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: PEI Information (Document) resource (ADR-0101, gap-closure
    // task #106) -- real PUT+GET per TS29505_Subscription_Data.yaml, no PATCH/DELETE. ---

    const std::string pei_info_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/pei-info";

    server.add_route(
        "PUT",
        pei_info_path_pattern,
        [&verifier, &pei_info, &pei_info_write_counter, pei_info_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::PeiUpdateInfo_Subscription_Data>(
                req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = pei_info.put(ue_id, j);
            pei_info_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", pei_info_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET", pei_info_path_pattern, [&verifier, &pei_info](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = pei_info.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No PEI Information for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Enhanced Coverage Restriction Data resource (ADR-0102, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup (no
    // create/update operation exists in the spec, same shape as provisioned-data). ---

    const std::string coverage_restriction_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/coverage-restriction-data";

    server.add_route(
        "GET",
        coverage_restriction_data_path_pattern,
        [&verifier, &coverage_restriction_data, &coverage_restriction_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = coverage_restriction_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Coverage Restriction Data for ueId " + ue_id);
            }
            coverage_restriction_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: LCS Privacy Subscription Data resource (ADR-0103, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup. ---

    const std::string lcs_privacy_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/lcs-privacy-data";

    server.add_route(
        "GET",
        lcs_privacy_data_path_pattern,
        [&verifier, &lcs_privacy_data, &lcs_privacy_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = lcs_privacy_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No LCS Privacy Data for ueId " + ue_id);
            }
            lcs_privacy_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: LCS Subscription Data resource (ADR-0104, gap-closure task #106)
    // -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup. ---

    const std::string lcs_subscription_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/lcs-subscription-data";

    server.add_route(
        "GET",
        lcs_subscription_data_path_pattern,
        [&verifier, &lcs_subscription_data, &lcs_subscription_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = lcs_subscription_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No LCS Subscription Data for ueId " + ue_id);
            }
            lcs_subscription_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: LCS Mobile Originated Subscription Data resource (ADR-0105,
    // gap-closure task #106) -- real GET-only per TS29505_Subscription_Data.yaml, seeded at
    // startup. ---

    const std::string lcs_mo_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/lcs-mo-data";

    server.add_route(
        "GET",
        lcs_mo_data_path_pattern,
        [&verifier, &lcs_mo_data, &lcs_mo_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = lcs_mo_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No LCS Mobile Originated Data for ueId " + ue_id);
            }
            lcs_mo_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Parameter Provision (Document) resource (ADR-0107, gap-closure
    // task #106) -- real GET+PATCH(RFC 6902) per TS29505_Subscription_Data.yaml, no PUT/DELETE. ---

    const std::string pp_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/pp-data";

    server.add_route(
        "GET",
        pp_data_path_pattern,
        [&verifier, &pp_data, &pp_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = pp_data.get(ue_id);
            pp_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No pp-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        pp_data_path_pattern,
        [&verifier, &pp_data, &pp_data_patch_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            json patched;
            try {
                patched = pp_data.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            pp_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: Parameter Provision profile Data (Document) resource (ADR-0108,
    // gap-closure task #106) -- real GET-only per TS29505_Subscription_Data.yaml, seeded at
    // startup. ---

    const std::string pp_profile_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/pp-profile-data";

    server.add_route(
        "GET",
        pp_profile_data_path_pattern,
        [&verifier, &pp_profile_data, &pp_profile_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = pp_profile_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No pp-profile-data for ueId " + ue_id);
            }
            pp_profile_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Provisioned Parameter Data Entry resource (ADR-0109, gap-closure
    // task #106) -- real PUT+GET+DELETE per TS29505_Subscription_Data.yaml, plus a real sibling
    // collection GET, composite (ueId, afInstanceId) key. ---

    const std::string pp_data_store_list_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/pp-data-store";
    const std::string pp_data_store_path_pattern =
        pp_data_store_list_path_pattern + "/{afInstanceId}";

    server.add_route(
        "GET",
        pp_data_store_list_path_pattern,
        [&verifier, &pp_data_entry](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            sbi_gen::PpDataEntryList list;
            list.ppDataEntryList = std::vector<sbi_gen::PpDataEntry>{};
            for (const auto& entry : pp_data_entry.list_for_ue(ue_id)) {
                list.ppDataEntryList->push_back(entry.get<sbi_gen::PpDataEntry>());
            }
            json j = list;
            return sbi_core::http2::Response::json(200, j.dump());
        });

    server.add_route(
        "GET",
        pp_data_store_path_pattern,
        [&verifier, &pp_data_entry](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto af_instance_id = req.path_params.at("afInstanceId");
            auto data = pp_data_entry.get(ue_id, af_instance_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(404,
                                                         "Not Found",
                                                         "No PP data entry for ueId/afInstanceId " +
                                                             ue_id + "/" + af_instance_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PUT",
        pp_data_store_path_pattern,
        [&verifier, &pp_data_entry, &pp_data_entry_write_counter, pp_data_store_list_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::PpDataEntry>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto af_instance_id = req.path_params.at("afInstanceId");
            json j = *body;
            const bool is_new = pp_data_entry.put(ue_id, af_instance_id, j);
            pp_data_entry_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location",
                                 pp_data_store_list_path_pattern + "/" + af_instance_id);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "DELETE",
        pp_data_store_path_pattern,
        [&verifier, &pp_data_entry, &pp_data_entry_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto af_instance_id = req.path_params.at("afInstanceId");
            if (!pp_data_entry.remove(ue_id, af_instance_id)) {
                return sbi_core::http2::problem_response(404,
                                                         "Not Found",
                                                         "No PP data entry for ueId/afInstanceId " +
                                                             ue_id + "/" + af_instance_id);
            }
            pp_data_entry_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: individual Shared Data resource (ADR-0110, gap-closure task #106)
    // -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup. Genuinely NOT
    // per-UE -- keyed by sharedDataId alone. ---

    const std::string shared_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/shared-data/{sharedDataId}";

    server.add_route(
        "GET",
        shared_data_path_pattern,
        [&verifier, &shared_data, &shared_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto shared_data_id = req.path_params.at("sharedDataId");
            auto data = shared_data.get(shared_data_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No shared data for sharedDataId " + shared_data_id);
            }
            shared_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Operator-Specific Data Container (Document) resource (ADR-0111,
    // gap-closure task #106) -- real GET+PATCH(RFC 6902) per TS29505_Subscription_Data.yaml, no
    // PUT/DELETE. ---

    const std::string operator_specific_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/operator-specific-data";

    server.add_route(
        "GET",
        operator_specific_data_path_pattern,
        [&verifier, &operator_specific_data, &operator_specific_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = operator_specific_data.get(ue_id);
            operator_specific_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No operator-specific-data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        operator_specific_data_path_pattern,
        [&verifier, &operator_specific_data, &operator_specific_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            json patched;
            try {
                patched = operator_specific_data.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            operator_specific_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: Event Exposure Data (Document) resource (ADR-0112, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup. ---

    const std::string ee_profile_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/ee-profile-data";

    server.add_route(
        "GET",
        ee_profile_data_path_pattern,
        [&verifier, &ee_profile_data, &ee_profile_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = ee_profile_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No ee-profile-data for ueId " + ue_id);
            }
            ee_profile_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: policy-data group, UE Policy Set resource (ADR-0113, gap-closure
    // task #106) -- real GET+PUT+PATCH(RFC 7396) per TS29519_Policy_Data.yaml, no DELETE. ---

    const std::string ue_policy_set_path_pattern =
        std::string(kApiRoot) + "/policy-data/ues/{ueId}/ue-policy-set";

    server.add_route(
        "GET",
        ue_policy_set_path_pattern,
        [&verifier, &ue_policy_set, &ue_policy_set_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = ue_policy_set.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No UE policy set for ueId " + ue_id);
            }
            ue_policy_set_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PUT",
        ue_policy_set_path_pattern,
        [&verifier, &ue_policy_set, &ue_policy_set_write_counter, ue_policy_set_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const bool is_new = ue_policy_set.put(ue_id, body);
            ue_policy_set_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", ue_policy_set_path_pattern);
            resp.body = body.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        ue_policy_set_path_pattern,
        [&verifier, &ue_policy_set, &ue_policy_set_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            ue_policy_set.merge_patch(ue_id, patch);
            ue_policy_set_patch_counter->Add(1);
            // Real spec: only 204 (no-body) is documented for this resource's real PATCH, unlike
            // am-data's own 200-with-body option -- confirmed by direct YAML read.
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: policy-data group, Operator-Specific Data resource (ADR-0114,
    // gap-closure task #106) -- real GET+PATCH(RFC 6902) per TS29519_Policy_Data.yaml, no
    // PUT/DELETE. ---

    const std::string policy_operator_specific_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/ues/{ueId}/operator-specific-data";

    server.add_route(
        "GET",
        policy_operator_specific_data_path_pattern,
        [&verifier, &policy_operator_specific_data, &policy_operator_specific_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = policy_operator_specific_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No policy-data operator-specific-data for ueId " + ue_id);
            }
            policy_operator_specific_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        policy_operator_specific_data_path_pattern,
        [&verifier, &policy_operator_specific_data, &policy_operator_specific_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            json patched;
            try {
                patched = policy_operator_specific_data.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            policy_operator_specific_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: policy-data group, Sponsor Connectivity Data resource (ADR-0115,
    // gap-closure task #106) -- real GET-only per TS29519_Policy_Data.yaml, keyed by sponsorId
    // (not ueId), seeded at startup. ---

    const std::string sponsor_connectivity_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/sponsor-connectivity-data/{sponsorId}";

    server.add_route(
        "GET",
        sponsor_connectivity_data_path_pattern,
        [&verifier, &sponsor_connectivity_data, &sponsor_connectivity_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto sponsor_id = req.path_params.at("sponsorId");
            auto data = sponsor_connectivity_data.get(sponsor_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No sponsor connectivity data for sponsorId " + sponsor_id);
            }
            sponsor_connectivity_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: policy-data group, individual BDT Data resource (ADR-0116,
    // gap-closure task #106) -- real GET+PUT+PATCH(RFC 7396)+DELETE per
    // TS29519_Policy_Data.yaml. ---

    const std::string bdt_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/bdt-data/{bdtReferenceId}";

    server.add_route(
        "GET",
        bdt_data_path_pattern,
        [&verifier, &bdt_data, &bdt_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto bdt_ref_id = req.path_params.at("bdtReferenceId");
            auto data = bdt_data.get(bdt_ref_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No BDT data for bdtReferenceId " + bdt_ref_id);
            }
            bdt_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PUT",
        bdt_data_path_pattern,
        [&verifier, &bdt_data, &bdt_data_write_counter, bdt_data_path_pattern](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto bdt_ref_id = req.path_params.at("bdtReferenceId");
            bdt_data.put(bdt_ref_id, body);
            bdt_data_write_counter->Add(1);
            // Real spec: CreateIndividualBdtData documents ONLY 201 as a success response (no
            // update-via-PUT status) -- confirmed by direct read, this route always responds 201,
            // matching the real spec literally rather than inventing an undocumented 204.
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", bdt_data_path_pattern);
            resp.body = body.dump();
            return resp;
        });

    server.add_route(
        "PATCH",
        bdt_data_path_pattern,
        [&verifier, &bdt_data, &bdt_data_patch_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto bdt_ref_id = req.path_params.at("bdtReferenceId");
            const auto patched = bdt_data.merge_patch(bdt_ref_id, patch);
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No BDT data for bdtReferenceId " + bdt_ref_id);
            }
            bdt_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched->dump());
        });

    server.add_route(
        "DELETE",
        bdt_data_path_pattern,
        [&verifier, &bdt_data, &bdt_data_delete_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto bdt_ref_id = req.path_params.at("bdtReferenceId");
            if (!bdt_data.remove(bdt_ref_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No BDT data for bdtReferenceId " + bdt_ref_id);
            }
            bdt_data_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: policy-data group, PLMN UE Policy Set resource (ADR-0117,
    // gap-closure task #106) -- real GET-only per TS29519_Policy_Data.yaml, seeded at startup (no
    // create/update operation exists in the spec, same shape as coverage-restriction-data). ---

    const std::string plmn_ue_policy_set_path_pattern =
        std::string(kApiRoot) + "/policy-data/plmns/{plmnId}/ue-policy-set";

    server.add_route(
        "GET",
        plmn_ue_policy_set_path_pattern,
        [&verifier, &plmn_ue_policy_set, &plmn_ue_policy_set_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto plmn_id = req.path_params.at("plmnId");
            auto data = plmn_ue_policy_set.get(plmn_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No PLMN UE Policy Set for plmnId " + plmn_id);
            }
            plmn_ue_policy_set_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: policy-data group, Slice-specific Policy Control Data resource
    // (ADR-0118, gap-closure task #106) -- real GET+PATCH-only per TS29519_Policy_Data.yaml, no
    // PUT/POST create operation exists, so merge_patch is upsert-capable (same precedent as
    // am-data). ---

    const std::string slice_control_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/slice-control-data/{snssai}";

    server.add_route(
        "GET",
        slice_control_data_path_pattern,
        [&verifier, &slice_control_data, &slice_control_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto snssai = req.path_params.at("snssai");
            auto data = slice_control_data.get(snssai);
            slice_control_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Slice Policy Control Data for snssai " + snssai);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        slice_control_data_path_pattern,
        [&verifier, &slice_control_data, &slice_control_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto snssai = req.path_params.at("snssai");
            const auto patched = slice_control_data.merge_patch(snssai, patch);
            slice_control_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: policy-data group, group-specific Policy Control Data resource
    // (ADR-0119, gap-closure task #106) -- real GET+PATCH-only per TS29519_Policy_Data.yaml, no
    // PUT/POST create operation exists, so merge_patch is upsert-capable (same precedent as
    // am-data/slice-control-data). ---

    const std::string group_control_data_path_pattern =
        std::string(kApiRoot) + "/policy-data/group-control-data/{intGroupId}";

    server.add_route(
        "GET",
        group_control_data_path_pattern,
        [&verifier, &group_control_data, &group_control_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto int_group_id = req.path_params.at("intGroupId");
            auto data = group_control_data.get(int_group_id);
            group_control_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Group Policy Control Data for intGroupId " + int_group_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        group_control_data_path_pattern,
        [&verifier, &group_control_data, &group_control_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch;
            try {
                patch = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto int_group_id = req.path_params.at("intGroupId");
            const auto patched = group_control_data.merge_patch(int_group_id, patch);
            group_control_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_GroupIDmap: GetRoutingIDs (ADR-0120, gap-closure task #106) -- real GET-only per
    // TS29504_Nudr_GroupIDmap.yaml, a genuinely different real Nudr API from Nudr_DataRepository
    // above (see this file's own header comment for the full disclosure). Two real required
    // query parameters, nf-type and nf-group-id; no path parameters. ---

    const std::string routing_ids_path_pattern = std::string(kGroupIdMapApiRoot) + "/routing-ids";

    server.add_route(
        "GET",
        routing_ids_path_pattern,
        [&verifier, &routing_ids, &routing_ids_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto nf_type_it = req.query_params.find("nf-type");
            const auto nf_group_id_it = req.query_params.find("nf-group-id");
            if (nf_type_it == req.query_params.end() || nf_group_id_it == req.query_params.end()) {
                return sbi_core::http2::problem_response(
                    400,
                    "Bad Request",
                    "nf-type and nf-group-id are both required query parameters");
            }
            auto data = routing_ids.get(nf_type_it->second, nf_group_id_it->second);
            routing_ids_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(404,
                                                         "Not Found",
                                                         "No Routing IDs for nf-type " +
                                                             nf_type_it->second + " nf-group-id " +
                                                             nf_group_id_it->second);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: NIDD Authorization Info context-data resource (ADR-0121,
    // gap-closure task #106) -- real PUT+GET+PATCH+DELETE per TS29505_Subscription_Data.yaml,
    // real distinct 201-vs-204 PUT response codes (same shape as amf-3gpp-access's own resource),
    // real RFC 6902 application/json-patch+json PATCH. ---

    const std::string nidd_authorization_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/nidd-authorizations";

    server.add_route(
        "PUT",
        nidd_authorization_path_pattern,
        [&verifier,
         &nidd_authorization_info,
         &nidd_authorization_write_counter,
         nidd_authorization_path_pattern](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::NiddAuthorizationInfo>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            const bool is_new = nidd_authorization_info.put(ue_id, j);
            nidd_authorization_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", nidd_authorization_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        nidd_authorization_path_pattern,
        [&verifier, &nidd_authorization_info](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = nidd_authorization_info.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No NIDD Authorization Info for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        nidd_authorization_path_pattern,
        [&verifier, &nidd_authorization_info, &nidd_authorization_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            std::optional<json> patched;
            try {
                patched = nidd_authorization_info.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No NIDD Authorization Info for ueId " + ue_id);
            }
            nidd_authorization_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "DELETE",
        nidd_authorization_path_pattern,
        [&verifier, &nidd_authorization_info, &nidd_authorization_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            if (!nidd_authorization_info.remove(ue_id)) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No NIDD Authorization Info for ueId " + ue_id);
            }
            nidd_authorization_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: Query/Modify Identity Data by SUPI or GPSI resource (ADR-0122,
    // gap-closure task #106) -- real GET+PATCH per TS29505_Subscription_Data.yaml, no PUT/POST
    // create operation exists, so apply_patch is upsert-capable (same precedent as pp-data). ---

    const std::string identity_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/identity-data";

    server.add_route(
        "GET",
        identity_data_path_pattern,
        [&verifier, &identity_data, &identity_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = identity_data.get(ue_id);
            identity_data_get_counter->Add(1);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Identity Data for ueId " + ue_id);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        identity_data_path_pattern,
        [&verifier, &identity_data, &identity_data_patch_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            json patched;
            try {
                patched = identity_data.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            identity_data_patch_counter->Add(1);
            return sbi_core::http2::Response::json(200, patched.dump());
        });

    // --- Nudr_DataRepository: Query ODB Data by SUPI or GPSI resource (ADR-0123, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup (no
    // create/update operation exists in the spec, same shape as coverage-restriction-data). ---

    const std::string odb_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/operator-determined-barring-data";

    server.add_route(
        "GET",
        odb_data_path_pattern,
        [&verifier, &odb_data, &odb_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = odb_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No ODB Data for ueId " + ue_id);
            }
            odb_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: V2X Subscription Data resource (ADR-0128, gap-closure task #106)
    // -- real GET-only per TS29505_Subscription_Data.yaml, seeded at startup (no create/update
    // operation exists in the spec, same shape as coverage-restriction-data). Keyed by ueId
    // alone, genuinely NOT part of the provisioned-data group's own composite key shape. ---

    const std::string v2x_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/v2x-data";

    server.add_route(
        "GET",
        v2x_data_path_pattern,
        [&verifier, &v2x_data, &v2x_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = v2x_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No V2X Subscription Data for ueId " + ue_id);
            }
            v2x_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: ProSe Service Subscription Data resource (ADR-0129, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml (real spec operationId
    // `QueryPorseData`, a literal spec typo, cited as-is), seeded at startup. Keyed by ueId
    // alone. ---

    const std::string prose_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/prose-data";

    server.add_route(
        "GET",
        prose_data_path_pattern,
        [&verifier, &prose_data, &prose_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = prose_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No ProSe Service Subscription Data for ueId " + ue_id);
            }
            prose_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: User Consent Subscription Data resource (ADR-0130, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml (real spec operationId
    // `QueryUserConsentData`), schema `UcSubscriptionData` (TS29503_Nudm_SDM.yaml), seeded at
    // startup. Keyed by ueId alone. ---

    const std::string uc_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/uc-data";

    server.add_route(
        "GET",
        uc_data_path_pattern,
        [&verifier, &uc_data, &uc_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = uc_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No User Consent Subscription Data for ueId " + ue_id);
            }
            uc_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Time Synchronization Subscription Data resource (ADR-0131,
    // gap-closure task #106) -- real GET-only per TS29505_Subscription_Data.yaml (real spec
    // operationId `QueryTimeSyncSubscriptionData`), schema `TimeSyncSubscriptionData`
    // (TS29503_Nudm_SDM.yaml), seeded at startup. Keyed by ueId alone. ---

    const std::string time_sync_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/time-sync-data";

    server.add_route(
        "GET",
        time_sync_data_path_pattern,
        [&verifier, &time_sync_data, &time_sync_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = time_sync_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Time Synchronization Subscription Data for ueId " + ue_id);
            }
            time_sync_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: UE's Location Information (Document) resource (ADR-0133,
    // gap-closure task #106) -- real GET-only per TS29505_Subscription_Data.yaml (real spec
    // operationId `QueryUeLocation`), schema `LocationInfo` (TS29503_Nudm_UECM.yaml), seeded at
    // startup. Keyed by ueId alone. ---

    const std::string location_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/context-data/location";

    server.add_route(
        "GET",
        location_data_path_pattern,
        [&verifier, &location_data, &location_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = location_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Location Information for ueId " + ue_id);
            }
            location_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: A2X Subscription Data resource (ADR-0134, gap-closure task #106)
    // -- real GET-only per TS29505_Subscription_Data.yaml (real spec operationId
    // `QueryA2xData`), seeded at startup. Keyed by ueId alone. ---

    const std::string a2x_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/a2x-data";

    server.add_route(
        "GET",
        a2x_data_path_pattern,
        [&verifier, &a2x_data, &a2x_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = a2x_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No A2X Subscription Data for ueId " + ue_id);
            }
            a2x_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Ranging and Sidelink Positioning Privacy Subscription Data
    // resource (ADR-0135, gap-closure task #106) -- real GET-only per
    // TS29505_Subscription_Data.yaml (real spec operationId `QueryRangingSlPrivacyData`), seeded
    // at startup. Keyed by ueId alone. Real, disclosed: the spec's own optional `fields` query
    // parameter for field-selection filtering is not honored -- the full stored document is
    // always returned. ---

    const std::string rangingsl_privacy_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/rangingsl-privacy-data";

    server.add_route(
        "GET",
        rangingsl_privacy_data_path_pattern,
        [&verifier, &rangingsl_privacy_data, &rangingsl_privacy_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = rangingsl_privacy_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Ranging and Sidelink Positioning Privacy Subscription Data for ueId " +
                        ue_id);
            }
            rangingsl_privacy_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Ranging and Sidelink Positioning Service Subscription Data
    // resource (ADR-0136, gap-closure task #106) -- real GET-only per
    // TS29505_Subscription_Data.yaml (real spec operationId `QueryRangingSlPosData`), seeded at
    // startup. Keyed by ueId alone. ---

    const std::string ranging_slpos_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/ranging-slpos-data";

    server.add_route(
        "GET",
        ranging_slpos_data_path_pattern,
        [&verifier, &ranging_slpos_data, &ranging_slpos_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = ranging_slpos_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Ranging and Sidelink Positioning Service Subscription Data for ueId " +
                        ue_id);
            }
            ranging_slpos_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: 5MBS Subscription Data (Document) resource (ADR-0137, gap-closure
    // task #106) -- real GET-only per TS29505_Subscription_Data.yaml (real spec operationId
    // `Query5mbsData`), seeded at startup. Keyed by ueId alone. ---

    const std::string mbs_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/5mbs-data";

    server.add_route(
        "GET",
        mbs_data_path_pattern,
        [&verifier, &mbs_data, &mbs_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = mbs_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No 5MBS Subscription Data for ueId " + ue_id);
            }
            mbs_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Service Specific Authorization Info (Document) context-data
    // resource (ADR-0139, gap-closure task #106) -- real PUT+GET+PATCH+DELETE per
    // TS29505_Subscription_Data.yaml, real distinct 201-vs-204 PUT response codes (same shape as
    // nidd-authorizations's own resource), real RFC 6902 application/json-patch+json PATCH.
    // Composite (ueId, serviceType) key. Real, disclosed: the sibling GET-only resource at
    // /subscription-data/{ueId}/service-specific-authorization-data/{serviceType} is genuinely
    // blocked, not attempted -- real required complex-object query parameters this project has
    // no parsing precedent for, same class of gap already disclosed for nidd-authorization-data.
    // ---

    const std::string service_specific_auth_path_pattern =
        std::string(kApiRoot) +
        "/subscription-data/{ueId}/context-data/service-specific-authorizations/{serviceType}";

    server.add_route(
        "PUT",
        service_specific_auth_path_pattern,
        [&verifier,
         &service_specific_authorization_info,
         &service_specific_auth_write_counter,
         service_specific_auth_path_pattern](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::ServiceSpecificAuthorizationInfo>(
                req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto service_type = req.path_params.at("serviceType");
            json j = *body;
            const bool is_new = service_specific_authorization_info.put(ue_id, service_type, j);
            service_specific_auth_write_counter->Add(1);

            if (!is_new) {
                sbi_core::http2::Response resp;
                resp.status = 204;
                return resp;
            }
            sbi_core::http2::Response resp;
            resp.status = 201;
            resp.headers.emplace("content-type", "application/json");
            resp.headers.emplace("location", service_specific_auth_path_pattern);
            resp.body = j.dump();
            return resp;
        });

    server.add_route(
        "GET",
        service_specific_auth_path_pattern,
        [&verifier, &service_specific_authorization_info](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto service_type = req.path_params.at("serviceType");
            auto data = service_specific_authorization_info.get(ue_id, service_type);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Service Specific Authorization Info for ueId/serviceType " + ue_id + "/" +
                        service_type);
            }
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        service_specific_auth_path_pattern,
        [&verifier, &service_specific_authorization_info, &service_specific_auth_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto service_type = req.path_params.at("serviceType");
            std::optional<json> patched;
            try {
                patched =
                    service_specific_authorization_info.apply_patch(ue_id, service_type, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Service Specific Authorization Info for ueId/serviceType " + ue_id + "/" +
                        service_type);
            }
            service_specific_auth_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "DELETE",
        service_specific_auth_path_pattern,
        [&verifier, &service_specific_authorization_info, &service_specific_auth_delete_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            const auto service_type = req.path_params.at("serviceType");
            if (!service_specific_authorization_info.remove(ue_id, service_type)) {
                return sbi_core::http2::problem_response(
                    404,
                    "Not Found",
                    "No Service Specific Authorization Info for ueId/serviceType " + ue_id + "/" +
                        service_type);
            }
            service_specific_auth_delete_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: Group Identifiers mapping resource (ADR-0140, gap-closure task
    // #106) -- real GET-only per TS29505_Subscription_Data.yaml (real spec operationId
    // `GetGroupIdentifiers`), seeded at startup. Genuinely NOT per-UE, no path parameters --
    // real, optional query parameters `ext-group-id`/`int-group-id` select the lookup key. Real,
    // disclosed: at least one of the two is required by this implementation (400 otherwise,
    // since the spec defines no "list all groups" behavior this project has any precedent for
    // returning); the real `ue-id-ind` query parameter is not honored -- `ueIdList` is always
    // included in the response regardless. ---

    const std::string group_identifiers_path_pattern =
        std::string(kApiRoot) + "/subscription-data/group-data/group-identifiers";

    server.add_route(
        "GET",
        group_identifiers_path_pattern,
        [&verifier, &group_identifiers, &group_identifiers_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ext_group_id_it = req.query_params.find("ext-group-id");
            const auto int_group_id_it = req.query_params.find("int-group-id");
            std::optional<json> data;
            std::string lookup_desc;
            if (ext_group_id_it != req.query_params.end()) {
                data = group_identifiers.get_by_ext_group_id(ext_group_id_it->second);
                lookup_desc = "ext-group-id " + ext_group_id_it->second;
            } else if (int_group_id_it != req.query_params.end()) {
                data = group_identifiers.get_by_int_group_id(int_group_id_it->second);
                lookup_desc = "int-group-id " + int_group_id_it->second;
            } else {
                return sbi_core::http2::problem_response(
                    400, "Bad Request", "At least one of ext-group-id or int-group-id is required");
            }
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No Group Identifiers for " + lookup_desc);
            }
            group_identifiers_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: NSSAI update ack (Document) resource (ADR-0141, gap-closure task
    // #106) -- real PUT+GET per TS29505_Subscription_Data.yaml, no PATCH/DELETE operation exists
    // at all. Real, disclosed: unlike this project's other PUT resources, the spec documents only
    // a single `204` response for this PUT (no `201`) -- no create-vs-update distinction. Keyed
    // by ueId. ---

    const std::string nssai_ack_data_path_pattern =
        std::string(kApiRoot) +
        "/subscription-data/{ueId}/ue-update-confirmation-data/subscribed-snssais";

    server.add_route(
        "PUT",
        nssai_ack_data_path_pattern,
        [&verifier, &nssai_ack_data, &nssai_ack_data_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::NssaiAckData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            nssai_ack_data.put(ue_id, j);
            nssai_ack_data_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        nssai_ack_data_path_pattern,
        [&verifier, &nssai_ack_data, &nssai_ack_data_get_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = nssai_ack_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No NSSAI Ack Data for ueId " + ue_id);
            }
            nssai_ack_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: CAG update ack (Document) resource (ADR-0142, gap-closure task
    // #106) -- real PUT+GET per TS29505_Subscription_Data.yaml, no PATCH/DELETE operation exists
    // at all, identical shape to subscribed-snssais's own resource above. Real, disclosed: the
    // spec documents only a single `204` response for this PUT (no `201`) -- no create-vs-update
    // distinction. Keyed by ueId. ---

    const std::string cag_ack_data_path_pattern =
        std::string(kApiRoot) +
        "/subscription-data/{ueId}/ue-update-confirmation-data/subscribed-cag";

    server.add_route(
        "PUT",
        cag_ack_data_path_pattern,
        [&verifier, &cag_ack_data, &cag_ack_data_write_counter](
            const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::CagAckData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            cag_ack_data.put(ue_id, j);
            cag_ack_data_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        cag_ack_data_path_pattern,
        [&verifier, &cag_ack_data, &cag_ack_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = cag_ack_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No CAG Ack Data for ueId " + ue_id);
            }
            cag_ack_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    // --- Nudr_DataRepository: Authentication SoR (Document) resource (ADR-0143, gap-closure task
    // #106) -- real PUT+GET+PATCH per TS29505_Subscription_Data.yaml. Real, disclosed: same as
    // subscribed-snssais/subscribed-cag above, the spec documents only a single `204` response for
    // this PUT (no `201`) -- no create-vs-update distinction. Genuinely richer than either ack
    // resource: a real RFC 6902 application/json-patch+json PATCH also exists (apply_patch is NOT
    // upsert-capable -- requires a prior PUT, same precedent as nidd-authorizations). Keyed by
    // ueId. ---

    const std::string sor_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/ue-update-confirmation-data/sor-data";

    server.add_route(
        "PUT",
        sor_data_path_pattern,
        [&verifier, &sor_data, &sor_data_write_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body = sbi_core::http2::parse_json_body<sbi_gen::SorData>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            sor_data.put(ue_id, j);
            sor_data_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        sor_data_path_pattern,
        [&verifier, &sor_data, &sor_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = sor_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SoR Data for ueId " + ue_id);
            }
            sor_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    server.add_route(
        "PATCH",
        sor_data_path_pattern,
        [&verifier, &sor_data, &sor_data_write_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            json patch_ops;
            try {
                patch_ops = json::parse(req.body);
            } catch (const json::parse_error& e) {
                return sbi_core::http2::problem_response(400, "Malformed JSON", e.what());
            }
            const auto ue_id = req.path_params.at("ueId");
            std::optional<json> patched;
            try {
                patched = sor_data.apply_patch(ue_id, patch_ops);
            } catch (const json::exception& e) {
                return sbi_core::http2::problem_response(400, "Invalid JSON Patch", e.what());
            }
            if (!patched.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No SoR Data for ueId " + ue_id);
            }
            sor_data_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    // --- Nudr_DataRepository: Authentication UPU (Document) resource (ADR-0143, gap-closure task
    // #106) -- real PUT+GET only per TS29505_Subscription_Data.yaml, no PATCH/DELETE operation
    // exists at all -- genuinely narrower than sor-data's own resource above despite sharing the
    // same UeUpdateStatus-based schema shape. Real, disclosed: same 204-only PUT, no
    // create-vs-update distinction. Keyed by ueId. ---

    const std::string upu_data_path_pattern =
        std::string(kApiRoot) + "/subscription-data/{ueId}/ue-update-confirmation-data/upu-data";

    server.add_route(
        "PUT",
        upu_data_path_pattern,
        [&verifier, &upu_data, &upu_data_write_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            sbi_core::http2::Response err;
            auto body =
                sbi_core::http2::parse_json_body<sbi_gen::UpuData_Subscription_Data>(req, err);
            if (!body.has_value()) {
                return err;
            }
            const auto ue_id = req.path_params.at("ueId");
            json j = *body;
            upu_data.put(ue_id, j);
            upu_data_write_counter->Add(1);
            sbi_core::http2::Response resp;
            resp.status = 204;
            return resp;
        });

    server.add_route(
        "GET",
        upu_data_path_pattern,
        [&verifier, &upu_data, &upu_data_get_counter](const sbi_core::http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth.has_value() && !auth->valid) {
                return sbi_core::http2::problem_response(401, "Unauthorized", auth->error);
            }
            const auto ue_id = req.path_params.at("ueId");
            auto data = upu_data.get(ue_id);
            if (!data.has_value()) {
                return sbi_core::http2::problem_response(
                    404, "Not Found", "No UPU Data for ueId " + ue_id);
            }
            upu_data_get_counter->Add(1);
            return sbi_core::http2::Response::json(200, data->dump());
        });

    std::thread(run_nrf_lifecycle, udr_instance_id, nrf_base_url).detach();

    server.start();
    spdlog::info("udr: listening on https://0.0.0.0:{} (TLS 1.3 + mTLS)", port);
    spdlog::info("udr: Prometheus metrics at http://{}/metrics", metrics_bind_address);
    ioc.run();
    return 0;
}
