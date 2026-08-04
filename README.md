# 5gc-r19

A modular, standards-faithful 5G Core (5GC) implementation in modern C++, targeting 3GPP
**Release 19 (5G-Advanced)**. Every Network Function's northbound API is meant to be **generated**
from the official 3GPP OpenAPI YAML — never hand-written — with a TM Forum SID-aligned
charging/BSS domain, a JSON-schema-driven operator GUI, and AI/ML pipelines wired into NWDAF.

This is a **lab-grade, spec-traceable reference implementation** of a meaningful subset of a 5GC —
useful for labs, demos, and interop experiments. It is explicitly not, and does not aim to be, a
production carrier core. See [`docs/DECISIONS.md`](docs/DECISIONS.md) for why, and for every
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
| 1 | Codegen spine: `tools/sbi-codegen`, generated DTOs/serializers from the R19 YAML | In progress |
| 2 | Control-plane core: NRF, AMF, SMF, UDM, UDR, AUSF, PCF | Not started |
| 3 | User plane: N4/PFCP, UPF datapath | Not started |
| 4 | Charging + TM Forum SID/BSS layer | Not started |
| 5 | NWDAF + AI/ML pipelines | Not started |
| 6 | R19 feature NFs (AIOTF, 5MBS, SEPP, ...) | Not started |
| 7 | GUI / operations console | Not started |
| 8 | Lab packaging (`make lab-up`) | Not started |

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
specs/            Vendored 3GPP R19 OpenAPI YAML (source of truth for all API shapes).
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
