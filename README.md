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

[![CI](https://github.com/prajithparan/5gc-r19/actions/workflows/ci.yml/badge.svg)](https://github.com/prajithparan/5gc-r19/actions/workflows/ci.yml)
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
| 3 | User plane: N4/PFCP, UPF datapath | Control plane done + live-verified (Stage 0-3: PFCP codec, UPF registers with NRF and answers Heartbeat/Association Setup/Session Establishment, SMF discovers UPF via real Nnrf_NFDiscovery, establishes a real Sx Association, and creates a real N4 session as part of every PDU Session Establishment). Datapath (Stage 4, eBPF/XDP): the real XDP program now **passes the BPF verifier** and real PFCP Session Establishment genuinely registers TEIDs in the live BPF map (both live-verified 2026-08-09, two real bugs found and fixed along the way) — actual packet decapsulation-and-delivery is still unconfirmed, blocked on an ARP resolution issue between the datapath's veth pair not yet root-caused; see `docs/DECISIONS.md` ADR-0043 |
| 4 | Charging + TM Forum SID/BSS layer | Not started |
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
libs/sbi-core/    Shared SBI infrastructure: HTTP/2 server+client, OAuth2 client-credentials,
                  ProblemDetails, 3gpp-Sbi-* headers, structured logging, OpenTelemetry tracing.
                  Every NF links this; no NF includes another NF's private headers.
nfs/<nf>/         One independent binary + library per Network Function. nfs/stub-nrf and
                  nfs/hello-nf are Phase 0 throwaways proving the transport works end-to-end,
                  not real NFs.
tools/            Build-time tooling, including the OpenAPI-to-C++ codegen spine.
specs/            Vendored 3GPP source material: R19 OpenAPI YAML (SBI API shapes), NGAP ASN.1
                  (specs/NGAP), and the PFCP spec text (specs/PFCP) for protocols with no YAML.
tests/            Integration and conformance tests.
docs/             DECISIONS.md (ADR log) and TRACEABILITY.md (procedure -> TS clause -> file -> test).
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
