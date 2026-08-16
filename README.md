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
| 0 | Foundations: CMake+vcpkg skeleton, `libs/sbi-core` (HTTP/2, OAuth2, ProblemDetails, headers, logging, tracing), hello-nf/stub-nrf proof of concept | Done |
| 1 | Codegen spine: `tools/sbi-codegen`, generated DTOs/serializers from the R19 YAML | Done |
| — | TLS 1.3 + mTLS across `libs/sbi-core` (closes ADR-0005, required before Phase 2 per ADR-0009) | Done |
| 2 | Control-plane core: NRF, AMF, SMF, UDM, UDR, AUSF, PCF; UE registration + PDU session establishment end-to-end | Done |
| 3 | User plane: N4/PFCP, UPF datapath | **Done, fully live-verified** (Stage 0-3: PFCP codec, UPF registers with NRF and answers Heartbeat/Association Setup/Session Establishment, SMF discovers UPF via real Nnrf_NFDiscovery, establishes a real Sx Association, and creates a real N4 session as part of every PDU Session Establishment. Stage 4, eBPF/XDP: the real XDP program passes the BPF verifier, real PFCP Session Establishment registers TEIDs in the live BPF map, and — after root-causing a same-network-namespace overlapping-route bug that was breaking ARP — a real GTP-U packet carrying a genuinely PFCP-allocated TEID is decapsulated and delivered to the TUN device end to end). See `docs/DECISIONS.md` ADR-0043 |
| 4 | Charging + TM Forum SID/BSS layer | CHF live-verified end to end for the full charging lifecycle: `Nchf_ConvergedCharging_Create`/`_Update`/`_Release` wired to real SMF triggers, real quota-consumption tracking/re-authorization (UPF eBPF/XDP usage measurement → SMF PFCP Session Report → CHF re-authorization → SMF PFCP Session Modification back into the live datapath, ADR-0050), real rating against real seeded product-catalog data. Real legacy-interconnect breadth added: SS7 M3UA+SCCP, TCAP, MAP (`insertSubscriberData`), CAP (CAMEL charging), and a Diameter Gy/Rf server — all live-verified, not just unit-tested (P4.5, ADR-0059–ADR-0065). Real TM Forum BSS layer: `bss/product-catalog` (TMF620), `bss/balance-management` (TMF654), `bss/subscriber-management` (TMF632), `bss/roaming-interconnect` (TMF651) — four standalone services, all real PostgreSQL-backed, live-verified over mTLS (ADR-0047/ADR-0056/ADR-0066). Real GSMA TAP3 roaming-CDR codec (`libs/tap3-core`, hand-rolled BER per the real spec, all 9 `CallEventDetail` variants) wired into `RoamingCdrFileStore` (ADR-0067). A real, ongoing gap-closure effort against free5GC/open5gs' own actual source (not just their docs) is adding real UDR PostgreSQL persistence, real UDM↔UDR wiring, and real SUCI de-concealment (ECIES Profile A/B per TS 33.501 Annex C, independently verified against the spec's own official test vectors) — see `docs/DECISIONS.md` ADR-0068–ADR-0070 onward for what's closed so far. `docs/CHARGING_MAPPING.md` covers the SID/BSS field mapping; real, disclosed gaps (no forwarding stop on quota exhaustion, no VOLTH/VOLQU trigger differentiation, several SID/TMF entities still deferred) are itemized per-ADR, not hidden |
| 5 | NWDAF + AI/ML pipelines | Not started |
| 6 | R19 feature NFs (AIOTF, 5MBS, SEPP, ...) | Not started |
| 7 | GUI / operations console | Not started |
| 8 | Lab packaging (`make lab-up`) | Not started |

All 7 Phase 2 NFs are implemented, and both target procedures (TS 23.502 §4.2.2.2.2 UE
Registration, §4.3.2.2.1 PDU Session Establishment) are wired end-to-end in both directions — real
NGAP/N2 (SCTP + ASN.1 PER) and NAS-5GS, through AUSF/PCF/SMF, no narrowed slice — and verified in a
single real `nr-gnb`/`nr-ue` interop run, first attempt, with zero retries or failures anywhere in
the procedure: NG Setup → Initial Registration (including a real SQN resynchronization,
TS 33.102 §6.3.3) → SecurityModeCommand/Complete → RegistrationAccept → a real AM Policy
Association with PCF → a real PDU Session Establishment Request → a real SM context with SMF → a
real PDU Session Establishment Accept, built from PCF's actual QoS decision and delivered back to
the UE via `Namf_Communication`'s `N1N2MessageTransfer`. `nr-ue`'s own log: `PDU Session
Establishment Accept received` → `PDU Session establishment is successful` — see
`docs/DECISIONS.md` ADR-0032–ADR-0038 and `docs/TRACEABILITY.md` for exactly what was proven how.

Full phase plan: [`PROMPT.md`](PROMPT.md). Per-procedure spec traceability:
[`docs/TRACEABILITY.md`](docs/TRACEABILITY.md).

## Repository layout

```
libs/sbi-core/     Shared SBI infrastructure: HTTP/2 server+client, OAuth2 client-credentials,
                   ProblemDetails, 3gpp-Sbi-* headers, structured logging, OpenTelemetry tracing.
                   Every NF links this; no NF includes another NF's private headers.
nfs/<nf>/          One independent binary + library per Network Function: nrf, amf, smf, udm, udr,
                   ausf, pcf, upf, chf. nfs/hello-nf is a Phase 0 throwaway, not a real NF.
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
