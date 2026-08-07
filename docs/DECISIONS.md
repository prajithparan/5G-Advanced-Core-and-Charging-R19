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
**Status:** Superseded by ADR-0011 (2026-08-04) -- real TLS 1.3 + mTLS is now in place.

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

## ADR-0009: Target raised from lab-grade to production-grade

**Date:** 2026-08-04
**Status:** Accepted

**Context:** PROMPT.md Section 2 and CLAUDE.md's original goal statement both scoped this project
as "lab-grade... not a carrier-grade core," and CLAUDE.md's own Reality Check section still says
so. The user edited CLAUDE.md's goal line to "production-grade" and, when the resulting
inconsistency (production-grade goal vs. still-lab-grade Reality Check, vs. PROMPT.md's original
framing) was flagged per CLAUDE.md's own "if this file and PROMPT.md disagree, treat that as a
bug" rule, confirmed the change is deliberate: the real target is production-grade, not lab-grade.

**Decision:** The project's quality bar is raised to production-grade. CLAUDE.md's Reality Check
section is updated to match (see that section's amended text) rather than left contradicting the
goal statement.

**Immediate consequence -- work already built must be revisited:**
- **ADR-0005 (h2c only, no TLS/mTLS)** was justified as "an acceptable Phase 0 lab
  simplification." Under a production-grade bar it is not acceptable as a final state -- real
  TLS 1.3 + mTLS (per TS 33.501 and the non-negotiable engineering rules) must be added before
  Phase 2 NFs talk to anything beyond the throwaway stub-nrf. Not fixed in this ADR; tracked as
  required follow-up, first candidate being before/during Phase 2's NRF work since NRF is the
  trust anchor every other NF's OAuth2 flow depends on.
- **stub-nrf's unsigned, fake OAuth2 token** similarly stops being acceptable once real NFs need
  to validate tokens against it -- the real NRF (Phase 2) must issue and NFs must validate
  properly signed JWTs.
- **ADR-0006 (synchronous libcurl client)** and the ~40 outstanding `clang-tidy` style warnings
  noted in PHASE0-NOTES below become real technical debt against a production bar, not
  acceptable-forever lab shortcuts. Not urgent individually, but should not accumulate further
  without a plan to close them.
- Every future phase's Definition of Done should be read as production-grade from here forward:
  full TS 23.502 procedure coverage (not "mandatory only" read loosely), real conformance testing,
  and no silent gaps -- consistent with CLAUDE.md's existing "flag stubs/simplifications honestly"
  rule, which does not change, only the bar for what's an acceptable *permanent* state does.

**Rejected alternative:** Revert CLAUDE.md's goal line back to "lab-grade" to match PROMPT.md and
avoid the rework above. Rejected -- user explicitly confirmed production-grade is the intended
target after the conflict was surfaced.

**Note:** PROMPT.md itself is left unedited as the historical record of the original brief;
CLAUDE.md is the living document and is what future sessions should treat as authoritative on this
point going forward.

---

## ADR-0010: Custom Jinja2 generator for `tools/sbi-codegen`, not openapi-generator

**Date:** 2026-08-04
**Status:** Accepted

**Context:** Phase 1 requires evaluating openapi-generator's C++ targets against a custom Jinja
generator on three pilot files (`TS29510_Nnrf_NFManagement.yaml`, `TS29518_Namf_Communication.yaml`,
`TS29571_CommonData.yaml`) and recommending one with evidence. Correction to PROMPT.md's own text:
current openapi-generator has no `cpp-restsdk` *server* target -- `cpp-restsdk` is client-only.
Available C++ server targets: `cpp-httplib-server`, `cpp-oatpp-server`, `cpp-pistache-server`,
`cpp-qt-qhttpengine-server`, `cpp-restbed-server`(-deprecated). `cpp-pistache-server` was used as
openapi-generator's representative since PROMPT.md named Pistache explicitly.

**Framework HTTP/2 support (checked, not assumed):** none of Pistache, cpp-httplib, restbed, or
oatpp advertise HTTP/2 in their vcpkg port descriptions, and this matches their known designs
(Pistache, cpp-httplib, restbed are HTTP/1.1-only by construction). This reinforces ADR-0004: no
openapi-generator C++ server target can replace `libs/sbi-core`'s hand-rolled HTTP/2 server, so
codegen server/route scaffolding is out of scope regardless of which model-generation approach
wins -- only the DTO/serializer layer is a real candidate for adoption.

**openapi-generator model output -- concrete defects found by actually compiling it:**
1. **anyOf-open-enum (3GPP's pervasive `anyOf: [{enum:[...]}, {type: string}]` pattern, e.g.
   `NFType`, `NFStatus`) generates a completely empty class.** `NFType.h`/`.cpp` from
   `cpp-pistache-server` has no data member; `to_json` emits `{}`; and `operator==`'s body is a bare
   `return;` in a `bool`-returning function -- **confirmed with `g++ -std=c++20 -fsyntax-only`: this
   does not compile** (`error: return-statement with no value, in function returning 'bool'`). This
   pattern covers dozens of types across the R19 API surface, not an edge case.
2. **Pattern/format constraints are silently dropped.** `Fqdn`'s regex pattern
   (`TS29571_CommonData.yaml`) and `Ipv6Addr`'s `allOf`-combined patterns have zero enforcement in
   generated code -- `grep -rl "std::regex" model/` finds regex usage only in the generator's own
   hardcoded RFC 3339 date/date-time helper, nowhere else.
3. **Cross-file `$ref` duplication.** Generating `TS29510_Nnrf_NFManagement.yaml` and
   `TS29518_Namf_Communication.yaml` independently (as real per-NF codegen invocations would)
   produces 84 duplicate files (42 types x .h+.cpp) for shared `TS29571_CommonData.yaml`-derived
   types (`PlmnId`, `Guami`, `InvalidParam`, `AccessTokenReq`, ...) -- an ODR risk if ever linked
   together, and definite drift risk if maintained per-NF.
4. No `discriminator:` usage exists in the three pilot files (checked, not assumed absent) --
   openapi-generator's polymorphism handling was not evaluated since there is nothing to evaluate
   it against here.
5. `nullable` fields use an `...IsSet` boolean-flag pattern rather than `std::optional`, which is
   functional (not a defect), just a different convention than this project's `std::optional`-based
   `sbi_core::put_optional`/`get_optional` helpers already in use in `libs/sbi-core`.

**Decision:** Build `tools/sbi-codegen` as a custom Python + Jinja2 generator
(`tools/sbi-codegen/`), not a wrapper around openapi-generator. It:
- Resolves `$ref` (internal and cross-file) into a single schema registry so a shared type (e.g.
  `Guami`) is generated exactly once regardless of how many pilot/NF files reference it, eliminating
  defect #3 above.
- Represents the anyOf-open-enum pattern as a plain-`std::string`-backed struct with named
  `static inline const std::string` constants for known values -- any value (known or not)
  round-trips correctly, fixing defect #1. Proven in `tests/conformance/test_round_trip.cpp`
  (`NFTypeUnknownValueRoundTrips`) with a value that is not in the known-enum list.
  See `libs/sbi-core/include/sbi_core/sbi_headers.hpp`.
- Enforces `pattern` (direct or `allOf`-combined) via a generated `validate_<Type>(const
  std::string&)` using `std::regex`, fixing defect #2. Proven in `tests/conformance/test_round_trip.cpp`
  (`AmfIdPatternValidation`) with both an accepted and a rejected value.
- Uses `std::optional<T>` for `nullable`/non-required fields via the existing
  `sbi_core::put_optional`/`get_optional` helpers (`json_serde.hpp`), collapsing the
  absent-vs-explicit-null distinction into one state -- a disclosed simplification (most real-world
  OpenAPI-to-C++ generators do the same; a true tri-state would need
  `std::optional<std::optional<T>>` or an explicit sentinel, not worth the complexity for a Phase 1
  prototype).
- Falls back to `using Name = nlohmann::json;` (opaque passthrough) for schema shapes not
  confidently modeled (inline-composed properties, non-string enums, unrecognized `allOf` member
  shapes) rather than guessing. Across the full transitive closure of the three pilot files (1076
  types, 22 source YAML files pulled in transitively -- 3GPP's own schemas are far more
  interconnected than "three pilot files" suggests), **114 types (10.6%) are opaque fallbacks**;
  the rest (583 objects, 193 aliases, 186 open-enums) are fully typed. This ratio is reported
  honestly, not hidden.

**A genuine structural finding, not a codegen bug:** the three pilot files' transitive `$ref`
closure forms one large strongly-connected component spanning 22 of the 33 initially-encountered
source files (`TS29571_CommonData.yaml` <-> `TS29510_Nnrf_AccessToken.yaml` <->
`TS29520_Nnwdaf_EventsSubscription.yaml` <-> ... ). A first, naive one-header-per-source-file version
of this generator hit real "not declared in this scope" compile errors from `#pragma once` silently
no-op'ing a re-entrant `#include` before the needed type was defined -- a genuine circular
C++-header dependency caused by a genuine circular schema dependency in 3GPP's own YAML, not
something either generator invented. Fixed by computing strongly-connected components (Tarjan's
algorithm, `render.py`) over the file dependency graph and emitting **one merged header per SCC**,
with types topologically sorted *within* the merged group by field-level dependency. Result: 11
output file-groups instead of 33, one of which (`TS29122_CommonData_grp.hpp`) is a 546KB file
covering all 22 mutually-dependent source files with a citation block listing all of them.
**Trade-off, disclosed:** this means a change to any one of those 22 files' schemas forces
recompilation of everything in the merged group -- acceptable for a Phase 1 prototype proving
feasibility, but worth revisiting (e.g. finer-grained per-type headers with forward declarations
where the field is used via reference/pointer) if build-time incrementality becomes a real pain
point once more NFs are generated in Phase 2+.

**Four more real bugs found only by actually compiling the generated output** (not by inspection --
each was caught by `g++ -fsyntax-only` on the real generated files, then fixed and re-verified):
- Multi-line 3GPP YAML descriptions (common -- long prose paragraphs with embedded quotes and
  clause references like "3GPP TS 23.003") only had their first line `//`-commented; subsequent
  lines leaked as raw uncommented text into the file. Fixed with a `cppcomment` Jinja filter that
  prefixes every line.
- JSON field names that legitimately start with a digit (`5qi`, `5gMmCapability` -- real 5G QoS
  Identifier field names) are invalid C++ identifiers. Fixed with an `n`-prefix shim
  (`_cpp_field_name`) that only affects the emitted C++ member name, not the JSON wire name.
- The same issue for *type* names (`5GDdnmfInfo`, `2G3GLocationArea` are real schema names), plus
  `and`/`or`/`not`/etc. (C++'s alternative-operator keywords, easy to forget) colliding with real
  field names. Fixed with a shared `cpp_type_name`/reserved-word sanitizer applied consistently at
  both a type's declaration and every reference to it.
- A field whose name is identical to its own type's name (e.g. field `Snssai` of type `Snssai`,
  observed in `SmallDataRateStatusInfo`) compiles but triggers `-Wchanges-meaning` and genuinely
  breaks subsequent name lookup within the same struct once GCC's strict mode is considered. Fixed
  by appending `_` to a field name when it collides with its own bare type name.

**Rejected alternative:** Use openapi-generator for the model/DTO layer specifically (since server
scaffolding was already ruled out by the HTTP/2 finding above), patching its Mustache templates to
fix the anyOf-open-enum bug. Rejected: openapi-generator is a Java tool with its own template
release cadence; maintaining a fork of its templates long-term is a comparable or larger maintenance
burden than owning a ~600-line Python generator we already have full visibility and control over,
and the duplication problem (defect #3) is architectural to openapi-generator's per-invocation model
generation, not fixable via template patches alone.

**Consequence:** `tools/sbi-codegen/generate.py` is wired into the CMake build
(`libs/sbi-generated/CMakeLists.txt`) as a configure-time + rebuild-time step (re-runs when the
generator, its templates, or the pilot YAML change; adding/removing a pilot file needs a
re-configure for `file(GLOB)` to notice new output files -- a known, disclosed CMake limitation of
globbing dynamically generated sources). Requires `python3` with `jinja2`/`pyyaml` on `PATH` --
**not** vcpkg-managed, since there is no vcpkg C++ substitute for a Python code generator; CI
installs these explicitly. This generator's ~10.6% opaque-fallback rate and disclosed
nullable-collapsing simplification are expected to shrink incrementally as Phase 2+ NF work
surfaces schema shapes not yet handled -- it is deliberately not attempting to be a complete,
general-purpose OpenAPI-to-C++ compiler on day one.

---

## ADR-0011: Real TLS 1.3 + mTLS, closing ADR-0005's gap

**Date:** 2026-08-04
**Status:** Accepted

**Context:** ADR-0009 raised the project's target to production-grade, which made ADR-0005's h2c-
only transport explicitly unacceptable as a permanent state, and the user confirmed TLS/mTLS must
land before Phase 2's NRF work starts (not retrofitted after).

**Decision:**
- **Lab PKI**: `scripts/gen-lab-pki.sh` generates a self-signed root CA and one leaf cert per NF
  (EC P-256/prime256v1, SHA-256, 365-day validity), each with **both** `serverAuth` and
  `clientAuth` EKU since every NF is simultaneously an SBI server and client. SAN covers
  `127.0.0.1`/`localhost`/the NF's own name. Output to `certs/` at repo root (gitignored --
  regenerable dev material, not secrets worth version-controlling); the script itself is
  committed.
- **`http2::Server`**: the accepted TCP socket is wrapped in `boost::asio::ssl::stream`, using an
  `ssl::context::tlsv13_server` context (method-table-level restriction to TLS 1.3, not just an
  option flag). Loads the NF's cert+key, sets `verify_peer | verify_fail_if_no_peer_cert` against
  the configured CA (real mTLS enforcement), and installs an ALPN select callback
  (`SSL_CTX_set_alpn_select_cb` + `SSL_select_next_proto`) that accepts only `h2` --
  `SSL_TLSEXT_ERR_ALERT_FATAL` on anything else, no HTTP/1.1 or no-protocol fallback.
  `TlsConfig{cert_path, key_path, ca_path}` (shared with the client, `tls_config.hpp`) is a
  required constructor argument, not optional/defaulted -- a caller cannot construct an
  unauthenticated or unencrypted `Server` by omission; the constructor throws if any path is
  missing or the material fails to load.
- **`http2::Client`**: almost entirely libcurl configuration -- `CURLOPT_SSLVERSION` pinned to
  `CURL_SSLVERSION_TLSv1_3`, `CURLOPT_SSLCERT`/`SSLKEY` (client cert for mTLS),
  `CURLOPT_CAINFO` (verify server), `CURLOPT_SSL_VERIFYPEER`/`VERIFYHOST` left on. HTTP version
  switched from `CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE` (h2c) to `CURL_HTTP_VERSION_2TLS` (ALPN-
  negotiated over TLS).
- **stub-nrf/hello-nf**: both load their own cert/key + the shared CA (paths baked in at compile
  time via `CERTS_DIR` -- see below), URLs switched to `https://`.

**Proof, not assertion:**
- `openssl s_client -connect 127.0.0.1:7777 -alpn h2 -tls1_3 -CAfile certs/ca/ca.crt` **without**
  a client cert: server sends `tlsv13 alert certificate required` -- mTLS is actually enforced,
  not just configured and silently unchecked.
- Same command **with** `-cert certs/hello-nf/cert.pem -key certs/hello-nf/key.pem`: succeeds,
  `New, TLSv1.3, Cipher is TLS_AES_256_GCM_SHA384`, `ALPN protocol: h2`, `Verify return code: 0
  (ok)`.
- `tests/integration/test_hello_nf_registration.cpp` (real subprocess-to-subprocess, not mocked)
  passes: hello-nf obtains an OAuth2 token, registers (201), heartbeats (200), deregisters (204),
  all over the real TLS 1.3 + mTLS handshake above.

**Disclosed simplifications / known gaps, not hidden:**
- **No graceful TLS shutdown**: `Connection::close()` closes the underlying TCP socket directly
  rather than performing `async_shutdown` (TLS `close_notify`). A fully correct version needs its
  own timeout handling to avoid an unresponsive peer leaking the connection indefinitely; that
  timeout machinery wasn't built. Every current `close()` call site is a teardown/error path, not
  a graceful reuse handoff, so the practical impact today is limited -- but this is real,
  disclosed debt, not fixed.
- **Cert paths are compile-time absolute paths** (`CERTS_DIR` baked in via
  `target_compile_definitions` from `CMAKE_SOURCE_DIR`), not a runtime config system. Acceptable
  for dev/lab binaries run in-place; will need real config (env vars, a config file, whatever
  Phase 2's NF config story ends up being) once these aren't just Phase 0 throwaways.
- **No cert rotation/revocation** (no CRL/OCSP checking, no rotation tooling) -- single static lab
  CA and leaf certs, regenerate-and-restart is the only "rotation" story right now.
- **stub-nrf's OAuth2 token is still unsigned** (ADR-0009 already flagged this separately) --
  TLS/mTLS secures the transport; it doesn't address token signature validation, which is
  unrelated and still Phase 2 NRF work.

**Rejected alternative:** Defer TLS/mTLS until Phase 2, retrofitting it once NRF exists. Rejected
per explicit user decision when the sequencing question was asked directly -- TLS/mTLS first so
Phase 2 starts on solid transport rather than needing a retrofit across whatever NF code lands in
the meantime.

**Note:** the "over actual h2c HTTP/2" phrasing in ADR-0008's PHASE0-NOTES appendix below describes
the Phase 0 build-validation run at the time it was written and is left as a historical record, not
updated to match current behavior -- the transport it describes has since changed per this ADR.

---

## ADR-0012: jwt-cpp + ES256 for NRF's OAuth2 token issuance/verification

**Date:** 2026-08-05
**Status:** Accepted

**Context:** `TS29510_Nnrf_AccessToken.yaml`'s `AccessTokenClaims` schema (lines 361-406) requires
`iss`, `sub`, `aud`, `scope`, `exp` -- real signed JWTs, not stub-nrf's Phase 0 unsigned placeholder
(ADR-0009 already flagged that as needing to close). Needed: a JWS/JWT library.

**Decision:** `jwt-cpp` (header-only, MIT, vcpkg-available, uses OpenSSL under the hood -- no new
crypto backend). Algorithm: ES256 (ECDSA P-256), not RS256. The lab PKI
(`scripts/gen-lab-pki.sh`) already generates P-256 keys for every NF's mTLS cert, so ES256 keeps
the codebase to one curve/algorithm family instead of two; EC keys are also smaller and faster to
verify than RSA at an equivalent security level. NRF's JWT signing keypair
(`certs/nrf-jwt/{private,public}.pem`) is generated separately from any NF's mTLS transport
cert -- reusing a transport key for JOSE signing mixes usages that real PKI practice (and
TS 33.501) keeps apart.

**Verification (not just chosen, checked to work):** a standalone round-trip test
(issue -> verify valid, issue -> tamper one byte -> verify rejects with "invalid signature", issue
-> verify against a Verifier expecting a different issuer -> rejects with "claim value does not
match expected value") passed before this was wired into NRF's request handlers. Re-verified live
against the running NRF over the real TLS+mTLS transport: a request with `Authorization: Bearer
<token>tampered` gets a real 401 with `ProblemDetails{"detail":"invalid signature", ...}`, not a
silent accept.

**Rejected alternative:** RS256. No functional reason against it, but see above -- would have
added a second key type/algorithm family to the codebase for no benefit over ES256 in a lab
context with no interop requirement forcing RSA specifically.

**Consequence:** `sbi_core::jwt::Verifier` lives in `libs/sbi-core` (not `nfs/nrf`), specifically
because every future NF needs to verify incoming bearer tokens NRF issued, not just NRF itself --
NRF only needs the `Issuer` side.

---

## ADR-0013: Prometheus metrics via opentelemetry-cpp's own exporter, not a separate prometheus-cpp dependency

**Date:** 2026-08-05
**Status:** Accepted

**Context:** CLAUDE.md's Definition of Done requires "OpenTelemetry spans + Prometheus metrics
emitted" per NF. Tracing (spans) already exists via opentelemetry-cpp (`otel.hpp`, Phase 0).
Metrics export does not exist yet anywhere in the project.

**Decision:** Enable opentelemetry-cpp's `prometheus` vcpkg feature
(`opentelemetry-cpp[prometheus]`) rather than adding `prometheus-cpp` as an independent top-level
dependency. `libs/sbi-core/include/sbi_core/metrics.hpp` mirrors `otel.hpp`'s shape
(`init_metrics(bind_address)` / `get_meter(name)`). Pull model: `PrometheusExporterFactory`
starts a small HTTP listener (civetweb, bundled transitively) that Prometheus scrapes directly at
`/metrics` -- no push/collector step, unlike the OTLP tracing path.

**Rejected alternative:** `prometheus-cpp` directly. It's what opentelemetry-cpp's Prometheus
feature uses internally anyway (visible in the vcpkg dependency resolution:
`prometheus-cpp[compression,core,pull]` gets pulled in transitively) -- depending on it directly
would mean two ways to create/register metrics in the same codebase (raw prometheus-cpp registry
API vs. the OTel Meter API already used for consistency with tracing) for no benefit.

**Verification:** NRF exposes `nrf_registrations_total`, `nrf_deregistrations_total`,
`nrf_heartbeats_total`, `nrf_tokens_issued_total` (counters) and `nrf_registered_nf_count` (an
`ObservableGauge` reading the live registry size via callback). Confirmed live:
`curl http://127.0.0.1:9464/metrics` returns real Prometheus text-format output with
`nrf_registered_nf_count{otel_scope_name="nrf"} 0` visible before any registration.

**API note for future NFs using this:** `opentelemetry::metrics::Counter<T>`/`ObservableInstrument`
are only forward-declared in `meter.h` (used for the `Meter` interface's return types); a caller
needs `#include <opentelemetry/metrics/sync_instruments.h>`,
`<opentelemetry/metrics/async_instruments.h>`, and `<opentelemetry/metrics/observer_result.h>`
directly to get complete types before calling `->Add(...)` or `->AddCallback(...)` -- otherwise it
fails to compile with "invalid use of incomplete type". `metrics.hpp` already pulls these in so
callers of `sbi_core::get_meter()` don't hit this themselves.

---

## ADR-0014: NRF Docker image -- Ubuntu 24.04 multi-stage, PKI generated at container start

**Date:** 2026-08-05
**Status:** Accepted

**Context:** No NF has been containerized before this (Phase 0's binaries were throwaways, never
built a Dockerfile). `nfs/nrf/CMakeLists.txt` bakes `CERTS_DIR` in as a compile-time absolute path
(`${CMAKE_SOURCE_DIR}/certs`, an already-disclosed Phase 0/ADR-0011 simplification) -- this has a
direct, concrete consequence for how the image has to be built.

**Decision:** `deploy/docker/nrf.Dockerfile`, multi-stage: `builder` (Ubuntu 24.04, full build
toolchain + vcpkg + codegen Python deps, builds the `nrf` target only -- `-D5GC_BUILD_TESTS=OFF`),
`runtime` (Ubuntu 24.04 + `openssl`/`ca-certificates` only, copies the built binary). Both stages
use `/build` as `WORKDIR` specifically so the compile-time `CERTS_DIR` path resolves at runtime
too. The entrypoint runs `scripts/gen-lab-pki.sh nrf` fresh on every container start (ephemeral
CA/certs per container instance by default -- consistent with this being a lab image, not a
provisioned deployment).

**Real build issues hit and fixed, not just anticipated:**
- Docker build context initially copied the host's `build/` directory (stale `CMakeCache.txt`
  pointing at the host's absolute path) -- CMake correctly refused to reuse it inside the
  container. Fixed with `.dockerignore` excluding `build/`, `build-*/`, `certs/`, `.git/`, etc.
- `git clone --depth 1` for vcpkg inside the Dockerfile failed vcpkg's own baseline resolution:
  `vcpkg.json`'s `builtin-baseline` pins a specific historical commit, but a shallow clone only
  fetches the current tip of the default branch, which does not contain that commit object. Fixed
  with a full clone + explicit `git checkout <pinned-commit>`.

**Verification:** actually built (`docker build -f deploy/docker/nrf.Dockerfile -t 5gc-nrf:test .`)
and ran in this environment, not just written-but-unverified Dockerfile text -- see
`docs/TRACEABILITY.md` for the concrete command/output this ADR's claims are backed by.

**Rejected alternative:** a distroless or Alpine runtime base for a smaller image. Rejected for
now on time/complexity grounds (musl vs. glibc ABI concerns with the vcpkg-built static libs
weren't worth resolving for a first Docker pass); worth revisiting once image size actually
matters for the lab.

**Disclosed gap:** no non-root `USER` directive yet -- the container currently runs as root. Real
gap against container-security best practice, not fixed in this pass; flagged rather than silently
shipped as if it were fine.

---

## ADR-0015: NRF-specific simplifications (in-memory storage, partial discovery/subscription semantics)

**Date:** 2026-08-05
**Status:** Accepted (disclosed, tracked)

Consolidates the Phase 2 NRF-specific gaps that don't warrant their own ADR individually but need
to be findable in one place rather than scattered only as code comments:

- **In-memory storage only** (`nfs/nrf/src/registry.hpp`'s `NfRegistry`/`SubscriptionRegistry`),
  no persistence across restarts. A restart loses every registered NF and subscription; every NF
  would need to re-register (which real NFs do periodically via heartbeat anyway, so this is a
  softer gap than it sounds for NFs already running, but a hard one for anything mid-flight during
  a restart).
- **`SearchNFInstances` filters on `target-nf-type` only.** `TS29510_Nnrf_NFDiscovery.yaml`'s real
  query parameter set is much larger (service-names, snssais, dnn, requester-nf-instance-fqdn,
  ...); only the one mandatory discriminating parameter needed for a single-NRF lab to be useful
  is implemented. Real per CLAUDE.md's source-of-truth rule -- not a fabricated subset, a
  disclosed incomplete one.
- **Subscription notification fan-out ignores `subscrCond`.** `SubscriptionData.subscrCond` is a
  real, conditionally-typed filter (which NF types/events a subscriber cares about); this
  implementation delivers every `NF_REGISTERED`/`NF_PROFILE_CHANGED`/`NF_DEREGISTERED` event to
  every active subscriber regardless of what they actually asked for.
- **Notification delivery is synchronous best-effort** (log a warning on failure, no retry) --
  consistent with ADR-0006's synchronous HTTP/2 client; a slow or unreachable subscriber blocks
  the NRF request that triggered the notification for the duration of that one delivery attempt.
- **`GetNFInstances` (list-all) has no pagination** -- `TS29510_Nnrf_NFManagement.yaml` doesn't
  mandate it for this operation, but a very large registry would return an unbounded response
  body; not a problem at lab scale, flagged for whenever it might become one.
- **`/shared-data*` (multi-NRF federation) and `/scp-domain-routing-info*` (SCP-specific) are not
  implemented at all** -- explicitly out of scope for this pass per the agreed procedure list, not
  silently dropped from consideration. No second NRF instance exists in this lab and SCP isn't
  built yet, so neither is exercisable regardless.

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

---

## ADR-0016: UERANSIM as an arms-length RAN/UE simulator, fetched not vendored

**Date:** 2026-08-05
**Status:** Accepted

**Context:** AMF's real northbound surface is `Namf_Communication` (SBI/REST, generated from
`TS29518_Namf_Communication.yaml`), but the actual UE Registration procedure (TS 23.502 clause
4.2.2.2.2) starts over N2 (gNB -> AMF, NGAP/TS 38.413, ASN.1 over SCTP) and N1 (NAS), neither of
which is SBI/REST or expressible in the OpenAPI YAML -- there is nothing to codegen here, and
implementing an NGAP stack is a substantial protocol effort in its own right (comparable to Phase
3's PFCP work), deliberately deferred to a later, dedicated turn (`docs/TRACEABILITY.md` will carry
that gap until it's closed). To eventually test AMF's N2/N1 handling against a real, spec-following
peer rather than a hand-rolled test harness that might silently diverge from the actual NGAP/NAS
wire format, the user asked for a RAN/UE simulator.

**Decision:** UERANSIM (`github.com/aligungr/UERANSIM`, tag `v3.3.0`, commit
`6bf5a1a96aaef6ae8778b9d8b477ac6e2bbf8156`) -- an open-source, 3GPP-Release-15-conformant 5G-SA UE
and gNodeB simulator (NGAP/N2 and NAS/N1 control plane, GTP-U/N3 user plane; the NR radio interface
itself is simulated over UDP, not real RF, which is fine since we have no RF hardware target
either).

**License, checked not assumed (correction to what the user was initially told):** UERANSIM is
**AGPL-3.0** (dual-licensed with a commercial option), *not* GPL-3.0 as first stated in this
session before verifying against the actual GitHub repository metadata and `LICENSE` file --
flagged and corrected before this ADR was written, not silently left wrong. AGPL-3.0 is OSI-approved
(satisfies CLAUDE.md's "OSI-approved open source" bar) but is copyleft with a network-use clause
that would require offering source to users of a modified version served over a network. That
clause is not triggered here: UERANSIM runs **unmodified**, as its own separate OS process
(`nr-gnb`/`nr-ue` binaries), communicating with our NFs only over the wire (NGAP/SCTP, NAS, GTP-U)
-- never linked into, statically or dynamically, any `libs/` or `nfs/` binary. No derivative work is
created on our side; our Apache-2.0 code and UERANSIM's AGPL-3.0 code remain two separate programs
talking a standard protocol, the same relationship our NFs will have with any other black-box UE/RAN
implementation.

**Not vendored into the repository.** UERANSIM's source is not committed into this git history --
consistent with keeping AGPL-licensed source code out of an Apache-2.0 project's own tree (avoids
any ambiguity about what license governs what part of the repository) and with the existing pattern
of not vendoring vcpkg dependencies either. `simulators/ransim/fetch-and-build.sh` clones the pinned
commit into `simulators/ransim/vendor/UERANSIM/` (gitignored) and builds it there on demand.

**Directory:** `simulators/ransim/` at repo root, sibling to `nfs/`, `libs/`, `tests/` -- not nested
under `tests/`, since it is a standalone external tool (its own build, its own binaries) rather than
test code we author, even though its primary purpose here is testing our NFs.

**Scope of this session, explicitly:** simulator scaffold only -- fetch script, pinned commit,
build verified to actually produce `nr-gnb`/`nr-ue`/`nr-cli` binaries in this environment (see
`docs/TRACEABILITY.md`), and placeholder `gnb.yaml`/`ue.yaml` configs pointed at AMF's future N2
listener address (`127.0.0.5:38412`, not yet bound by anything -- AMF has no NGAP server yet). **Not
wired to AMF this session** -- there is nothing on the other end yet. Running `nr-gnb` against this
config today will fail to connect (SCTP `ECONNREFUSED`), which is the expected, disclosed state
until AMF's NGAP termination is built in a later turn.

**Test PLMN:** `mcc: '999'`, `mnc: '70'` -- not a 3GPP-assigned real-network PLMN; this is the
de facto "not a real network" test PLMN convention used across the open5gs/free5GC/UERANSIM lab
ecosystem (UERANSIM's own `config/open5gs-gnb.yaml` in the same repo uses the identical pair). A
config-value convention, not a fabricated spec fact -- disclosed as a choice we made, not something
transcribed from a 3GPP TS.

**Rejected alternative:** hand-roll a minimal NGAP/NAS test client that only emits the exact
messages our own AMF implementation expects. Rejected -- that risks tautological tests (our fake
peer and our AMF agreeing with each other while both diverge from the real TS 38.413/24.501 wire
format), which a real, independently-implemented, spec-conformant simulator avoids.

**Consequence / follow-up required:** AMF's own turn(s) still need to (a) implement Namf_Communication
per the procedure list already agreed with the user, and (b) implement NGAP/N2 termination (SCTP
transport, ASN.1 PER encode/decode per TS 38.413) before UERANSIM's `nr-gnb` can successfully reach
it. Both tracked as not-yet-done, not silently assumed complete because the simulator now exists.

---

## ADR-0017: Fix tools/sbi-codegen's cross-file schema name collision bug

**Date:** 2026-08-05
**Status:** Accepted

**Context:** While building AMF's `Namf_Communication` surface, cross-referencing the already-agreed
`AMFStatusChangeSubscribe` procedure's request schema against the generated C++ output surfaced a
real generator defect, not a spec ambiguity: `TS29518_Namf_Communication.yaml` defines its own local
`SubscriptionData` schema (`{amfStatusUri (required), guamiList}`), but only ONE `SubscriptionData`
struct existed anywhere in generated output, and it had NRF's shape
(`{nfStatusNotificationUri (required), subscrCond, ...}` from `TS29510_Nnrf_NFManagement.yaml`) --
AMF's real schema had been silently dropped.

**Root cause, verified by reading `tools/sbi-codegen/sbi_codegen/loader.py`:**
`SchemaRegistry.schemas` was keyed by bare schema name only (`dict[str, tuple[schema, source_file]]`),
and `load_file()` only inserted a schema `if name not in self.schemas` -- first file loaded wins
silently, every later file's same-named schema is discarded with no warning. The loader's own
docstring asserted this was safe ("3GPP's OpenAPI files consistently reuse the same schema name for
the same concept across files"). Checked, not assumed: a script comparing every locally-defined
schema name shared by two or more of the 5 current pilot files
(`TS29510_Nnrf_NFManagement/NFDiscovery/AccessToken.yaml`, `TS29518_Namf_Communication.yaml`,
`TS29571_CommonData.yaml`) found 7 collisions -- `SubscriptionData`, `NFProfile`, `NFService`
(NFManagement vs. NFDiscovery), `Ipv4AddressRange`, `Ipv6PrefixRange`, `TransportProtocol`,
`MbsSession` (NFManagement vs. CommonData) -- and **every one of the 7 has genuinely different
content** (structural dict comparison, descriptions excluded), not an identical redefinition. The
docstring's premise was false for locally-declared-but-coincidentally-named schemas; it only holds
for the deliberate pattern of one file externally `$ref`-ing another's canonical type (e.g.
`NfInstanceId` from `TS29571_CommonData.yaml`), which never hits this collision path since those
files don't also redeclare it locally.

**Blast radius, checked:** `grep` across `nfs/`, `libs/sbi-core/`, `tests/` for every one of the 7
colliding type names found zero hand-written C++ references to any of them -- NRF works with raw
`nlohmann::json` for `SubscriptionData`, not the generated struct (`nfs/nrf/src/registry.hpp`).
Renaming the generated C++ types was therefore safe with no call-site fallout.

**Decision:**
- `SchemaRegistry.schemas` re-keyed to `dict[(source_file, name), schema]`. `load_file()` now
  registers unconditionally (idempotent per-file, not per-name) -- no more silent drops.
  `resolve_ref()` for an internal (`#/...`) ref now always resolves within the *calling* file's own
  namespace, never an arbitrary other file's same-named schema.
- `schema_to_ir.py`'s `Converter` now runs a two-pass build: types are collected keyed by
  `(source_file, yaml_name)` (`TypeRef` gained a `ref_key` field to carry this through), then a final
  `_disambiguate()` pass assigns every key a guaranteed-unique C++ name -- the plain
  `cpp_type_name(name)` when only one file defines that name (the overwhelming majority: 1092 of the
  previous 1104-type output), or `{name}_{file_tag}` (e.g. `SubscriptionData_Nnrf_NFManagement` /
  `SubscriptionData_Namf_Communication`) when multiple files collide on it -- then patches every
  `IRType.name` and every `TypeRef.cpp_name` to match before handing off to `render.py` (which itself
  assumes name-uniqueness via `name_to_type`/`name_to_file` dicts and would have silently re-collided
  the two variants back into one if the renaming weren't resolved before reaching it).
- `tests/conformance/validate_structural_conformance.py` updated for the new `(file, name)`-keyed
  registry API (it looked up `registry.schemas["Guami"]`/`["NFType"]` directly).

**Verification:** regenerated from scratch -- type count went from 1092 to 1104 (12 additional
distinct types now correctly emitted instead of silently merged/dropped, more than the 7 found in the
5-pilot-file check above since the fix applies to the full transitive closure, not just the pilots).
`SubscriptionData_Nnrf_NFManagement` and `SubscriptionData_Namf_Communication` both now exist as
distinct structs with their real, correct fields (confirmed by direct inspection of the generated
header). Full project rebuild green; all 6 existing tests (integration + conformance round-trip +
structural) pass unchanged -- NRF's behavior is provably unaffected by this fix.

**Rejected alternative:** hand-write AMF's `SubscriptionData` DTO instead of fixing the generator.
Rejected as a direct violation of CLAUDE.md's "never hand-write a DTO the YAML can generate" rule --
the correct fix belongs in the shared generator, especially since the same collision pattern will
keep recurring as more NF YAML files are added as codegen roots in later phases.

**Consequence:** the disambiguation-suffix naming scheme (`{Name}_{Nnrf_NFManagement|Namf_Communication|...}`)
is now a permanent, load-bearing part of the generated API surface for every currently-colliding type
and any future collision the same mechanism catches. Future NF work referencing a schema name known to
collide must use the qualified name; this is discoverable by grepping the generated header for the
plain name if a compile error suggests it's missing.

---

## ADR-0018: NRF's own nfInstanceId is a fixed constant, not randomly generated per run

**Date:** 2026-08-05
**Status:** Accepted

**Context:** Discovered while wiring AMF's `sbi_core::jwt::Verifier` for incoming bearer tokens.
`sbi_core::jwt::Verifier`'s constructor requires the exact expected issuer id up front (checked
against every token's `iss` claim). NRF's own `main.cpp` previously called
`sbi_core::generate_uuid_v4()` for its own `nfInstanceId` at every process start -- fine for NRF
itself (it passed its own freshly-generated id to both its `Issuer` and its own `Verifier` in the
same process, so it always agreed with itself), but a real bootstrapping gap for every other NF:
AMF has no way to know what random id NRF picked this run before constructing its own `Verifier`.
Previously latent because NRF was the only NF that both issued and verified tokens; AMF is the
first NF to need to verify tokens NRF issued from outside NRF's own process.

**Decision:** `nfs/nrf/src/main.cpp`'s `kNrfInstanceId` is now a fixed compile-time constant
(`"5ba9a927-1d31-4c8e-8a10-000000000001"`, an arbitrary but validly-shaped UUID) instead of
`generate_uuid_v4()`. Every other NF that verifies NRF-issued tokens (starting with AMF) hardcodes
the identical constant. This is arguably the more correct design independent of the bug it fixes:
NRF is the trust anchor every other NF's OAuth2 flow depends on, and a real deployment's root of
trust having a stable, well-known identity (not one that changes every restart) is the normal
expectation, not a lab shortcut.

**Verification:** rebuilt `nrf`, reran the full test suite (all existing tests, unaffected) --
confirmed nothing depended on the identity being random. `grep` for `nrf_instance_id`/the removed
`generate_uuid_v4()` call site across the repo found only NRF's own file referencing it.

**Disclosed gap, not fixed here:** the constant is duplicated by hand in every NF that needs it
(`nfs/nrf/src/main.cpp` and `nfs/amf/src/main.cpp` so far) rather than coming from one shared
source of truth (e.g. a config file, environment variable, or `libs/sbi-core` constant). Acceptable
for two NFs; worth revisiting (shared config) once several more NFs need the same value -- tracked
as follow-up, not silently left as an accepted-forever hand-copy pattern.

**Rejected alternative:** have `Verifier` accept any issuer and expose the actual `iss` claim in
`VerifyResult` for the caller to check itself against a dynamically-discovered value. Rejected --
weakens the security-by-default property `Verifier` currently has (ADR-0012's tamper-rejection
test relies on issuer mismatch being rejected unconditionally), and there is no discovery mechanism
for "NRF's own identity" today anyway (NRF does not register itself in its own `NfRegistry`), so
this would have traded a real security check for solving a problem the fixed-constant approach
solves more simply.

---

## ADR-0019: AMF (Phase 2's second NF) and the docker-compose shared-PKI fix it required

**Date:** 2026-08-05
**Status:** Accepted

**Context:** AMF implements `Namf_Communication` (`specs/5G_APIs-REL-19/TS29518_Namf_Communication.yaml`)
-- the procedure list agreed with the user before implementation: `ReleaseUEContext`,
`EBIAssignment`, `UEContextTransfer`, `RegistrationStatusUpdate`, `N1N2MessageTransfer`,
`N1N2MessageSubscribe`, `N1N2MessageUnSubscribe`, `NonUeN2MessageTransfer`, `NonUeN2InfoSubscribe`,
`NonUeN2InfoUnSubscribe`, `AMFStatusChangeSubscribe`, `AMFStatusChangeUnSubscribe`,
`AMFStatusChangeSubscribeModfy` -- every operationId with a real `application/json` request-body
alternative in the YAML.

**Deferred, not dropped:** `CreateUEContext`, `RelocateUEContext`, `CancelRelocateUEContext` are
`multipart/related`-ONLY per spec (checked: no `application/json` alternative exists for any of the
three), and `libs/sbi-core` has no multipart/related support. Building that is a substantial
protocol effort in its own right (RFC 2046-style parsing/encoding layered onto the hand-rolled
nghttp2 server), useful to more than just AMF, and was explicitly deferred to a later, dedicated
turn per the user's decision when this was raised. Consequence: nothing in this build can ever
populate `nfs/amf/src/ue_context_store.hpp`'s store, so the four per-`ueContextId` operations
(`ReleaseUEContext`/`EBIAssignment`/`UEContextTransfer`/`RegistrationStatusUpdate`) can only be
exercised on their "no such UE context" (404) branch until `CreateUEContext` lands -- their "found"
branches are implemented for real (correct per spec) but currently unreachable/unverified. Disclosed
in `nfs/amf/src/main.cpp`'s file header, not hidden.

**Also disclosed:** subscriptions (`N1N2Message*`, `NonUeN2Info*`, `AMFStatusChange*`) are created
and removed for real, but notification *delivery* is not implemented -- there is no trigger path
yet (no NGAP/N2, no real UE, no multi-AMF deployment) that would ever fire one.

**NRF client lifecycle runs on a dedicated thread, not the server's `io_context`:** `libs/sbi-core`'s
`http2::Client` is synchronous (ADR-0006, disclosed debt). AMF is the first NF that is
simultaneously a real inbound SBI server (serving `Namf_Communication`) and needs to make ongoing
outbound calls (NRF registration + periodic heartbeat) -- exactly the scenario ADR-0006 flagged as
not viable on the same thread. Resolved minimally: `run_nrf_lifecycle` owns its own `http2::Client`
instance and runs on a dedicated `std::thread`, leaving the server's `io_context` free to serve
inbound requests without stalling during a heartbeat call. This is not the full `curl_multi`+Asio
integration ADR-0006 names as the eventual real fix -- that remains future work once more NFs need
outbound calls from their own request handlers (not just a background lifecycle loop).

**Docker Compose PKI bug found and fixed:** `deploy/docker/docker-compose.yml` previously had each
NF's container generate its own lab PKI independently at startup (`nrf.Dockerfile`'s entrypoint
runs `scripts/gen-lab-pki.sh nrf` fresh every start) -- harmless with only one NF/container in the
compose file (all that existed before AMF), but broken the moment a second container needs to
mutually trust the first over mTLS: two independently-generated root CAs do not validate each
other's leaf certificates. A `pki-init` one-shot service now provisions `certs/` once (via
`scripts/gen-lab-pki.sh nrf amf`) into a shared `certs_data` named volume that both `nrf` and `amf`
mount at `/build/certs`; `nrf`/`amf` both `depends_on: pki-init: condition:
service_completed_successfully`. `nrf.Dockerfile`'s own entrypoint still calls
`gen-lab-pki.sh nrf` too (harmless -- the script skips regeneration when `ca.key`/the NF's cert
already exist, so it's a no-op against the shared volume; kept so the image is still self-sufficient
if ever run standalone via `docker run` outside Compose, matching ADR-0014's original usage).
`amf.Dockerfile`'s entrypoint does NOT attempt its own PKI generation -- it assumes
`/build/certs` is already populated, since running it standalone without `pki-init` having run
first would produce a cert chaining to nobody NRF trusts anyway.

**Disclosed gap, not fixed here:** the equivalent problem exists in Helm (`deploy/helm/amf/` and
`deploy/helm/nrf/` are separate releases with no shared-secret mechanism) -- see
`deploy/helm/amf/Chart.yaml`'s description for the explicit disclosure. A real fix (shared
`Secret` provisioned by a one-shot `Job`, or an external cert-manager `Issuer`) is genuine,
non-trivial scope, deferred to Phase 8 (lab packaging and conformance) per PROMPT.md rather than
attempted piecemeal per-NF-chart in this turn.

**Verification:** `docker build -f deploy/docker/amf.Dockerfile -t 5gc-amf:test .` actually run in
this environment (see `docs/TRACEABILITY.md` for the result) -- matching the bar ADR-0014 set for
NRF's own image. `docker compose up` (both containers actually mTLS-registering with each other)
was NOT run this session, same disclosed-not-silently-assumed gap ADR-0014 already recorded for
NRF alone.

**Ports:** AMF's SBI port (7778) and metrics port (9465) are NRF's (7777/9464) +1 -- a lab
convention chosen for this session, not a value from any spec (`TS29500`/`TS29501` don't mandate
specific ports for SBI services).

---

## ADR-0020: multipart/related codec in sbi-core, unblocking CreateUEContext and SMF's CreateSMContext

**Date:** 2026-08-05
**Status:** Accepted

**Context:** Starting SMF's turn, `TS29502_Nsmf_PDUSession.yaml`'s `PostSmContexts` (CreateSMContext
-- the actual AMF-triggered PDU Session Establishment trigger, TS 23.502 clause 4.3.2.2.1, and
CLAUDE.md's stated Phase 2 end-state goal) turned out to be `multipart/related`-ONLY, same as
AMF's already-deferred `CreateUEContext`/`RelocateUEContext`/`CancelRelocateUEContext`
(ADR-0019). Unlike AMF's case, SMF's other `/sm-contexts` operations
(`UpdateSmContext`/`ReleaseSmContext`/`RetrieveSmContext`) all depend on an SmContext that only
`CreateSMContext` can create -- deferring it a second time would leave SMF's entire `/sm-contexts`
surface untestable beyond 404s, not just three peripheral operations. Given a second NF in a row
hitting the same wall on its most central operation, the user chose to build multipart/related
support now rather than keep accumulating deferred/unreachable code across NFs.

**Decision:** `sbi_core::multipart` (`libs/sbi-core/include/sbi_core/multipart.hpp` +
`src/multipart.cpp`): `parse(content_type_header, body) -> tl::expected<vector<Part>, string>` and
`encode(parts) -> Encoded{content_type_header, body}`. This is RFC 2046 ("multipart") + RFC 2387
("related") -- standard IETF MIME framing 3GPP SBI reuses verbatim, NOT a 3GPP-specific wire
format; the only 3GPP-specific knowledge (which named parts a given operation expects) stays in
each NF's own handler code, never in this codec. No changes to `http2::Server`/`Client` were
needed -- multipart bodies are just opaque bytes as far as HTTP/2 framing is concerned; this is
purely a body-content codec NF handler code opts into after checking `Content-Type`.

**One assumption disclosed as unverified, not asserted as fact:** whether 3GPP peers send/expect
`Content-Id` values wrapped in RFC 2045 msg-id angle brackets (`<foo>`) or as a bare token matching
`RefToBinaryData.contentId` verbatim. The OpenAPI YAML only declares
`Content-Id: {schema: {type: string}}`, which doesn't settle this, and there is no real external
SBI peer in this lab to interop-test against (`simulators/ransim` speaks NGAP/NAS to a gNB, not
Namf_Communication/Nsmf_PDUSession multipart bodies). The codec parses leniently (accepts either
form) and encodes without brackets (the bare-token convention this project recalls from other
open-source 5GC interop reports, not verified firsthand). Flagged for revisit the first time this
needs to interop with a real external SBI peer.

**Verification:** 8 unit tests (`tests/conformance/test_multipart.cpp`) -- encode-then-parse
round-trips (single part, multi-part with genuinely opaque binary bytes including embedded null
bytes), a hand-crafted body shaped exactly like `CreateUEContext`'s real wire format (proving the
codec works against literal bytes, not just its own `encode()` output), and 4 malformed-input
rejection cases (wrong content-type, missing boundary, no delimiter, unterminated body) -- all
wrapped in try/catch so malformed network input becomes a returned error, never an uncaught
exception. `sbi_core` builds warning-clean under the project's strict flags (`-Wall -Wextra
-Wpedantic -Wshadow -Wconversion -Wsign-conversion`, `5gc_project_options`).

**Applied immediately to retroactively unblock AMF:** `nfs/amf/src/main.cpp`'s `CreateUEContext`
(`PUT /ue-contexts/{ueContextId}`), `RelocateUEContext`, `CancelRelocateUEContext` are now
implemented for real (previously deferred per ADR-0019) via a shared `parse_multipart_json_body<T>`
helper mirroring the existing `parse_json_body<T>` pattern. Their N2/NGAP binary-content fields
(`targetToSourceData.ngapData.contentId`, etc.) are stub placeholders, disclosed in
`nfs/amf/src/main.cpp`'s file header -- this lab has one AMF and no NGAP stack, so there is no real
inter-AMF handover state to populate them with; the point of this pass was proving the multipart
plumbing end-to-end (encode -> real HTTP/2 wire bytes -> amf's parser -> store -> downstream
operations), not modeling real handover semantics.

**Proof, not assertion:** two new real subprocess-to-subprocess integration tests
(`tests/integration/test_amf_namf_communication.cpp`) construct an actual multipart/related
`CreateUEContext` request via `sbi_core::multipart::encode` (the same codec `amf` uses to parse),
send it over real TLS 1.3 + mTLS HTTP/2, and confirm: the response deserializes as real
`UeContextCreatedData`; `EBIAssignment`/`ReleaseUEContext`'s previously-unreachable "found"
branches now work end-to-end and correctly 404 on a second `ReleaseUEContext`;
`RelocateUEContext`/`CancelRelocateUEContext` work over real multipart bodies and correctly 404 for
a nonexistent context; a non-multipart body on a multipart-only operation correctly gets 400, not
silently accepted. All 19 project tests pass, stable across repeated runs.

**Rejected alternative:** keep deferring `CreateSMContext` a third time (this NF's turn) and build
SMF's `/pdu-sessions` collection instead (the I-SMF/inter-SMF scenario, which does have a JSON-only
alternative). Rejected per the user's explicit choice -- `/pdu-sessions` is the less common
inter-SMF/roaming scenario, not the standard AMF-triggered PDU session establishment CLAUDE.md
names as the Phase 2 end-state goal, and deferring a third time would mean SMF's entire
`/sm-contexts` surface (the actually-important one) stays untestable indefinitely.

**Consequence:** SMF's `CreateSMContext` (this NF's next turn) can now be built for real using this
same codec, rather than deferred a third time.

---

## ADR-0021: SMF (Phase 2's third NF) -- /sm-contexts scope, shared json_body.hpp promotion

**Date:** 2026-08-05
**Status:** Accepted

**Context:** SMF implements `Nsmf_PDUSession` (`specs/5G_APIs-REL-19/TS29502_Nsmf_PDUSession.yaml`).
The full file covers two largely-independent operation groups: `/sm-contexts` (the AMF-triggered
collection real UE PDU Session Establishment uses, TS 23.502 clause 4.3.2.2.1 --
CLAUDE.md's Phase 2 end-state goal) and `/pdu-sessions` (the I-SMF/inter-SMF roaming-scenario
collection), plus `SendMoData`/`TransferMoData` (small-data-over-NAS, multipart-only) and two
entirely separate services (`Nsmf_EventExposure.yaml`, `Nsmf_NIDD.yaml`). Agreed with the user:
this turn scopes to `/sm-contexts` only -- `CreateSMContext`, `RetrieveSMContext`,
`UpdateSMContext`, `ReleaseSMContext` -- deferring `/pdu-sessions`, `SendMoData`/`TransferMoData`,
and the two separate services, all disclosed in `nfs/smf/src/main.cpp`'s file header, not silently
dropped.

**`TS29502_Nsmf_PDUSession.yaml` added as a codegen root file**
(`libs/sbi-generated/CMakeLists.txt`) -- none of SMF's DTOs existed in generated output before this
turn. Regenerating grew the type count from 1104 to 1240; `SmContext{Create,Created,Update,Updated,
Release,Released,Retrieve,Retrieved}Data` all landed with their real field shapes (verified by
direct inspection of the generated header before writing any handler code), no new cross-file name
collisions observed among the schemas this turn's handlers actually use.

**No real PCF/UDM/UPF to talk to, same shape of gap AMF had with AUSF/UDM:** `CreateSMContext` does
real request validation (mandatory `servingNfId`/`servingNetwork`/`anType`/`smContextStatusUri`
per `SmContextCreateData`) and real store bookkeeping, but cannot perform the real procedure's
PCF (SM Policy Association)/UDM (subscription data)/UPF (N4/PFCP, Phase 3) interactions -- it
always succeeds (201) rather than modeling the failure modes those absent dependencies would
produce. `UpdateSMContext` acknowledges (204) without fabricating `SmContextUpdatedData` content
(EBI allocation, N1/N2 info) since there is nothing real behind those fields yet. Both disclosed in
`nfs/smf/src/main.cpp`'s file header.

**`sbi_core::http2::problem_response`/`parse_json_body<T>`/`parse_multipart_json_body<T>` promoted
to shared `libs/sbi-core/include/sbi_core/json_body.hpp` + `src/json_body.cpp`.** This exact
pattern was independently written in `nfs/nrf/src/main.cpp` and then `nfs/amf/src/main.cpp` --
SMF made it a third occurrence, past the point where duplicating it again was the right call
(CLAUDE.md's "three similar lines is better than a premature abstraction" -- this is the fourth).
`nfs/nrf`'s and `nfs/amf`'s own local copies are deliberately left as-is (already tested, already
committed) rather than churned to use the new shared header in the same turn that introduces it --
a disclosed, non-blocking cleanup opportunity for later, not silently left unnoticed. `check_bearer`
(JWT-verification-specific, not JSON-body-parsing) was NOT promoted -- still duplicated a third
time in `nfs/smf/src/main.cpp`, consistent with the existing NRF/AMF pattern; small enough that
promoting it wasn't judged worth the extra churn this turn.

**Verification:** manual `curl` end-to-end (real multipart `CreateSMContext` -> real
`RetrieveSMContext`/`UpdateSMContext`/`ReleaseSMContext` on the resulting `smContextRef`, real
Prometheus counters) plus 2 new real subprocess-to-subprocess integration tests
(`tests/integration/test_smf_pdu_session.cpp`): a full lifecycle test (multipart `CreateSMContext`
constructed via `sbi_core::multipart::encode`, response deserializes as real `SmContextCreatedData`,
`smContextRef` extracted from the real `Location` header, then `RetrieveSMContext` with no request
body -- proving the spec's `required: false` is actually honored -- `UpdateSMContext`, and
`ReleaseSMContext` followed by a second release correctly 404ing) and an error-path test (404 on a
nonexistent context, 401 on a tampered token, 400 when `CreateSMContext` gets a plain JSON body
instead of multipart/related). All 21 project tests pass, stable across repeated runs.

**Rejected alternative:** implement `/pdu-sessions` alongside `/sm-contexts` in the same turn since
both were now unblocked by the multipart codec. Rejected to keep this turn's scope matched to what
was actually agreed with the user (the standard AMF-triggered flow) rather than silently expanding
scope just because the blocker was gone -- `/pdu-sessions` remains a clearly-scoped future addition
rather than something started, then left half-verified alongside everything else this turn already
covers.

---

## ADR-0022: Fix tools/sbi-codegen's "one cycle poisons the whole group" topo-sort bug

**Date:** 2026-08-06
**Status:** Accepted

**Context:** Adding `TS29503_Nudm_UECM.yaml`/`TS29503_Nudm_SDM.yaml` as codegen pilot files for
UDM produced a `TS29122_CommonData_grp.cpp` that failed to compile with dozens of "has no member
named X" / "was not declared in this scope" errors across many, mostly unrelated types
(`AllowedNssai`, `Point`, `LocationArea`, `RegistrationDatasetNames`, ...). Root-caused (not
assumed) via a minimal standalone reproduction (`g++ -fsyntax-only` on a tiny test file including
just the generated header) plus a direct Tarjan-SCC probe of the IR graph: a **real, genuine 3GPP
schema cycle** -- `SharedData.sharedAmData` -> `AccessAndMobilitySubscriptionData` ->
`AccessAndMobilitySubscriptionData.sharedDataList` -> `SharedData` (a real "shared subscription
data aggregates per-type data, per-type data can itself be marked shared" pattern, not a generator
artifact) -- combined with a real generator bug in `render.py`'s `_topo_sort_types`: the instant
its DFS found ANY cycle anywhere in a merged group, it discarded the entire computed ordering and
fell back to raw input order for **every type in the group**, not just the two cyclic ones. A
2-type cycle was silently corrupting the emission order for (in this group) over a thousand
otherwise-acyclic, unrelated types.

**A second, related gap found while fixing the first:** `_referenced_names` (the function that
walks a type's fields to build the dependency graph) only ever looked at `ObjectType` fields.
`AliasType` (`using X = std::vector<Y>;`) dependencies on other named types in the same group were
never tracked at all -- both for topo-sort ordering AND for cross-group `#include` computation.
Combined with `header.hpp.j2`'s template structure (which always emits the opaque, then alias,
then open-enum, then object blocks in that fixed category order, regardless of what the topo-sort
computes), any alias depending on a struct/enum type -- e.g. `using RegistrationDatasetNames =
std::vector<RegistrationDataSetName>;` where `RegistrationDataSetName` is an open-enum struct --
was structurally guaranteed to reference it before its definition, independent of the first bug.
This is why fixing only the cycle-poisoning bug still left `RegistrationDatasetNames`-shaped
failures; both needed fixing together.

**Decision:**
- `_topo_sort_types` rewritten to use proper SCC condensation: compute strongly-connected
  components via the existing Tarjan implementation, build the condensation graph (guaranteed
  acyclic by construction -- SCCs cannot cycle with each other), topologically sort *that*, and
  return `(ordered_names, cyclic_names)` where `cyclic_names` is only the (typically tiny) set of
  types actually participating in a real cycle. Every acyclic type elsewhere in the group keeps
  its correct dependency order regardless of an unrelated cycle existing somewhere else in the
  same group. Only `ObjectType` nodes can have outgoing edges (enums/aliases-as-targets never
  originate one via the old field-walk), so a multi-member SCC can only ever consist of
  `ObjectType`s -- meaning `struct X;` forward declaration is always sufficient and correct for
  breaking it, never attempted on an alias (which can't be forward-declared in C++).
- `_referenced_names` extended to also extract dependencies from `AliasType.cpp_underlying` via
  identifier tokenization (regex) rather than adding a structured `TypeRef` field to `AliasType` --
  the only thing that matters for dependency tracking is which names appear, and every call site
  already filters the result against a known-names set, so stray non-type tokens (`std`, `vector`)
  are harmless noise, not false edges.
- `render()` now computes cross-group `#include` deps uniformly for every kind (previously
  `ObjectType`-only), and separately computes `alias_forward_decls`: any struct/enum-shaped type an
  `AliasType` in the group depends on, forward-declared unconditionally (not just when part of a
  detected cycle), since the alias block's fixed position before the enum/object blocks makes that
  dependency direction structurally impossible to satisfy any other way.
- `header.hpp.j2` emits a `// Forward declarations ...` block (the union of both sources above)
  immediately after `namespace sbi_gen {`, before the opaque/alias/enum/object blocks.

**Verification:** minimal standalone compile (`g++ -fsyntax-only` against just the regenerated
header) went from ~40 distinct errors to a clean compile. Full project rebuild from a completely
clean `build/` directory (no stale-cache possibility) succeeds. All 21 pre-existing tests
(NRF/AMF/SMF integration, multipart unit tests, round-trip/structural conformance) still pass
unchanged, proving the fix is purely additive (extra forward declarations, more accurate ordering)
with no behavioral change for code that already compiled correctly.

**Rejected alternative:** patch around this one specific cycle (e.g. demote
`SharedData.sharedAmData` or `AccessAndMobilitySubscriptionData.sharedDataList` to an opaque
`nlohmann::json` fallback to break the cycle without touching the topo-sort algorithm itself).
Rejected -- would have fabricated a simplification not called for by the schema (both fields are
perfectly representable, real 3GPP types; the cycle is a normal, valid C++ mutual-reference
pattern once forward-declared) and would not have fixed the underlying flaw, which was already
guaranteed to resurface -- with a different, unpredictable set of collateral damage -- the next
time any future NF's pilot file introduced a different cycle.

**Consequence:** this is the second real generator bug found only by actually compiling generated
output against a growing pilot-file set (after ADR-0017's cross-file name collision) -- both
precisely the kind of bug `docs/DECISIONS.md` ADR-0010 already anticipated ("this generator is
deliberately not attempting to be a complete, general-purpose OpenAPI-to-C++ compiler on day
one... expected to shrink incrementally as Phase 2+ NF work surfaces schema shapes not yet
handled"). No known residual gaps in this area, but per that same expectation, not asserted as the
last one either.

---

## ADR-0023: UDM (Phase 2's fourth NF) -- Nudm_UECM + Nudm_SDM scope

**Date:** 2026-08-06
**Status:** Accepted

**Context:** UDM implements `Nudm_UECM` (`TS29503_Nudm_UECM.yaml`) and `Nudm_SDM`
(`TS29503_Nudm_SDM.yaml`), two of UDM's ten Nudm services. Scope agreed with the user before
implementation: `Nudm_UECM`'s AMF 3GPP-access registration group (`3GppRegistration`,
`Update3GppRegistration`, `Get3GppRegistration`, `deregAMF`) and SMF registration group
(`GetSmfRegistration`, `Registration`, `RetrieveSmfRegistration`, `UpdateSmfRegistration`,
`SmfDeregistration`) -- the operations AMF and SMF actually call during registration and PDU
session establishment -- plus `Nudm_SDM`'s `GetAmData`/`GetSmfSelData`/`GetSmData`/`Subscribe`/
`Unsubscribe`. Deferred, disclosed in `nfs/udm/src/main.cpp`'s file header: `Nudm_EE`, `Nudm_MT`,
`Nudm_NIDDAU`, `Nudm_PP`, `Nudm_RSDS`, `Nudm_SSAU`, `Nudm_UEAU`, `Nudm_UEID` (separate services);
UECM's non-3GPP-AMF/SMSF(3GPP+non-3GPP)/IP-SM-GW/NWDAF registration groups; SDM's remaining ~25
GET operations (LCS/V2X/ProSe/MBS/UC data, shared-data operations, `GetSupiOrGpsi`, Sor/Upu Ack,
`GetGroupIdentifiers`, ...).

**No multipart wall this time:** checked before proposing scope -- neither `TS29503_Nudm_UECM.yaml`
nor `TS29503_Nudm_SDM.yaml` uses `multipart/related` anywhere. Both codegen pilot files added
directly; this is also the turn that surfaced ADR-0022's topo-sort bug (see that entry for the
generator fix required before UDM's types would even compile).

**RFC 7396 JSON Merge Patch, not RFC 6902 JSON Patch:** `Update3GppRegistration` and
`UpdateSmfRegistration` both use `application/merge-patch+json` (confirmed by grep across the
whole YAML file -- all 6 PATCH operations in `Nudm_UECM` use it consistently), unlike NRF's
`UpdateNFInstance` which uses RFC 6902 JSON Patch (`application/json-patch+json`). Implemented via
`nlohmann::json::merge_patch()` (built-in, does exactly RFC 7396 semantics) rather than NRF's
`nlohmann::json::patch()` (RFC 6902) -- a different standard for a different operation, not an
inconsistency. Both `Amf3GppAccessRegistrationModification` and `SmfRegistrationModification`
mark their own primary key field (`guami`, `smfInstanceId` respectively) as spec-`required` even
though the request is a partial-update patch -- checked directly against the YAML's `required:`
list rather than assumed to be a codegen artifact; it is what the spec actually says.

**Disclosed simplification, stated up front:** UDM normally proxies subscriber-provisioned data
from UDR (`Nudr_DataRepository`), which doesn't exist yet in this build order (UDR is next).
`GetAmData`/`GetSmfSelData`/`GetSmData` therefore return a schema-valid but empty/default response
for any `supi` -- there is no UDR-backed store yet to return real provisioned data from.
`Nudm_UECM`'s registration operations are real bookkeeping (an AMF or SMF really did register),
not a UDR-dependent gap -- the two services have genuinely different honesty postures and that
distinction is stated explicitly in code, not left for the reader to infer.

**Verification:** manual `curl` end-to-end for all 14 operations (including RFC 7396 merge-patch
actually merging rather than replacing, confirmed by patching only `guami`/`smfSetId` and checking
untouched fields like `amfInstanceId`/`pduSessionId` survive) plus 4 new real
subprocess-to-subprocess integration tests (`tests/integration/test_udm_uecm_sdm.cpp`): AMF
registration full lifecycle (create, idempotent-replace 200-not-201, get, merge-patch, deregister,
404-after-deregister), SMF registration full lifecycle (create, list-for-ue collection GET,
retrieve, merge-patch, delete, 404-after-delete), SDM data retrieval + subscribe/unsubscribe
(double-unsubscribe correctly 404s), and the 404/401 error paths. All 25 project tests pass,
stable across repeated runs.

**Rejected alternative:** also implement `Nudm_UEAU` (`Nudm_UEAuthentication_Get`, the operation
AUSF calls for authentication vectors) in this same turn, since it's arguably just as central to
UE registration as UECM/SDM. Rejected -- AUSF doesn't exist yet in this build order (comes after
UDM, UDR), so `Nudm_UEAU` would be unreachable/unverifiable by anything in this repository until
then, the same reasoning that kept AMF's turn from reaching into PCF/UDM territory prematurely.
Tracked as a natural addition once AUSF's own turn needs it.

---

## ADR-0024: Fix sbi-codegen mishandling pure `$ref`-only schema re-exports

**Date:** 2026-08-06
**Status:** Accepted

**Context:** Adding `TS29505_Subscription_Data.yaml` (UDR's real schema file) as a codegen pilot
surfaced a third real generator bug. Checked directly against the YAML: `TS29505_Subscription_
Data.yaml` locally "defines" roughly 25 schemas -- `SmfRegistration`, `Amf3GppAccessRegistration`,
`AccessAndMobilitySubscriptionData`, `SdmSubscription`, and others -- that are each nothing but a
pure indirection, e.g.:
```yaml
SmfRegistration:
  $ref: 'TS29503_Nudm_UECM.yaml#/components/schemas/SmfRegistration'
```
i.e. "this name, here, just means UDM's own already-defined schema of the same name" -- UDR's spec
authors re-exporting UDM's types by reference for convenience within their own file's local
namespace, not defining a second, different concept. `schema_to_ir.py`'s `_convert_one` had no
branch for a top-level schema whose entire body is `{"$ref": ...}` -- it fell through to the
`OpaqueType` fallback (`unhandled schema shape, keys=['$ref']`). Combined with ADR-0017's
collision disambiguation (correct in isolation: two different `(source_file, name)` keys sharing a
plain name), this made things actively worse than a plain opaque fallback: each of these ~25 names
got disambiguated into a spurious, disconnected `TypeName_Subscription_Data` opaque type,
completely unrelated to the real `TypeName_Nudm_UECM`/`TypeName_Nudm_SDM` struct UDM already uses
for the identical concept -- exactly the kind of duplicate-type problem ADR-0010 picked a custom
generator specifically to avoid.

**Decision:** `SchemaRegistry.resolve_ref` (`loader.py`) now transparently follows pure-`$ref`
indirection schemas to their real target before returning, via a new `pure_ref_target(schema)`
helper (returns the inner `$ref` string iff the schema dict is exactly `{"$ref": X}`, else `None`)
and a loop with cycle detection (`ValueError` on a repeated `(file, name)` key). Any caller asking
to resolve `TS29505_Subscription_Data.yaml#/components/schemas/SmfRegistration` now transparently
gets back `("SmfRegistration", <the real object schema>, "TS29503_Nudm_UECM.yaml")` -- the actual
shape, from its actual owning file -- never the pass-through wrapper. `schema_to_ir.py`'s
`Converter.convert_files` seeding loop (which enqueues every locally-defined schema of a pilot
file directly, without going through `resolve_ref`) now skips any entry where `pure_ref_target`
returns non-`None` -- these names never need their own IR type; anything that references them by
name resolves straight through to the real target instead.

**Verification:** regenerated from scratch -- type count dropped 1549 -> 1512 (the ~25 spurious
disconnected types no longer generated) and opaque-fallback count dropped 154 -> 100 (exactly
matching -- these were misclassified as opaque before). Direct inspection: exactly one
`struct SmfRegistration`/`Amf3GppAccessRegistration`/`AccessAndMobilitySubscriptionData` each now
exist, matching UDM's already-generated definitions (confirmed via `grep -c`). Minimal standalone
`g++ -fsyntax-only` compile using all three types together succeeds. Full project rebuild from a
clean configure succeeds; all 25 pre-existing tests pass unchanged -- purely a correctness fix
(fewer, more correct types), no behavior change for anything that already compiled correctly.

**Rejected alternative:** special-case UDR's route handlers to use the (wrongly) disambiguated
`*_Subscription_Data` opaque types directly, treating them as `nlohmann::json` passthrough.
Rejected as a direct violation of CLAUDE.md's "never hand-write a DTO the YAML can generate" rule
in spirit -- these fields have full, real, already-generated typed shapes one file over; using an
opaque passthrough instead would be pure workaround, not a fix, and would have left UDR's DTOs
structurally inconsistent with UDM's for what are explicitly, per the spec text itself, the exact
same schema.

**Consequence:** this is the third real generator bug found only by actually compiling generated
output against a growing pilot-file set (after ADR-0017's cross-file name collision and ADR-0022's
topo-sort/alias-ordering bug). The pure-$ref-reexport pattern is likely to recur as more NF YAML
files reference each other's schemas this way (a natural, common pattern once enough of the API
surface is covered) -- this fix generalizes to any future occurrence, not just this file pair.

---

## ADR-0025: UDR (fifth NF) -- Nudr_DataRepository context-data group

**Date:** 2026-08-06
**Status:** Accepted

**Context:** Fifth NF in the agreed Phase 2 build order (NRF -> AMF -> SMF -> UDM -> UDR -> AUSF ->
PCF). Source: `specs/5G_APIs-REL-19/TS29505_Subscription_Data.yaml` (the file `TS29504_Nudr_DR.
yaml`'s paths `$ref` into for their request/response schemas -- TS29504 itself defines almost no
schemas of its own, just path aggregation), commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`.

**Scope agreed with the user before implementation:** the `context-data` group only --
`QueryAmfContext3gpp`/`CreateAmfContext3gpp`/`AmfContext3gpp` (AMF 3GPP-access context, singular
per UE -- confirmed by reading the YAML that no delete operation exists for this resource, not
assumed) and `QuerySmfRegList`/`QuerySmfRegistration`/`CreateOrUpdateSmfRegistration`/
`UpdateSmfContext`/`DeleteSmfRegistration` (SMF registration context, one per UE+pduSessionId).
User explicitly declined ("UDR standalone this turn") to wire UDM's existing `AmfRegistrationStore`
/`SmfRegistrationStore` to call through to UDR in this same turn, even though that is UDR's real
intended role as 3GPP's data-repository NF behind UDM -- kept as a separate, deliberate future turn
touching already-committed UDM code, not silently done here.

**Deliberately deferred, not dropped:** the `provisioned-data` group (am-data/smf-selection-
subscription-data/sm-data) is GET-only in this spec with no way to provision it through the API at
all, so implementing it now would just be another permanently-empty stub, no more useful than
UDM's existing disclosed `GetAmData`/`GetSmfSelData`/`GetSmData` stub. Also deferred:
authentication-data (AUSF doesn't exist yet in this build order), ue-update-confirmation-data
(SoR/UPU), context-data's other sub-resources (non-3gpp-access, smsf-3gpp/non-3gpp, ip-sm-gw, mwd,
roaming-information, pei-info, ee-subscriptions, sdm-subscriptions, nidd-authorizations),
operator-specific-data, lcs-*, pp-data, group-data, shared-data, subs-to-notify, and all of
`TS29504_Nudr_GroupIDmap.yaml`.

**RFC 6902 JSON Patch, not RFC 7396 Merge Patch:** `AmfContext3gpp` and `UpdateSmfContext` both use
`application/json-patch+json` (confirmed by reading the YAML directly), unlike UDM's
`Update3GppRegistration`/`UpdateSmfRegistration` which use `application/merge-patch+json`. This is
a real, spec-mandated difference between the two NFs' PATCH semantics, not an inconsistency to
paper over -- applied via `nlohmann::json::patch()` (matching NRF's own `UpdateNFInstance`), not
`.merge_patch()`. `nfs/udr/src/stores.hpp`'s `AmfContextStore`/`SmfRegistrationStore` are therefore
deliberately NOT the same classes as `nfs/udm/src/stores.hpp`'s same-shaped stores, despite the
near-identical field layout, because the two NFs' patch application semantics differ. Both PATCH
responses always return 204 (the spec permits either 204-no-body or 200 with a `PatchResult` report
body listing per-operation outcomes; 204 is simpler and doesn't require fabricating report items
with no real per-op tracking behind them -- a disclosed, deliberate choice, not an oversight).

**Decision:** implemented as `nfs/udr` (port 7781, metrics 9468, `kApiRoot = "/nudr-dr/v2"`),
following the same structural pattern as every prior NF: NRF registration/heartbeat on a background
thread (`run_nrf_lifecycle`, reusing the pattern from ADR-0006/ADR-0019), OAuth2 JWT verification
against NRF's fixed `kNrfInstanceId` (ADR-0018), TLS 1.3 + mTLS via the shared lab PKI, Prometheus
metrics, ProblemDetails error responses per TS 29.500. 8 routes total across the two resource
groups; PUT returns 201+body+Location on create, 204 no-body on replace (idempotent-replace
semantics, verified by re-PUTting the same resource and confirming 204 not a second 201).

**Verification:** manual `curl` end-to-end for all 8 operations, including a real
`[{"op":"replace","path":"/guami/amfId","value":"FEDCBA"}]` RFC 6902 patch call confirmed to change
only the targeted field while `amfInstanceId`/`deregCallbackUri` survive untouched, and Prometheus
metrics confirmed via `curl http://127.0.0.1:9468/metrics`. 3 new real subprocess-to-subprocess
integration tests (`tests/integration/test_udr_context_data.cpp`): AMF context full lifecycle
(create, idempotent-replace 204-not-201, get, RFC 6902 patch, get-after-patch confirming only the
patched field changed), SMF registration full lifecycle (create, list-for-ue collection GET via the
real generated `sbi_gen::SmfRegList` type, retrieve individual, RFC 6902 patch adding a new field,
delete, 404-after-delete), and the 404/401 error paths (nonexistent ueId, tampered bearer token).
All 28 project tests pass, stable across repeated runs.

**Rejected alternative:** wire UDM's stores to UDR in this same turn (making UDR the real backing
store 3GPP intends it to be). Rejected per explicit user decision -- kept UDR's turn scoped to
standing up the NF's own API surface and verifying it independently first, deferring the
UDM<->UDR integration to a dedicated future turn where it can be reviewed on its own.

**Consequence:** adding this turn's codegen pilot file (`TS29505_Subscription_Data.yaml`) surfaced
one more real `sbi-codegen` bug before UDR's own code could be written -- see ADR-0024
(pure-`$ref`-only schema re-export handling). A generator-level fix, not a UDR-specific workaround,
benefiting every NF generated from this point forward.

---

## ADR-0026: libs/aka-crypto (Milenage + TS 33.501 Annex A + EAP-AKA') and UDM's Nudm_UEAU

**Date:** 2026-08-06
**Status:** Accepted

**Context:** Phase 2 build order is NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF; AUSF (next NF)
cannot do anything real without a UDM that can generate authentication vectors, and AUSF's own
Nausf_UEAuthentication/EAP-AKA' handshake needs a from-scratch MILENAGE + TS 33.501 key-derivation
implementation regardless of which NF calls it first. Rather than build that crypto inline inside
AUSF's turn (a second full NF plus a from-scratch cryptographic primitive in one turn), this turn
extracted the crypto into its own library (`libs/aka-crypto`) and used it to add Nudm_UEAU to the
already-committed UDM (ADR-0023's rejected alternative -- deferred then, done now that AUSF's turn
actually needs it) -- same precedent as UDR's turn extending nothing but adding a new NF; here it's
the reverse, one new library plus one extension to an existing NF, no second full NF. AUSF itself
remains a future turn: `TS29509_Nausf_UEAuthentication.yaml` was added to `libs/sbi-generated`'s
codegen pilot files this turn (so its generated DTOs exist and were exercised for compile-cleanliness)
but no `nfs/ausf` binary was created -- confirmed by `CMakeLists.txt` not gaining an
`add_subdirectory(nfs/ausf)` line.

**Source:** `specs/5G_APIs-REL-19/TS29503_Nudm_UEAU.yaml` (Nudm_UEAU's GenerateAuthData/ConfirmAuth/
DeleteAuth operations), commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`. The crypto itself --
MILENAGE (TS 35.205/35.206), the generic KDF (TS 33.220 Annex B.2.0), and the 5G-specific key
derivations (TS 33.501 Annex A) -- has no OpenAPI YAML representation (it's stage-2/stage-3 crypto,
not an SBI schema); implemented from the public algorithm definitions rather than any generated
DTO, per CLAUDE.md's carve-out for "where stage-3 YAML is genuinely missing... implement against
stage-2 and mark the gap explicitly." EAP-AKA' packet framing is RFC 5448 (key derivation) + RFC
4187 Section 8 (attribute TLV format), not 3GPP YAML at all -- AUSF's own future turn will wire
`libs/aka-crypto/include/aka_crypto/eap_aka_prime.hpp` to `TS29509_Nausf_UEAuthentication.yaml`'s
`EapSession`/`UEAuthenticationCtx` schemas (base64 `EapPayload` string fields).

**Scope agreed with the user before implementation:** `libs/aka-crypto` implements MILENAGE's f1
(network authentication, MAC-A) and f2/f3/f4/f5 (RES, CK, IK, AK) -- deliberately NOT f1*/f5*
(resynchronisation MAC-S/AK*), so there is no AUTS/SQN-resynchronisation support anywhere in this
build; `AuthenticationSubscriptionStore`'s SQN is a bare monotonically-incrementing 48-bit counter
with no windowing, the simplest possible SQN scheme that still produces a fresh, valid vector on
every call. On top of MILENAGE: the TS 33.501 Annex A derivations AUSF/UDM need (KAUSF, CK'/IK',
RES*/XRES*, HRES*/HXRES*, KSEAF) and RFC 5448/4187's EAP-AKA' Challenge-only packet exchange
(Request/Response/Success/Failure/Client-Error; not AKA-Identity, Notification, Reauthentication).
UDM's Nudm_UEAU scope is GenerateAuthData (5G-AKA and EAP-AKA' vector generation), ConfirmAuth, and
DeleteAuth only -- GetRgAuthData (5G-RG), GenerateAv (EPS/IMS/HSS via `TS29562_Nhss_imsUEAU.yaml`'s
merged schemas), GenerateGbaAv, and GenerateProseAV are out of this build's Tier-1 5G-AKA scope and
deferred, not dropped.

**Verified against real published vectors, not self-consistency alone:** `tests/conformance/
test_milenage.cpp` checks the from-scratch MILENAGE implementation against 3GPP TS 35.207 Test Set
1 (OPc, MAC-A, RES, CK, IK, AK all match the published values exactly), cross-checked this turn
against two independent sources (the `milenage` Rust crate's docs.rs page and the
`mitshell/CryptoMobile` Python test suite) before being trusted, per the project's fabrication-is-
the-worst-failure-mode rule -- a subtle rotation-amount or constant-byte bug in a hand-rolled MILENAGE
port would otherwise silently produce self-consistent-but-wrong keys, the worst possible failure
mode for authentication crypto (looks like it works; every derived key is wrong). The TS 33.501
Annex A FC values (0x6A/0x20/0x6B/0x6C) and each derivation's parameter list were cross-checked
against free5GC's `github.com/free5gc/util/ueauth` (Apache-2.0, consulted as reference reading only
per CLAUDE.md's build-vs-fork policy, not vendored) rather than trusted from memory alone.
`tests/conformance/test_eap_aka_prime.cpp` verifies PRF'/MK derivation determinism and key-
sensitivity, full Request/Response packet round-trips including real AT_MAC computation and
verification, and that both a tampered MAC and a wrong K_aut are correctly rejected.

**Decision:** `libs/aka-crypto` (new static library, links `OpenSSL::Crypto` for AES-128-ECB/HMAC-
SHA-256/SHA-256/RAND_bytes) with `milenage.hpp/cpp`, `kdf.hpp/cpp`, `eap_aka_prime.hpp/cpp`, and a
small `hex.hpp/cpp` helper. `nfs/udm` gains `AuthenticationSubscriptionStore` (seeded at startup
with two fixed test subscribers -- an 5G_AKA one and an EAP_AKA_PRIME one, both using the real TS
35.207 Test Set 1 K/OP/SQN/AMF values, not invented numbers, so an external test client can
independently verify UDM's output against the same published vector) and `AuthEventStore`, plus
three new routes under `/nudm-ueau/v1` (`nfs/udm/src/main.cpp`): `POST .../generate-auth-data`,
`POST .../auth-events` (ConfirmAuth), `PUT .../auth-events/{authEventId}` (DeleteAuth -- a PUT, not
a DELETE, per the YAML: confirmed by reading `TS29503_Nudm_UEAU.yaml` directly, not assumed from the
operationId's name). GenerateAuthData branches on the subscriber's stored `authentication_method` to
return either `Av5GHeAka` (RAND/XRES\*/AUTN/KAUSF) or `AvEapAkaPrime` (RAND/XRES/AUTN/CK'/IK'), both
real generated DTOs from `TS29503_Nudm_UEAU_grp.hpp`, not hand-written structs. AK/SQN⊕AK/AUTN
construction, RES\*/KAUSF/CK'/IK' derivation all real, wired end to end -- not stubbed.

**Disclosed simplification, real gap, not silently hidden:** `AuthenticationSubscriptionStore` is
the same kind of gap `docs/DECISIONS.md`'s prior ADRs already disclosed for UDM/UDR -- real
deployments provision K/OPc/SQN/AMF via UDR's authentication-data group, which UDR's own turn
(ADR-0025) explicitly deferred. Two hardcoded test subscribers, not provisionable through any API.
Real SUCI de-concealment (TS 33.501 Annex C, ECIES over the home network's public key) is NOT
implemented -- `generate-auth-data`'s `supiOrSuci` path parameter must be passed a SUPI-formatted
id; a real SUCI would 404. Disclosed in `nfs/udm/src/main.cpp`'s ProblemDetails error message
itself, not just in this ADR, so it's visible to an actual API caller, not only a reader of this
file.

**Verification:** `tests/conformance/test_milenage.cpp` and `test_eap_aka_prime.cpp` (8 new unit
tests, all against real vectors or real round-trip crypto, not placeholder assertions) plus 4 new
real subprocess-to-subprocess integration tests (`tests/integration/test_udm_ueau.cpp`, following
the same `nrf`+`udm` real-TLS-1.3+mTLS+signed-JWT pattern as every prior NF's integration tests):
GenerateAuthData against the 5G_AKA subscriber confirms two consecutive calls produce distinct
RAND/AUTN/KAUSF (proving the SQN-advance-per-call and fresh-RAND-per-call behaviour is real, not
cached), GenerateAuthData against the EAP_AKA_PRIME subscriber confirms the `AvEapAkaPrime` branch
and correctly-sized CK'/IK'/XRES fields, ConfirmAuth-then-DeleteAuth confirms the full
create/Location-header/delete/404-on-redelete lifecycle, and the 404 (unknown SUPI, with the
SUCI-not-implemented message)/401 (tampered bearer token) error paths. All 45 project tests pass
(37 pre-existing + 4 conformance + 4 integration), stable across repeated runs.

**Rejected alternative:** build AUSF and its EAP-AKA'/5G-AKA crypto together in one turn, treating
UDM's GenerateAuthData as an inline stub returning fabricated key material until AUSF needed real
values. Rejected because a fabricated authentication vector is exactly the kind of thing CLAUDE.md
calls out as the project's worst failure mode -- if UDM's stub keys were ever compared against
AUSF's real MILENAGE computation, they'd silently mismatch in a way that looks like an AUSF bug, not
a UDM shortcut, wasting a future turn's debugging time. Building the real crypto now, backed by
published test vectors, means AUSF's own turn can trust UDM's output immediately, and can focus
its scope purely on the Nausf_UEAuthentication SBI surface and the AUSF-side of the EAP-AKA'
exchange, not on re-deriving MILENAGE from scratch a second time.

**Consequence:** `libs/aka-crypto` is now a shared dependency AUSF's own turn will link against
directly (for `eap_aka_prime.hpp`'s Request/Response packet builders and `kdf.hpp`'s KSEAF
derivation) rather than re-implementing -- one crypto implementation, verified once against
published vectors, used by both NFs that need it, consistent with CLAUDE.md's "never hand-write a
DTO the YAML can generate" spirit extended to "never hand-roll a second copy of already-verified
crypto." Also surfaces the next real decision AUSF's turn will have to make explicitly: `AKAV1_HXRES`
comparison logic (SEAF forwards HRES* for the network to compare against AUSF's HXRES* before AUSF
will release KSEAF) lives partly in AMF/SEAF territory (TS 23.502 §4.2.2.2.2, N12/N8), which this
build hasn't touched yet either -- not resolved by this turn, flagged for when AUSF's own procedure
list is proposed.

---

## ADR-0027: AUSF (sixth NF) -- Nausf_UEAuthentication ue-authentications group

**Date:** 2026-08-06
**Status:** Accepted

**Context:** Sixth NF in the agreed Phase 2 build order (NRF -> AMF -> SMF -> UDM -> UDR -> AUSF ->
PCF). Source: `specs/5G_APIs-REL-19/TS29509_Nausf_UEAuthentication.yaml`, commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6` (added to `libs/sbi-generated`'s codegen pilot files in
ADR-0026's turn, in preparation for this one). This is the first NF in the build whose own request
handlers make a real synchronous SBI client call to another NF's business API (UDM's
`Nudm_UEAU_GenerateAuthData`) rather than only to NRF for registration/OAuth2 -- every prior NF's
only outbound client role was `run_nrf_lifecycle`.

**Scope agreed with the user before implementation:** the `ue-authentications` resource group only
-- `POST /ue-authentications` (initiate; calls UDM), `PUT .../5g-aka-confirmation`
(Confirm5gAkaAuthentication), `DELETE .../5g-aka-confirmation` (Delete5gAkaAuthenticationResult),
`POST .../eap-session` (EapAuthMethod), `DELETE .../eap-session` (DeleteEapAuthenticationResult),
`POST .../deregister` (UEAuthenticationsDeregister). `/rg-authentications` (5G-RG) and
`/prose-authentications` + `/prose-authentications/{authCtxId}/prose-auth` (ProSe) deliberately
deferred, not dropped -- same Tier-1 5G-AKA-for-a-normal-UE boundary ADR-0026 already drew for
UDM's Nudm_UEAU turn.

**Disclosed simplification, stated up front:** AUSF does NOT call UDM's ConfirmAuth/DeleteAuth
(`Nudm_UEAuthentication_ResultConfirmation`) after an authentication completes, even though UDM's
ConfirmAuth/DeleteAuth were built in ADR-0026 anticipating exactly this caller. Wiring that up is a
real design decision of its own (sync-in-the-response-path vs. fire-and-forget, what to do if UDM
is unreachable at that point) that wasn't part of the scope agreed for this turn -- explicitly
deferred to a dedicated future turn, not silently done or silently skipped. SUCI de-concealment is
also not implemented, same disclosed gap as UDM's GenerateAuthData (ADR-0026):
`AuthenticationInfo.supiOrSuci` is passed straight through to UDM, so a real SUCI-formatted id
404s exactly like it does calling UDM directly.

**KAUSF for EAP-AKA' -- resolved with the user mid-implementation, not assumed:** the file header
comment proposed to the user before coding claimed "KAUSF = EMSK" for the EAP-AKA' path, citing TS
33.501 §6.1.3.1 from memory. Implementing it exposed the claim was likely wrong before any code
was trusted: RFC 5448's EMSK is 64 bytes, but every `Kausf` field across both YAML files is 32
bytes (`pattern: '[A-Fa-f0-9]{64}'`, i.e. 64 hex chars), a dimensional mismatch that should not
exist if EMSK were really used unmodified. Per CLAUDE.md's "if unsure, say so plainly" rule, this
was raised back to the user explicitly (with the size-mismatch evidence) rather than silently
picking a reconciliation (e.g. truncating EMSK) or silently proceeding on the original unverified
claim -- neither of which had a spec citation behind it. Resolved: KAUSF for EAP-AKA' is derived
via the *same* Annex A.2 generic-KDF function already implemented for 5G-AKA
(`aka_crypto::derive_kausf`, FC=0x6A), just keyed on CK'/IK' instead of CK/IK -- dimensionally
consistent (32 bytes) and consistent with Annex A's one-KDF-family-different-key-inputs structure.
This is still not a spec citation (no TS 33.501 text was available locally to confirm either way --
`specs/` only holds the OpenAPI YAML, not the normative spec PDF), and is disclosed as the AUSF's
best-available reconstruction, not a confirmed-correct citation. SQN xor AK for this derivation is
read directly out of AUTN's own first 6 bytes (AUSF never learns SQN any other way either, same as
a real UE/USIM) -- see `nfs/ausf/src/main.cpp`'s EAP-AKA' branch and `nfs/ausf/src/stores.hpp`.

**Real bug found and fixed in already-committed `libs/aka-crypto` code (ADR-0026's turn), not new
code from this turn:** `tests/integration/test_ausf_ue_authentication.cpp` -- which plays the
UE/USIM role, independently re-deriving every key from the same TS 35.207 (K, OP) values
`nfs/udm/src/main.cpp` seeds its test subscribers with, then cross-checks AUSF's output against
that independent computation -- caught `aka_crypto::eap::derive_keys()` producing a *different*
K_aut in AUSF's process than in the test's own process, given byte-identical (CK', IK', identity)
inputs (confirmed identical via temporary debug instrumentation on both sides before concluding
this). Root cause: `libs/aka-crypto/src/eap_aka_prime.cpp`'s seed construction was
`std::vector<uint8_t> seed(std::string("EAP-AKA'").begin(), std::string("EAP-AKA'").end());` --
this evaluates `std::string("EAP-AKA'")` **twice**, constructing two distinct temporary objects,
and takes `.begin()` from one and `.end()` from the other. Iterating a range between two unrelated
objects' iterators is undefined behavior, not "the same short string twice" -- in practice this
read whatever stack memory happened to sit between the two (short-string-optimized) temporaries,
producing a seed with the right prefix and suffix but ~24-52 bytes of garbage silently spliced into
the middle, varying by process/stack layout. Every existing unit test in `test_eap_aka_prime.cpp`
missed this because each one only ever called `derive_keys` once and checked self-consistency
(non-zero, keys differ from each other) -- never checked it was *deterministic* for identical
inputs, which is the one property that would have failed. Fixed by storing the label in a named
`static const std::string` before taking iterators from it (`libs/aka-crypto/src/eap_aka_prime.cpp`),
and a regression test added directly to close the gap:
`EapAkaPrime.DeriveKeysIsDeterministicForIdenticalInputs`
(`tests/conformance/test_eap_aka_prime.cpp`) calls `derive_keys` twice with byte-identical inputs
and asserts byte-identical output -- the buggy version would have failed this. Grepped the rest of
the codebase for the same `std::string("...").begin(), std::string("...").end()` two-temporaries
pattern; no other occurrences found.

**Decision:** implemented as `nfs/ausf` (port 7782, metrics 9469, `kApiRoot = "/nausf-auth/v1"`),
following the same structural pattern as every prior NF: NRF registration/heartbeat on a background
thread (`run_nrf_lifecycle`), OAuth2 JWT verification against NRF's fixed `kNrfInstanceId`
(ADR-0018), TLS 1.3 + mTLS via the shared lab PKI (new `certs/ausf/` leaf cert added via
`scripts/gen-lab-pki.sh ausf`), Prometheus metrics, ProblemDetails error responses per TS 29.500.
A second, separate `http2::Client`/`OAuth2Client` pair (distinct from `run_nrf_lifecycle`'s) is used
from route handlers to call UDM -- safe without a mutex because every route handler runs on the
server's single `io_context` thread, never concurrently with each other or with the lifecycle
thread's own separate client instance. `ausf::AuthContextStore` (`nfs/ausf/src/stores.hpp`) holds
per-`authCtxId` state (XRES*/KAUSF for 5G-AKA; K_aut/XRES/KAUSF/MSK for EAP-AKA') between
`POST /ue-authentications` and the later confirmation calls, in-memory only, no expiry -- same
disclosed simplification as every other NF's store so far. UDM's 404 (unknown SUPI) is mapped to
AUSF's own documented 404 ("User does not exist in the HPLMN", the exact YAML response wording, not
paraphrased); UDM unreachable or any other non-200 maps to a 500. `PUT .../5g-aka-confirmation`
returns HTTP 200 for both a matching and a mismatching RES* (the spec documents one response code
covering both outcomes; only the body's `authResult` differs) -- confirmed by reading the YAML, not
assumed from the 5G-AKA convention of "wrong answer = different status".

**Verification:** manual `curl` end-to-end (recorded in `docs/TRACEABILITY.md`) plus 6 new real
subprocess-to-subprocess integration tests (`tests/integration/test_ausf_ue_authentication.cpp`,
spawning real `nrf`+`udm`+`ausf`, real TLS 1.3 + mTLS, real signed JWT): a full 5G-AKA lifecycle
that independently re-derives HXRES* and KSEAF from the same (K, OP) test vector and asserts
byte-for-byte equality with AUSF's own output (not just a 2xx status); a wrong-RES* case asserting
`AUTHENTICATION_FAILURE` with no KSEAF; a full EAP-AKA' lifecycle that independently verifies
AUSF's Request/Challenge AT_MAC, builds a real Response/Challenge packet, and cross-checks the
returned KSEAF/MSK; a wrong-RES EAP-AKA' case asserting a real EAP-Failure packet comes back; a
deregister-then-404-on-second-deregister case; and the 404 (unknown SUPI)/401 (tampered token)
error paths. Plus the crypto library's own regression test (`EapAkaPrime.
DeriveKeysIsDeterministicForIdenticalInputs`) added alongside the bug fix. All 52 project tests
pass, stable across repeated runs (verified twice in a row). Docker image built and verified in
this environment (`docker build -f deploy/docker/ausf.Dockerfile -t 5gc-ausf:test .`); `deploy/
docker/docker-compose.yml` gained an `ausf` service (depends on both `nrf` and `udm` being started,
the first NF whose compose dependency isn't just `nrf`); `deploy/helm/ausf/` mirrors `deploy/helm/
udr/`, including the same disclosed cross-chart shared-PKI gap. `docker compose up` (all six
containers actually mTLS-registering and AUSF actually calling UDM) was NOT run this session --
same disclosed-not-silently-assumed gap ADR-0014 already recorded for NRF alone; `helm install`/
`helm template` also not run, same as every prior chart.

**Rejected alternative:** reconcile the KAUSF/EMSK size mismatch by truncating or hashing EMSK down
to 32 bytes to keep the originally-proposed "KAUSF = EMSK" claim technically satisfiable. Rejected
because that reconciliation step itself would have been invented with no spec citation -- exactly
the fabrication CLAUDE.md's guardrails exist to prevent, just moved one level down (inventing *how*
to make EMSK fit, instead of inventing whether EMSK applies at all).

**Consequence:** the derive_keys bug fix changes EAP-AKA' key output for every caller (there was
only ever one call site before this turn added the second) -- no behavioral compatibility concern
since nothing depended on the old, wrong, non-deterministic values. This turn's cross-process
integration-test pattern (independently re-deriving keys in the test client rather than only
round-tripping the NF's own numbers) found a real bug that three separate unit tests in the prior
turn did not; worth keeping as the default verification bar for any future crypto-adjacent NF work,
not just a one-off for this turn.

---

## ADR-0028: PCF (seventh NF) -- Npcf_AMPolicyControl + Npcf_SMPolicyControl

**Date:** 2026-08-07
**Status:** Accepted

**Context:** Seventh and, per the originally-agreed Phase 2 build order (NRF -> AMF -> SMF -> UDM
-> UDR -> AUSF -> PCF), final NF in that list. Source: `specs/5G_APIs-REL-19/
TS29507_Npcf_AMPolicyControl.yaml` + `TS29512_Npcf_SMPolicyControl.yaml`, commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`, added as new codegen pilot files this turn.

**Scope agreed with the user before implementation:** the two services CLAUDE.md's stated Phase 2
end goal actually needs -- `Npcf_AMPolicyControl`'s `CreateIndividualAMPolicyAssociation`
(`POST /policies`), `ReadIndividualAMPolicyAssociation` (`GET /policies/{polAssoId}`),
`DeleteIndividualAMPolicyAssociation` (`DELETE /policies/{polAssoId}`),
`ReportObservedEventTriggersForIndividualAMPolicyAssociation`
(`POST /policies/{polAssoId}/update`); and `Npcf_SMPolicyControl`'s `CreateSMPolicy`
(`POST /sm-policies`), `GetSMPolicy` (`GET /sm-policies/{smPolicyId}`), `UpdateSMPolicy`
(`POST /sm-policies/{smPolicyId}/update`), `DeleteSMPolicy` (`POST /sm-policies/{smPolicyId}/
delete`).

**Deliberately deferred, not dropped:** both services' callback notifications (`PolicyUpdate`/
`TerminationNotification`, `SmPolicyNotification`, pushed BY PCF TO the `notificationUri` AMF/SMF
supply) -- neither AMF nor SMF has a receiver for these, same shape as every other proactive/
callback flow this build has deferred (AMF's own N1N2, UDM's SDM subscriptions, etc. never got a
PCF-side push implemented either). `Npcf_PolicyAuthorization` (AF/Rx-style), `Npcf_UEPolicyControl`
(URSP), `Npcf_EventExposure`, `Npcf_BDTPolicyControl`, `Npcf_PDTQPolicyControl`,
`Npcf_AMPolicyAuthorization`, `Npcf_MBSPolicyControl`/`Authorization` -- separate PCF sub-services,
out of scope for the core registration/PDU-session flows. Most importantly: **wiring AMF/SMF to
actually call this PCF** -- this turn stands up PCF's own API surface + tests standalone, same
precedent as UDR's turn (ADR-0025) and UDM's Nudm_UEAU turn (ADR-0026, before AUSF called it in
ADR-0027) -- a deliberate future turn touching already-committed AMF/SMF code, reviewable on its
own, not silently done or silently skipped.

**Disclosed simplification, stated up front:** real PCF policy decisions are computed from
subscriber data UDR would hold (via `Npcf`'s own UDR client for the policy-data group), which
UDR's turn (ADR-0025) never implemented (UDR's `provisioned-data` group is GET-only with nothing to
provision through the API at all). So `CreateSMPolicy`/`UpdateSMPolicy` here return one default
`SessionRule` per SM Policy: the request's own `subsSessAmbr`/`subsDefQos` when the caller supplies
them (so the decision at least reflects what was actually asked for), falling back to a fixed
1 Gbps/1 Gbps session AMBR and 5QI 9 (non-GBR, the one number in this default actually drawn from a
real TS 23.501 5QI table) otherwise. The ARP priority level used in that fallback (8) is an
arbitrary placeholder with **no** spec citation -- disclosed both in `nfs/pcf/src/main.cpp`'s file
header and again inline at the point it's constructed, not just here, since it's the one field with
zero spec backing among otherwise-real defaults. `UpdateSMPolicy` similarly just re-derives the same
decision from the originally-stored context rather than doing any real trigger-driven
re-evaluation of the reported `SmPolicyUpdateContextData` -- there is no policy engine behind this,
by design, for the reason above.

**`SmPolicyUpdateContextData` is an opaque `nlohmann::json` fallback, not hand-modeled:** confirmed
by inspecting the generated header, not assumed -- its schema is an `allOf` with a `not` keyword
tools/sbi-codegen doesn't handle (documented opaque-fallback pattern, same category as
`AuthenticationVector`'s `oneOf`+`discriminator` fallback from ADR-0026's UDM turn). `UpdateSMPolicy`'s
handler accepts it as raw parsed JSON rather than requiring it to match a typed DTO -- and since
this build's decisioning doesn't actually branch on the reported triggers anyway (see the
simplification above), nothing is lost by not having a typed shape for it this turn.

**Cross-file `$ref` closure merged AM/SM Policy Control's types into the same giant
`TS29122_CommonData_grp.hpp` as everything else, confirmed not assumed:** `PolicyAssociation`,
`SmPolicyContextData`, etc. did NOT land in their own `TS29507_*.hpp`/`TS29512_*.hpp` files the way
earlier single-service NFs' types did -- `chfInfo`/`sliceReplReq`/`uePolFailReport` and friends
transitively `$ref` into `TS29512`/`TS29534`/`TS29525`/`TS29522`/`TS29520`/... forming one large
cross-file reference cycle that tools/sbi-codegen's existing cycle-merge logic (ADR-0010) folds
into the same merged group nearly every other NF's common types already live in. Not a new
generator bug -- same documented behavior UDM's Nudm_UEAU turn already exercised (ADR-0026, the
UEAU+imsUEAU merge), just a much larger cycle this time. `nfs/pcf/src/main.cpp` includes
`TS29122_CommonData_grp.hpp` for this reason, same as most other NFs already do.

**Decision:** implemented as `nfs/pcf` (port 7783, metrics 9470, `kAmApiRoot =
"/npcf-am-policy-control/v1"`, `kSmApiRoot = "/npcf-smpolicycontrol/v1"`), following the same
structural pattern as every prior standalone-turn NF: NRF registration/heartbeat on a background
thread, OAuth2 JWT verification against NRF's fixed `kNrfInstanceId` (ADR-0018), TLS 1.3 + mTLS via
the shared lab PKI (new `certs/pcf/` leaf cert), Prometheus metrics, ProblemDetails error handling.
`AmPolicyStore`/`SmPolicyStore` (`nfs/pcf/src/stores.hpp`) hold plain `nlohmann::json` rather than
generated structs -- both `PolicyAssociation` and `SmPolicyControl` are dozens-of-optional-fields
DTOs that every route handler already builds/reads as full JSON, so a typed store would just be a
second, redundant place these fields could drift from the wire format (same reasoning as UDM's
`SdmSubscriptionStore`/`AuthEventStore`). `CreateSMPolicy` returns just the `SmPolicyDecision`
(HTTP 201 body, per the YAML) while `GetSMPolicy` returns the full `SmPolicyControl`
(`{context, policy}`, per the YAML) -- two different response shapes for the same underlying stored
resource, confirmed from the spec, not assumed to be symmetric.

**Verification:** manual `curl` end-to-end (recorded in `docs/TRACEABILITY.md`) plus 4 new real
subprocess-to-subprocess integration tests (`tests/integration/test_pcf_policy_control.cpp`,
spawning real `nrf`+`pcf`, real TLS 1.3 + mTLS, real signed JWT -- PCF plays no client role this
turn per the deferred-AMF/SMF-wiring decision above, so these tests act as the AMF/SMF role
directly, same as every NF's own tests before its real caller existed): a full AM Policy
Association lifecycle (create, get, report-triggers-and-update confirming the update is really
persisted not just echoed, delete, 404 after delete, 404 on re-delete); a full SM Policy lifecycle
where the request supplies its own `subsSessAmbr`/`subsDefQos` and the returned decision reflects
those exact values, not the fixed fallback; a second SM Policy case with neither field supplied,
confirming the decision falls back to the documented 1 Gbps/5QI-9 defaults; and the 404/401 error
paths. All 56 project tests pass, stable across repeated runs. Docker image built and verified in
this environment (`docker build -f deploy/docker/pcf.Dockerfile -t 5gc-pcf:test .`); `deploy/
docker/docker-compose.yml` gained a `pcf` service; `deploy/helm/pcf/` mirrors `deploy/helm/ausf/`,
including the same disclosed cross-chart shared-PKI gap. `docker compose up` and `helm install`/
`helm template` were NOT run this session -- same disclosed-not-silently-assumed gap ADR-0014
already recorded for NRF alone.

**Rejected alternative:** build a small real policy-decision engine (e.g. actually branching SM
policy on `pduSessionType`/`sliceInfo`/whether the UE requested a specific DNN) to make the
defaults feel less arbitrary. Rejected because there is no real subscriber policy data behind any
of it either way (UDR's policy-data group doesn't exist -- see the disclosed simplification above)
-- a more elaborate-looking decision function fed by nothing but the request itself would be
*more* misleading than a small, honestly-labeled fixed default, not less; the honest minimal
version is easier to audit and to replace wholesale once UDR-backed policy data is real.

**Consequence:** this turn completes Phase 2's originally-agreed seven-NF list
(NRF/AMF/SMF/UDM/UDR/AUSF/PCF) with every NF's own API surface standing up standalone and tested.
What remains before "UE registration (TS 23.502 §4.2.2.2.2) and PDU session establishment
(§4.3.2.2.1) end-to-end" (CLAUDE.md's stated Phase 2 finish line) is real, not stubbed, is entirely
inter-NF wiring: AMF calling PCF for AM policy, SMF calling PCF for SM policy (this ADR's deferred
list), AMF calling AUSF for authentication (a real flow, ADR-0027's AUSF already built the
callee side), UDM/UDR wiring (ADR-0025's deferred list), and AMF/SMF's own N2/N4 termination
(NGAP/PFCP, both explicitly out of scope for the SBI-only build so far). None of that is resolved
by this turn -- flagged here as the actual shape of what's left, not assumed away.

---

## ADR-0029: SMF -> PCF wiring (SM Policy Association Establishment/Termination); AMF -> PCF deferred

**Date:** 2026-08-07
**Status:** Accepted

**Context:** The first cross-NF wiring turn since Phase 2's seven NFs were all stood up standalone
(ADR-0025 through ADR-0028). User asked to wire both AMF and SMF to PCF; before implementing,
research turned up a real asymmetry between the two that changed the scope agreed with the user.

**SMF -> PCF has a clean, correct trigger.** `CreateSMContext` (`POST /sm-contexts`) is exactly TS
23.502 §4.3.2.2.1's SM Policy Association Establishment trigger, and `nfs/smf/src/main.cpp`'s file
header already named "no real PCF" as the specific, disclosed reason CreateSMContext couldn't do
this before PCF existed (ADR-0021).

**AMF -> PCF has no correct trigger in this build, and was NOT forced onto one.** AMF only
implements `Namf_Communication` (ADR: initial AMF turn) -- there is no NAS/N1 Registration entry
point in this codebase at all (no NGAP stack, disclosed since ADR-0016), so nothing in AMF's
current surface corresponds to "a UE just registered," which is the real trigger for AM Policy
Association Establishment (TS 23.502 §4.2.2.2.2). The closest existing AMF operation,
`CreateUEContext`, is specifically the *inter-AMF mobility/handover* operation (a UE context
arriving from a different AMF) -- attaching fresh AM Policy Association Establishment to it would
misrepresent which real procedure it belongs to (at real inter-AMF mobility, the new AMF typically
reuses/modifies an existing association, not creates one the way initial Registration does).
Presented three options to the user: (1) use `CreateUEContext` anyway, disclosed as a proxy;
(2) defer AMF->PCF entirely until AMF has a real registration-adjacent trigger; (3) write and test
the calling logic but leave it unwired. **User chose (2)** -- this turn is SMF->PCF only. AMF->PCF
remains unimplemented, not attached to the wrong procedure to force something to ship.

**Decision (SMF's side):** `CreateSMContext` is now a real SBI client to PCF -- a second
`http2::Client`/`OAuth2Client` pair (scope `npcf-smpolicycontrol`, target `PCF`), same pattern
ADR-0027 established for AUSF calling UDM (separate from `run_nrf_lifecycle`'s own client; safe
without a mutex because route handlers and the lifecycle thread never touch the same `Client`
instance). On success, the returned `SmPolicyDecision` and PCF-assigned `smPolicyId` are stored
alongside the SM context (not exposed in `SmContextCreatedData` -- TS29502 has no field for it,
matching `n2SmInfo`'s already-disclosed unpopulated state) so `ReleaseSMContext` can call PCF's
`DeleteSMPolicy` later. `UpdateSMContext` is NOT wired to PCF's `UpdateSMPolicy` this turn --
scope stayed to the two operations TS 23.502 §4.3.2.2.1 actually needs (establishment and
termination), not every SM Policy operation that exists.

**Two real gaps, disclosed rather than smoothed over:**
- `SmContextCreateData`'s schema allows `supi`/`pduSessionId`/`dnn`/`sNssai` to all be absent
  (e.g. unauthenticated-SUPI edge cases), but PCF's `SmPolicyContextData` requires all four. This
  build's PCF wiring has nothing to fall back to without them, so `CreateSMContext` now returns
  400 if any are missing -- a real, disclosed narrowing of what this build accepts versus what the
  full spec permits, confirmed by reading `SmContextCreateData`'s generated struct (all four
  fields are `std::optional`), not assumed.
- `SmContextCreateData` has **no `pduSessionType` field at all** -- confirmed by grepping the full
  generated struct, not assumed absent. It's negotiated inside the NAS SM message (`n1SmMsg`, an
  opaque binary blob this build never decodes, the same class of gap as every other NAS-decoding
  simplification here). SMF sends a fixed default (`IPV4`) to PCF instead of the UE's real
  requested type.

**Fail-closed on create, best-effort on release -- a deliberate asymmetry, not an inconsistency:**
`CreateSMContext` fails (500) if PCF is unreachable or errors, matching TS 23.502's real intent
that SM Policy Association Establishment failure fails PDU session establishment -- verified with
a dedicated test that starts SMF without PCF running at all. `ReleaseSMContext`'s `DeleteSMPolicy`
call, by contrast, is best-effort: local release still succeeds (204) even if PCF is unreachable,
so a downstream PCF outage can't strand SMF's own cleanup path (logged on failure, not silently
swallowed) -- the same reasoning a real SMF's teardown path would use.

**Verification:** manual `curl` end-to-end (recorded in `docs/TRACEABILITY.md`) plus updates to
`tests/integration/test_smf_pdu_session.cpp`: the existing full-lifecycle test now spawns a real
`pcf` alongside `nrf`/`smf` and, after `CreateSMContext`, queries PCF *directly* for the SM Policy
it should have created (`GET /npcf-smpolicycontrol/v1/sm-policies/smpolicy-1` -- relying on PCF's
sequential id assignment from a freshly-spawned process, disclosed as a test-only assumption, not
an SMF behavior), confirming `context.supi`/`context.dnn` and the `default` session rule are real,
not just that SMF returned 201; after `ReleaseSMContext`, the same PCF query now returns 404,
proving `DeleteSMPolicy` was really called too. Two new tests: `CreateSMContextFailsClosedWhenPcfUnreachable`
(SMF spawned without PCF at all -- 500, not a silent success) and
`CreateSMContextRequiresSupiPduSessionIdDnnAndSNssai` (the old ADR-0020-era minimal body, now 400).
All 58 project tests pass, stable across three repeated runs (the PCF-dependent tests are the ones
most exposed to spawn-order flakiness, checked specifically).

**Rejected alternative:** attach AM Policy Association Establishment to `CreateUEContext` anyway
(option 1 above). Rejected per explicit user choice -- see the AMF section above.

**Consequence:** SMF is now the second NF in this build (after AUSF) whose route handlers make a
real synchronous SBI client call to another NF for actual business logic, not just to NRF. AMF->PCF
wiring remains open; per this ADR, the real blocker is a NAS/N1 Registration trigger, which is a
substantially larger effort (NGAP stack, ADR-0016) than a wiring turn -- worth surfacing as its own
decision point (build NGAP now vs. keep deferring AMF-side policy) rather than assuming the next
turn is automatically "wire AMF too."
