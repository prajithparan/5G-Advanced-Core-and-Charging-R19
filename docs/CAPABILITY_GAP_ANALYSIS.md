# Capability gap analysis vs free5GC and open5GS

**Status: first pass COMPLETE for all 9 built NFs, code-free review gate** (same pattern as
`docs/CHARGING_MAPPING.md`) -- user-directed, full sweep of every built NF against both real
reference implementations, procedure/behavior-level (not literal line-by-line -- free5GC is Go,
open5GS is C, this project is C++, so "line by line" is interpreted as exhaustive
capability/behavior comparison, the real cross-language equivalent). Governed by ADR-0075's
capability-completeness mandate: every real finding here is tracked to eventual implementation,
none silently dropped. Nothing has been implemented yet -- this file is the evidence base an
implementation plan gets built from. `nssf`/`nef`/`scp`/`bsf` (Tier-1 NFs that don't exist in this
project at all yet) are a separate, larger "whole NF missing" gap, out of this sweep's scope, not
blended into the per-NF findings below. See the Summary section at the end for the
cross-NF priority picture.

## Method

For each of this project's built NFs (`nrf, amf, ausf, chf, pcf, smf, udm, udr, upf` --
`nssf/nef/scp/bsf` don't exist yet in this project at all, a separate "whole NF missing" gap
tracked at the end, not blended into per-NF findings below), real source was pulled directly:
- **free5GC**: cloned live from `github.com/free5gc/<nf>.git` (the `free5gc-main.zip` umbrella
  repo only contains empty git-submodule pointers, no real source -- confirmed by inspection,
  each real NF repo cloned individually as needed).
- **open5GS**: extracted from the user-supplied `open5gs-main.zip`, a real, full monorepo
  (`src/<nf>/`).

Every finding below cites the real file/line evidence it came from. Nothing is asserted without
having actually read the cited source.

## NRF

### Real endpoint/service-surface comparison

| Service | free5GC | open5GS | This project |
|---|---|---|---|
| `Nnrf_NFManagement` (Register/Get/Update/Deregister/GetInstances) | Real (`internal/sbi/api_nfmanagement.go`) | Real (`src/nrf/nnrf-handler.c`) | Real, matches route-for-route (`nfs/nrf/src/main.cpp`) |
| `Nnrf_NFDiscovery` (SearchNFInstances) | Real | Real | Real |
| `Nnrf_AccessToken` (OAuth2 token) | Real | Real | Real |
| `Nnrf_NFManagement` Subscriptions (Create/Update/Remove) | Real | Real | Real |
| `Nnrf_Bootstrapping` | **Stub only** -- free5GC's own `HTTPBootstrappingInfoRequest` returns HTTP 501 Not Implemented (`internal/sbi/api_bootstrapping.go`) | Not found | Not implemented | Not a real gap -- free5GC itself doesn't implement this either. |

### Real gaps found (this project is missing real behavior both/one reference has)

1. **`NFProfile` semantic validation is missing.** free5GC has ~290 lines of real validation
   (`internal/sbi/processor/nf_profile_validation.go`): `nfInstanceId` must be a real UUID v4,
   `heartBeatTimer` bounds-checked, `nfType`/`nfStatus`/`nfServiceStatus`/`uriScheme` checked
   against real enum sets, `ipv4Addresses`/`ipv6Addresses` format-validated, `ipEndPoint.transport`
   must be TCP, port range-checked. This project's NRF (`nfs/nrf/src/main.cpp:253`) only checks
   that `nfInstanceId`/`nfType`/`nfStatus` keys are **present** in the JSON body -- no format or
   enum validation at all. A malformed `nfType` or an out-of-range `heartBeatTimer` is silently
   accepted today. **Real gap, not disclosed before this analysis.**

2. **No active heartbeat-expiry timer.** open5GS runs a real per-NF-instance timer
   (`src/nrf/nf-sm.c:187-216`, `t_no_heartbeat`, started on entry to the `registered` state for
   `heartbeat_interval + no_heartbeat_margin`) that proactively deregisters an NF instance if it
   stops sending heartbeats, and fires a real `NF_REGISTERED`/de-registration notification to
   subscribers on each transition. This project's NRF accepts `PATCH` heartbeats
   (`nfs/nrf/src/main.cpp:313`) but has no background expiry sweep anywhere in `nfs/nrf/src/` --
   a crashed NF that never sends `DELETE` stays registered forever. free5GC does not appear to
   have this either (only stores the `HeartBeatTimer` value,
   `internal/context/management_data.go:62`) -- **this is specifically an open5GS capability, not
   a universal one**, so the honest framing is "we lack what open5GS has," not "we lack what both
   have." **Real gap.**

