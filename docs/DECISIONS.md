# Architecture Decision Records

Format: one entry per decision, in the order made. Rejected alternatives are recorded, not deleted,
per CLAUDE.md's working-style rules. See also `CLAUDE.md`'s "Project decisions (resolved at
kickoff)" section for the kickoff-time answers this log expands on.

---

## ADR-0001: Greenfield build, no fork of Open5GS or free5GC

**Date:** 2026-08-04
**Status:** Accepted

**Context:** PROMPT.md Section 2 flags build-vs-fork as the decision most likely to dominate this
project's economics, and asks for a build-vs-fork analysis as a first deliverable.

**Decision:** Build greenfield. Open5GS (C) and free5GC (Go) remain reference reading only.

**Rejected alternative:** Fork or structurally borrow from Open5GS/free5GC. Rejected per explicit
user instruction ("Greenfield from scratch"), given without requesting the comparative analysis
first.

**Consequence:** No shortcut on control-plane procedure implementations later; everything is
generated from the R19 YAML or hand-written against the spec text, per the source-of-truth rule.

---

## ADR-0002: Public repository, Apache-2.0 license

**Date:** 2026-08-04
**Status:** Accepted

**Context:** Brief mandates open source; hosting/license choice was left open.

**Decision:** Public GitHub repository, Apache-2.0.

**Rejected alternatives:**
- MIT: simpler but no explicit patent grant, which matters more here given likely corporate
  forks/contributors in a standards-adjacent project.
- AGPL-3.0: copyleft network-use clause is a poor fit for a reference implementation meant to be
  forked and embedded freely.

---

## ADR-0003: GoogleTest + gMock as the primary test framework

**Date:** 2026-08-04
**Status:** Accepted

**Context:** Mandated stack lists both GoogleTest and Catch2 without specifying which is primary.

**Decision:** GoogleTest + gMock for unit and integration tests (natural pairing; gMock only works
with GoogleTest).

**Rejected alternative:** Catch2 as primary. No strong reason against it, but GoogleTest + gMock's
mocking integration is more valuable given how much of this codebase is SBI clients/servers that
will want mocked peers in unit tests. Catch2 was initially left in the vcpkg manifest for later
BDD-style use, then dropped during Task 8 (build validation) because its vcpkg port needs
`pkg-config`, which isn't installed and nothing yet uses Catch2 -- re-add when a real test wants
it rather than carrying an unused dependency.

---

## ADR-0004: Hand-rolled HTTP/2 server on raw nghttp2 + Boost.Asio

**Date:** 2026-08-04
**Status:** Accepted

**Context:** The mandated stack lists "nghttp2 (core), Boost.Beast or Pistache for the service
layer." In practice: vcpkg's `nghttp2` port builds only the core C library, not upstream's bundled
`asio_http2` ("nghttp2-asio") convenience wrapper -- it isn't packaged at all. Neither Boost.Beast
(HTTP/1.1 + WebSocket only) nor Pistache (HTTP/1.1 only) implement HTTP/2. Since the non-negotiable
rules require "100% of SBI traffic is HTTP/2," something has to actually speak the protocol.

**Decision:** `libs/sbi-core`'s `http2::Server` drives nghttp2's raw C session API
(`nghttp2_session_server_new`, `nghttp2_session_mem_recv`/`mem_send`, header/frame/data callbacks)
directly, with Boost.Asio supplying the TCP accept/read/write loop. See
`libs/sbi-core/src/http2_server.cpp`.

**Rejected alternative:** Add Drogon (MIT, in vcpkg, has built-in HTTP/2 support) as the server
framework. Would have cut implementation effort significantly, but deviates from the brief's
mandated stack without being asked for; user chose the raw-nghttp2 path specifically to avoid an
undisclosed dependency substitution.

**Consequence:** More code to review and maintain than a framework would need, and it's the
highest-risk hand-written piece in Phase 0 -- stream lifecycle and flow-control bugs here would be
subtle. Flagged explicitly for extra scrutiny during build/test (see PHASE0-NOTES below).

