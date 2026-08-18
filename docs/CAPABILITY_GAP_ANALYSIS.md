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

1. **`NFProfile` semantic validation was missing -- closed, docs/DECISIONS.md ADR-0079.** free5GC
   has ~290 lines of real validation (`internal/sbi/processor/nf_profile_validation.go`):
   `nfInstanceId` must be a real UUID v4, `heartBeatTimer` bounds-checked, `nfType`/`nfStatus`/
   `nfServiceStatus`/`uriScheme` checked against real enum sets, `ipv4Addresses`/`ipv6Addresses`
   format-validated, `ipEndPoint.transport` must be TCP, port range-checked. This project's NRF
   previously only checked that `nfInstanceId`/`nfType`/`nfStatus` keys were **present** -- no
   format or enum validation at all, so a malformed `nfType` or an out-of-range `heartBeatTimer`
   was silently accepted. Now real: every constraint above is enforced, each independently
   grounded in the actual OpenAPI YAML (cited per-field in `nfs/nrf/src/main.cpp`'s own comment,
   not copied from free5GC's own choices) -- live-verified via real HTTP 400 rejections for each
   violation class.

2. **No active heartbeat-expiry timer -- closed, docs/DECISIONS.md ADR-0079.** open5GS runs a
   real per-NF-instance timer (`src/nrf/nf-sm.c:187-216`, `t_no_heartbeat`, started on entry to
   the `registered` state for `heartbeat_interval + no_heartbeat_margin`) that proactively
   deregisters an NF instance if it stops sending heartbeats, firing a real de-registration
   notification to subscribers. This project's NRF previously accepted `PATCH` heartbeats but had
   no background expiry sweep at all -- a crashed NF that never sent `DELETE` stayed registered
   forever. free5GC does not appear to have this either (only stores the `HeartBeatTimer` value,
   `internal/context/management_data.go:62`) -- this was specifically an open5GS capability this
   project lacked, not a universal gap. Now real: `NfRegistry::sweep_expired`, a periodic
   background sweep (this project's own design, not a byte-for-byte port of open5GS's per-NF
   timer object) -- live-verified end-to-end, including that a `PATCH` heartbeat genuinely resets
   the expiry window, not just that expiry fires at all.

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

This project's `ngap_task.cpp` handles 7 real procedures (grep-confirmed, `void handle_*`):
`NGSetupRequest`, `InitialUEMessage`, `UplinkNASTransport` (+ 2 internal sub-variants for
SMC-complete and PDU-session-establishment relay), and (closed, docs/DECISIONS.md ADR-0078)
`UEContextReleaseRequest`/`UEContextReleaseComplete` (RAN-initiated direction only). Grep-confirmed
still absent from `ngap_codec.hpp` entirely -- not even encodable/decodable, not just unhandled:
`Handover*`, `PathSwitchRequest`, `NGReset`, `InitialContextSetup*`.

**This project's AMF has zero N2 handover support** -- no `HandoverRequired`, no
`PathSwitchRequest`, nothing. A UE cannot be handed over between gNBs in this implementation
today; only initial attach + PDU session establishment on a single gNB is real. This is a
structural gap, not a missing edge case. **Not addressed by ADR-0076 or ADR-0078** (those passes
closed `ServiceRequest`/GMM and `UEContextRelease` specifically, not this NGAP-side gap) -- still
mostly open, tracked as the remaining scope of task #100/#101.

**Partial closure, ADR-0090**: `PathSwitchRequest`/`PathSwitchRequestAcknowledge`/
`PathSwitchRequestFailure` (the AMF-facing tail of Xn-based handover, TS 38.413 §8.4.4) are now
real and live-verified -- a genuine, previously-missing `amf_ue_ngap_id -> tmsi` cross-association
index and real TS 33.501 Annex A.9/A.10 vertical key derivation (KgNB/NH) were built along the
way. See ADR-0090 for the full real, disclosed scope (including a real, found-in-passing correction
to `ngap_task.hpp`'s own pre-existing "one thread per association" claim -- confirmed at the time
to be single-association-at-a-time, sequential; that specific limitation is what ADR-0095 below
fixes for real).

**Closed, ADR-0095/ADR-0096**: the real N2-based handover chain (`HandoverRequired`/
`HandoverRequest`/`HandoverRequestAcknowledge`/`HandoverCommand`/`HandoverPreparationFailure`/
`HandoverFailure`/`HandoverNotify`) is now real and live-verified with two genuinely,
simultaneously-open gNB associations -- the real architectural prerequisite this remaining scope
needed (`run_ngap_lifecycle`'s accept loop was strictly sequential, confirmed above; ADR-0095
rearchitected it to one real `std::thread` per association plus a new `GnbAssociationRegistry` for
real cross-thread relay/correlation). Live-verified against a real, unmodified UERANSIM
registration plus two hand-crafted scratch gNB clients, including a genuine, unplanned bonus
confirmation: UERANSIM's own real gNB correctly processed a real, new AMF-initiated
`UEContextReleaseCommand` this same pass added (closing the exact gap ADR-0078 disclosed as the
"AMF-INITIATED direction... not implemented" below). Real, disclosed remaining gap:
`HandoverCancel`/`HandoverCancelAcknowledge` (a separate elementary procedure, not part of the
closed chain), and `HandoverRequest`'s own per-session PDU transfer content is real but carries a
placeholder N3 address (no real AMF->SMF relay built for handover, same disclosed class as
`PathSwitchRequest`'s own still-open "AMF doesn't call SMF yet" gap two rows below).

**Real NGAP gap found via live interop, closed the same way it was found**: attempting to
trigger a real `ServiceRequest` naturally (gNB-initiated idle-mode re-entry via UERANSIM's
`nr-cli <gnb> --exec 'ue-release <id>'`) sent a real NGAP `UEContextReleaseRequest` this AMF could
not decode at all -- `amf-ngap: failed to decode NGAP PDU (25 bytes), ignoring: 002a4015...`.
Closed as of ADR-0078: real `UEContextReleaseRequest`/`UEContextReleaseCommand`/
`UEContextReleaseComplete` (RAN-initiated direction), live-verified via the identical interop
scenario -- gNB's own `ue-list` went from one entry to empty, UE transitioned to CM-IDLE, real
NGAP bytes exchanged both directions. The AMF-initiated `UEContextRelease` direction (an AMF
deciding on its own to release, e.g. after Deregistration) remains unimplemented, disclosed as a
real, deliberate scope boundary in ADR-0078, not silently dropped.

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

1. **`Nausf_SoRProtection`** (`POST /{supi}/ue-sor`, real TS 33.501 Annex A.17/A.18 (originally
   miscited as "Annex E.2" -- corrected and independently verified against a real local copy of
   TS 33.501 v19.6.0 before implementation) -- protects Steering-of-Roaming (SoR) list/CMCI data
   against tampering by a compromised VPLMN). Real in free5GC (`internal/sbi/api_sorprotection.go`).
   **Grep-confirmed absent from open5GS's AUSF too** -- free5GC-only capability. **Closed, docs/
   DECISIONS.md ADR-0081**: real SoR-MAC-IAUSF/SoR-MAC-IUE crypto (`libs/aka-crypto`), a new
   persistent per-SUPI `KausfStore` (the real architectural prerequisite this needed), and the
   real CounterSoR freshness-counter state machine including wrap-around suspension -- all
   live-verified cross-process. Real, disclosed scope narrowing: only the "SOR header supplied by
   requester" branch is implemented (AUSF constructing its own header per TS 24.501 §9.11.3.51 is
   a different spec section not in hand); the structured-array form of the optional steering-info
   parameter is also out of scope for the same reason.
2. **ProSe (Proximity Services / D2D) authentication** (`POST /prose-authentications`, `DELETE
   .../prose-auth`, a real R17+ extension). Real in free5GC. **Grep-confirmed absent from
   open5GS's AUSF** as well -- same free5GC-only status as SoR protection. **Closed, docs/
   DECISIONS.md ADR-0091**: real TS 33.503 Annex A.2/A.3/A.4 CP-PRUK/CP-PRUK-ID*/KNR_ProSe
   derivations (`libs/aka-crypto`), a new UDM `GenerateProseAV` route reusing the existing
   EAP-AKA' Milenage path, a new AUSF `ProSeAuthContext` store, all live-verified cross-process
   (a hand-crafted UE-role client independently recomputed RES/K_aut from real TS 35.207 Test Set
   1 public vectors). Real, disclosed narrower scope than free5GC's: only the first-time/new-
   CP-PRUK path is built -- the `5gPrukId`-based returning-UE path and CP-PRUK's own real
   cross-session persistence both need a live PAnF (ProSe Anchor Function) this project doesn't
   have (a whole separate, unbuilt NF), and return a real, disclosed `501 Not Implemented`.

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

**Partial closure, ADR-0092**: `PATH_SWITCH_REQ`/`PATH_SWITCH_REQ_ACK` are now real -- `UpdateSMContext`
gained real multipart parsing, decodes the real NGAP `PathSwitchRequestTransfer`, issues a real
PFCP Session Modification creating this project's first-ever real downlink PDR/FAR (with a real
TS 29.244 §8.2.56 Outer Header Creation), and returns a real `PathSwitchRequestAcknowledgeTransfer`
carrying UPF's own real N3 uplink F-TEID. A real, previously-undiscovered UPF bug (false-positive
`RequestAccepted` on an unrecognized `CreatePDR`/`CreateFAR`) was found and fixed along the way.
Live-verified, three-way corroborated (SMF log, UPF log, and independent decode of the real HTTP
response). Real, disclosed scope: AMF's own `PathSwitchRequest` handler does not call this
endpoint yet (deferred, separate relay-wiring piece); the downlink PDR's match criteria is
`SourceInterface`-only (this project has no real UE IP allocation anywhere, a real, deeper,
separate gap found while scoping this ADR); the new FAR's `OuterHeaderCreation` is real at the
PFCP control-plane level only, not wired into the real eBPF/XDP datapath (no downlink GTP-U
encapsulation path exists there). The other 20 real `N2SmInfoType` values (`PDU_RES_*`,
`HANDOVER_*`, ...) remain unreal -- see ADR-0092 for full disclosure.

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
| `Npcf_PolicyAuthorization` | Real (`policyauthorization.go`) | Real -- confirmed via `OGS_SBI_RESOURCE_NAME_APP_SESSIONS` in `npcf-handler.c` (1,686 lines), the real TS 29.514 resource name | **Closed, docs/DECISIONS.md ADR-0080.** Both references implement this; this project's own `nfs/pcf/src/main.cpp` now does too (`PostAppSessions`/`GetAppSession`/`ModAppSession`/`DeleteAppSession`/`updateEventsSubsc`/`DeleteEventsSubsc`/`PcscfRestoration`, all live-verified). Real, disclosed gap remaining: no PCC-rule engine to authorize a requested media flow against -- returns a schema-correct, non-fabricated "authorized" outcome (no `ServAuthInfo` failure code), not real subscriber-specific decisioning. |
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