3. **Subscription `subscrCond` filtering is ignored.** open5GS validates and applies
   `subscrCond` (an `nfType`-or-`serviceName` XOR filter, `src/nrf/nnrf-handler.c:481-603`, citing
   its own real upstream bug fix, issue #2630: "must be `oneOf`"). This project's own code already
   self-discloses this exact gap (`nfs/nrf/src/main.cpp:18`: "Subscription notification fan-out
   ignores `subscrCond` (delivers to every active subscriber...)"). **Already known, now
   independently corroborated, not a new finding** -- listed here for completeness of the record.

### Not yet checked for NRF (deferred to a follow-up pass if worth it)

NRF-to-NRF federation/forwarding (multi-NRF deployments), `searchOptions` completeness in
discovery (free5GC/open5GS support a large set of optional discovery query filters -- this
project's own discovery filter completeness was not compared field-by-field this pass), rate
limiting / TPS protection (already known-absent everywhere per ADR-0049, not re-derived here).

## AMF

**Real scale context, checked before diving in**: this project's AMF is 3,116 lines
(`nfs/amf/src/*.cpp/.hpp`); free5GC's is 43,058 lines (Go, `internal/{gmm,ngap,sbi}/`); open5GS's
is 32,982 lines (C, `src/amf/`). Roughly a 10-14x difference -- and the findings below show why:
this project's AMF covers a real but narrow slice of the full NGAP/GMM procedure set, not a
roughly-equivalent implementation with a few missing edges.

### Namf_* SBI services

| Service | free5GC | This project |
|---|---|---|
| `Namf_Communication` (UEContext CRUD, N1N2MessageTransfer(+subscribe), non-UE-N2-messages(+subscribe)) | Real (`internal/sbi/processor/{ue_context,n1n2message}.go`) | Real, route-for-route match (`nfs/amf/src/main.cpp`, 16 routes) |
| `Namf_EventExposure` (Create/Delete/Modify subscription -- mobility, reachability, comm-failure, location-report events, TS 29.518) | Real (`processor/event_exposure.go`, 3 real handlers) | **Missing entirely** -- no route, no handler, anywhere in `nfs/amf/src/` |
| `Namf_Location` (ProvideLocationInfo -- used by LMF/GMLC for positioning) | Real (`processor/location_info.go`) | **Missing entirely** |
| `Namf_MT` (ProvideDomainSelectionInfo -- CS/PS domain selection for MT SMS/call) | Real (`processor/mt.go`) | **Missing entirely** |
| `Namf_OAM` (RegisteredUEContext query) | Real (`processor/oam.go`) | **Missing entirely** |

Grep-confirmed: no occurrence of `EventExposure`, `namf-loc`, `namf-mt`, `namf-oam`, or their real
operation names anywhere in this project's AMF source. **3 of 4 real Namf_* services are entirely
unimplemented**, not partially covered.

### Real NAS (GMM) procedure coverage

free5GC's `internal/gmm/handler.go` implements 17 real GMM procedure handlers (grep-confirmed,
`func Handle*`): `ULNASTransport`, `RegistrationRequest` (dispatching to real
`InitialRegistration`/`MobilityAndPeriodicRegistrationUpdating` sub-procedures),
`IdentityResponse`, `NotificationResponse`, `ConfigurationUpdateComplete`, **`ServiceRequest`**,
`AuthenticationResponse`, `AuthenticationError`, `AuthenticationFailure`,
`RegistrationComplete`, `SecurityModeComplete`, `SecurityModeReject`, `DeregistrationRequest`
(UE-initiated), `DeregistrationAccept` (network-initiated ack), `Status5GMM`. Backed by a real,
explicit GMM state machine (`internal/gmm/sm.go`: `DeRegistered`, `Registered`,
`DeregisteredInitiated`, plus `Authentication`/`SecurityMode` sub-states, `internal/gmm/init.go`).

This project's `nas_codec.hpp` decodes/handles: `RegistrationRequest`, `AuthenticationOutcome`,
`SecurityModeComplete`, `RegistrationComplete`, `ULNASTransport`, and (closed, docs/DECISIONS.md
ADR-0076) **`ServiceRequest`/`ServiceAccept`/`ServiceReject`**, backed by a real, persistent
(Redis-backed) NAS security context (`UeSecurityContextStore`) keyed by 5G-GUTI/5G-TMSI, so it
survives across NG associations rather than living only in per-association memory as every other
procedure here still does. Grep-confirmed still absent, entirely: `IdentityResponse`,
`NotificationResponse`, `ConfigurationUpdateComplete`, `AuthenticationError` (distinct from
`AuthenticationFailure` -- resync vs. abort), `SecurityModeReject`,
`DeregistrationRequest`/`DeregistrationAccept`, `Status5GMM`. No explicit GMM state machine
exists in this project -- registration/mobility state is tracked, but not as a named, real RM/CM
state machine matching TS 24.501's own model.

**`ServiceRequest`'s absence was the single highest-impact finding in this whole analysis --
closed as of ADR-0076.** It is the dominant real NAS procedure for CM-IDLE -> CM-CONNECTED
transitions (a UE resuming from idle after paging, or UE-triggered uplink data resume) -- in real
network traffic it fires far more often than full Registration. Real-interop-verified for the
RegistrationAccept/GUTI-assignment/RegistrationComplete side (a full UERANSIM run through PDU
Session Establishment); the decode path itself (peek-TMSI-then-verify-MAC) is verified by 6 new
hand-built-genuine-message unit tests, not by interop, since no reachable trigger for a real
`ServiceRequest` from UERANSIM was found this pass (see the NGAP `UEContextRelease` finding
below). What `ServiceRequest` does NOT yet do: drive real N2 PDU Session Resource Setup for any
PDU session its own `uplinkDataStatus` reports pending -- that requires SMF's own `UpdateSMContext`
N2SmInfo dispatch, still a gap (see next section).

### Real NGAP (N2) procedure coverage

free5GC's `internal/ngap/handler.go` implements ~39 real NGAP procedure handlers (grep-confirmed,
`func handle*Main`), including the full real N2 handover suite (`HandoverRequired`,
`HandoverRequestAcknowledge`, `HandoverFailure`, `HandoverNotify`, `HandoverCancel`,
`PathSwitchRequest`), `NGReset`/`NGResetAcknowledge`, `UEContextRelease{Request,Complete}`,
`InitialContextSetup{Response,Failure}`, `UEContextModification{Response,Failure}`,
`PDUSessionResource{Setup,Modify,Release}Response`, `PDUSessionResourceNotify`,
`RRCInactiveTransitionReport`, `RANConfigurationUpdate`, `ErrorIndication`, `CellTrafficTrace`,
NRPPa transport (positioning) relay, and more.

This project's `ngap_task.cpp` handles 4 real procedures (grep-confirmed, `void handle_*`):
`NGSetupRequest`, `InitialUEMessage`, `UplinkNASTransport` (+ 2 internal sub-variants for
SMC-complete and PDU-session-establishment relay). Grep-confirmed absent from `ngap_codec.hpp`
entirely -- not even encodable/decodable, not just unhandled: `Handover*`, `PathSwitchRequest`,
`UEContextRelease*`, `NGReset`, `InitialContextSetup*`.

**This project's AMF has zero N2 handover support** -- no `HandoverRequired`, no
`PathSwitchRequest`, nothing. A UE cannot be handed over between gNBs in this implementation
today; only initial attach + PDU session establishment on a single gNB is real. This is a
structural gap, not a missing edge case. **Not addressed by ADR-0076** (that pass closed
`ServiceRequest`/GMM specifically, not this NGAP-side gap) -- still fully open, tracked as the
remaining scope of task #100/#101.

**Real, additional NGAP gap found this pass, via live interop, not grep alone**: attempting to
trigger a real `ServiceRequest` naturally (gNB-initiated idle-mode re-entry via UERANSIM's
`nr-cli <gnb> --exec 'ue-release <id>'`) sends a real NGAP `UEContextReleaseRequest` this AMF
cannot decode at all -- `amf-ngap: failed to decode NGAP PDU (25 bytes), ignoring: 002a4015...`.
`UEContextRelease{Request,Complete}` was already named above as part of free5GC's ~39-procedure
NGAP coverage this project lacks, but this pass is the first time its absence was confirmed to
concretely block testing a DIFFERENT, already-built procedure (`ServiceRequest`) via the most
natural real-UE trigger path, not just an abstract line-count gap.

### Real finding, not yet checked further

Whether this project's simpler mobility tracking (no explicit RM/CM state machine) produces
behaviorally-correct results for the procedures it DOES implement was not re-verified in this
pass -- this section is about coverage (what's present vs. absent), not re-auditing correctness
of what's already built and already has its own tests.

---

## AUSF

**Scale context**: this project's AUSF is 676 lines vs free5GC's 3,317 (Go) vs open5GS's 2,173
(C) -- a smaller ratio than AMF's, and the findings match: the core service is fully covered.

### `Nausf_UEAuthentication` -- real, full parity

free5GC's real routes (`internal/sbi/api_ueauthentication.go`): `POST /ue-authentications`
(initiate), `PUT .../5g-aka-confirmation`, `DELETE .../5g-aka-confirmation`, `POST
.../eap-session`, `DELETE .../eap-session`. This project's AUSF (`nfs/ausf/src/main.cpp`)
implements all five, route-for-route, plus one additional real route,
`POST /ue-authentications/deregister`. **No gap on the core service** -- both 5G-AKA and
EAP-AKA' paths, confirmed already live-tested in this project's own integration tests
(`AusfIntegration.FiveGAkaSuccessfulAuthentication...`,
`AusfIntegration.EapAkaPrimeSuccessfulAuthentication...`).

### Real gaps found -- both free5GC-only, not shared with open5GS

1. **`Nausf_SoRProtection`** (`POST /:supi/ue-sor/generate-sor-data`, real TS 33.501 Annex E --
   protects Steering-of-Roaming (SoR) list/CMCI data against tampering by a compromised VPLMN).
   Real in free5GC (`internal/sbi/api_sorprotection.go`). **Grep-confirmed absent from open5GS's
   AUSF too** (`src/ausf/` has no SoR-related handler at all) -- this is specifically a
   free5GC-only capability, not something both references implement. Entirely missing from this
   project.
2. **ProSe (Proximity Services / D2D) authentication** (`POST /prose-authentications`, `DELETE
   .../prose-auth`, a real R17+ extension). Real in free5GC. **Grep-confirmed absent from
   open5GS's AUSF** as well -- same free5GC-only status as SoR protection. Entirely missing from
   this project.

Per ADR-0075's capability-completeness mandate, both are real gaps to close eventually regardless
of being free5GC-only -- "superior to both, never behind either" doesn't stop at whichever
reference happens to have less.

---

## SMF

**Scale context**: this project's SMF is 2,320 lines vs free5GC's 23,422 (Go) vs open5GS's 38,138
(C) -- roughly 10-16x, the same order as AMF, and for the same underlying reason: the Create path
is real, the Update path is largely a stub.

### `Nsmf_PDUSession` -- Create and Release real, Update is the gap

free5GC's real top-level procedures (`internal/sbi/processor/pdu_session.go`):
`HandlePDUSessionSMContextCreate` (334 lines), `HandlePDUSessionSMContextUpdate` (852 lines --
by far the largest single procedure in the file), `HandlePDUSessionSMContextRelease`,
`HandlePDUSessionSMContextLocalRelease`. This project's SMF (`nfs/smf/src/main.cpp`) implements
all four route-for-route, and **its own header comments already self-disclose** the real gap:
"`UpdateSMContext` still does NOT call PCF's `UpdateSMPolicy`... acknowledges (204) without
fabricating `SmContextUpdatedData` content" (`main.cpp:25,46`). Confirmed by reading free5GC's
852-line `HandlePDUSessionSMContextUpdate`: it's a real dispatcher over a large, genuine set of N2
SM info sub-procedures (grep-confirmed, `models.N2SmInfoType_*`/`HoState_*` cases):
`PDU_RES_SETUP_{REQ,RSP,FAIL}`, `PDU_RES_MOD_{REQ,RSP,IND,CFM}`, `PDU_RES_REL_{CMD,RSP}`,
`PATH_SWITCH_{REQ,REQ_ACK,SETUP_FAIL}`, `HANDOVER_REQUIRED`. This project's `UpdateSMContext` is,
today, effectively a no-op ack -- **none of these sub-procedures exist**.

This directly couples to AMF's own finding above (zero N2 handover NGAP support): even if SMF
computed real updated N2 SM info for a handover, AMF has no NGAP handler to carry
`PATH_SWITCH_REQ`/`HANDOVER_REQUIRED` in the first place. The two gaps are the same real
capability (N2 handover) viewed from each NF's own side, not two independent gaps.

### Real, further, not-yet-fully-characterized findings (flagged, not yet fully drilled down)

- **ULCL (Uplink Classifier) / multi-homed PDU sessions** (R16 local-breakout/edge-computing
  feature): free5GC has a dedicated `internal/sbi/processor/ulcl_procedure.go`. Grep-confirmed no
  equivalent concept anywhere in this project's SMF. Real gap, not yet measured for size/depth.
  Whether open5GS has an equivalent was not yet checked in this pass.
- **PFCP/N4 procedure breadth**: free5GC's `internal/pfcp/handler/handler.go` +
  `internal/context/pfcp_{rules,reports,session_context}.go` suggest a real, fuller N4 session
  report/rule surface than this project's own UPF-side implementation (ADR from gap-closure Tier
  1d covers Create/Modify/Delete + QER/BAR, but a direct procedure-by-procedure PFCP diff against
  both references was not done in this pass -- flagged as owed, not yet delivered).
- Charging trigger integration (`internal/sbi/processor/charging_trigger.go`) -- this project's
  own N40/CHF wiring is real and already tested (unlike free5GC's, this project's charging system
  is comparatively the more built-out side per the CHF brief), so this is flagged for a
  behavior-level diff, not assumed to be a gap in either direction.

---

## PCF

**Scale context**: this project's PCF is 961 lines vs free5GC's 9,557 (Go) vs open5GS's 6,472
(C).

### Real service surface comparison

free5GC's real PCF processors (`internal/sbi/processor/`): `ampolicy.go`
(`Npcf_AMPolicyControl`), `smpolicy.go` (`Npcf_SMPolicyControl`), `bdtpolicy.go`
(`Npcf_BDTPolicyControl`), `policyauthorization.go` (`Npcf_PolicyAuthorization`), plus a real
`api_uepolicy.go` (`Npcf_UEPolicyControl`). This project's PCF (`nfs/pcf/src/main.cpp`) implements
`Npcf_AMPolicyControl` and `Npcf_SMPolicyControl` in full (including the real N28 spending-limit
loop from ADR-0072/-0073) -- **this project's own code already self-discloses the rest as
deferred** (`main.cpp:17-18`): "`Npcf_PolicyAuthorization` (AF/Rx-style), `Npcf_UEPolicyControl`
(URSP), `Npcf_EventExposure`, `Npcf_BDTPolicyControl`, `Npcf_PDTQPolicyControl`,
`Npcf_AMPolicyAuthorization`..." -- these are not new findings, but this pass adds real,
verified priority evidence for them:

| Service | free5GC | open5GS | Priority signal |
|---|---|---|---|
| `Npcf_PolicyAuthorization` | Real (`policyauthorization.go`) | Real -- confirmed via `OGS_SBI_RESOURCE_NAME_APP_SESSIONS` in `npcf-handler.c` (1,686 lines), the real TS 29.514 resource name | **Both references implement this.** This is the AF-facing interface IMS/VoNR call setup uses to request media/QoS policy -- a real, high-impact gap, not a minor one. |
| `Npcf_UEPolicyControl` (URSP) | Real (`api_uepolicy.go`) | Grep-confirmed absent (no URSP/UE-policy resource name found in open5GS's PCF source) | free5GC-only. Still owed per ADR-0075, lower relative priority than PolicyAuthorization. |
| `Npcf_BDTPolicyControl` | Real (`bdtpolicy.go`) | Grep-confirmed absent | free5GC-only. |
| `Npcf_EventExposure` | Not found in free5GC's own processor directory either | Not checked in depth | Neither reference clearly implements this -- lowest priority of the self-disclosed list, pending a closer look. |

### Not yet checked for PCF

Real PCC rule richness (QoS flow granularity, charging-rule interaction depth) within the SM
policy path that IS implemented -- this pass compared service-surface presence, not full
behavioral depth of the already-built `Npcf_SMPolicyControl`/`Npcf_AMPolicyControl` paths.

---

## UDM

**Scale context**: this project's UDM is 1,520 lines vs free5GC's 8,409 (Go) vs open5GS's 4,409
(C).

### Real service surface comparison

free5GC's real UDM processors: `subscriber_data_management.go` (`Nudm_SDM`),
`ue_context_management.go` (`Nudm_UECM`), `generate_auth_data.go` (`Nudm_UEAU`),
`event_exposure.go` (`Nudm_EE`), `parameter_provision.go` (`Nudm_PP`). This project's UDM
(`nfs/udm/src/main.cpp`) implements the three core services in real, solid depth: `Nudm_UECM`
(AMF 3GPP-access registration + deregistration, SMF registration full CRUD), `Nudm_SDM`
(am-data/smf-select-data/sm-data + subscription CRUD), `Nudm_UEAU` (generate-auth-data,
auth-events) -- route-for-route matching free5GC's own core surface, already covered by this
project's own live-verified integration tests.

**Real gap, entirely missing, both references**: `Nudm_EE` (Event Exposure -- `CreateEe
Subscription`/`UpdateEeSubscription`/`DeleteEeSubscription`, real subscription-based UDM event
notifications, e.g. reachability-for-data or location-report events sourced from subscription
data changes) and `Nudm_PP` (Parameter Provisioning -- `UpdateProcedure`, real
operator/OAM-driven subscriber parameter updates, e.g. MSISDN or external-ID provisioning).
Grep-confirmed absent from this project's UDM entirely (no `event-exposure`/`parameter-provision`
route anywhere). **Grep-confirmed absent from open5GS's UDM too** (`src/udm/` has no EE/PP
resource-name handling) -- both are free5GC-only capabilities, still real gaps per ADR-0075, but
lower relative priority than a both-references gap.