---

## ADR-0005: h2c (cleartext) only for Phase 0, no TLS/mTLS yet

**Date:** 2026-08-04
**Status:** Accepted (temporary; revisit before Phase 2 talks to anything but stub-nrf)

**Context:** The non-negotiable rules mandate OpenSSL 3.x with TLS 1.3 and mTLS for real SBI
traffic. Standing up a lab PKI (CA, per-NF certs, mTLS validation) is real scope on its own and
wasn't needed to prove Phase 0's actual goal: that the hand-rolled HTTP/2 transport and OAuth2 flow
work end-to-end.

**Decision:** `http2::Server` and `http2::Client` speak h2c (HTTP/2 cleartext, "prior knowledge")
only. hello-nf <-> stub-nrf runs entirely over plaintext TCP.

**Consequence -- explicitly non-conformant:** This is a real gap against TS 33.501 and the
non-negotiable rules, not a hidden one. It must be closed with real TLS 1.3 + mTLS (ALPN
negotiating "h2") before any NF-to-NF traffic beyond the Phase 0 stub is considered done. Tracked
as follow-up work for Phase 2.

---

## ADR-0006: Synchronous libcurl calls for the HTTP/2 client (Phase 0)

**Date:** 2026-08-04
**Status:** Accepted (temporary)

**Context:** libcurl's easy interface is blocking by default; integrating it with Boost.Asio's
event loop properly means driving `curl_multi_socket_action` from Asio's reactor.

**Decision:** `http2::Client::send()` blocks the calling thread via `curl_easy_perform`. Fine for
hello-nf, which has nothing else to do while waiting.

**Consequence:** Not viable for a real NF handling concurrent inbound SBI requests while also
making outbound calls -- that would stall the server's io_context if called from the same thread.
Revisit (curl_multi + Asio integration, or a dedicated client thread pool) before any Phase 2 NF
needs to make outbound calls while serving inbound ones.

---

## ADR-0007: ProblemDetails/InvalidParam hand-written in sbi-core, not generated

**Date:** 2026-08-04
**Status:** Accepted (disclosed, deliberate exception to "never hand-write a DTO")

**Context:** `sbi-core` (Phase 0) is itself a dependency of `tools/sbi-codegen` (Phase 1) -- there
is no codegen pipeline yet to generate anything from. But `ProblemDetails` (TS 29.500 clause 5.2.7
generic error handling) is needed by sbi-core's own server/client error paths.

**Decision:** `sbi_core::ProblemDetails` and `sbi_core::InvalidParam` are hand-written in
`libs/sbi-core/include/sbi_core/problem_details.hpp`, with every field name transcribed verbatim
from `specs/5G_APIs-REL-19/TS29571_CommonData.yaml` (commit `bca84b6`, lines 518-559 and 615-630).
Three fields that reference other files' schemas (`accessTokenError`, `accessTokenRequest`,
`noProfileMatchInfo`) are carried as opaque `nlohmann::json` rather than fully modeled, since fully
modeling them would mean also generating the NRF access-token/discovery schemas early.

**Consequence:** Once Phase 1's codegen exists, generated NF code should depend on and reuse
`sbi_core::ProblemDetails` rather than regenerating a second copy from `CommonData.yaml` --
otherwise there would be two competing definitions of the same wire type.

---

## ADR-0008: CI split into build / sanitize / lint jobs on free GitHub-hosted runners

**Date:** 2026-08-04
**Status:** Accepted

**Context:** User chose free-tier CI compute (no self-hosted runners) and small-increment cadence.