**Real gap, entirely missing, both references -- closed, docs/DECISIONS.md ADR-0082**: `Nudm_EE`
(Event Exposure -- `CreateEeSubscription`/`UpdateEeSubscription`/`DeleteEeSubscription`, real
subscription-based UDM event notifications) and `Nudm_PP` (Parameter Provisioning -- the real
`Get`/`Update` `pp-data` operation the analysis named). Both free5GC-only (grep-confirmed absent
from open5GS's UDM too), still real gaps per ADR-0075, now closed and live-verified. Real,
newly-discovered additional `Nudm_PP` scope found while implementing this (not in the original
finding above): three larger, more specialized resource groups -- `/5g-vn-groups/{extGroupId}`
(5G LAN/VN group CRUD), `/{ueId}/pp-data-store/{afInstanceId}` (PP Data Entry CRUD),
`/mbs-group-membership/{extGroupId}` (5G MBS group CRUD) -- flagged for a future turn, not built
or silently dropped.

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

This project's UDR (`nfs/udr/src/main.cpp`) implements 33 real resource endpoints: AMF 3GPP-access
context-data, AMF non-3GPP-access context-data (docs/DECISIONS.md ADR-0093), SMSF 3GPP-access and
non-3GPP-access context-data (ADR-0097 -- see below), IP-SM-GW Registration context-data
(ADR-0098 -- see below), Message Waiting Data (Document) context-data (ADR-0099 -- see below),
Roaming Information (Document) context-data (ADR-0100 -- see below), PEI Information (Document)
context-data (ADR-0101 -- see below), Enhanced Coverage Restriction Data (ADR-0102 -- see below),
LCS Privacy Subscription Data (ADR-0103 -- see below), LCS Subscription Data (ADR-0104 -- see
below), LCS Mobile Originated Subscription Data (ADR-0105 -- see below), Parameter Provision
(Document) (ADR-0107 -- see below), Parameter Provision profile Data (Document) (ADR-0108 -- see
below), Provisioned Parameter Data Entry / pp-data-store (ADR-0109 -- see below), individual
Shared Data (ADR-0110 -- see below, first non-per-UE UDR resource), Operator-Specific Data
Container (Document) (ADR-0111 -- see below), Event Exposure Data (Document) (ADR-0112 -- see
below), UE Policy Set (ADR-0113 -- see below), policy-data Operator-Specific Data (ADR-0114 --
see below), Sponsor Connectivity Data (ADR-0115 -- see below, second non-per-UE UDR resource),
individual BDT Data (ADR-0116 -- see below, richest policy-data resource yet), PLMN UE Policy Set
(ADR-0117 -- see below, keyed by plmnId rather than ueId), Slice-specific Policy Control Data
(ADR-0118 -- see below, real GET+PATCH-only, upsert-capable merge-patch),
SMF-registrations context-data (full CRUD,
`{pduSessionId}`-scoped), provisioned-data (`am-data`, `smf-selection-subscription-data`,
`sm-data`, and -- ADR-0106 -- `lcs-bca-data`), and the real nested `policy-data/ues/{ueId}/sm-data` resource from ADR-0072
(`SmPolicyData` with full `SmPolicySnssaiData -> SmPolicyDnnData` nesting and RFC 7396 merge-patch
semantics -- genuinely more complete for THIS one resource than a bare CRUD document, per that
ADR's own real, deliberate design). What's covered is solid; the real gap is breadth -- roughly 33
of free5GC's ~42+ real resource types (9 as of ADR-0083, 10 as of ADR-0093, 12 as of ADR-0097, 13
as of ADR-0098, 14 as of ADR-0099, 15 as of ADR-0100, 16 as of ADR-0101, 17 as of ADR-0102, 18 as
of ADR-0103, 19 as of ADR-0104, 20 as of ADR-0105, 21 as of ADR-0106, 22 as of ADR-0107, 23 as of
ADR-0108, 24 as of ADR-0109, 25 as of ADR-0110, 26 as of ADR-0111, 27 as of ADR-0112, 28 as of
ADR-0113, 29 as of ADR-0114, 30 as of ADR-0115, 31 as of ADR-0116, 32 as of ADR-0117, now 33 as of
ADR-0118 -- see below).

**Highest-priority missing resources** (the ones with real, direct behavioral impact elsewhere in
this project, not just data-model completeness): Authentication Data / Authentication Status
documents (UDR-side persistence for AUSF's own authentication vectors and result status), AM
Policy Data (UDR-side backing for PCF's `Npcf_AMPolicyControl`). **Closed, docs/DECISIONS.md
ADR-0083**: all three now real, live-verified endpoints (`authentication-subscription` GET+PATCH,
`authentication-status` PUT+GET+DELETE, `/policy-data/ues/{ueId}/am-data` GET+PATCH), taking UDR
from 6 to 9 of free5GC's ~42+ real resource types. Real, disclosed architectural note (flagged in
the original finding, not glossed over): AUSF/UDM/PCF's own existing stores were NOT migrated to
call these new routes -- that's a real, separate, deliberate future decision, same "stand up the
surface first, wire consumers later" precedent already used for UDR's own `provisioned-data`
group (ADR-0069) and for PCF itself (ADR-0028). **Closed, docs/DECISIONS.md ADR-0093**: AMF
non-3GPP-access context-data (`QueryAmfContextNon3gpp`/`CreateAmfContextNon3gpp`, real GET+PUT,
schema `AmfNon3GppAccessRegistration`, a real, distinct resource from the 3GPP-access one, not a
rename) -- taking UDR from 9 to 10 of free5GC's ~42+ real resource types. Same "surface first,
consumer wiring later" disclosed precedent -- AMF's own registration path does not yet call this
endpoint. **Closed, docs/DECISIONS.md ADR-0097**: SMSF Registration context-data, 3GPP-access and
non-3GPP-access (`CreateSmsfContext3gpp`/`QuerySmsfContext3gpp`/`DeleteSmsfContext3gpp` and their
non-3GPP counterparts, real GET+PUT+DELETE, schema `SmsfRegistration` shared by both real, distinct
resources) -- taking UDR from 10 to 12 of free5GC's ~42+ real resource types. Same "surface first,
consumer wiring later" precedent -- SMSF itself doesn't exist as a built NF in this project yet
(Tier 2), so nothing calls these new routes. **Closed, docs/DECISIONS.md ADR-0098**: IP-SM-GW
Registration context-data (`CreateIpSmGwContext`/`QueryIpSmGwContext`/`ModifyIpSmGwContext`/
`DeleteIpSmGwContext`, real PUT+GET+PATCH+DELETE, schema `IpSmGwRegistration`, real RFC 6902
`application/json-patch+json` patch) -- taking UDR from 12 to 13 of free5GC's ~42+ real resource
types, and the first UDR context-data resource with all four real operations together. Same
"surface first, consumer wiring later" precedent -- no NF currently calls this new endpoint.
**Closed, docs/DECISIONS.md ADR-0099**: Message Waiting Data (Document) context-data
(`CreateMessageWaitingData`/`QueryMessageWaitingData`/`ModifyMessageWaitingData`/
`DeleteMessageWaitingData`, real PUT+GET+PATCH+DELETE, schema `MessageWaitingData`, real
distinct `201`-vs-`204` PUT response codes unlike `ip-sm-gw`'s own always-`204` PUT) -- taking UDR
from 13 to 14 of free5GC's ~42+ real resource types. Same "surface first, consumer wiring later"
precedent -- SMSF, the real originator of MWD data, doesn't exist as a built NF in this project
yet (Tier 2). **Closed, docs/DECISIONS.md ADR-0100**: Roaming Information (Document) context-data
(`UpdateRoamingInformation`/`QueryRoamingInformation`, real GET+PUT only, schema
`RoamingInfoUpdate`, real distinct `201`-vs-`204` PUT response codes) -- taking UDR from 14 to 15
of free5GC's ~42+ real resource types. Same "surface first, consumer wiring later" precedent --
no NF currently calls this new endpoint. **Closed, docs/DECISIONS.md ADR-0101**: PEI Information
(Document) context-data (`CreateOrUpdatePeiInformation`/`QueryPeiInformation`, real GET+PUT only,
real `allOf`-composed schema `PeiUpdateInfo` correctly flattened by sbi-codegen into
`PeiUpdateInfo_Subscription_Data`, real distinct `201`-vs-`204` PUT response codes) -- taking UDR
from 15 to 16 of free5GC's ~42+ real resource types. Same "surface first, consumer wiring later"
precedent -- no NF currently calls this new endpoint. **Closed, docs/DECISIONS.md ADR-0102**:
Enhanced Coverage Restriction Data (`QueryCoverageRestrictionData`, real GET-only, seeded at
startup for the same two real test SUPIs every other GET-only UDR resource seeds -- same shape as
`provisioned-data`) -- taking UDR from 16 to 17 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md
ADR-0103**: LCS Privacy Subscription Data (`QueryLcsPrivacyData`, real GET-only, seeded at
startup) -- taking UDR from 17 to 18 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md
ADR-0104**: LCS Subscription Data (`QueryLcsSubscriptionData`, real GET-only, seeded at startup)
-- taking UDR from 18 to 19 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md
ADR-0105**: LCS Mobile Originated Subscription Data (`QueryLcsMoData`, real GET-only, seeded at
startup) -- taking UDR from 19 to 20 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md
ADR-0106**: LCS Broadcast Assistance Data (`QueryLcsBcaData`, real GET-only, added as a 4th column
on the existing `provisioned-data` group/`ProvisionedDataStore` rather than a new store) -- taking
UDR from 20 to 21 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md ADR-0107**:
Parameter Provision (Document) (`GetppData`/`ModifyPpData`, real GET+PATCH, RFC 6902,
upsert-capable) -- taking UDR from 21 to 22 of free5GC's ~42+ real resource types. **Closed,
docs/DECISIONS.md ADR-0108**: Parameter Provision profile Data (Document) (`QueryPPData`, real
GET-only, seeded at startup) -- taking UDR from 22 to 23 of free5GC's ~42+ real resource types.
**Closed, docs/DECISIONS.md ADR-0109**: Provisioned Parameter Data Entry / `pp-data-store`
(`Create`/`Get`/`Delete PP Data Entry` plus `Get Multiple PP Data Entries`, real PUT+GET+DELETE
plus a real sibling collection GET, composite `(ueId, afInstanceId)` key) -- taking UDR from 23 to
24 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md ADR-0110**: individual
Shared Data (`GetIndividualSharedData`, real GET-only, keyed by `sharedDataId` alone -- the first
UDR resource in this project genuinely not keyed per-UE) -- taking UDR from 24 to 25 of free5GC's
~42+ real resource types; its real sibling collection resource (`GetSharedData`, array query
parameter) remains deferred, needing array-query-param parsing this project has no precedent for
yet. **Closed, docs/DECISIONS.md ADR-0111**: Operator-Specific Data Container (Document)
(`QueryOperSpecData`/`ModifyOperSpecData`, real GET+PATCH, RFC 6902, upsert-capable, same
"no PUT/DELETE" shape as pp-data) -- taking UDR from 25 to 26 of free5GC's ~42+ real resource
types. **Closed, docs/DECISIONS.md ADR-0112**: Event Exposure Data (Document) (`QueryEEData`,
real GET-only, seeded at startup, distinct from this project's own UDM-side `Nudm_EE` work) --
taking UDR from 26 to 27 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md
ADR-0113**: UE Policy Set (`ReadUEPolicySet`/`CreateOrReplaceUEPolicySet`/`UpdateUEPolicySet`,
real GET+PUT+PATCH RFC 7396, the first `policy-data` group resource combining both a real
create-or-replace PUT and a real merge-patch PATCH) -- taking UDR from 27 to 28 of free5GC's ~42+
real resource types. **Closed, docs/DECISIONS.md ADR-0114**: `policy-data` group's
Operator-Specific Data (`ReadOperatorSpecificData`/`UpdateOperatorSpecificData`, real GET+PATCH
RFC 6902, genuinely distinct real resource from the `subscription-data`-scoped one, ADR-0111,
confirmed live) -- taking UDR from 28 to 29 of free5GC's ~42+ real resource types. **Closed,
docs/DECISIONS.md ADR-0115**: Sponsor Connectivity Data (`ReadSponsorConnectivityData`, real
GET-only, keyed by `sponsorId` alone -- the second UDR resource genuinely not keyed per-UE) --
taking UDR from 29 to 30 of free5GC's ~42+ real resource types. **Closed, docs/DECISIONS.md
ADR-0116**: individual BDT Data (`ReadIndividualBdtData`/`CreateIndividualBdtData`/
`UpdateIndividualBdtData`/`DeleteIndividualBdtData`, real GET+PUT+PATCH+DELETE, the richest real
`policy-data` operation set closed so far -- real PUT documents only `201`, no update-via-PUT
status, and real PATCH is NOT upsert-capable, both genuine divergences from every prior resource
of the same shape) -- taking UDR from 30 to 31 of free5GC's ~42+ real resource types. **Closed,
docs/DECISIONS.md ADR-0117**: PLMN UE Policy Set (`ReadPlmnUePolicySet`, real GET-only, reuses the
`UePolicySet` schema but keyed by `plmnId` rather than `ueId` -- a genuinely distinct,
H-PLMN-scoped resource, not a UE-scoped alias) -- taking UDR from 31 to 32 of free5GC's ~42+ real
resource types. **Closed, docs/DECISIONS.md ADR-0118**: Slice-specific Policy Control Data
(`ReadSlicePolicyControlData`/`UpdateSlicePolicyControlData`, real GET+PATCH-only, no `PUT`/`POST`
create operation exists at all, so the real RFC 7396 merge-patch is upsert-capable, same
disclosed precedent as `am-data`; keyed by `snssai`, this project's own disclosed
`sst + '-' + sd` string convention reused from ADR-0072 since the YAML itself documents no bare
path-segment encoding for the `Snssai` object schema) -- taking UDR from 32 to 33 of free5GC's
~42+ real resource types.
Influence Data (AF traffic-steering, needed once NEF
exists) remains open, out of scope until NEF is built.

---

## UPF

**Scale context**: this project's UPF is 1,743 lines vs free5GC's go-upf 8,606 (Go) vs open5GS's
4,423 (C).

### Real PFCP (N4) message coverage

free5GC's `internal/pfcp/pfcp.go` dispatches the full real PFCP message set (grep-confirmed,
request+response pairs): `Heartbeat`, `PFDManagement`, `AssociationSetup`, `AssociationUpdate`,
`AssociationRelease`, `NodeReport`, `SessionSetDeletion`, `SessionEstablishment`,
`SessionModification`, `SessionDeletion`, `SessionReport`. This project's UPF now handles every one
of these except `SessionSetDeletion` -- `Heartbeat`, `AssociationSetup`, `AssociationUpdate`,
`AssociationRelease`, `SessionEstablishment`/`Modification`/`Deletion`, `PFDManagement`, sends and
now (real, both directions) receives `NodeReport`. `AssociationUpdate`/`AssociationRelease` closed
by gap-closure task #107 part 1 (ADR-0084, live-verified over real raw UDP); `PFDManagement` closed
by task #107 part 2 (ADR-0086, live-verified over real raw UDP -- real, disclosed gap: no
Application Detection Filter engine yet consumes the provisioned PFDs); `NodeReport` closed by task
#107's final slice (ADR-0087, live-verified between two real, independently-built processes --
UPF's own send-side encoder and SMF's real `PfcpPeer` receive-side handler).

**`SessionSetDeletion` real, disclosed correction to this section's own original finding** (ADR-
0087): this project's vendored spec text (`specs/PFCP/29244-e30.pdf`, Table 7.3-1's own
"Applicability" columns) marks Session Set Deletion (message types 14/15) applicable to Sxa/Sxb
only -- **not Sxc**, and N4 (this project's own real interface) is Sxc. Its own IE table (Table
7.4.6.1-1) independently confirms this: every conditional IE is an EPC-only FQ-CSID concept
(SGW-C/PGW-C/SGW-U/PGW-U/TWAN/ePDG/MME) this project's 5GC-only scope has no real analogue for.
free5GC's own dispatch of this message type is real, but does not by itself mean the message
applies to a 5GC-only N4 deployment such as this one's -- a genuine "not applicable" per the spec
text actually read, not a deferred stub. **Task #107 is now closed in full**: 4 of its 5 originally-
named gaps are real and closed, the 5th is a real non-gap.

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