---

## UDR

**Scale context**: this project's UDR is 963 lines vs free5GC's 9,547 (Go) vs open5GS's 2,400 (C).
UDR is architecturally a wide-but-shallow data repository (TS 29.504) -- the real gap here is
resource-type COUNT, not per-resource complexity.

### Real resource-type coverage

free5GC's real UDR (`internal/sbi/processor/`) has **42 distinct resource-document/collection
processor files**, the real TS 29.504 `Nudr_DR` data model in full: Application Data (influence
data + subscriptions, PFD data), Exposure Data (event exposure group subscriptions), Policy Data
(AM policy data, SM policy data), Subscription Data (AM data, SMF-selection data, SM data, SMS
management/subscription data, PDU-session-management data, session-management-subscription data,
AMF 3GPP/non-3GPP access registration, SMF registration(s), SMSF 3GPP/non-3GPP registration,
authentication data/status/SoR, trace data, query-identity-by-supi-or-gpsi, query-ODB-data,
operator-specific-data-container, shared-data retrieval), and PP (Parameter Provisioning) data.

This project's UDR (`nfs/udr/src/main.cpp`) implements 6 real resource endpoints: AMF 3GPP-access
context-data, SMF-registrations context-data (full CRUD, `{pduSessionId}`-scoped), provisioned-data
(`am-data`, `smf-selection-subscription-data`, `sm-data`), and the real nested `policy-data/ues/
{ueId}/sm-data` resource from ADR-0072 (`SmPolicyData` with full `SmPolicySnssaiData ->
SmPolicyDnnData` nesting and RFC 7396 merge-patch semantics -- genuinely more complete for THIS
one resource than a bare CRUD document, per that ADR's own real, deliberate design). What's
covered is solid; the real gap is breadth -- roughly 6 of free5GC's ~42+ real resource types.

**Highest-priority missing resources** (the ones with real, direct behavioral impact elsewhere in
this project, not just data-model completeness): Authentication Data / Authentication Status /
Authentication SoR documents (UDR-side persistence for AUSF's own authentication vectors and
result status -- this project's own AUSF currently holds auth context in its own store rather
than UDR, a real architectural divergence worth its own look, not just a missing endpoint), AM
Policy Data (UDR-side backing for PCF's `Npcf_AMPolicyControl`, which this project's PCF already
implements against a different store), Influence Data (AF traffic-steering, needed once NEF
exists).

---

## UPF

**Scale context**: this project's UPF is 1,743 lines vs free5GC's go-upf 8,606 (Go) vs open5GS's
4,423 (C).

### Real PFCP (N4) message coverage

free5GC's `internal/pfcp/pfcp.go` dispatches the full real PFCP message set (grep-confirmed,
request+response pairs): `Heartbeat`, `PFDManagement`, `AssociationSetup`, `AssociationUpdate`,
`AssociationRelease`, `NodeReport`, `SessionSetDeletion`, `SessionEstablishment`,
`SessionModification`, `SessionDeletion`, `SessionReport`. This project's UPF handles
`Heartbeat`, `AssociationSetup`, `SessionEstablishment`/`Modification`/`Deletion`, and sends
`SessionReport` (UPF-initiated usage reports) -- the core session lifecycle is real and already
covered by gap-closure Tier 1d's own QER/BAR work. **Grep-confirmed missing**: `PFDManagement`
(real, used to deploy Application Detection Filters from SMF to UPF for App-ID traffic
classification), `AssociationUpdate` (updating UPF capabilities without a full re-association),
`AssociationRelease` (graceful N4 teardown), `NodeReport` (UPF-initiated node-level reporting,
e.g. GTP-U path failure), `SessionSetDeletion` (bulk session cleanup tied to a specific CP
instance -- used on CP function restart/failure recovery).