**Decision:** Three jobs: `build` (plain Debug build + ctest), `sanitize` (matrix: ASan+UBSan vs.
TSan, kept as two separate configs since they're mutually exclusive at compile time), `lint`
(clang-format --dry-run, clang-tidy). vcpkg binary caching via `actions/cache` to keep runtimes
reasonable on the free tier.

**Rejected alternative:** A libFuzzer job. Deferred -- there's no codec worth fuzzing yet; the
first real target is the PFCP codec in Phase 3.

---

## PHASE0-NOTES: build validation outcome

Resolved during the "build and run locally" pass (2026-08-04), g++ 13.3 / vcpkg baseline
`f1d4bbc7`, against the actual installed package versions:

- **nghttp2 C API** (nghttp2 1.69.0): the classic API written against
  (`nghttp2_session_mem_recv`/`mem_send`, `nghttp2_data_provider`, `nghttp2_submit_response`)
  compiled and ran correctly as-is -- no `*2`/`nghttp2_ssize` migration needed.
- **nghttp2 CMake integration**: wrong on the first attempt. vcpkg's `nghttp2` port ships only a
  pkg-config file (`libnghttp2.pc`), not a CMake config -- `find_package(nghttp2 CONFIG REQUIRED)`
  / `nghttp2::nghttp2` do not exist. Fixed to `pkg_check_modules(nghttp2 REQUIRED IMPORTED_TARGET
  libnghttp2)` / `PkgConfig::nghttp2` in `libs/sbi-core/CMakeLists.txt`. See ADR-0004.
- **OpenTelemetry C++ SDK** (opentelemetry-cpp 1.28.0): header paths and factory API
  (`TracerProviderFactory`, `OtlpHttpExporterFactory`, `OStreamSpanExporterFactory`,
  `SimpleSpanProcessorFactory`/`BatchSpanProcessorFactory`) were correct as written. One real bug:
  constructing `opentelemetry::nostd::shared_ptr<TracerProvider>` directly from the
  `std::unique_ptr` returned by `TracerProviderFactory::Create` is ambiguous (two applicable
  constructor overloads). Fixed in `otel.cpp` by routing through an intermediate
  `std::shared_ptr<TracerProvider>` first.
- **Other vcpkg CMake target names** used in `libs/sbi-core/CMakeLists.txt` --
  `opentelemetry-cpp::trace`, `opentelemetry-cpp::ostream_span_exporter`,
  `opentelemetry-cpp::otlp_http_exporter`, `tl::expected`, `nlohmann_json::nlohmann_json`,
  `spdlog::spdlog`, `CURL::libcurl`, `Boost::system` -- all resolved correctly, no changes needed.
- **vcpkg manifest**: `catch2` and (transitively) `openssl` need `pkg-config` on the build host to
  compile from source; wasn't installed initially. `catch2` was dropped from the manifest instead
  (unused -- see amended ADR-0003); `pkg-config` was installed for `openssl`, which is a real
  dependency and couldn't be dropped.
- **Result**: full build green (`sbi_core`, `stub-nrf`, `hello-nf`, `integration_tests`), the
  `tests/integration` GoogleTest suite passes, and a manual run confirms the real behaviour: hello-nf
  obtains an OAuth2 token, registers (HTTP 201), heartbeats (HTTP 200), deregisters (HTTP 204), all
  over actual h2c HTTP/2 through the hand-rolled nghttp2 server, with OTel spans emitted for the
  register/heartbeat steps.
- **Not yet clean**: `clang-tidy` (now runnable locally, clang-18 installed) reports ~40 style-only
  warnings across the hand-written files -- `readability-identifier-length` (short names like `ms`,
  `it`, `pd`), `misc-include-cleaner` (missing direct includes for transitively-available symbols),
  `misc-const-correctness`, one `google-build-using-namespace`, one `bugprone-exception-escape` on
  each `main()` (no top-level try/catch around the NF lifecycle). None are errors --
  `WarningsAsErrors` is empty and CI's lint job won't fail on them -- but they're real, undone
  cleanup, not silently ignored. Left for an incremental pass rather than gold-plating Phase 0
  further before Phase 1 starts.
