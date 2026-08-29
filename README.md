# 5G Advanced Core and Charging R-19

A modular, standards-faithful 5G Core (5GC) implementation in modern C++, targeting 3GPP
**Release 19 (5G-Advanced)**. Every Network Function's northbound API is meant to be **generated**
from the official 3GPP OpenAPI YAML — never hand-written — with a TM Forum SID-aligned
charging/BSS domain, a JSON-schema-driven operator GUI, and AI/ML pipelines wired into NWDAF.

This targets a **production-grade, spec-traceable reference implementation** (raised from an
original lab-grade scope — see `docs/DECISIONS.md` ADR-0009 for why and what that changed).
Repo slug (`5gc-r19`) and technical identifiers (CMake project name, vcpkg package name) stay as
short slugs; this is the display name. See [`docs/DECISIONS.md`](docs/DECISIONS.md) for every
architectural choice made (and rejected) along the way.

[![CI](https://github.com/prajithparan/5G-Advanced-Core-and-Charging-R19/actions/workflows/ci.yml/badge.svg)](https://github.com/prajithparan/5G-Advanced-Core-and-Charging-R19/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

## Source of truth

3GPP OpenAPI YAML (REL-19, vendored under [`specs/`](specs/)) is the only source for API shapes,
paths, schemas, and enums used anywhere in this repo. Nothing here hand-writes a DTO that the YAML
can generate, and nothing invents a TS number, reference point, or field name that isn't in the
spec text. Full conventions are in [`CLAUDE.md`](CLAUDE.md).

## Status

| Phase | What | Status |
|---|---|---|
| 0 | Foundations: CMake+vcpkg skeleton, `libs/sbi-core` (HTTP/2, OAuth2, ProblemDetails, headers, logging, tracing), TLS 1.3 + mTLS | Done |
| 1 | Codegen spine: `tools/sbi-codegen`, generated DTOs/serializers from the R19 YAML | Done |
| 2 | Control-plane core: NRF, AMF, SMF, UDM, UDR, AUSF, PCF; UE registration + PDU session establishment end-to-end | Done |
| 3 | User plane: N4/PFCP, UPF datapath (including a real eBPF/XDP fast path) | Done |
| 4 | Charging + TM Forum SID/BSS layer | Live-verified end to end |
| 5 | NWDAF + AI/ML pipelines | Not started |
| 6 | R19 feature NFs (AIOTF, 5MBS, SEPP, ...) | Not started |
| 7 | GUI / operations console | Not started |
| 8 | Lab packaging (`make lab-up`) | Not started |

**Phase 2** — all 7 NFs implemented; both target procedures (TS 23.502 §4.2.2.2.2 UE Registration,
§4.3.2.2.1 PDU Session Establishment) verified end-to-end over real NGAP/N2 (SCTP + ASN.1 PER) and
NAS-5GS in a single `nr-gnb`/`nr-ue` interop run: NG Setup → Initial Registration (including a real
SQN resynchronization) → SecurityModeCommand/Complete → RegistrationAccept → AM Policy Association
with PCF → PDU Session Establishment, built from PCF's real QoS decision and delivered to the UE
via `Namf_Communication`. See `docs/DECISIONS.md` ADR-0032–ADR-0038 and
[`docs/TRACEABILITY.md`](docs/TRACEABILITY.md) for exactly what was proven how.

**Phase 3** — UPF registers with NRF, answers PFCP Heartbeat/Association/Session Establishment;
SMF discovers UPF via `Nnrf_NFDiscovery` and creates a real N4 session on every PDU Session
Establishment. The eBPF/XDP datapath passes the BPF verifier, registers TEIDs from real PFCP
signalling in a live BPF map, and decapsulates a real GTP-U packet end-to-end to the TUN device.
See ADR-0043.

**Phase 4** — CHF live-verified for the full charging lifecycle (`Nchf_ConvergedCharging`
Create/Update/Release, real quota-consumption tracking and re-authorization closing the loop
through UPF usage measurement and a live PFCP Session Modification, ADR-0050). Real legacy
interconnect (SS7 M3UA+SCCP, TCAP, MAP, CAP/CAMEL, Diameter Gy/Rf) and a real TM Forum BSS layer
(product-catalog/TMF620, balance-management/TMF654, subscriber-management/TMF632,
roaming-interconnect/TMF651) are all live-verified over mTLS. GSMA TAP3 roaming-CDR encoding is
wired end-to-end. See [`docs/CHARGING_MAPPING.md`](docs/CHARGING_MAPPING.md) for the SID/BSS field
mapping.

Full phase plan: [`PROMPT.md`](PROMPT.md).

## Capability-completeness gap-closure

Alongside the phase plan, an ongoing effort closes real gaps found by comparing this project
against free5GC's and open5GS's own actual source (not just their docs) — every change is real,
live-verified, and tracked as its own ADR rather than just unit-tested. NRF, AMF, SMF, AUSF, PCF,
UDM, CHF, and UPF have each closed multiple real procedure/resource gaps this way (N2 handover,
5G ProSe authentication, TS 32.298 CDR encoding, the full PFCP Association lifecycle, and more).
UDR is the largest single target: it now implements the large majority of free5GC's real
`Nudr_DataRepository` resource types, plus a distinct `Nudr_GroupIDmap` resource.

The same effort has also added whole new NFs beyond the original Phase 2 seven: **NSSF**
(`Nnssf_NSSelection` + `Nnssf_NSSAIAvailability`, all 8 real operations, ADR-0183), **BSF**
(`Nbsf_Management`, all 15 real operations, ADR-0184), **NEF** (`Nnef_PFDmanagement`, 6 real
operations — 1 of NEF's own 14 real YAML files; the other 13 remain unbuilt, ADR-0185), and **SCP**
(`Nscp_EventExposure`, all 3 real operations, ADR-0186) — closing the original "still not done"
`nssf`/`nef`/`scp`/`bsf` list, though SCP's own real defining role (an inline HTTP/2
message-forwarding proxy, TS 29.500 §§6.10-6.11) is a real architectural departure from every
other NF here and remains entirely undesigned — a real, explicit scope decision made via
`AskUserQuestion`, not a silent gap. As of ADR-0184, this project moves to the next NF/subsystem
continuously as each one completes rather than waiting on a fresh per-NF decision each time — the
same quality bar (live verification, zero-warning builds, full doc trail) still applies to each.
Tier 2 has now begun with **5G-EIR** (`N5g-eir_EquipmentIdentityCheck`, its entire real API — 1
operation, `GetEquipmentStatus` — fully implemented, not a partial slice, ADR-0187); the real
provisioning/write path for equipment status is genuinely out of 3GPP's own SBI framework scope
here (OAM/GSMA IMEI database sync), disclosed rather than built as unreachable code. And **SMSF**
(`Nsmsf_SMService`, all 5 real operations, including real `multipart/related` handling, ADR-0188)
— no real downstream SMS-GMSC/IWMSC or TS 24.011 SMS-over-NAS relay exists in this project, so
`SendSMS`/`SendMtSMS` report SMSF-level acceptance only, disclosed rather than fabricated. And
**GMLC** (`Ngmlc_Location`, all 5 real operations, ADR-0189) — 3 of the 5 are real, complete,
independent of any other NF; the other 2 (`RequestLocation`/`CancelLocation`) genuinely require
this project's own LMF, so they honestly report `501`/`404` rather than fabricate UE positioning
data. And **LMF** (`Nlmf_Location` + `Nlmf_Broadcast` + `Nlmf_DataExposure`, 11 real operations,
ADR-0191/ADR-0196) — 8 are real, complete, RF-independent (including `Nlmf_Broadcast`'s
`CipheringKeyData`, which honestly always reports no key data available since its own YAML has no
provisioning path, and `Nlmf_DataExposure`'s full subscription CRUD lifecycle);
`DetermineLocation`/`LocationMeasure`/`CancelLocation` genuinely need real LPP (TS 37.355) UE
positioning or PRU/NRPPa measurement data this project doesn't have, so they honestly report
`501`/`404` (GMLC->LMF wiring itself remains a real, disclosed, deferred step), and
`Nlmf_DataExposure`'s own notification path shares that same disclosed gap. Building LMF also
found and fixed 2 real defects — a vendored-spec transcription typo and a real
`tools/sbi-codegen` allOf-merge field-deduplication bug (ADR-0190).

A separate, project-wide audit (ADR-0193) checked every real `N<nf>_*` YAML against every NF's
actual wiring — whole files never added to the sbi-codegen pilot set ("Tier-A" gaps) and, for
wired files, individual operations that were stubbed or silently missing ("Tier-B" gaps). Closed so
far: **NRF** `Nnrf_Bootstrapping` (ADR-0194), **AUSF** `Nausf_UPUProtection` (ADR-0195), **UDR**'s
own two documentation-bug fixes plus a hand-written-DTO replacement (ADR-0197/ADR-0198), **AMF**
`Namf_Location`+`Namf_EventExposure` (ADR-0199) and `Namf_AIoT`/`Namf_MBSBroadcast`/
`Namf_MBSCommunication`/`Namf_MT` (ADR-0200), **SMF** `Nsmf_EventExposure`+`Nsmf_NIDD` (ADR-0201),
**UDM** `Nudm_MT`/`Nudm_NIDDAU`/`Nudm_RSDS`/`Nudm_SSAU`/`Nudm_UEID` (ADR-0202, including a real,
working TS 33.501 SUCI de-concealment endpoint, not a stub), and **UPF**
`Nupf_EventExposure`+`Nupf_GetUEPrivateIPaddrAndIdentifiers` (ADR-0203 — also corrected a real
documentation bug: UPF's own file header had incorrectly claimed no `Nupf_*` API existed at all;
UPF's real primary control interface remains N4/PFCP per TS 23.501, unchanged), and **PCF** — all
7 Tier-A files across three slices: `Npcf_EventExposure`+`Npcf_UEPolicyControl` (ADR-0204),
`Npcf_AMPolicyAuthorization`+`Npcf_MBSPolicyAuthorization`+`Npcf_MBSPolicyControl` (ADR-0205), and
`Npcf_PDTQPolicyControl`+`Npcf_BDTPolicyControl` (ADR-0206), and **NEF** — all 13 of its own
Tier-A files across four slices: `Nnef_SMService`/`Nnef_UEId`/`Nnef_DNAIMapping`/`Nnef_EASDeployment`
(ADR-0207); `Nnef_SMContext`/`Nnef_Authentication`/`Nnef_ECSAddress` (ADR-0208 — also found and
fixed real cross-NF codegen name collisions this slice introduced into SMF/AMF/AUSF's own
already-shipping code, a new consequence class, disclosed in full in the ADR); `Nnef_EventExposure`
(ADR-0209 — also fixed a real `tools/sbi-codegen` allOf-narrowing limitation, and the fix's own
real, project-wide common-data-group-rename consequence, both disclosed in full in the ADR); and
`Nnef_TrafficInfluenceData`/`Nnef_Inference`/`Nnef_Training`/`Nnef_VFLInference`/`Nnef_VFLTraining`
(ADR-0210, task #164 now complete — no NEF Tier-A gaps remain). **Every NF's real Tier-A gaps are
now closed project-wide.** Tier-B closure has begun: **NRF** `OptionsNFInstances`+
`RetrieveStoredSearch`/`RetrieveCompleteSearch`+`RetrieveKeyRequest` (ADR-0211, the smallest
remaining Tier-B item), and **UDR**'s Individual Authentication Status (Document) +
`QueryProvisionedData` (ADR-0212 — also corrected a stale claim in the audit doc itself: a
previously-recorded "`subs-to-notify` bulk-DELETE" gap does not correspond to any real operation in
the spec, and that resource was already fully implemented), and **UDR**'s remaining
`Policy_Data.yaml` backlog in full (ADR-0213 — `ReadPolicyData` aggregate, Usage Monitoring
Information, `ReadBdtData` collection GET, and the `Policy_Data`-specific Policy Data
Subscriptions family). **UDR now has zero known Tier-B gaps.** UDM is now the only remaining real
Tier-B item project-wide; a background audit produced the first real per-operation breakdown
(`Nudm_UECM` ~24, `Nudm_SDM` ~33, `Nudm_PP` 11 already flagged deferred in ADR-0082) and closed
nine slices: **UDM** `Nudm_UEAU`'s 3 operations — `GetRgAuthData`, `GenerateAv` (real HSS
EPS/IMS/GBA-domain vectors, real `501` for the one branch needing a KDF not yet in
`libs/aka-crypto`), `GenerateGbaAv` (ADR-0214) — `Nudm_UECM`'s `PeiUpdate` +
`UpdateRoamingInformation` (ADR-0215), `Nudm_UECM`'s AMF non-3GPP-access registration group
(`Non3GppRegistration`/`GetNon3GppRegistration`/`UpdateNon3GppRegistration`, ADR-0216, mirrors the
already-built `amf-3gpp-access` group), `Nudm_UECM`'s SMSF registration groups (both
3GPP-access and non-3GPP-access, ADR-0217, sharing the identical `SmsfRegistration` schema),
`Nudm_UECM`'s IP-SM-GW registration resource (`IpSmGwRegistration`/`GetIpSmGwRegistration`/
`IpSmGwDeregistration`, ADR-0218 — genuinely no PATCH exists for this resource in the real spec
at all), `Nudm_UECM`'s NWDAF registration group
(`NwdafRegistration`/`GetNwdafRegistration`/`NwdafDeregistration`/`UpdateNwdafRegistration`,
ADR-0219 — real finding: no individual GET exists for a single NWDAF registration at all, only the
collection GET), `Nudm_UECM`'s `GetRegistrations` bare aggregate itself (ADR-0220 — composes
`RegistrationDataSets` from the six real per-group stores, gated by the required
`registration-dataset-names` query param; disclosed: `single-nssai`/`dnn` filtering of the
`SMF_PDU_SESSIONS` dataset not honored), `Nudm_UECM`'s `SendRoutingInfoSm` (ADR-0221 —
composes `RoutingInfoSmResponse` from the real SMSF/IP-SM-GW stores; disclosed: `smsRouter`/
`ipSmGwGuidance` never populated), and `Nudm_UECM`'s last three independent single ops —
`Trigger P-CSCF Restoration`, `GetLocationInfo`, `authTrigger` (ADR-0227 — `GetLocationInfo` is a
real, complete local composition from the already-stored AMF registration records; the other two
are real accept-and-validate operations with a disclosed non-relay to the serving AMF/AUSF) —
individual stubbed/missing operations within already-wired files, not a structural gap.
**`Nudm_UECM`'s entire Tier-B backlog is now fully closed.** `Nudm_SDM` closure has begun:
group A (`GetSmsData`/`GetSmsMngtData`/`GetTraceConfigData`/`GetLcsBcaData`, backed by UDR's own
individual provisioned-data routes) and group B (`GetLcsPrivacyData`/`GetLcsMoData`/
`GetLcsSubscriptionData`/`GetV2xData`/`GetProseData`/`GetMbsData`/`GetUcData`/`GetA2xData`/
`GetRangingSlPrivacyData`, backed only by UDR's bulk `ProvisionedDataSets` aggregate, extracted via
a new helper) closed together as a 13-operation slice (ADR-0228), then group C1 (`GetNSSAI`/
`GetUeCtxInAmfData`/`GetUeCtxInSmfData`/`GetUeCtxInSmsfData`/`GetEcrData`, real local composition
from UDM's own already-stored AMF/SMF/SMSF registration records or an existing UDR route) closed as
a 5-operation slice (ADR-0230), then `Modify` (real RFC 7396 merge-patch) closed
`Subscribe`/`Unsubscribe`/`Modify` in full (ADR-0232), then group C2
(`GetTimeSyncSubscriptionData`/`GetRangingSlPosData`) closed as a 2-operation slice (ADR-0233 —
a real, disclosed correction of ADR-0230's own scoping note: UDR already had both resources fully
live, only UDM-side wiring was needed). **`Nudm_SDM`'s entire group C is now fully closed.**
SOR/UPU/ack write ops, shared-data family, and identifier-lookup ops, plus `Nudm_PP` (11 ops),
remain open.

The full evidence base, current per-resource breakdown, and what's still open lives in
[`docs/CAPABILITY_GAP_ANALYSIS.md`](docs/CAPABILITY_GAP_ANALYSIS.md); the ADR trail (ADR-0075
onward) is in [`docs/DECISIONS.md`](docs/DECISIONS.md).

Standing engineering debt and what's left before carrier-grade status (ADR-0049) is tracked in
[`docs/DECISIONS.md`](docs/DECISIONS.md) and
[`docs/CAPABILITY_GAP_ANALYSIS.md`](docs/CAPABILITY_GAP_ANALYSIS.md).

## Repository layout

```
libs/sbi-core/     Shared SBI infrastructure: HTTP/2 server+client, OAuth2 client-credentials,
                   ProblemDetails, 3gpp-Sbi-* headers, structured logging, OpenTelemetry tracing.
                   Every NF links this; no NF includes another NF's private headers.
nfs/<nf>/          One independent binary + library per Network Function: nrf, amf, smf, udm, udr,
                   ausf, pcf, upf, chf, nssf, bsf, nef, scp, eir, smsf, gmlc, lmf. nfs/hello-nf is
                   a Phase 0 throwaway, not a real NF.
bss/<service>/     Standalone TM Forum ODA-layer services (not 3GPP NFs, no NRF registration):
                   product-catalog (TMF620), balance-management (TMF654), subscriber-management
                   (TMF632), roaming-interconnect (TMF651).
libs/bss-sid/      Shared TM Forum SID DTOs/mapping code bss/ services and nfs/chf link against.
libs/aka-crypto/   5G-AKA/EAP-AKA' crypto (Milenage, KDF) and real SUCI de-concealment (ECIES
                   Profile A/B, TS 33.501 Annex C) -- both independently verified against real,
                   officially-published 3GPP test vectors, not self-consistency tested only.
libs/pfcp-core/    N4/PFCP (TS 29.244) codec, shared by nfs/smf and nfs/upf.
libs/ss7-core/, libs/tcap-core/, libs/map-core/, libs/cap-core/, libs/diameter-core/
                   Legacy 2G/3G interconnect: M3UA+SCCP, TCAP, MAP, CAP (CAMEL), and Diameter
                   Gy/Rf -- real protocol codecs for the CHF/UDM roaming and legacy-HLR paths.
libs/tap3-core/    Real, hand-rolled GSMA TAP3 (TD.57) roaming-CDR BER codec, all 9 real
                   CallEventDetail variants.
libs/tbcd-core/    TBCD-STRING codec (TS 23.003), shared by the legacy-interconnect libs above.
tools/             Build-time tooling, including the OpenAPI-to-C++ codegen spine.
specs/             Vendored 3GPP/ETSI source material: R19 OpenAPI YAML (SBI API shapes), NGAP
                   ASN.1 (specs/NGAP), and spec PDFs for protocols with no YAML (PFCP, TS 33.501,
                   and several legacy TCAP/MAP/CAP TS documents). GSMA-member-confidential material
                   (the TAP3 spec libs/tap3-core cites) is deliberately NOT vendored here, even
                   though it's freely-published-equivalent material otherwise would be -- only real
                   cited facts in code comments, never the source document itself.
tests/             Integration and conformance tests.
docs/              DECISIONS.md (ADR log) and TRACEABILITY.md (procedure -> TS clause -> file -> test).
```

## Building

Requires CMake 3.28+, Ninja, a C++20/23 compiler (developed against GCC 13 and Clang 18), and
[vcpkg](https://github.com/microsoft/vcpkg) in manifest mode.

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics

cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer builds: add `-D5GC_ENABLE_ASAN=ON` or `-D5GC_ENABLE_TSAN=ON` at configure time (mutually
exclusive). CI runs both, plus `clang-format`/`clang-tidy`, on every push — see
[`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Contributing / working style

This project is built in small, reviewable increments — one NF or subsystem at a time, with the
TS 23.502 procedure list for each NF shown and approved before implementation. Every stub,
simplification, or non-conformant shortcut is called out explicitly rather than left for review to
discover. See [`CLAUDE.md`](CLAUDE.md) for the full engineering rules and mandated tech stack.

## License

Apache License 2.0 — see [`LICENSE`](LICENSE).