### Datapath: this project is already ahead here, worth recording per ADR-0075's "superior, not
just parity" framing, not just gaps

free5GC's UPF forwards via **`gtp5g`**, a real, separate Linux kernel module the free5GC project
maintains and drives via netlink (`internal/forwarder/gtp5g.go`/`gtp5glink.go`) -- a real,
reasonably fast kernel-module datapath, but a custom out-of-tree kernel module dependency.
open5GS's UPF (`src/upf/gtp-path.c`) uses plain userspace socket-based GTP-U forwarding -- no
kernel module, no DPDK, no XDP -- simpler to deploy but the least performant of the three. This
project's own UPF uses **eBPF/XDP** (ADR-0043, "fully live-verified end to end") -- a real,
in-kernel, no-custom-module, generally higher-throughput datapath approach than either reference's
own choice. **Real, disclosed, not yet benchmarked**: ADR-0049 already recorded "zero
benchmarking of any kind... performed against free5GC or anything else" -- this is architectural
superiority on paper, not yet a proven-faster claim, and P4.12/ADR-0049's own benchmarking-harness
gap is what would actually prove it.

---

## CHF

**Correction to an earlier, wrong statement made in this same session**: when first asked about
CHF's status, this assistant said free5GC has no charging function at all. That was wrong and has
now been checked directly, not just retracted on faith: free5GC has a real `chf` repo
(`github.com/free5gc/chf`, 10,931 lines Go). open5GS, by contrast, genuinely has **no** `chf`
directory in its real monorepo (`find` over the full extracted source confirms it) -- the
original claim was right for open5GS, wrong for free5GC.