### Real gap, now CLOSED (task #108, ADR-0089): TS 32.298 CDR encoding

free5GC's CHF has a real, substantial `cdr/` module (4,746 lines) implementing genuine TS 32.298
ASN.1 BER CDR encoding -- confirmed by real, specific 3GPP IE-matching type names
(`cdr/cdrType/`: `CHFRecord` -- the actual real TS 32.298 top-level 5G CDR record type name --
`AllocationRetentionPriority`, `CauseForRecClosing`, `AMFID`, `ChargingID`,
`AgeOfLocationInformation`, dozens more), plus its own hand-written BER marshal/unmarshal
(`cdr/asn/ber_{marshal,unmarshal}.go` -- not asn1c-generated from a vendored `.asn` file, but a
real, faithful transcription of the real TS 32.298 field layout into Go structs).

This project's own `nfs/chf/schema.clickhouse.sql` used to self-disclose the matching gap: "this
is NOT a conformant TS 32.298 CDR... TS 32.298 is not vendored in this repo." The user supplied
the real spec (`specs/TS_32_298.pdf`, ETSI TS 132 298 V18.8.0/Release 18, verified genuine) and
`nfs/chf/src/cdr_asn1.{hpp,cpp}` now implements a real, BER-encoded `[200] chargingFunctionRecord`
transcribed independently from the spec itself -- never from free5GC's own Go structs, per
ADR-0001's "reference reading only, never copy" rule. Real, disclosed, narrower scope than
free5GC's: 10 of 46 real `ChargingRecord` fields are populated (the ones this project's own CHF
has genuine data for); the rest need unvendored specs (TS 32.255, TS 32.260) or don't apply to
this project's current scope. Live-verified via real curl + direct ClickHouse hex-decode of the
stored `asn1_cdr` column against a real running CHF instance. Disclosed version gap: v18.8.0/
Release 18, not yet re-verified against REL-19. Full detail in ADR-0089/`docs/TRACEABILITY.md`.

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
| AMF | ~10-14x | `ServiceRequest`: CLOSED (ADR-0076). N2 handover: CLOSED in full (task #100, ADR-0090 `PathSwitchRequest` slice + ADR-0095/ADR-0096 real concurrent associations + the full `HandoverRequired`...`HandoverNotify` chain); `HandoverCancel` and real AMF->SMF PDU-session-transfer depth remain a real, disclosed, smaller open gap |
| AUSF | ~3-5x | `Nausf_SoRProtection`, ProSe auth (free5GC-only, both) |
| SMF | ~10-16x | `UpdateSMContext`: `PATH_SWITCH_REQ`/`_ACK` slice CLOSED (task #101, ADR-0092, real downlink FAR/GTP-U control-plane); the other 20 real N2SmInfoType values remain a stub. AMF's own N2 handover NGAP side is now closed (ADR-0095/ADR-0096), but AMF still doesn't call SMF during handover -- the real AMF->SMF relay wiring for handover-triggered PDU session resource re-setup remains a real, disclosed open gap |
| PCF | ~7-10x | `Npcf_PolicyAuthorization` (AF/IMS-facing QoS) -- confirmed in BOTH references, high real-world impact |
| UDM | ~3-6x | `Nudm_EE`/`Nudm_PP` (free5GC-only, both) |
| UDR | ~2.5-10x | Resource-type breadth (~12 of 42+ real TS 29.504 resources) |
| UPF | ~1x (task #107 fully closed: Association Update/Release, ADR-0084; PFD Management, ADR-0086; Node Report, ADR-0087; Session Set Deletion correctly found not applicable to this project's own N4/Sxc interface) | datapath (XDP) already ahead of both references on paper, unbenchmarked |
| CHF | ~2.2x (free5GC), N/A (open5GS has none) | TS 32.298 real CDR encoding: CLOSED (task #108, ADR-0089, narrower disclosed scope than free5GC's); already ahead on 5G-native service breadth + AI-native charging |

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