### `Nchf_*` SBI service surface -- this project is ahead here

free5GC's CHF processor surface (`internal/sbi/processor/`) is narrow: `converged_charging.go`
(`HandleChargingdataInitial/Update/Release` -- `Nchf_ConvergedCharging` only) and `cdr.go`. No
`Nchf_SpendingLimitControl`, no `Nchf_OfflineOnlyCharging` found anywhere in free5GC's real CHF
source. This project's CHF implements all three real 5G-native services (`Nchf_ConvergedCharging`,
`Nchf_SpendingLimitControl` with the real, live-verified full N28 loop from ADR-0072/-0073,
`Nchf_OfflineOnlyCharging`), plus the legacy Diameter Gy/Rf/Sy and CAP/CAMEL protocol-translation
layer (P4.5), plus AI-native predictive quota sizing (P4.8, ADR-0074) -- none of which free5GC's
CHF has any equivalent of. **On 5G-native service breadth and legacy-protocol translation, this
project is already ahead of free5GC**, consistent with ADR-0075's "superior, not just parity"
mandate already being true in this specific area, not just aspirational.

### Real gap found: TS 32.298 CDR encoding

free5GC's CHF has a real, substantial `cdr/` module (4,746 lines) implementing genuine TS 32.298
ASN.1 BER CDR encoding -- confirmed by real, specific 3GPP IE-matching type names
(`cdr/cdrType/`: `CHFRecord` -- the actual real TS 32.298 top-level 5G CDR record type name --
`AllocationRetentionPriority`, `CauseForRecClosing`, `AMFID`, `ChargingID`,
`AgeOfLocationInformation`, dozens more), plus its own hand-written BER marshal/unmarshal
(`cdr/asn/ber_{marshal,unmarshal}.go` -- not asn1c-generated from a vendored `.asn` file, but a
real, faithful transcription of the real TS 32.298 field layout into Go structs).

This project's own `nfs/chf/schema.clickhouse.sql` already self-discloses the matching gap: "this
is NOT a conformant TS 32.298 CDR... TS 32.298 is not vendored in this repo... proceed with TS
32.291's already-vendored field shape instead of inventing TS 32.298's real taxonomy." Seeing
free5GC's real implementation **confirms this is a real, substantial, closeable gap** -- but per
ADR-0001's greenfield discipline, closing it means obtaining the real 3GPP TS 32.298 document
directly (it's freely 3GPP-published, unlike GSMA's member-restricted TAP3 spec that blocked
RAP/NRTRDE) and transcribing it independently -- not reading or adapting free5GC's own Go structs,
which would violate this project's own "reference reading only, never copy" rule. Seeing free5GC's
`CHFRecord`/IE names is confirmation the real spec artifact exists and roughly what it's shaped
like, not a substitute for actually obtaining and citing TS 32.298 itself.

### `ccs_diameter/` -- roughly parallel to this project's own Gy/Rf, not a new gap

free5GC's `ccs_diameter/` (Diameter CCS -- Credit Control Service, its own Ro/Gy-class dictionary
and codec) is architecturally the same real capability this project's own Diameter Gy/Rf/Sy
implementation (P4.5, ADR-0059/-0060) already covers. Not re-derived line-by-line in this pass
since this project's own Diameter work is already real, tested, and live-verified -- flagged for
a closer behavioral diff only if a specific discrepancy surfaces later, not assumed to be a gap.

---

## Summary and priority signal (all 9 built NFs covered)

| NF | Scale ratio (ref/ours) | Highest-priority real gap |
|---|---|---|
| NRF | ~1-1.3x | NFProfile semantic validation; active heartbeat-expiry timer (open5GS only) |
| AMF | ~10-14x | **`ServiceRequest` NAS procedure entirely missing; zero N2 handover support (NGAP + GMM both sides)** -- highest-impact finding in the whole sweep |
| AUSF | ~3-5x | `Nausf_SoRProtection`, ProSe auth (free5GC-only, both) |
| SMF | ~10-16x | `UpdateSMContext` is a near-total stub -- couples directly to AMF's handover gap |
| PCF | ~7-10x | `Npcf_PolicyAuthorization` (AF/IMS-facing QoS) -- confirmed in BOTH references, high real-world impact |
| UDM | ~3-6x | `Nudm_EE`/`Nudm_PP` (free5GC-only, both) |
| UDR | ~2.5-10x | Resource-type breadth (~6 of 42+ real TS 29.504 resources) |
| UPF | ~2.5-5x | PFD Management, Association Update/Release, Node Report, Session Set Deletion; datapath (XDP) already ahead of both references on paper, unbenchmarked |
| CHF | ~2.2x (free5GC), N/A (open5GS has none) | TS 32.298 real CDR encoding; already ahead on 5G-native service breadth + AI-native charging |

**Real, honest pattern across the sweep**: this project's "happy path" (initial attach, PDU
session establishment, core CRUD on each NF's primary resource) is consistently real and already
tested. The consistent gap is **procedure/service BREADTH** -- mobility/handover, the less-common
NAS/NGAP procedures, the wider data-repository resource surface, and a handful of
optional-but-real 3GPP services each NF has. AMF/SMF's shared N2-handover gap is the single
highest-impact, highest-effort item found. CHF is the one NF where this project is already
ahead of at least one reference on real service breadth.

**Still not done, per ADR-0075's own scope**: `nssf`/`nef`/`scp`/`bsf` don't exist in this project
at all yet (separate, larger "whole NF missing" gap, not blended into the per-NF findings above).
No implementation against any finding above has started -- this document is the evidence base;
sequencing which gap gets implemented first is the next real decision, not yet made.
