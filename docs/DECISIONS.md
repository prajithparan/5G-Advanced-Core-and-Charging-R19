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

---

## ADR-0030: NGAP/N2 transport infrastructure (SCTP, ASN.1 PER codegen) for AMF's Registration path

**Date:** 2026-08-07
**Status:** Accepted

**Context:** ADR-0029 left AMF->PCF wiring open specifically because there is no real NAS/N1
Registration trigger in this build. The user chose to build that trigger for real: NGAP (N2,
gNB<->AMF, TS 38.413, ASN.1 PER over SCTP) and NAS-5GS (N1, UE<->AMF, TS 24.501), ending in a real
`CreateIndividualAMPolicyAssociation` call to PCF. This ADR covers the transport/codegen
infrastructure stage (Stage 0 of a staged plan); the NGAP/NAS procedure implementation itself is
covered by later ADRs as each stage lands. Planned and approved via Claude Code's plan-mode
workflow after three parallel research passes (AMF/sbi-core conventions, UERANSIM's real NG-Setup
through Registration message sequence with file:line evidence, and the asn1c/SCTP toolchain
state) -- see the plan file's own findings for the full trail; this ADR records the decisions that
came out of it.

**SCTP: system `libsctp-dev` (kernel one-to-one sockets), not vcpkg.** vcpkg has no
kernel-SCTP-socket port -- its only SCTP-adjacent port, `usrsctp`, is Google's userspace-over-UDP
stack (built for WebRTC data channels), not a binding to the Linux kernel SCTP the way NGAP/N2
actually runs. `libsctp-dev`/`libsctp1` were already installed on this system
(`/usr/include/netinet/sctp.h` present). New `libs/ngap-core` wraps a raw
`socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP)` one-to-one socket (`accept()` returns a fully
connected per-association socket directly, the same model as a TCP listening socket -- no
`SCTP_ASSOC_CHANGE` notification handling needed, unlike a one-to-many `SOCK_SEQPACKET` socket
multiplexing several associations over one fd). `sctp_sendmsg`/`sctp_recvmsg` call shapes (PPID
placement, flag handling, `RECEIVE_BUFFER_SIZE`) were copied from
`simulators/ransim/vendor/UERANSIM/src/lib/sctp/internal.cpp` -- a working reference
implementation already building and (per ADR-0016) attempting to connect against this exact lab --
not written from the `sctp_sendmsg` man page alone. NGAP's PPID is 60, confirmed against
UERANSIM's own `src/lib/sctp/types.hpp`, not from memory. Boost.Asio (already this project's
event-loop library for SBI/HTTP2) has no native SCTP support, so `SctpSocket` is meant to be
driven from a dedicated blocking-I/O thread, the same discipline ADR-0006 already established for
`run_nrf_lifecycle`. Disclosed simplification: every message in this build uses SCTP stream 0
only; real deployments reserve stream 0 for non-UE-associated signaling and assign dynamic
per-UE streams otherwise, not needed yet at this build's single-UE scope.

**ASN.1 PER codec: `asn1c` (BSD-licensed, `vlm/asn1c`), generating our own copy from a vendored
ASN.1 module, not from UERANSIM's own generated `.c`/`.h` files.** `tools/sbi-codegen` is entirely
OpenAPI/JSON-shaped and not reusable for ASN.1 -- new `libs/ngap-generated` mirrors only
`libs/sbi-generated`'s *CMake* pattern (configure-time `execute_process`, `add_custom_command` to
regenerate on source change, `file(GLOB)` into a `STATIC` lib), not its Python codegen internals.
The exact `asn1c` invocation flags
(`-pdu=all -fcompound-names -findirect-choice -fno-include-deps -no-gen-OER -gen-PER
-no-gen-example`) were copied from UERANSIM's own generated-file header comments (which cite the
precise command line used to produce them) -- proven correct against this exact `.asn` file, not
guessed.

**Real bug found and fixed during Stage 0's own verification, not assumed away:** `-no-gen-OER`
only suppresses per-type OER function *bodies*; the shared `constr_TYPE.h` skeleton still
unconditionally `#include`s `<oer_decoder.h>`/`<oer_encoder.h>` unless the caller also defines
`ASN_DISABLE_OER_SUPPORT` at *compile* time (a separate, undocumented-in-the-codegen-flags
requirement, discovered only because the generated code was actually compiled against real
`asn1c` output rather than assumed to work from the flag list alone). Confirmed via
`simulators/ransim/vendor/UERANSIM/src/asn/asn1c/CMakeLists.txt`, which sets this exact define for
its own identically-flagged NGAP codec build -- not guessed from the compiler error alone, cross-
checked against a working reference. `libs/ngap-generated/CMakeLists.txt` now sets this
`PUBLIC` on the `ngap_generated` target.

**`asn1c` itself is not installed system-wide, and this dev environment has no passwordless
sudo.** Built from `asn1c`'s own official GitHub release tarball (`v0.9.29`, matching the version
UERANSIM's own vendored codec was generated with, confirmed via that codec's generated-file
header) into `build-tools/asn1c/` -- a release tarball, not a git checkout, ships a
pre-generated `configure` script needing no `autoconf`/`automake`/`bison`/`flex` (none of which
were available either), only a C compiler and `make`. `build-tools/` is gitignored (already
covered by the existing `build-*/` glob pattern) -- this is a local build tool, not committed,
same category as vcpkg's own downloaded packages. `find_program` in
`libs/ngap-generated/CMakeLists.txt` checks system `PATH` first, falling back to this local
install location, so a CI runner with real `apt-get install asn1c` (the expected real-world path)
needs no special-casing.

**The ASN.1 module itself is now vendored into `specs/NGAP/ngap-17.9.asn`, committed, not read
from the gitignored `simulators/ransim/vendor/` tree at build time.** Reasoning: every NF/lib in
this project must be buildable standalone per CLAUDE.md, and `simulators/ransim/vendor/` only
exists after a separate, large, AGPL-3.0-licensed third-party fetch (`fetch-and-build.sh`) that a
normal `cmake --build .` should not hard-depend on. The ASN.1 module text itself is 3GPP-published
interface specification content, not UERANSIM's own copyrightable work -- the same category as the
OpenAPI YAML already committed under `specs/5G_APIs-REL-19/`, copied here for the identical reason.
**Disclosed version mismatch, not silently treated as equivalent:** this is TS 38.413 v17.9.0
(Release 17), not REL-19 -- no REL-19 NGAP ASN.1 module was available locally (NGAP has no OpenAPI
representation to source from `specs/5G_APIs-REL-19/` the way every other NF's API surface in this
project is sourced). The procedures this build actually uses (NG Setup, Initial UE Message,
Downlink/Uplink NAS Transport) are stable across R17-R19, but this remains a real, disclosed gap
against the project's stated REL-19 target, flagged in the vendored file's own header comment as
well as here. A stray finding while writing that header comment: ASN.1 `--` comments terminate at
the next `--` token even mid-line, not just at a newline -- this project's usual C++ comment style
(using `--` freely as an em dash) is unsafe inside actual `.asn` file comments and had to be
avoided when writing the provenance header.

**Real bug found and fixed in `simulators/ransim/config/ue.yaml`, verified against source, not
assumed correct:** two of its subscriber credential fields did not match `nfs/udm/src/main.cpp`'s
already-seeded `imsi-999700000000001` test subscriber (TS 35.207 Test Set 1). `op` was UERANSIM's
own unrelated published example default, not the TS 35.207 value UDM actually seeds -- would have
failed authentication regardless of NGAP/NAS correctness. `amf` (the Authentication Management
Field) was also wrong, and matters for a reason worth stating explicitly: UERANSIM's own MAC
validation (`simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/auth.cpp`'s `calculateMilenage`,
lines ~472-521) recomputes the expected MAC-A using **this config file's configured `amf` value**,
not the AMF field actually received inside AUTN -- confirmed by reading that function directly,
not assumed from general AKA protocol knowledge (a spec-compliant USIM extracts AMF from the
received AUTN; this particular simulator's implementation does not, for whatever reason of its
own). Both fields now match UDM's seed (`op=CDC202D5123E20F62B6D676AC72CB318`, `amf=B9B9`); `key`
already matched. `protectionScheme: 0` (null-scheme SUCI, plaintext MSIN, no real ECIES needed)
was already correctly configured and did not need changing.

**Verification:** `ngap_generated` (1109 compiled objects) and `ngap_core` build clean under the
project's full `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` flag set with no
suppressions beyond the standard generated-code `-w` already used for `sbi_generated`. Full project
rebuild from a clean configure succeeds; all 58 pre-existing tests still pass, unchanged -- this
stage adds build infrastructure only, no behavior change to any existing NF.

**Rejected alternative:** skip vendoring the ASN.1 module and require
`simulators/ransim/fetch-and-build.sh` to have run before `libs/ngap-generated` can configure.
Rejected because it would make a core library's buildability depend on a separate, large,
AGPL-licensed third-party fetch that CLAUDE.md's "every NF/lib buildable standalone" rule doesn't
otherwise require of anything else in this project.

**Consequence:** Stages 1 onward (NG Setup, Initial UE Message, Authentication, Security Mode,
Registration Accept/Complete, the real PCF call) build on this transport/codegen foundation. Each
stage gets its own verification against real `nr-gnb`/`nr-ue` processes and, where deterministic,
real `gtest` unit tests -- tracked in the approved plan, reported here as later ADRs as each stage
completes.

## ADR-0031: ConcreteProtocolIE-Container ASN.1 patch, and a self-authored Aligned PER (X.691) patch for asn1c 0.9.29

**Date:** 2026-08-07
**Status:** Accepted

**Context:** ADR-0030 got `libs/ngap-generated` compiling, but Stage 1's actual goal --
AMF successfully completing NG Setup against the real `nr-gnb` binary -- required solving two
further, real problems discovered only by attempting the real build and the real interop test, not
foreseen at ADR-0030's time.

**Problem 1: asn1c 0.9.29 cannot resolve NGAP's parameterized `ProtocolIE-Container {{IEsSetParam}}`
Information Object Class syntax into a usable type.** Compiling `specs/NGAP/ngap-17.9.asn` unpatched
produced a `NGSetupRequest_t.protocolIEs` field typed as an empty `ATF_OPEN_TYPE` CHOICE with no
concrete member bound to it -- verified by inspecting the generated `.c` (`asn_MBR_ProtocolIE_Field_*`
showing `ATF_OPEN_TYPE | ATF_NOFLAGS`, `asn_OP_OPEN_TYPE`, no concrete type binding), not assumed
from a compiler warning. Confirmed via UERANSIM's own GitHub Discussions that other users hit the
same asn1c limitation on this exact file. **Fix:** `specs/NGAP/ngap-17.9.asn` is patched (see its own
header comment) to add a concrete, non-parameterized `ConcreteProtocolIE-Field`/
`ConcreteProtocolIE-Container` pair, and the `protocolIEs` field of the 6 messages this build
currently handles (`NGSetupRequest`, `NGSetupResponse`, `NGSetupFailure`, `InitialUEMessage`,
`DownlinkNASTransport`, `UplinkNASTransport`) is repointed to it instead of the real spec's
parameterized `ProtocolIE-Container {{XxxIEs}}`. This is justified, not just expedient: X.691 clause
10.9 defines an ASN.1 open type as PER-encoded identically to an octet-string-wrapped blob --
`ConcreteProtocolIE-Field`'s `value OCTET STRING` field is exactly that wrapper, manually filled in
by `nfs/amf/src/ngap_codec.cpp`'s `make_ie`/`decode_ie_value` (which PER-encode/decode the IE's real
typed value against its own type descriptor, e.g. `&asn_DEF_AMFName`, and store/load the result as
the wrapper's raw bytes) -- functionally equivalent to what the real parameterized IE-set machinery
would produce, without asn1c needing to resolve the full Information Object Class table. Each
repointed field cites this ADR and the real IE-list name inline. Other NGAP messages beyond these 6
are unaffected (still use the real, unresolvable parameterized form, but are not yet compiled by
`-pdu=all`'s auto-discovery mattering here since they're simply not referenced/used); extending
this pattern to further messages in later stages is expected to be mechanical.

**Problem 2: neither vanilla asn1c 0.9.29 nor an available fork provides usable Aligned PER.**
TS 38.413 mandates X.691 *Aligned* PER; vanilla asn1c 0.9.29 only ever implemented *Unaligned*
PER (`uper_encode`/`uper_decode` family) -- confirmed via `grep` finding zero `aper_*`/
`ATS_ALIGNED_CANONICAL_PER` symbols anywhere in its built output, and empirically: a real
`nr-gnb`-sent `NGSetupRequest` connected over real SCTP but AMF logged "failed to decode NGAP PDU"
using the Unaligned decoder. Two escape routes were evaluated and rejected before landing on a
third:
- *Copy UERANSIM's own vendored, already-Aligned-PER-capable asn1c runtime* (they ship one at
  `simulators/ransim/vendor/UERANSIM/src/asn/asn1c/`). Rejected: UERANSIM is AGPL-3.0-licensed;
  copying its code into this project's own compiled artifacts (this build's own `amf` binary) would
  contaminate Apache-2.0-licensed code with AGPL obligations. ADR-0016 already established an
  arms-length relationship with UERANSIM for exactly this reason (external test peer only, never a
  source dependency of this project's own binaries) -- reusing their code here would break that
  boundary. UERANSIM's binaries and source were, however, legitimately used throughout this stage
  as a **test oracle** (running the real `nr-gnb` binary as an unmodified external process, and
  separately compiling small standalone decode-test harnesses directly against their vendored
  sources to get detailed X.691-conformant debug traces) -- reading/running vendored code to check
  this project's own independently-authored code against a reference implementation is materially
  different from copying that code into a shipped binary, and was essential to finding every rule
  documented below.
- *`osmocom/asn1c`'s `aper-prefix` branch* (a real, published fork with genuine Aligned PER support).
  Attempted, then abandoned after two real, independent failures: (1) its **compiler** cannot parse
  the actual NGAP-17.9 ASN.1 module -- fails with a hard grammar error on the very first
  `NGAP-PROTOCOL-IES ::= { ... }` Information Object Set definition it encounters (line ~1411 of
  the vendored module), a construct that appears hundreds of times throughout the file and that
  vanilla 0.9.29's newer parser handles without issue; this fork's grammar predates whatever parser
  fix let 0.9.29 handle it. (2) Even setting compilation aside, its **runtime skeleton** turned out
  to be ABI-incompatible with vanilla 0.9.29's own generated code: 0.9.29 uses a newer, shared
  `asn_bit_data_t`/`asn_bit_outp_t` architecture (`asn_bit_data.h`) with macros like
  `ASN__DECODE_FAILED`, while the osmocom fork's lineage predates that refactor and uses older
  `asn_per_data_t`-direct types and `_ASN_DECODE_FAILED`-style macros -- confirmed by a real link/
  compile failure (`error: conflicting types for 'ENUMERATED_decode_uper'`, `'_ASN_DECODE_FAILED'
  undeclared`) when vanilla-generated per-type code was compiled against this fork's skeleton files.
  Mixing "vanilla compiler output + fork skeleton" was the first thing tried (since the fork's
  compiler couldn't even parse the file) and failed for exactly this reason.
- **Chosen approach: patch vanilla asn1c 0.9.29's own skeleton sources in place**, adding Aligned
  PER support self-authored from the X.691 standard's rules (not copied from either rejected
  source above), keeping the same struct layouts and macros vanilla 0.9.29 already uses everywhere
  else in this build. This means asn1c's *compiler* binary needed zero changes -- the per-type C
  descriptor tables it already generates correctly (including for Problem 1's concrete-container
  patch) work unmodified; only the shared, ASN.1-module-independent runtime primitives needed new
  code.

**What the patch adds, and the X.691 rules behind each piece** (full unified diff at
`scripts/patches/asn1c-aligned-per.patch`, applied by `scripts/setup-asn1c.sh` after extracting
the official `asn1c-0.9.29` release tarball -- `build-tools/` itself stays gitignored, matching
every other local build tool in this project, but the *patch* is committed since it is this
project's own original work product, not a rebuildable-on-demand artifact):

1. `asn_bit_data.{h,c}`: adds an `int aligned` field to both `asn_bit_data_t` (decode) and
   `asn_bit_outp_t` (encode), plus `asn_get_align`/`asn_put_align` primitives that consume/emit
   padding bits up to the next octet boundary (a no-op when already aligned).
2. `per_encoder.{h,c}` / `per_decoder.{h,c}`: adds `aper_encode`/`aper_encode_to_buffer`/
   `aper_encode_to_new_buffer` and `aper_decode`/`aper_decode_complete` as new top-level entry
   points, each setting the new `aligned` field before dispatching through the *same*
   `td->op->uper_encoder`/`uper_decoder` function pointer `uper_encode`/`uper_decode` already use.
   This is the crux of why a skeleton patch (not a compiler patch) suffices: asn1c generates only
   one PER codec function per type regardless of alignment -- alignment is a runtime property of
   *how the bits are packed*, decided by the low-level primitives below via this flag, not by which
   generated function is called. (The function-pointer struct field is still named
   `uper_decoder`/`uper_encoder`, a naming artifact predating this project's own aligned-PER
   addition -- misleading but left as-is throughout to minimize the diff against upstream.)
3. `per_support.h` adds four `static inline` helpers, and `per_support.c`/`OCTET_STRING.c`/
   `BIT_STRING.c`/`constr_SEQUENCE_OF.c`/`constr_SET_OF.c` call them at each point X.691 requires
   alignment. **Three distinct, real X.691 rules were needed here, each found the hard way** --
   this project's first attempt applied one rule everywhere and broke every message it touched;
   each correction below was pinned down by decoding real bytes (a real `nr-gnb`-sent
   `NGSetupRequest`, and this build's own `NGSetupResponse`) against **both** this project's own
   decoder **and** a standalone harness compiled directly against UERANSIM's own vendored asn1c
   sources as an independent reference oracle (see the licensing note above for why that's a
   legitimate, arms-length use):
   - **VALUE fields** (`aper_align_value_get_nbits`/`put_nbits`, used only by
     `uper_get_constrained_whole_number`/`uper_put_constrained_whole_number_u`, i.e. plain
     INTEGER-typed fields like NGAP's `ProcedureCode`): ALIGNED PER octet-aligns **even when the
     range fits in 8 bits or fewer** -- there is no small-range exception. Found because
     `InitiatingMessage.procedureCode` (`INTEGER(0..255)`, exactly 8 bits) decoded as `0` instead
     of the real `21` (id-NGSetup) until alignment was added here; the real value landed exactly on
     a clean octet boundary in the captured bytes.
   - **LENGTH/COUNT determinants** (`aper_align_length_get_nbits`/`put_nbits`, used by
     `uper_get_length`'s small-range fast path and by `OCTET_STRING.c`/`BIT_STRING.c`/
     `constr_SEQUENCE_OF.c`/`constr_SET_OF.c` for a SIZE-constrained string's character count or a
     SEQUENCE-OF/SET-OF's element count): the *opposite* rule -- **no** alignment when the range
     fits in 8 bits or fewer; alignment (and widening to exactly 16 bits) only kicks in for 9-16
     bits. Found because `AMFName`'s implicit length (`PrintableString (SIZE(1..150,...))`,
     `effective_bits=8`) decoded as length `1` instead of the real `11` when the VALUE-field rule
     was (wrongly) applied here too -- cross-checked byte-for-byte against UERANSIM's own reference
     decoder's debug trace, which reads the same 8-bit length with zero padding immediately after
     the preceding 1-bit size-extension flag.
   - **CHOICE presence index and ENUMERATED root-value index** (`constr_CHOICE.c`,
     `NativeEnumerated.c`): neither rule applies -- these two have their own dedicated X.691
     procedures and are **never** octet-aligned, identical to Unaligned PER, regardless of range.
     No helper call at all; deliberately reverted back to plain `per_get_few_bits`/`per_put_few_bits`
     after an earlier attempt wrongly aligned these too. Confirmed via the same real capture: the
     1-bit extension-presence flag and 2-bit `NGAP-PDU` CHOICE index that precede `procedureCode`
     are packed with zero padding between them.
   - `per_opentype.c`'s open-type wrapper (used for NGAP's real, non-patched open types, and
     internally structurally identical to what this project's own `ConcreteProtocolIE-Field.value`
     OCTET STRING does by hand) needed two separate fixes: its decode-side `memset(&spd, 0, ...)`
     was silently zeroing the new `aligned` field for the nested sub-decode (forcing every open
     type's *contents* into Unaligned PER regardless of the outer call -- since this project's own
     concrete-container IEs are wrapped exactly this way, this single bug affected every NGAP
     message field this project handles, not just real open types), fixed by propagating
     `spd.aligned = pd->aligned`. Its encode-side `uper_open_type_put` was hardcoded to call
     `uper_encode_to_new_buffer` unconditionally; fixed to call `aper_encode_to_new_buffer` when
     `po->aligned`.
   - `nbits > 16` is not exercised by this project's current NGAP message set for the VALUE or
     LENGTH rule; both helpers apply a best-effort octet-round-up rather than silently leaving it
     unaligned, but that path is explicitly unverified against a real peer -- flagged in the code
     comment, not silently assumed correct, per this project's disclosure rule.
4. `OCTET_STRING.c`/`BIT_STRING.c` also needed two further, narrower fixes specific to character
   string / fixed-size string encoding (X.691 clause 16 and 27):
   - **Per-character bit width rounds up to a canonical size** (`aper_char_unit_bits`, new helper):
     Aligned PER packs PER-visible-alphabet-constrained characters using the smallest of
     `{1, 2, 4, 8, 16, 32}` bits that fits the alphabet, not the exact `ceil(log2(alphabet size))`
     bits Unaligned PER uses. Found because `AMFName`'s 91-symbol `PrintableString` alphabet
     (range 32..122) needs only 7 bits exactly, but the real encoding uses 8 -- cross-checked again
     against UERANSIM's own reference decoder's debug trace (`"(32..122):8"`, and its own
     generated constraint table literally records `range_bits=7` for the *unaligned* interpretation
     while the runtime still packs 8 in aligned mode).
   - **Character content itself starts octet-aligned**, separately from its own length
     determinant's alignment: a length with `effective_bits<=8` (the common case, per the LENGTH
     rule above) is read with *no* preceding padding, so the bit position immediately after it is
     not itself byte-aligned -- an explicit align call was added right before the actual character
     data read/write in both the general (unconstrained-length) path and `OCTET_STRING_encode_uper`'s
     separate small-`effective_bits` fast-path branch (decode already funnels both cases through one
     shared loop, so only encode needed the second call site). Fixed fixed-size (`SIZE` non-
     extensible, `effective_bits==0`) octet/bit strings wider than 2 octets (16 bits) similarly, per
     X.691 #16.6 (`<=2` octets, no alignment) vs #16.7 (`>2` octets, aligned) -- this specific
     sub-case remains unverified against a real peer since no field in this build's current message
     set exercises a fixed-size character string, only fixed-size plain octet strings (e.g.
     `PLMNIdentity`, 3 octets) and small bit strings (`AMFRegionID`/`AMFSetID`/`AMFPointer`, all
     <=10 bits, under the 16-bit BIT STRING threshold where this rule doesn't change behavior
     anyway) -- flagged, not assumed.

**Distribution mechanics:** `scripts/patches/asn1c-aligned-per.patch` is a plain unified diff
(`diff -ru`, `skeletons/<file>` paths, applies cleanly with `patch -p1` from an extracted
`asn1c-0.9.29` tarball root -- verified against a fresh extraction, not just the already-patched
tree) and is committed (unlike everything under `build-tools/`, which is gitignored by the existing
`build-*/` glob -- the patch lives under `scripts/` specifically so it isn't swept up in that
pattern). Everything under `build-tools/` itself (the vanilla tarball, the patched build, the
earlier abandoned `osmocom/asn1c` and GNU-autotools-toolchain build attempts from investigating
Problem 2) is reproduced by `scripts/setup-asn1c.sh`, matching `scripts/gen-lab-pki.sh`'s existing
precedent for a setup script that produces gitignored local state. `libs/ngap-generated/
CMakeLists.txt`'s `find_program` failure message points here.

**Verification:** the full staged Stage 1 goal -- AMF (freshly rebuilt from a *clean*
`scripts/setup-asn1c.sh` run, not the already-patched tree left over from debugging) completing NG
Setup against the real, unmodified `nr-gnb` binary -- succeeds end-to-end: `nr-gnb` logs "NG Setup
Response received" / "NG Setup procedure is successful", AMF logs receiving and correctly
dispatching the real `NGSetupRequest` and sending a real `NGSetupResponse`. All 58 pre-existing
tests still pass unchanged. No `gtest`/`ctest`-integrated regression test exists yet for the ASN.1
PER codec itself (the verification above is the real-binary interop test the project's own
methodology treats as authoritative for this kind of protocol work, per ADR-0030's precedent) --
a dedicated automated round-trip test is deferred to Stage 6's documentation/verification pass
alongside `docs/TRACEABILITY.md`.

**Rejected alternative:** hand-roll a partial PER parser scoped to only this build's exact IE set,
skipping asn1c entirely. Rejected for the same reason the original plan gave: this project
explicitly committed to "a real ASN.1 PER codec generated from the actual 3GPP NGAP module, not
hand-rolled partial parsing" -- a scoped hand-rolled codec would silently stop being spec-traceable
the moment a new IE or message is added in a later stage, exactly the failure mode CLAUDE.md's
anti-fabrication rules exist to prevent.

**Consequence:** Stage 1 (NG Setup) is now complete and empirically verified. Stages 2-5 (Initial
UE Message/NAS Registration Request, Authentication, Security Mode, Registration Accept/Complete
ending in the real PCF call) can now build on a genuinely working Aligned PER codec rather than
inheriting this stage's Unaligned-only limitation -- though each stage may still surface further,
not-yet-exercised gaps in the patch (per the explicit `nbits > 16` and fixed-size-character-string
disclosures above), to be found and fixed the same way: real bytes, real peer, real oracle, not
assumed. **This is exactly what happened in Stage 2 -- see ADR-0032.**

## ADR-0032: Stage 2 (InitialUEMessage -> NAS RegistrationRequest -> real AUSF call -> AuthenticationRequest), three more real Aligned PER bugs, and a hand-rolled minimal NAS-5GS codec

**Date:** 2026-08-07
**Status:** Accepted

**Context:** Stage 2 of the staged NGAP/NAS plan: AMF decodes a real `InitialUEMessage` (containing
a NAS-PDU with a `RegistrationRequest`), extracts the SUPI from the null-protection-scheme SUCI in
the 5GS Mobile Identity IE, calls real AUSF's `POST /nausf-auth/v1/ue-authentications` with it,
gets back a 5G-AKA vector, encodes a NAS `AuthenticationRequest` (RAND/AUTN), wraps it in
`DownlinkNASTransport`, and sends it to the gNB. Grounding for the real NGAP IE names/codes, the
real NAS-5GS wire format, and AUSF's real SBI schema was gathered via a research pass (real
`specs/NGAP/ngap-17.9.asn` IE definitions; `simulators/ransim/vendor/UERANSIM/src/lib/nas` read
as a reference oracle per ADR-0016/ADR-0031's arms-length policy; `nfs/ausf/src/main.cpp`'s
already-implemented `/ue-authentications` endpoint) before any code was written.

**New library: `nfs/amf/src/nas_codec.{hpp,cpp}`, a minimal hand-rolled NAS-5GS (TS 24.501)
codec.** Unlike NGAP, NAS-5GS is TLV-encoded, not ASN.1 -- there is no `asn1c`-equivalent codegen
tool to generate this from, so hand-writing it is not the "hand-rolled partial parsing" ADR-0031
explicitly rejected for NGAP (that concern was about skipping a *generatable* codec; no such thing
exists for NAS-5GS in any real 5GC implementation either). Scope is deliberately narrow --
`decode_registration_request` extracts only the SUPI from a null-protection-scheme SUCI (returns
`std::nullopt`, not a guess, for anything else: ciphered NAS, GUTI-based registration, a real
protection scheme, a padded 1-octet routing indicator); `encode_authentication_request` builds
exactly the fields needed (ngKSI, ABBA, RAND, AUTN). Every byte layout (header format, IE ordering,
the SUCI's exact field layout for the null scheme, RAND/AUTN's IE encodings) is cited against real
files, not memory -- see the file's own comments.

**AUSF client wiring**: `run_ngap_lifecycle` (in `nfs/amf/src/ngap_task.cpp`) now takes
`amf_instance_id`/`nrf_base` and constructs a dedicated `http2::Client`/`OAuth2Client` pair for
AUSF (scope `"nausf-auth"`, matching AUSF's own `kApiRoot`), living on the NGAP thread itself --
not shared with `run_nrf_lifecycle`'s pair or any ioc-thread client, since `http2::Client` is
synchronous/not thread-shared (same discipline ADR-0006/ADR-0027 already established, extended
here to a *third* dedicated-thread client after `run_nrf_lifecycle`'s own). SUPI is passed straight
through as `AuthenticationInfo.supiOrSuci` -- AUSF/UDM have no separate SUCI-deconcealment logic
yet (confirmed by reading `nfs/ausf/src/main.cpp`: `supiOrSuci` is forwarded verbatim into UDM's
URL path), so this is a faithful, disclosed simplification consistent with what's already built,
not a new gap introduced here.

**Real config gap found and fixed, unrelated to any of this project's own code**:
`simulators/ransim/config/ue.yaml` was missing three fields (`integrityMaxRate`, `uacAic`,
`uacAcc`) that UERANSIM's `nr-ue` refuses to start without -- this file had never actually been run
against real `nr-ue` before Stage 2 (Stage 0/1 only ran `nr-gnb`). Added with UERANSIM's own
reference `config/custom-ue.yaml` values, not invented.

**Three more real Aligned PER bugs found via the same "real gNB, real UE, real reference-decoder
oracle" methodology ADR-0031 used** -- Stage 2's real message content (a non-zero `S-NSSAI.sST`
inside `NGSetupResponse`'s `PLMNSupportList`, and a real `RAN-UE-NGAP-ID` value from a real gNB)
exercised code paths Stage 1's own testing never touched (Stage 1's BIT STRINGs were all-zero, and
no field in Stage 1's fixed test message needed a >16-bit value-field encoding):

1. **A short (<=2 octet) fixed-size OCTET/BIT STRING got wrongly octet-aligned on encode.** Found
   because `S-NSSAI.sST` (a 1-octet `OCTET STRING`, set to `1`) silently became `0` on the wire --
   `PLMNSupportList` correctly *decoded* structurally (Stage 1's own verification), but the
   gNB rejected AMF's `NGSetupResponse` for real ("Could not find a suitable AMF" -- `nr-gnb`'s own
   slice-selection logic reads `sST` and found no match), a failure mode Stage 1's decode-only
   testing couldn't have caught. Root cause: `OCTET_STRING_encode_uper`'s
   `csiz->effective_bits >= 0 && !inext` branch had a single, unconditional content-alignment call
   added after its if/else (originally written only for the `effective_bits > 0`,
   AMFName-style variable-length case) that also fired for the `effective_bits == 0` fixed-size
   case -- wrongly aligning (and thus byte-shifting) any short fixed-size field, while the
   already-correct `effective_bits > 0` case needed exactly that call. Fixed by moving the call
   inside the `else` branch only. The *decode* side never had this bug (its fixed-size branch
   returns early, structurally unable to reach the shared align call) -- confirmed via a standalone
   `S-NSSAI` encode/decode round-trip test before touching the real pipeline again.
2. **A >31-bit constrained-whole-number VALUE field got re-aligned mid-value by ADR-0031's own
   alignment fix.** Found because `RAN-UE-NGAP-ID` (`INTEGER(0..4294967295)`, exactly 32 bits, so
   *always* hits `uper_get/put_constrained_whole_number`'s pre-existing >31-bit recursive split --
   `per_get_few_bits` itself caps at 31 bits per call) failed to decode from a real gNB's
   `InitialUEMessage`. ADR-0031's `aper_align_value_get/put_nbits` call was applied *inside* the
   `nbits<=31` recursive base case, which is *also* the code path the >31-bit recursion's leftover
   fragment hits -- wrongly re-aligning partway through an already-in-progress value. Fixed by
   splitting each function into a `_raw` variant (pure recursive bit-splitting, no alignment) and a
   public wrapper that aligns exactly once, up front, using the caller's true (pre-split) `nbits`.
3. **The real X.691 rule for value fields needing MORE than 2 octets (`nbits>16`) is not a
   fixed-width encoding at all -- it's a small unaligned octet-count selector, then alignment,
   then the value in the minimum octets it actually needs.** This directly contradicted ADR-0031's
   own (wrong) assumption that all value fields round up to a fixed whole-octet width based on the
   *range*. Found the same way as everything else in this saga: a real gNB's `RAN-UE-NGAP-ID=1`
   arrived as 2 bytes (`00 01`), not the 4 bytes ADR-0031's rule predicted. Confirmed byte-for-byte,
   not guessed, by compiling a standalone encoder against
   `simulators/ransim/vendor/UERANSIM/src/asn/asn1c`'s vendored reference codec (same read-only
   oracle pattern as ADR-0031) with `ASN_EMIT_DEBUG` tracing: encoding `RAN-UE-NGAP-ID=1` (range
   32 bits, so `max_octets=ceil(32/8)=4`) produces a 2-bit selector (`ceil(log2(4))=2` bits, value
   `0` meaning "1 octet used", *not* octet-aligned itself), then 6 bits of alignment padding, then
   exactly 1 octet (`0x01`) -- `"00 01"` over the wire. Implemented in
   `uper_get/put_constrained_whole_number` as a genuinely new code path for `nbits>16`, replacing
   the wrong fixed-width assumption; the `nbits<=16` path (procedureCode, ProtocolIE-ID, both
   already verified in Stage 1) is untouched.
4. **A NAS-5GS IE encoding mistake, not an ASN.1/PER bug**: `AuthenticationParameterRand` (NAS
   `AuthenticationRequest`'s RAND field) was given a length octet like `AuthenticationParameterAutn`
   (TS 24.501's genuinely length-prefixed AUTN field). RAND is actually a **Type-3** IE (fixed
   16-octet value, no length octet at all -- confirmed against
   `simulators/ransim/vendor/UERANSIM/src/lib/nas/ie3.hpp`'s `IEAuthenticationParameterRand` base
   class, `InformationElement3`, vs. AUTN's `ie4.hpp`/`InformationElement4`). This one byte of
   drift shifted every subsequent field, and real `nr-ue` didn't just reject the message -- it threw
   an *uncaught* C++ exception (`"Bad constructed NAS message"`) and **crashed the process**,
   confirmed by compiling and running UERANSIM's own `DecodeNasMessage` directly against the exact
   bytes AMF sent as a standalone oracle test (same arms-length read-only pattern). Fixed by
   dropping the length octet for RAND only.

**Verification:** the full Stage 2 goal -- AMF (freshly rebuilt from a *clean*
`scripts/setup-asn1c.sh` run) receiving a real `InitialUEMessage` from real `nr-gnb`/`nr-ue`,
correctly extracting SUPI `imsi-999700000000001` from the null-scheme SUCI, making a real,
successful `POST /nausf-auth/v1/ue-authentications` call to real AUSF (which itself made a real
call to real UDM), and sending back a real NAS `AuthenticationRequest` -- succeeds end-to-end,
reproducibly. Real `nr-ue` logs confirm it **decoded** AMF's `AuthenticationRequest` correctly
(`"Authentication Request received"`) and extracted the real RAND/AUTN/SQN from it; it then sent a
NAS `AuthenticationFailure` with cause "SQN out of range" -- this is `nfs/udm/src/main.cpp`'s own
seeded TS 35.207 test SQN (`ff9bb4d0b607`, a large fixed value chosen as test data, not derived
from any counter) legitimately exceeding a fresh UE's initial `SQN-MS=0` by design, exactly the
real TS 33.102 synchronization-failure procedure a real UE is supposed to trigger in this
situation -- not a bug, and not this stage's concern (SQN resync, and handling whatever
`AuthenticationResponse`/`AuthenticationFailure` the UE actually sends, is Stage 3's territory).
All 58 pre-existing tests still pass unchanged. `scripts/patches/asn1c-aligned-per.patch` was
regenerated to capture bugs 1-3 above (applies cleanly against a fresh `asn1c-0.9.29` extraction,
verified).

**Rejected alternative:** treat the SQN-out-of-range `AuthenticationFailure` as a Stage 2 blocker
and implement the resync procedure now. Rejected: Stage 2's own scope (per the approved staged
plan) ends at "AMF sends `AuthenticationRequest`, UE receives it" -- the plan's Stage 3 is
specifically "Authentication Response -> AUSF confirmation -> KAMF derivation", and handling
whichever real NAS message the UE sends back (a success `AuthenticationResponse` *or* a
spec-correct `AuthenticationFailure`) is exactly that stage's job, not something to pull forward.

**Consequence:** Stage 2 is complete and empirically verified, including the crash-on-decode NAS
bug that a purely self-consistent round-trip test (encode+decode with only this project's own
codec) could never have caught -- every one of this stage's four bugs was found only because the
*other* side of the wire was a real, independent implementation. Stage 3 (Authentication Response
decode -- including a real `AuthenticationFailure` path now that one's been observed for real --
AUSF confirmation, KAMF derivation) can proceed knowing the transport and RAND/AUTN encoding are
now genuinely interoperable, not just self-consistent.

## ADR-0033: Stage 3 (AuthenticationResponse/Failure decode -> real AUSF confirmation -> KAMF derivation)

**Date:** 2026-08-07
**Status:** Accepted

**Context:** Stage 3 of the staged NGAP/NAS plan: decode the NAS-PDU carried in the UE's
`UplinkNASTransport` reply to Stage 2's `AuthenticationRequest`. TS 24.501 defines exactly two real
outcomes here (EAP out of scope): a success `AuthenticationResponse` (carries RES*), confirmed with
real AUSF's `PUT .../5g-aka-confirmation`, from which KAMF (TS 33.501 Annex A.7) is derived; or an
`AuthenticationFailure` (cause + optional AUTS), which this stage decodes and logs but does not
act on -- SQN resynchronization is a disclosed, explicitly out-of-scope gap (needs new AUSF/UDM
logic to reissue a vector from the UE's AUTS, not just an AMF-side change).

**New code:** `amf::nas::decode_authentication_outcome` (`nfs/amf/src/nas_codec.{hpp,cpp}`) decodes
both outcomes via generic Type-4 TLV walks, byte layouts cross-checked against
`simulators/ransim/vendor/UERANSIM/src/lib/nas` as before. `aka_crypto::derive_kamf`
(`libs/aka-crypto/src/kdf.cpp`, FC=0x6D) -- same reconstruction-not-citation disclosure as
ADR-0026's KAUSF-for-EAP-AKA', cross-checked against UERANSIM's own `DeriveKeysSeafAmf`.
`nfs/amf/src/ngap_task.cpp`'s `handle_uplink_nas_transport` reuses the exact
`ConfirmationData{resStar}` -> `ConfirmationDataResponse{authResult,kseaf}` shape AUSF's endpoint
already defines (ADR-0027).

**Real interop confirms the failure path, not the success path -- an unavoidable, structural
limitation, not a gap in this stage's testing.** UDM's seeded subscriber uses TS 35.207 Test Set
1's fixed SQN (`ff9bb4d0b607`) -- legitimately larger than any fresh UE's `SQN-MS=0`, so a real
`nr-ue` *always* sends `AuthenticationFailure` (SYNCH_FAILURE, with AUTS) on first contact, never
`AuthenticationResponse`. Confirmed via a real run: AMF correctly decoded the failure
(`mmCause=0x15`, AUTS present), logged the disclosed-gap message, and did not crash or misbehave --
this exactly matches `nr-ue`'s own reported behavior. The success path (RES* decode -> AUSF
confirmation -> KAMF) cannot be reached this way without implementing SQN resync, which is out of
scope by design (see ADR-0032's own rejected-alternative entry).

**Verification strategy for the unreachable success path:** hand-constructed, spec-correct NAS-PDU
byte vectors (`tests/conformance/test_nas_codec.cpp`) cover
`decode_authentication_outcome`'s success path directly, plus `decode_registration_request`
(previously only indirectly covered via Stage 2's real interop) and `derive_kamf`'s determinism/
input-dependence. The AUSF-confirmation+KAMF chain itself is *not* re-verified with a new
standalone harness -- `handle_uplink_nas_transport`'s AUSF call uses the identical request/response
shape `AusfIntegration.FiveGAkaSuccessfulAuthenticationCrossChecksHxresAndKseaf`
(`tests/integration/test_ausf_ue_authentication.cpp`) already exercises end-to-end against a real
running AUSF with real Milenage-computed RES* -- a second harness would only re-prove the same
thing.

**Consequence:** Stage 3 complete, 59 tests total (was 51 before this stage; +8 new). The SQN
blocker discovered here recurs identically in every downstream stage (4, 5) -- documented once
here, referenced rather than re-litigated in ADR-0034/ADR-0035.

## ADR-0034: Stage 4 (SecurityModeCommand/Complete -- NAS security activation) and new 128-NEA2/128-NIA2 primitives

**Date:** 2026-08-07
**Status:** Accepted

**Context:** Stage 4 activates real NAS security: AMF derives KNASenc/KNASint from KAMF (TS 33.501
Annex A.8), sends a `SecurityModeCommand` (integrity-protected only, per TS 24.501 -- never
ciphered, since the UE cannot yet be assumed to trust a brand-new KNASenc), and verifies the UE's
`SecurityModeComplete` (integrity-protected **and** ciphered, proving both directions work). This
project's only implemented algorithm pair is 128-NEA2 (AES-128-CTR) / 128-NIA2 (AES-128-CMAC) --
128-EEA0/"null" and the SNOW-3G/ZUC-based NEA1/NIA1/NEA3/NIA3 families are out of scope, a
disclosed simplification matching UERANSIM's own default algorithm selection, not an attempt at
full algorithm-agility.

**New code:** `aka_crypto::derive_knas_enc`/`derive_knas_int` (`libs/aka-crypto/src/kdf.cpp`,
FC=0x69) -- same reconstruction-not-citation disclosure pattern as every other Annex A derivation
in this file, cross-checked against UERANSIM's `DeriveNasKeys`. New
`aka_crypto::nea2_apply`/`nia2_mac` (`libs/aka-crypto/src/nas_security.cpp`, new file) implement
128-NEA2/128-NIA2 directly against OpenSSL's `EVP_aes_128_ctr`/`EVP_MAC` (CMAC) APIs, input formats
(the shared COUNT/BEARER/DIRECTION prefix, padded differently for each algorithm) reconstructed
from `simulators/ransim/vendor/UERANSIM/src/lib/crypt/eea2.cpp`/`eia2.cpp`.
`amf::nas::encode_security_mode_command`/`decode_security_mode_complete`
(`nfs/amf/src/nas_codec.{hpp,cpp}`) build/verify the secured NAS envelope, including a new
`extract_uplink_nas_pdu`/`send_downlink_nas_transport` factoring in `ngap_task.cpp` (previously
duplicated inline across Stage 2/3's own handlers).

**Real interop was blocked by ADR-0033's SQN gap before ever reaching this stage** -- a real run
confirmed the exact same `AuthenticationFailure` outcome, never advancing to `SecurityModeCommand`.
Given this stage's crypto (128-NEA2/128-NIA2) is entirely new, hand-rolled code with no other proof
point, self-consistency-only unit tests were judged insufficient (per this project's own prior
lesson -- a real UB bug in `derive_keys()` shipped past three self-consistency tests before an
independent-re-derivation test caught it, see ADR-0027). **Verification: a standalone scratch
harness** (not committed -- would require linking UERANSIM's vendored `crypt/` sources into the
permanent build, out of proportion to what it's for) compiled UERANSIM's real `eea2.cpp`/`eia2.cpp`
directly and cross-checked `nea2_apply`/`nia2_mac` against them: 20 random trials each of
key/count/bearer/direction/message, **40/40 byte-exact matches, 0 failures**. 17 new/updated unit
tests (`tests/conformance/test_nas_security.cpp`, `test_nas_codec.cpp`) cover determinism,
round-trip, and the SMC/Complete envelope's own MAC-verify/tamper/reject paths.

**Consequence:** Stage 4 complete, 75 tests total. `encode_security_mode_command`/
`decode_security_mode_complete` were refactored onto two new shared low-level helpers
(`encode_secured_downlink`/`decode_secured_uplink`) once Stage 5 needed the identical
envelope-building logic a third time (see ADR-0035) -- confirmed zero behavior change by rerunning
this stage's own unit tests unmodified after the refactor.

## ADR-0035: Stage 5 (RegistrationAccept/Complete -> real PCF AM Policy Association call) -- the goal this whole effort was for

**Date:** 2026-08-07
**Status:** Accepted

**Context:** Stage 5 closes the loop ADR-0025 -> ADR-0029 left open: AMF sends `RegistrationAccept`
(integrity-protected **and** ciphered -- the normal secured-message case, not
`SecurityModeCommand`'s "new security context" variant, since the NAS security context is now
established), receives `RegistrationComplete`, and makes the real call this entire staged
NGAP/NAS effort existed to produce: `Npcf_AMPolicyControl`'s `CreateIndividualAMPolicyAssociation`
(TS 29.507) to real PCF.

**New code:** `amf::nas::encode_registration_accept`/`decode_registration_complete`
(`nfs/amf/src/nas_codec.{hpp,cpp}`), built on ADR-0034's `encode_secured_downlink`/
`decode_secured_uplink` refactor. Only the one mandatory IE (`registrationResult`, fixed to
THREEGPP_ACCESS) is sent -- GUTI reassignment and other optional IEs are a disclosed
simplification, moot given this project's single-registration-per-association scope (ADR-0031).
`handle_uplink_nas_transport_registration_complete` (`ngap_task.cpp`) calls PCF using the same
client-call template SMF->PCF already established (ADR-0029), storing the resulting
`PolicyAssociation` in `nfs/amf/src/ue_context_store.hpp` keyed by SUPI -- the first thing that
ever populates that store for real (see its own long-standing disclosed-gap comment).

**Real interop blocked at the same SQN point as Stages 3/4 (expected, not re-investigated) --
verified the PCF call directly instead, and it caught a real bug.** Obtained a genuine OAuth2
token from NRF via `curl` using AMF's own mTLS client cert, then POSTed the *exact* JSON body
`handle_uplink_nas_transport_registration_complete` builds directly to real PCF's
`/npcf-am-policy-control/v1/policies`. First attempt: **real HTTP 400**,
`"key 'suppFeat' not found"` -- `PolicyAssociationRequest.suppFeat` (TS 29.571 `SupportedFeatures`,
a hex-encoded optional-feature bitmask) is mandatory in the generated schema and had been omitted.
Fixed (`preq.suppFeat = ""`, meaning "none of PCF's optional features requested," the correct value
given none are implemented), re-sent the identical request: **real HTTP 201**, a genuine
`PolicyAssociation` body back. This is a bug the generated-schema type system didn't catch at
compile time (`suppFeat` is a plain, default-constructed `std::string`, not `std::optional`, so it
serializes as `""` either way -- the omission was leaving the field *unset in the request builder's
intent*, not a type error) and unit tests alone would not have caught either, since nothing in this
codebase independently re-validates PCF's actual mandatory-field schema -- only a real request
against a real server does.

**Also found during this stage's verification, unrelated to any of the code above:** leftover
`nrf`/`udm`/`ausf`/`pcf`/`amf` processes manually started earlier in this session (for live
`nr-gnb`/`nr-ue` interop testing) were still running and squatting the same fixed ports
`tests/integration/*.cpp`'s own `spawn_all()`-style helpers bind to, causing 2-3 tests
(`AusfIntegration.*`, `SmfIntegration.CreateSMContextFailsClosedWhenPcfUnreachable`) to fail
intermittently across Stages 3-5's own regression runs -- previously misdiagnosed as "flaky
parallel port contention." Killing every manually-started process before the next `ctest` run
produced a clean 80/80 pass in 15s (down from ~60s with retries/timeouts). Documented as a
recorded lesson (not a code change) so future manual-verification sessions clean up before the
final regression pass.

**Consequence:** Stage 5 complete, 80 tests total. The full staged NGAP/NAS Registration procedure
(NG Setup -> InitialUEMessage/RegistrationRequest -> AuthenticationRequest/Response ->
SecurityModeCommand/Complete -> RegistrationAccept/Complete -> real PCF AM Policy Association) is
now implemented end-to-end in code, verified stage-by-stage via whichever proof (real `nr-gnb`/
`nr-ue` interop, an independent cross-check harness, or a direct real-service HTTP call) was
actually reachable at each point -- never assumed correct from code review alone. The one
structural gap every stage from 3 onward shares -- SQN resynchronization -- remains explicitly
out of scope and is the reason a single, fully-automated real `nr-ue` end-to-end registration
cannot be demonstrated in one run; `docs/TRACEABILITY.md` records this plainly rather than
implying full automated coverage exists.

## ADR-0036: PDU Session Establishment (TS 23.502 §4.3.2.2.1) -- AMF's UlNasTransport decode -> real SMF CreateSMContext call

**Date:** 2026-08-08
**Status:** Accepted

**Context:** CLAUDE.md's Phase 2 definition of done requires UE registration *and* PDU session
establishment end-to-end, "no narrowed slice." ADR-0032 through ADR-0035 closed the registration
half. Investigating whether the second half was actually done (prompted by a direct "is Phase 2
complete" question) found it was not: AMF's NAS codec had no 5GSM message types at all, and
`ngap_task.cpp`'s post-registration dispatch explicitly logged and dropped every further
`UplinkNASTransport` ("no post-registration NAS procedures implemented yet"). SMF's own
`CreateSMContext` (stood up in an earlier turn, TS 29.502) was real and PCF-wired but had never had
a live trigger -- only ever exercised by a test client POSTing directly over HTTP, bypassing
AMF/NGAP entirely (confirmed by reading `tests/integration/test_smf_pdu_session.cpp`). This ADR
closes that gap: AMF now decodes the UE's PDU Session Establishment Request (wrapped in a NAS
`UlNasTransport` message) and makes the real `CreateSMContext` call.

**Key design decision, made explicit before writing any code: AMF does not decode the 5GSM PDU
Session Establishment Request payload itself.** TS 24.501's payload-container mechanism exists
precisely so AMF can route SM messages without understanding their contents -- only SMF decodes
real 5GSM content, and even SMF's own current turn doesn't (`nfs/smf/src/main.cpp`'s own disclosed
"PduSessionType is negotiated inside the NAS SM message... not available from SmContextCreateData
at all" scope, predating this session). So `amf::nas::decode_ul_nas_transport`
(`nfs/amf/src/nas_codec.{hpp,cpp}`) only extracts the transport-level optional IEs that determine
*where* to route the request -- PDU session ID, S-NSSAI, DNN -- treating the payload container
itself (the actual PDU Session Establishment Request bytes) as an opaque length-prefixed blob,
skipped over, never parsed. Byte layouts (UlNasTransport's mandatory/optional IE order, the DNN
IE's TS 23.003 §9.1 label-length-prefix encoding, requestType's Type-1 half-octet packing that a
naive Type-4-only IE walker would desync on) were confirmed against
`simulators/ransim/vendor/UERANSIM/src/lib/nas/msg.cpp`'s `UlNasTransport::onBuild` and
`src/ue/nas/sm/transport.cpp`/`src/lib/nas/utils.cpp`'s `DnnFromApn`, the same read-only reference
oracle methodology as every prior stage.

**Symmetric decision on the way back: AMF does not send anything to the UE after the SMF call
succeeds.** SMF's real `CreateSMContext` response (`SmContextCreatedData`) carries no `n1SmMsg` --
`nfs/smf/src/main.cpp`'s handler only ever sets `pduSessionId`/`sNssai` on it, a disclosed gap from
SMF's own earlier turn, not introduced here. There is therefore no real PDU Session Establishment
Accept content for AMF to forward to the UE. Synthesizing one would mean AMF fabricating SM-layer
decisions (PDU session type, QoS rules, session-AMBR) that are properly SMF's job to decide --
worse than disclosing the gap plainly, which is what `handle_uplink_nas_transport_pdu_session_
establishment`'s own comment does. This is consistent with (not a new instance of) the
already-established rule: never invent content a real peer would need to have actually decided.

**Refactored `nfs/amf/src/ngap_task.cpp`'s `UeAuthState::Phase` enum** (previously a 4-state linear
progression ending at `Done`) to add `AwaitingPduSessionEstablishmentRequest` between
`AwaitingRegistrationComplete` and `Done` -- this project's single-registration-per-association
scope (ADR-0031) extends naturally to "single PDU session per association," so a simple additional
enum value is correct; a real AMF serving concurrent PDU sessions would need a proper per-session
state machine. New dedicated `http2::Client`/`OAuth2Client` pair for SMF (scope
`"nsmf-pdusession"`), same one-client-per-NF-per-thread discipline as AUSF's/PCF's.

**Real interop blocked at the same SQN point as every prior stage (expected, not re-investigated)
-- verified the SMF call directly instead, same methodology as ADR-0035's PCF verification, and
this time it worked on the first try.** Obtained a genuine OAuth2 token from NRF via `curl`
(AMF's own mTLS client cert, scope `nsmf-pdusession`), hand-built a `multipart/related` body with
the *exact* JSON `SmContextCreateData` fields `handle_uplink_nas_transport_pdu_session_
establishment` constructs (`servingNfId`, `servingNetwork`, `anType`, `smContextStatusUri`,
`supi`, `pduSessionId`, `dnn`, `sNssai` with a hex-encoded `sd`), and POSTed it directly to real
SMF's `/nsmf-pdusession/v1/sm-contexts`. Got a real HTTP 201 with a genuine `SmContextCreatedData`
body back on the first attempt (unlike ADR-0035's PCF call, which needed a real fix first) --
confirming SMF's own internal PCF call also succeeded as part of the same request (no 500 from a
failed downstream PCF call). 4 new unit tests (`tests/conformance/test_nas_codec.cpp`,
`NasCodec.DecodeUlNasTransport*`) cover the transport-level IE extraction (pduSessionId/sNssai/dnn,
including the DNN label-decode) and the MAC-verify/tamper/reject/wrong-payload-container-type
paths, following this file's own established pattern.

**Consequence:** Phase 2's stated definition of done -- "UE registration... and PDU session
establishment... end-to-end, no narrowed slice" -- is now met in the sense that both procedures
are fully implemented in code and each real SBI call along the way (AUSF, PCF, SMF, and SMF's own
call to PCF) has been verified against a real running peer. Neither procedure can currently be
demonstrated end-to-end in a single automated `nr-ue` run, because of the shared SQN
resynchronization gap (ADR-0032/ADR-0033) that blocks a fresh UE from ever completing
authentication against this build's current UDM seed data -- this is a real, disclosed limitation
of the *demonstration*, not of the implementation itself, and is recorded plainly in
`docs/TRACEABILITY.md` rather than left for a reader to discover. SQN resynchronization and a real
SMF-side PDU Session Establishment Accept (closing the UE-visible half of this procedure) remain
the two largest disclosed gaps going into whatever comes after Phase 2.

## ADR-0037: SQN resynchronization (TS 33.102 §6.3.3), and three real bugs found only by closing the loop with a real `nr-ue`

**Date:** 2026-08-08
**Status:** Accepted

**Context:** ADR-0036 left SQN resynchronization as the largest disclosed gap blocking a real,
unmodified `nr-ue` from ever completing authentication: UDM's seeded test SQN and UERANSIM's own
USIM-simulator SQN state start out of sync (`FF9BB4D0B607` received vs. `000000000000` expected on
the UE side, confirmed in this session's own interop logs below), and TS 33.102's normal AKA
procedure has no way to recover from that except the explicit resync exchange (`AuthenticationFailure`
with `mmCause=0x15`/SYNC_FAILURE carrying `AUTS`, TS 24.501 §5.4.1.3.7). Every prior stage's real
interop was blocked at exactly this point (ADR-0032 through ADR-0036 all record it). This ADR closes
it, and in the process of finally being able to run a real UE through the *entire* procedure for the
first time, surfaces and fixes two further real bugs that no amount of self-consistency testing
against synthetic vectors had caught.

**f1\*/f5\* (Milenage "star" functions) added to `libs/aka-crypto`** (`milenage.hpp`/`.cpp`):
TS 33.102 Annex C.3's resync MAC-S/AK\* computation uses distinct rotation constants and a fixed
AMF (`0x0000`, not the real network AMF) from the normal f1/f5 functions used for authentication --
confirmed against UERANSIM's own compiled `milenage.c` (`crypt-ext/milenage.c`), not invented.
`verify_and_decode_auts` computes AK\* via f5\*, XORs it into the received AUTS to recover SQN_MS,
recomputes MAC-S via f1\*, and compares. Cross-checked via a standalone scratch harness linking
UERANSIM's real compiled `milenage_f1`/`milenage_f2345`/`milenage_auts` functions directly (not
UERANSIM's higher-level wrappers): 80/80 matches across 20 random trials × 4 checks (f5\*, f1\*, full
AUTS round-trip, tamper-rejection). 3 new unit tests in `tests/conformance/test_milenage.cpp`.

**UDM's `resync_sqn` (`nfs/udm/src/stores.cpp`) advances the stored SQN by `+0x10000`, not `+1`.**
UERANSIM tracks freshness via TS 33.102 Annex C.3's array scheme (`SqnManager`, confirmed at
`usim.cpp:18`: `indBitLen=5`) -- the low 5 bits of SQN are an array index (IND), not
freshness-checked directly; only the upper SEQ bits are. A naive `SQN_MS + 1` lands entirely inside
the IND bits, leaving SEQ unchanged, so the "resynced" vector would still fail freshness on the UE
side. Advancing by `2^16` guarantees SEQ moves regardless of IND width up to the spec's max allowed
16 bits. `AuthenticationInfoRequest`/`AuthenticationInfo`'s existing (real, present-in-YAML)
`resynchronizationInfo` field (`ResynchronizationInfo_Nudm_UEAU{rand, auts}`, TS 29.503/29.509) is
plumbed UDM<-AUSF<-AMF, and AMF's retry loop (`nfs/amf/src/ngap_task.cpp`,
`initiate_5g_aka_authentication`) is guarded by a new `sqn_resync_attempted` flag so it retries
exactly once per association, not in an unbounded loop against a still-broken vector.

**KAMF derivation (TS 33.501 Annex A.7) was using the wrong SUPI format.** AMF fed the full
`"imsi-999700000000001"` string into `derive_kamf`; UERANSIM's own `Supi::Parse` strips the prefix
before its KDF call, using bare digits only. Fixed via a `strip_imsi_prefix` helper applied only at
the `derive_kamf` call site (`auth_state.supi` itself is left prefixed everywhere else -- AUSF/PCF/
SMF calls all expect the `"imsi-"` form). Verified against UERANSIM's real `crypto::CalculateKdfKey`
using both synthetic and real live-captured KSEAF values. This fix alone did not resolve real
interop -- SecurityModeCommand still failed -- leading to the next, deeper bug.

**The actual root cause of every `SecurityModeCommand`/NAS-integrity failure this whole staged
effort had hit: `libs/aka-crypto`'s 128-NIA2 primitive was correct, but `nfs/amf/src/nas_codec.cpp`
was calling it on the wrong input.** TS 24.501's NAS MAC construction (as implemented by
UERANSIM's `nas_enc::ComputeMac`) prepends the 1-octet NAS sequence number (COUNT's low-order byte)
to the message bytes *before* computing the 128-NIA2 MAC -- a NAS-security-layer detail on top of
(not a replacement for) EIA2's own COUNT parameter, which this project's `encode_secured_downlink`/
`decode_secured_uplink` had never accounted for. Found only after exhausting every other
possibility: KAUSF/KSEAF/KAMF/KNASint/KNASenc all cross-checked byte-for-byte against UERANSIM for
real live values; the raw EIA2 primitive cross-checked against UERANSIM's `eia2::Compute` for both
arbitrary and real captured inputs; the NGAP/ASN.1 wire encoding decoded correctly through
UERANSIM's own real `libasn-ngap.a`; manual NAS TLV framing traced against `SecurityModeCommand::
onBuild`'s real decode order -- all correct. Resolved by instrumenting UERANSIM's actual `nr-ue`
binary with temporary debug logging (since fully reverted, confirmed via `grep` before rebuild),
confirming keys and message bytes matched exactly between AMF and UE yet computed MACs still
differed, which is what led to re-reading `nas_enc::ComputeMac`'s exact body and finding the
prepended sequence-number byte. Fixed in both `encode_secured_downlink` and `decode_secured_uplink`
(shared by every secured NAS message this project builds or verifies); 4 existing unit tests in
`tests/conformance/test_nas_codec.cpp` updated to match. **This fix produced this project's
first-ever complete real registration**: `nr-ue` logged `Initial Registration is successful` and
automatically proceeded to `Sending PDU Session Establishment Request`.

**A fourth bug surfaced immediately by that same success: AMF's state machine was waiting for a
`RegistrationComplete` a real UE will never send.** TS 24.501's actual rule (confirmed by reading
UERANSIM's `receiveInitialRegistrationAccept`,
`simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/register.cpp:346-426`) is that `RegistrationComplete`
is sent *conditionally* -- only if `RegistrationAccept` carried a 5G-GUTI, an NSSCI=CHANGED
indication, or a configuredNSSAI. `encode_registration_accept` sends none of those (an
already-disclosed simplification, see its own comment -- no GUTI allocation scheme exists in this
project), so a real UE correctly never sends `RegistrationComplete` and instead proceeds straight to
PDU Session Establishment. AMF was staying in a phase that would never advance, so it rejected the
UE's next real message (the PDU Session Establishment Request's `UlNasTransport`) as a
"RegistrationComplete MAC verification FAILED," even though the MAC itself was fine -- the message
was just a different, expected type. **This was a design gap, not a crypto bug**: removed
`UeAuthState::Phase::AwaitingRegistrationComplete` entirely; AMF now proceeds directly from sending
`RegistrationAccept` to requesting the AM Policy Association from PCF (folded into
`handle_uplink_nas_transport_smc_complete`, since that call no longer waits on anything from the
UE), then to `AwaitingPduSessionEstablishmentRequest`. This also fixed a related off-by-one: the PDU
Session Establishment handler's `uplink_count` was hardcoded to `2` (assuming a RegistrationComplete
had used count `1`); it is now correctly `1`, since SecurityModeComplete (count `0`) is genuinely the
only secured uplink message before it. `amf::nas::decode_registration_complete` itself is kept
(unit-tested, spec-correct) but is now documented as currently unreachable by any production
handler, for whenever a future turn adds real GUTI reassignment.

**Verification:** full real interop, first attempt, no manual message spoofing anywhere in the
chain -- real `nrf`/`udr`/`udm`/`ausf`/`pcf`/`smf`/`amf` plus real `nr-gnb`/`nr-ue`: NG Setup →
Initial Registration → Authentication Failure (SQN out of range) → SQN-resync Authentication Request
accepted → SecurityModeCommand/Complete verified → RegistrationAccept sent → AM Policy Association
established with PCF → PDU Session Establishment Request verified → SM context established with
SMF. `nr-ue` logged `Initial Registration is successful` followed immediately by
`Sending PDU Session Establishment Request`, with **zero retries or failures anywhere in the
registration or SM-context-creation path** -- the first time this has happened in the project's
history. The UE does still retransmit its PDU Session Establishment Request afterward (`T3580`
expiry), which is expected and separately disclosed (ADR-0036: SMF's `CreateSMContext` response
carries no `n1SmMsg`, so AMF has no real Accept content to send back yet); AMF handles the
retransmit gracefully via its pre-existing `Done`-phase fallback (logs and ignores, no crash, no bad
state). Full `ctest` suite re-run clean: 87/87 passing, zero regressions.

**Consequence:** Phase 2's "no narrowed slice" definition of done -- UE registration *and* PDU
session establishment, demonstrated end-to-end against a real, unmodified UE -- is now genuinely
met for the first time, not just implemented-but-blocked. The one remaining disclosed gap in this
path is the missing PDU Session Establishment Accept content (ADR-0036's own disclosed scope,
unchanged by this ADR).

## ADR-0038: PDU Session Establishment Accept -- real Namf_Communication N1N2MessageTransfer, real 5GSM codec on SMF

**Date:** 2026-08-08
**Status:** Accepted

**Context:** ADR-0036/ADR-0037 left one disclosed gap in the PDU Session Establishment path: SMF's
`CreateSMContext` response carried no `n1SmMsg`, so AMF had no PDU Session Establishment Accept
content to send back to the UE, and a real `nr-ue` would retransmit its Request under `T3580` and
eventually give up. Initial scoping for closing this gap assumed the fix was as simple as adding an
`n1SmMsg` field to `SmContextCreatedData` -- checking the real generated schema
(`build/generated/sbi_gen/TS29122_CommonData_grp.hpp:11977`) showed that field does not exist.
The real TS 23.502 §4.3.2.2.1 mechanism (step 11) is a separate, asynchronous SBI call SMF makes
back to AMF: `Namf_Communication`'s `N1N2MessageTransfer`
(`specs/5G_APIs-REL-19/TS29518_Namf_Communication.yaml:1298`,
`POST /namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages`) -- confirmed against the real YAML,
not assumed, before any code was written (per CLAUDE.md's "a fabricated field costs a week of
review" rule). This is real new protocol surface on both AMF (a new server-side endpoint;
`main.cpp` already had a disclosed stub for it, predating NGAP, that only ever returned a fake
"initiated" acknowledgment) and SMF (a new SBI client role, plus SMF's first real 5GSM NAS codec)
-- scoped and approved with the user before implementation, given the size.

**AMF now forwards the real N1 SM container instead of dropping it.**
`amf::nas::decode_ul_nas_transport`'s `UlNasTransportInfo` gained a `payload_container` field
capturing the opaque payload-container bytes verbatim (previously walked past and discarded --
ADR-0036's own comment called this out as deliberately unparsed, not deliberately *discarded*; this
ADR closes that gap without violating the "AMF stays opaque to 5GSM content" principle, since AMF
still never decodes the bytes, only forwards them). `handle_uplink_nas_transport_pdu_session_
establishment` now sends them to SMF as a real `multipart/related` binary part
(`application/vnd.3gpp.5gnas`, per the real YAML's `encoding` block) referenced by
`SmContextCreateData.n1SmMsg` (a real, already-generated `RefToBinaryData` field this project
simply hadn't populated yet).

**New `nfs/smf/src/nas_5gsm_codec.{hpp,cpp}`: SMF's first real 5GSM (TS 24.501 Session Management)
NAS codec.** Decodes the PDU Session Establishment Request's header only (EPD/pduSessionId/PTI,
TS 24.501 §8.3.1) -- disclosed, deliberate scope: this build always responds IPv4/SSC-mode-1,
matching UERANSIM's own hardcoded request content (`sendEstablishmentRequest` rejects any other
PDU session type before even building the message), so nothing else in the request currently
affects the response. Encodes a genuinely spec-shaped PDU Session Establishment Accept
(TS 24.501 §8.3.5): one QoS rule using TS 24.501 §9.11.4.13's real "zero packet filters" case
(spec-valid only for the DQR/default rule, which this is -- not an arbitrary shortcut), and
session-AMBR. Byte layouts (Type-1/3/4/6 IE encoding rules, exact field order, message type/EPD
values) confirmed against UERANSIM's real, independent implementation
(`simulators/ransim/vendor/UERANSIM/src/lib/nas/msg.cpp`'s `onBuild` methods,
`src/lib/nas/base.hpp`'s `EncodeIe1/3/4/6`, `src/lib/nas/ie6.cpp` -- which also confirmed
`IEQoSRules` is treated as an opaque octet string by UERANSIM, i.e. its internal structure is never
validated by the peer this project interops with, though this codec still encodes a real rule, not
arbitrary bytes, per CLAUDE.md's non-fabrication rule regardless of whether the peer would notice).
QFI and session-AMBR are sourced from PCF's actual `SmPolicyDecision` (already computed by SMF's
existing SM Policy Association call, ADR-0029) -- `authSessAmbr`/`authDefQos.n5qi`, NOT fabricated
-- with a disclosed fallback (1 Mbps) only if PCF returns no session rule at all. QFI is derived
directly from the 5QI value (`n5qi & 0x3F`), a disclosed simplification: a real network allocates
QFI via separate QoS flow binding, which no subsystem in this project implements.

**New `amf::nas::encode_dl_nas_transport`** (`nfs/amf/src/nas_codec.{hpp,cpp}`): AMF's delivery
vehicle, a secured DlNasTransport (TS 24.501 §8.2.9) wrapping SMF's opaque Accept bytes -- same
"AMF never decodes 5GSM content" discipline in the downlink direction, confirmed against
UERANSIM's real `DlNasTransport::onBuild`.

**New `amf::ngap::NgapUeRegistry`** (`nfs/amf/src/ngap_task.{hpp,cpp}`): the actual reason this
turn needed real architecture work, not just a codec. `Namf_Communication`'s `N1N2MessageTransfer`
arrives on the SBI HTTP/2 server's `io_context` thread; delivering it means writing to a specific
UE's live NGAP association, which is owned by that association's own dedicated blocking-I/O thread
(`ADR-0030`) -- a genuine cross-thread handoff this project had never needed before (every prior
NGAP/NAS message flowed in one direction, gNB-thread-only). `NgapUeRegistry` is a thread-safe
(single-mutex) map from SUPI to a non-owning pointer at the association's `SctpSocket` plus its NAS
security keys and downlink COUNT, registered by the NGAP thread once registration reaches
`AwaitingPduSessionEstablishmentRequest` (folded into `handle_uplink_nas_transport_smc_complete`,
since that's also where the PCF AM Policy Association call already lives) and unregistered on
association close. `main.cpp`'s pre-existing `N1N2MessageTransfer` stub (a disclosed
"bookkeeping-only, no real delivery pipeline exists yet" placeholder predating NGAP) is now real:
parses the real `multipart/related` body, looks up the binary part by `n1MessageContainer.
n1MessageContent.contentId`, and calls `NgapUeRegistry::send_dl_nas_transport`. Disclosed scope
narrowing: the schema's `application/json`-only alternative (no binary N1 message, e.g. an N2-only
transfer) is rejected with 400, not silently mishandled -- this build has no N2 SM info source
without a real UPF/N4 (Phase 3) to send anyway.

**Verification:** real end-to-end interop, first attempt, zero retries anywhere in the chain --
real `nrf`/`udr`/`udm`/`ausf`/`pcf`/`smf`/`amf` plus real `nr-gnb`/`nr-ue`. `nr-ue`'s own log:
`PDU Session Establishment Accept received` immediately followed by `PDU Session establishment is
successful PSI[1]` -- the real UE genuinely decoded and accepted this codec's QoS rules/
session-AMBR/SSC-mode/PDU-session-type content, not just a MAC-verified opaque blob. AMF/SMF logs
confirm the full real chain: AMF's `SM context established with SMF...` followed immediately by
SMF's `PDU Session Establishment Accept delivered to AMF for SUPI imsi-999700000000001,
pduSessionId 1`. Re-run twice against two independent clean NF/UE process sets with identical
results. 8 new unit tests (`tests/conformance/test_nas_5gsm_codec.cpp`,
`NasCodec.EncodesDlNasTransport*`) lock the byte layouts down deterministically; full `ctest` suite
re-run clean, 95/95 passing, zero regressions.

**Consequence:** the PDU Session Establishment Accept gap ADR-0036/ADR-0037 both disclosed is now
closed for real. Phase 2's full scope -- UE Registration and PDU Session Establishment, both
directions, against a real unmodified UE, first attempt -- is genuinely complete. The
`app: TUN interface could not be setup. Permission denied` line in `nr-ue`'s log after successful
PDU session establishment is an OS-level UE-side limitation (needs root to create a TUN device),
unrelated to and after this project's own NAS/SBI work; not a gap in this implementation.

## ADR-0039: Phase 3 Stage 0 -- PFCP (N4/Sx) codec infrastructure, and the UPF datapath evaluation

**Date:** 2026-08-08
**Status:** Accepted

**Context:** Phase 2 is complete (ADR-0032 through ADR-0038). CLAUDE.md's Phase 3 scope is "User
plane: N4/PFCP, UPF datapath," explicitly flagging the datapath choice ("DPDK, VPP, or eBPF/XDP for
the UPF datapath -- evaluate and justify") as a real decision, not a default to assume. Staged with
the user before any code, mirroring the AMF NGAP/NAS staged plan's shape (ADR-0030 onward):
Stage 0 (this ADR) infra + datapath decision; Stage 1 UPF skeleton (Association Setup + Heartbeat);
Stage 2 SMF becomes a PFCP client (real Association Setup at startup); Stage 3 real N4 Session
Establishment wired into the existing `CreateSMContext` flow; Stage 4 UPF datapath (actual packet
forwarding).

**No OpenAPI YAML exists for PFCP/TS 29.244** -- it is a binary protocol over UDP (port 8805,
IANA-assigned), entirely outside the `specs/5G_APIs-REL-19/` corpus this project's SBI codegen
spine processes. This is not a gap to route around silently: per CLAUDE.md's source-of-truth rule,
a schema-driven codec requires the schema, and none exists here, so this is the same class of
justified exception NAS-5GS's hand-rolled codec already established
(`nfs/amf/src/nas_codec.hpp`) -- `tools/sbi-codegen` does not apply, a hand-rolled TLV codec does.

**Real spec source, verified before writing any code, not from memory:** the actual, official
3GPP TS 29.244 V14.3.0 (2018-03) specification PDF, located via `WebSearch` and fetched via
`WebFetch` from an ARIB (a genuine 3GPP Organizational Partner) archive mirror
(`arib.or.jp/.../29244-e30.pdf`). `WebFetch`'s HTML-conversion pipeline could not parse the PDF's
compressed content streams, but it saved the raw PDF locally, which this project's PDF-reading
tooling (`Read` with a `pages` range) parsed directly -- clause 7.1 (Transmission Order and Bit
Definitions), 7.2.2 (Message Header, both node-related and session-related forms), 7.3 (Message
Types, the full numeric enumeration table), and 8.1.1/8.2.x (the generic IE TLV format plus every
individual IE this Stage's messages use: Cause, Node ID, Recovery Time Stamp, UP/CP Function
Features) were read directly from the real spec text, not reconstructed from general protocol
knowledge or an LLM's training-data memory of PFCP. The PDF is vendored at `specs/PFCP/29244-e30.pdf`
(same "cite the exact source artifact this project's byte layouts depend on" convention
`specs/NGAP/ngap-17.9.asn` already established) so this citation stays reproducible in future
sessions rather than depending on an ephemeral tool-fetch cache. **Disclosed version gap**:
V14.3.0 was the release actually available to verify against, not this project's REL-19 baseline.
The core PFCP header/TLV format has been stable since PFCP's Release 14 introduction (CUPS) and no
later-release change to clause 7.2.2/8.1.1 themselves is known -- but this is a disclosed
assumption carried in `libs/pfcp-core/include/pfcp_core/header.hpp`'s own comment, not silently
presented as REL-19-text-verified. If a REL-19 PFCP YAML-equivalent or spec text becomes available,
this should be revisited, same as any other disclosed gap in this project.

**New `libs/pfcp-core`**: a pure codec library (no transport of its own) --
`header.{hpp,cpp}` (the 8-byte node-related / 16-byte session-related PFCP message header),
`ie.{hpp,cpp}` (the generic Type-Length-Value IE codec every PFCP IE uses, confirmed against the
real spec's Figure 8.1.1-1/8.1.1-2), and `common_ies.{hpp,cpp}` (Cause, Recovery Time Stamp, Node ID
IPv4-only form, UP/CP Function Features -- the specific IEs Stage 1's Heartbeat and Association
Setup messages need). Recovery Time Stamp's NTP-epoch-to-Unix-epoch conversion
(`2208988800` seconds) is standard RFC 5905 knowledge, not a 3GPP-specific fact, used without
further citation. **No dedicated UDP transport wrapper was written**, unlike `libs/ngap-core`'s
hand-written SCTP socket class: Boost.Asio (already this project's event-loop library for SBI/HTTP2
and the choice `ngap-core`'s own header cites as lacking SCTP support) supports UDP natively
(`boost::asio::ip::udp`), so NFs using this library talk UDP directly rather than needing a
purpose-built wrapper -- a genuine simplification, not a shortcut around a real need. 12 new unit
tests (`tests/conformance/test_pfcp_core.cpp`) cover header/IE round-trips, byte-exact layout
checks against the real spec figures, and malformed-input rejection. Node ID's IPv4-only scope is
a disclosed narrowing (this project only ever speaks IPv4, matching `libs/sbi-core`'s own existing
IPv4-only scope) -- not the full IPv4/IPv6/FQDN union TS 29.244 §8.2.38 supports.

**UPF datapath evaluation (DPDK vs. VPP vs. eBPF/XDP), decided: eBPF/XDP.** Real tradeoffs, not
guessed: DPDK gives the highest raw throughput via full kernel-bypass polling, but needs hugepages
and either a dedicated NIC bound to a DPDK-compatible driver or a paravirtualized one configured
for it -- a real hardware/lab-tier dependency this project's current dev environment (bare-metal
Ubuntu, MX450, no NIC reserved for kernel bypass) does not have, and CLAUDE.md's own "Reality check"
section already anticipates this ("UPF datapath... will want a larger lab tier"). VPP is itself
DPDK-based plus a full vector-packet-processing framework on top -- more moving parts to stand up
correctly than this turn's scope justifies, for the same underlying hardware dependency. eBPF/XDP
runs in-kernel, attaches to a normal interface (a TUN/veth pair is sufficient for a lab, no
dedicated NIC or hugepages needed), and is a real, modern, production-used technique (this is
Cilium's entire design, not a toy). Given this project's actual dev environment and CLAUDE.md's own
disclosed constraint, eBPF/XDP is the correct choice for Stage 4's initial implementation --
DPDK/VPP remain a legitimate later swap-in once a real dedicated-NIC lab tier exists, not a redo of
this decision, since N4/PFCP control-plane work (Stages 1-3) is entirely datapath-agnostic.
Approved by the user (asked explicitly, given CLAUDE.md's "evaluate and justify" instruction for
this specific choice) before any Stage 4 code is written -- Stage 4 itself is not part of this ADR,
which covers Stage 0 only.

**Verification:** 12 new unit tests, full `ctest` suite re-run clean, 107/107 passing, zero
regressions.

**Consequence:** Phase 3's control-plane groundwork can now begin for real (Stage 1: UPF skeleton
answering Heartbeat/Association Setup). No PFCP byte layout in this project is fabricated or
guessed -- every one is either a direct citation of the real V14.3.0 spec text (disclosed version
gap noted above) or explicitly marked as this project's own disclosed scope narrowing.

## ADR-0040: Phase 3 Stage 1 -- UPF (eighth NF), PFCP Heartbeat + Association Setup

**Date:** 2026-08-08
**Status:** Accepted

**Context:** ADR-0039 (Stage 0) built the PFCP codec; this stage stands up the actual UPF binary
using it. TS 23.502's PDU Session Establishment flow requires a real N4/Sx Association between SMF
and UPF (established via the Association Setup procedure, TS 29.244 §6.2.6) before any Session
Establishment can happen -- this is the node-level bring-up UPF needs before Stage 3 can wire real
sessions through it, the same role NG Setup played for AMF/gNB in the earlier NGAP staging.

**UPF has no SBI service of its own -- confirmed against the real generated types before assuming
either way, not guessed.** No `Nupf_*` API exists anywhere in `specs/5G_APIs-REL-19/`: real 3GPP
architecture has SMF talk to UPF exclusively over N4/PFCP, never SBI. UPF's only real SBI role is
as an NRF *registration client* -- confirmed real (not fabricated) by finding `NFType::UPF` and
`NFProfile.upfInfo` (a genuine `UpfInfo` struct, with `sNssaiUpfInfoList`/`DnnUpfInfoItem` etc.) in
the generated `TS29122_CommonData_grp.hpp` before writing any registration code, so SMF can
discover UPF dynamically via NRF (Stage 2) rather than a hardcoded address. `nfs/upf/src/main.cpp`
therefore has no HTTP2 server at all -- a genuine architectural difference from every other NF in
this project, not an oversight: `run_nrf_lifecycle` (same pattern every NF already uses) runs on a
background thread purely as an outbound client, while the main thread runs a blocking PFCP/UDP
loop instead of `server.start()`.

**UPF's advertised `upfInfo` uses this project's actual configured S-NSSAI/DNN** (sst=1/sd=1,
matching `simulators/ransim/config/gnb.yaml`; dnn="internet", matching SMF's own existing default)
-- not an arbitrary placeholder, so Stage 3's real N4 Session Establishment will be requesting
exactly what UPF already declared it serves.

**PFCP/UDP transport: no dedicated wrapper class, unlike NGAP/SCTP.** `boost::asio::ip::udp::socket`
is used directly and synchronously (blocking `receive_from`/`send_to`) on UPF's main thread -- the
same "blocking I/O gets its own thread" discipline ADR-0006/ADR-0030 established, except here PFCP
is the *only* thing UPF's main thread does, so no explicit second thread is needed for it.

**Implemented this stage:** Heartbeat Request/Response (TS 29.244 §7.4.2, Recovery Time Stamp IE
only) and Association Setup Request/Response (§7.4.4.1/§7.4.4.2: UPF replies with Node ID, Cause=
Request accepted, Recovery Time Stamp, and an all-zero UP Function Features bitmask -- this build
declares no optional PFCP features, a disclosed minimal-viable scope, not a feature evaluated and
rejected). Every other PFCP message type is logged and ignored (disclosed, not silently
mishandled) -- Session Establishment/Modification/Deletion is Stage 3's scope, not this one's.

**Verification:** real interop, not just unit tests -- started real `nrf` + real `upf`, confirmed
real NRF registration (HTTP 201, `nfType=UPF`), then sent real hand-crafted PFCP datagrams (a
Python script constructing genuine wire bytes per this project's own `pfcp_core` codec) for both
Heartbeat Request and Association Setup Request over a real UDP socket to port 8805. Both responses
decoded byte-for-byte as expected: Heartbeat Response with the correct message type and Recovery
Time Stamp; Association Setup Response with Node ID (127.0.0.1), Cause=1 (accepted), Recovery Time
Stamp, and UP Function Features=0x0000, in the exact TLV order this build encodes them. This is not
interop against a second independent PFCP implementation (unlike the AMF/NGAP staging, which had
real UERANSIM as a genuine third-party peer) -- disclosed: no third-party PFCP client/CP-function
reference implementation is vendored in this project yet, so this verification proves UPF's own
codec and message handling are internally consistent and match this project's own spec-derived
understanding, not that it interops with an independent real-world PFCP peer. That remains a gap
Stage 2 (SMF as a real PFCP client) will close for real, the same way UERANSIM closed it for NGAP.
12 unit tests were already added in ADR-0039 for the codec layer this stage's message handlers use;
full `ctest` suite re-run clean, 107/107 passing, zero regressions.

**Consequence:** UPF exists, registers with NRF for real, and answers the two PFCP procedures
needed before any N4 session can be established. Stage 2 (SMF becomes a real PFCP client,
discovering UPF via NRF and performing a real Association Setup at SMF startup) is the next
increment -- that is what will finally provide the independent-peer verification this stage's own
disclosed limitation calls out as missing.

## ADR-0041: Phase 3 Stage 2 -- SMF as a real PFCP client, real Nnrf_NFDiscovery, real Sx Association

**Date:** 2026-08-08
**Status:** Accepted

**Context:** ADR-0040 (Stage 1) left one disclosed gap: UPF's Heartbeat/Association Setup handling
had only been verified against a hand-crafted test script, not an independent third-party PFCP
implementation. This stage closes that gap for real by making SMF a genuine PFCP client that
performs a real Association Setup against real UPF -- two independently-built processes in this
project's own codebase, neither aware of the other's internals, actually interoperating over the
wire. This is also the next step TS 23.502's real PDU Session Establishment flow needs: SMF must
have an established Sx/N4 Association with a UPF before Stage 3's Session Establishment can target
one.

**Real `Nnrf_NFDiscovery`, not a hardcoded address -- and the first real use of an NRF capability
this project built but never actually called.** Checked NRF's own code (`nfs/nrf/src/main.cpp`)
before assuming either way: `SearchNFInstances` (`GET /nnrf-disc/v1/nf-instances?target-nf-type=...`)
has been implemented since NRF's own turn, but every NF-to-NF call in this project so far
(SMF->PCF, SMF->AMF, AMF->PCF/AUSF/SMF, ...) has used a hardcoded `kXxxBase` constant instead of
ever calling it -- a real, pre-existing gap this stage closes, not a new one introduced. SMF now
calls the real endpoint (`discover_upf_ipv4` in `nfs/smf/src/main.cpp`), parses the real
`NFInstances` response, and extracts UPF's real registered `ipv4Addresses` entry -- the exact value
`nfs/upf/src/main.cpp`'s own `run_nrf_lifecycle` (ADR-0040) put there. Retries forever with a 2s
backoff if no UPF is registered yet, same discipline `run_nrf_lifecycle` itself already uses for
NRF-not-up-yet.

**New `run_pfcp_lifecycle` in `nfs/smf/src/main.cpp`**, a dedicated thread (same "blocking
transport gets its own thread" discipline as every prior blocking-I/O thread in this project --
ADR-0006/ADR-0030/ADR-0039's own UDP-instead-of-a-wrapper-class choice) that builds and sends a
real PFCP Association Setup Request (Node ID, Recovery Time Stamp, CP Function Features -- the same
three IE types UPF's own Association Setup Response already uses, now exercised from the other
side), decodes UPF's real response, and confirms `Cause=Request accepted (1)` before proceeding.
Retry structure: T1 timer (2s, via `SO_RCVTIMEO` on the raw UDP socket -- Boost.Asio's synchronous
API has no built-in receive timeout, so this uses the same direct-POSIX-call discipline
`libs/ngap-core`'s SCTP wrapper already established where Asio doesn't cover something) and N1=3
retries per TS 29.244 §6.4's reliable-delivery model, with the whole procedure restarting after a
5s backoff if all N1 retries are exhausted -- both T1/N1 values are this build's own reasonable
fixed choices, since the spec itself leaves them implementation-specific, not a citable fixed
number.

**Disclosed, deliberate scope limit**: the established UPF endpoint is not yet persisted anywhere
other than a log line -- no cross-thread storage was added for `CreateSMContext`'s handler to read,
since nothing reads it yet. Stage 3 (real N4 Session Establishment wired into `CreateSMContext`)
is where that storage becomes genuinely needed, and adding it now would be exactly the kind of
"design for a hypothetical future requirement" CLAUDE.md's engineering rules warn against.

**Verification:** real end-to-end interop, first attempt -- real `nrf` + real `upf` + real `smf`,
three independent processes. `smf`'s log: `discovered UPF at 127.0.0.1 via Nnrf_NFDiscovery`
immediately followed by `PFCP Sx Association established with UPF at 127.0.0.1`. `upf`'s own,
independently-generated log confirms the same exchange from its side: `Sx Association Setup
accepted from 127.0.0.1`. This is the independent-peer verification ADR-0040 disclosed as missing
-- SMF and UPF are separately-linked binaries built from separate source files, with no shared
in-process state, genuinely exchanging real PFCP wire bytes over a real UDP socket. Full `ctest`
suite re-run clean, 107/107 passing, zero regressions (no new unit tests this stage -- the codec
layer this stage exercises was already covered by ADR-0039's 12 tests, and this stage's own new
code is a client-orchestration loop around that codec, verified via the real interop run above
rather than a second layer of mocked-transport unit tests).

**Consequence:** SMF and UPF have a real, independently-verified Sx/N4 Association. Stage 3 (real
PFCP Session Establishment wired into `CreateSMContext`, closing Phase 3's control-plane arc) is
the next increment -- it can now build on a real, proven Association rather than a hardcoded
assumption.

## ADR-0042: Phase 3 Stage 3 -- real N4 Session Establishment, wired into CreateSMContext

**Date:** 2026-08-08
**Status:** Accepted

**Context:** ADR-0041 (Stage 2) gave SMF a real, verified Sx Association with UPF but nothing used
it yet. This stage wires a real PFCP Session Establishment (TS 29.244 §7.5.2/§7.5.3) into the
existing `CreateSMContext` flow, so a real PDU Session Establishment now creates a real N4 session
end to end -- the control-plane arc Phase 3 set out to close.

**New spec research: grouped IEs.** Unlike every PFCP message implemented so far (Heartbeat,
Association Setup -- both flat IE lists), Session Establishment needs Create PDR/PDI/Create
FAR/Forwarding Parameters, all *grouped* IEs (an IE whose value is itself a sequence of child IEs,
TS 29.244 §7.2.3.3). Read the real spec text for this (clauses 7.5.2.2/7.5.2.3/8.2.2-8.2.3/8.2.11/
8.2.24/8.2.37/8.2.74, from the already-vendored `specs/PFCP/29244-e30.pdf`) before writing any
code, confirming: a grouped IE's on-wire value is simply its child IEs' bytes concatenated --
meaning `pfcp_core::encode_ie`/`decode_ies` (built for flat IEs in ADR-0039) needed **no
modification** to handle grouped IEs; a grouped IE's "value" passed to `encode_ie` is just another
buffer built by calling `encode_ie` repeatedly, and `decode_ies` recurses into it by construction.
Confirmed by a new unit test (`PfcpSessionIes.GroupedIeRoundTripsViaExistingIeCodec`) before using
it for anything real.

**New `libs/pfcp-core/session_ies.{hpp,cpp}`**: F-SEID, PDR ID, Precedence (confirmed from the real
spec: *lower* value means *higher* precedence, the non-intuitive direction, not assumed), FAR ID,
Apply Action (FORW only -- the only action this build's minimal PDR/FAR needs), Source/Destination
Interface (confirmed as the same value table via two independently-read spec figures, not assumed
identical), and F-TEID in both its "CH request" (CP asks UP to allocate) and "allocated" (UP's real
answer) shapes. 10 new unit tests.

**Minimal but genuinely spec-correct session shape: one uplink PDR/FAR pair, no downlink.** SMF's
`perform_n4_session_establishment` builds: a PDR (Source Interface=Access, F-TEID CH-requested so
UPF allocates its own local GTP-U endpoint) paired with a FAR (Apply Action=FORW, Destination
Interface=Core). No downlink PDR/FAR is created -- that would need the gNB's real N3 GTP-U
endpoint (TEID+IP), which only arrives via NGAP's PDU Session Resource Setup procedure, still not
implemented (a disclosed gap predating this turn, first noted in ADR-0038's N2 SM info comment).
Disclosed explicitly in `nfs/upf/src/main.cpp`'s own header comment, not left for a reader to
discover: UPF allocates a real F-TEID and echoes it back, but no packet will ever actually flow
through it yet (Stage 4's datapath doesn't exist either).

**UPF's Association Setup response now honestly declares FTUP support** (`UP Function Features`
bit 5, "F-TEID allocation/release in the UP function is supported") -- Stage 1 sent an all-zero
"no optional features" bitmask because that was true at the time; it would have been dishonest to
leave unchanged now that UPF genuinely does allocate F-TEIDs on request.

**Cross-thread storage, deferred from Stage 2 until genuinely needed.** ADR-0041 explicitly held
off on persisting the discovered UPF endpoint because nothing read it yet. This stage is that
reader: new `UpfEndpointStore` (mutex-protected, matching `NgapUeRegistry`'s discipline in AMF) is
written once by `run_pfcp_lifecycle` after a successful Association and read by `CreateSMContext`'s
route handler (the `ioc` thread) -- the same kind of genuine cross-thread handoff AMF's
`NgapUeRegistry` needed for N1N2MessageTransfer (ADR-0038), now needed here for the same underlying
reason: a blocking-transport background thread's state has to reach an HTTP request handler on a
different thread.

**Refactored, not duplicated: `send_pfcp_request_and_await_response`.** Stage 2's Association Setup
and this stage's Session Establishment both need identical send/wait-with-timeout/retry mechanics
(T1=2s via `SO_RCVTIMEO`, N1=3 retries, TS 29.244 §6.4) -- extracted into one shared function
(`nfs/smf/src/main.cpp`) rather than copy-pasting Stage 2's loop a second time, since this is now a
genuine second real call site, not a hypothetical one.

**Best-effort, matching ADR-0038's own precedent**: N4 Session Establishment failure is logged, not
fatal to `CreateSMContext`'s 201 response -- the same non-blocking-on-a-downstream-real-integration
discipline already established for the N1N2MessageTransfer call in the same handler.

**Verification:** full real end-to-end interop, first attempt, zero regressions -- real
`nrf`/`udr`/`udm`/`ausf`/`pcf`/`upf`/`smf`/`amf` plus real `nr-gnb`/`nr-ue`. `smf`'s log: `N4
Session Establishment succeeded for pduSessionId 1, UPF F-SEID=0x1, allocated uplink F-TEID=0x1`,
correctly ordered *before* `PDU Session Establishment Accept delivered to AMF` (matching TS 23.502's
real step ordering: N4 Session Establishment happens before the Accept is sent). `upf`'s own,
independently-generated log confirms the same exchange: `allocated F-TEID 0x1 for PDR ID 1` /
`Sx Session established from 127.0.0.1`. The real UE's own log is unaffected and still shows the
full procedure succeeding (`PDU Session establishment is successful PSI[1]`) -- this stage adds a
real N4 session underneath an already-working procedure, it doesn't change UE-visible behavior
(expected: UPF's datapath doesn't exist yet, so there was nothing for the UE to notice). 10 new
unit tests; full `ctest` suite re-run clean, 117/117 passing.

**Consequence:** Phase 3's control-plane arc (PFCP Association + real Session Establishment,
triggered by a real PDU Session Establishment procedure end to end) is now genuinely complete.
Stage 4 (the eBPF/XDP datapath itself, ADR-0039's own evaluated-and-approved choice) is the only
remaining increment before Phase 3's stated scope ("User plane: N4/PFCP, UPF datapath") is fully
met.

## ADR-0043: Phase 3 Stage 4 -- eBPF/XDP GTP-U decapsulation datapath (fully live-verified end to end)

**Date:** 2026-08-08 (initial, unverified version), updated 2026-08-09 (partial live-testing
update, one gap left open), updated again 2026-08-09 (root cause found and fixed, full end-to-end
live verification obtained -- see "Resolution: the ARP gap, root-caused and fixed" at the end of
this ADR).
**Status:** Accepted. Superseded in full by the final section below: the one remaining gap the
"Live-testing update" section left open (ARP resolution / final packet delivery) has since been
root-caused, fixed, and live-verified end to end with a real PFCP-allocated TEID. Nothing about
this stage's runtime behavior is unverified anymore.

**Context:** ADR-0042 (Stage 3) closed Phase 3's control-plane arc: a real N4 session is created,
UPF allocates a real F-TEID, but no packet has ever actually flowed through it. This stage builds
the real datapath ADR-0039 evaluated and chose (eBPF/XDP over DPDK/VPP) to close that gap.

**Environment blocker, disclosed as it happened rather than worked around silently.** Loading and
attaching an XDP program needs `CAP_BPF`/`CAP_NET_ADMIN` (and, on this kernel, apparently
`CAP_SYS_ADMIN` too for the `RLIMIT_MEMLOCK` bump `bpf_object__probe_loading` performs); creating
the veth pair and TUN device this design needs also requires `CAP_NET_ADMIN`. This session's
shell environment has an empty active capability set and `sudo` requires a password that cannot be
supplied non-interactively (confirmed: `sudo -n true` fails; a real `bpftool prog load` attempt
against the compiled object failed with a plain `EPERM`, not a verifier rejection). The user chose,
explicitly asked via `AskUserQuestion`, to grant the built UPF binary the needed capabilities via
`setcap` themselves rather than have this turn stop here or proceed with zero testing. `libbpf-dev`
and `clang` were installed by the user (`sudo apt install -y libbpf-dev clang`) enabling real
compilation. **The `setcap` grant itself was not completed during this turn** -- after being asked
directly and given the exact command, and after several subsequent "keep going" instructions with
no confirmation the command had been run, the turn proceeded on the reasonable reading that the
user wanted the code finished and disclosed as untested rather than the turn blocked indefinitely.
This is recorded plainly, not glossed over: this is the first stage in this entire project's
NGAP/PFCP staged work where the code was NOT verified against real, live execution before being
called done.

**Real spec research, done properly regardless of the above.** TS 29.281 (GTPv1-U) V10.3.0's real
spec PDF was fetched (WebSearch/WebFetch, ARIB archive mirror, same methodology as PFCP's own
ADR-0039) and read directly: Figure 5.1-1 "Outline of the GTP-U Header" (the mandatory 8-octet
header: version/PT/E/S/PN flags, message type, length, TEID), Table 6.1-1 (message type 255 =
G-PDU, the only message type carrying real T-PDU payload), and clause 4.4.2.3 (UDP destination
port 2152). The XDP program's header parsing is built from this real spec text, not memory.

**Design: XDP does real in-kernel parsing/matching; a boring, certainly-correct userspace write()
does final delivery.** New `nfs/upf/bpf/gtpu_decap.bpf.c`: parses Ethernet/IPv4/UDP/GTP-U headers
with full bounds checks (required for BPF verifier acceptance -- every pointer dereference is
preceded by a `data_end` comparison), looks up the TEID in a `BPF_MAP_TYPE_HASH` populated by
UPF's own control plane (wired into Stage 3's existing F-TEID allocation in
`nfs/upf/src/main.cpp`), and on match extracts the T-PDU into a `BPF_MAP_TYPE_RINGBUF` using the
standard "mask the dynamic length to a provable power-of-two bound" idiom BPF's verifier needs for
a non-constant `bpf_ringbuf_reserve` size. **Deliberately does NOT use `bpf_redirect`/
`XDP_REDIRECT`** to inject the decapsulated packet into a TUN device directly from kernel context:
whether `XDP_REDIRECT` can target a TUN device specifically could not be confirmed from current,
authoritative documentation without risking kernel code that looks plausible but silently fails at
runtime -- a risk this ADR is explicitly unwilling to take silently, consistent with every other
"verify, don't assume" decision this project has made. Instead, `nfs/upf/src/datapath.cpp`'s
background thread polls the ring buffer (`ring_buffer__poll`) and writes each decapsulated T-PDU
to a TUN device (`upf-tun0`, created via the real `TUNSETIFF` ioctl) with an ordinary `write()` --
XDP does exactly the part it's good at (fast in-kernel header parsing and TEID matching), and the
part with unverified kernel-API risk is avoided entirely rather than gambled on.

**veth pair (`upf-n3`/`upf-n3-peer`) instead of loopback.** Whether loopback's SKB layout at the
XDP layer reliably presents a real Ethernet header (this program's parser assumes one) was also
not something this project could confirm confidently -- veth pairs are the standard,
unambiguously-Ethernet-framed interface type XDP tutorials and the kernel's own BPF selftests use,
and creating one needs no more privilege than the datapath already requires. Interface/address
setup (`ip link add ... type veth`, `ip addr add`, `ip link set ... up`) is delegated to a
shell-out to `ip` (iproute2) rather than hand-written netlink code -- a disclosed, deliberate
simplification (netlink message construction is a substantial separate scope this stage's actual
goal, correct GTP-U decapsulation, doesn't need to justify).

**Compile-time toolchain, separate from this project's normal C++ build.** New
`find_program(CLANG_EXECUTABLE ...)` + `add_custom_command` in `nfs/upf/CMakeLists.txt` invokes
`clang -target bpf` (a completely different backend from the x86-64 C++ compilation the rest of
this project uses) to produce a BPF ELF object; `PkgConfig::libbpf` links the userspace loader
side. `libbpf-dev`/`clang` are new build dependencies for this one NF only.

**What IS verified (real, not claimed):**
- The BPF C program compiles cleanly with `clang -target bpf` -- zero warnings, zero errors.
- The compiled object's structure is correct, confirmed via static inspection that needs no
  kernel privileges (`llvm-objdump -h`: real `xdp`/`.maps`/`.BTF`/`license` ELF sections present;
  `bpftool btf dump file` -- read-only static analysis, does NOT load anything into the kernel --
  confirms `teid_map` is a `BPF_MAP_TYPE_HASH` with `__u32` key/value and 64 max entries exactly as
  written, `tpdu_ringbuf` is `BPF_MAP_TYPE_RINGBUF` with 262144 max entries exactly as written, and
  `gtpu_decap_prog`'s BTF function signature correctly takes a `struct xdp_md*`).
- `nfs/upf/src/datapath.cpp`/`main.cpp` compile and link cleanly against real `libbpf`, real
  `<linux/if_tun.h>`, and real POSIX socket/ioctl APIs -- including catching and fixing (during
  this same turn, via code review before any attempted execution) a real correctness bug in an
  earlier draft of the BPF program: reserving/copying a fixed 1500-byte ring buffer slot
  regardless of the actual packet's length, which would have leaked adjacent kernel memory bytes
  into every decapsulated T-PDU shorter than 1500 bytes -- fixed with the length-masking idiom
  described above before this was ever run.
- Full `ctest` suite (117 tests, none new this stage -- see below) re-run clean after adding this
  stage's code, confirming zero regressions to every previously-verified stage.

**What is NOT verified (the actual gap):**
- Whether the BPF *verifier* (not just the compiler) accepts this program -- bounds-checking
  logic that looks correct to a human reviewer is a well-known source of BPF verifier rejections
  that only a real load attempt reveals.
- Whether the veth pair, TUN device, and XDP attach actually succeed at runtime.
- Whether a real GTP-U packet sent to the attached interface is actually matched, decapsulated,
  and correctly delivered to the TUN device -- i.e., whether the datapath does what it claims to
  do at all. A test script (`gtpu_test.py`, kept in the scratchpad, not committed -- it has no
  purpose until the code above can actually run) was prepared but never executed.
- No new unit tests were added this stage for exactly this reason: a unit test asserting behavior
  that has never been observed to occur would be worse than no test, since it would look like
  verification without being any.

**Consequence (superseded by the section below):** Phase 3's code is now complete for its full
stated scope (control plane through ADR-0042, datapath through this ADR), but this stage's
real-world correctness is genuinely unknown, not just formally caveated. The next session (or this
one, once the capability grant lands) must run `gtpu_test.py` against a real, privileged `upf`
process and report the actual result -- success, a verifier rejection needing a fix, or a runtime
bug -- before this stage can be considered done in the sense every other stage in this project has
been.

### Live-testing update (2026-08-09)

The user granted the capability set this ADR's original text disclosed as needed
(`sudo setcap cap_net_admin,cap_bpf,cap_sys_admin+eip` on the built `upf` binary) and live testing
proceeded for real. This section records exactly what that testing found -- two real bugs fixed,
substantial genuine verification gained, and one specific gap that remains, described precisely
rather than glossed over.

**Real bug 1, found immediately: ambient capabilities, two layers deep.** `setcap` grants
capabilities to the *file*; a child process this binary spawns via `popen()` (the `ip` shell-outs
in `datapath.cpp`) does NOT automatically inherit them -- confirmed for real (not assumed) by
reproducing the exact same `RTNETLINK answers: Operation not permitted` manually, unprivileged,
before writing the fix. The standard fix, Linux ambient capabilities, itself needed a second real
fix once applied: per `execve(2)`'s actual capability-transition rules, a new process's
*inheritable* set is inherited from its parent (whatever shell launched it, which has an empty
inheritable set), not populated from the binary's file capabilities the way *permitted* is --
so the ambient-raise itself failed with a second real, confirmed `EPERM` even though `getcap`
showed the grant present on the file. Fixed by explicitly moving `CAP_NET_ADMIN` from this
process's own permitted set into its own inheritable set first (via `libcap`'s
`cap_set_flag`/`cap_set_proc` -- a new build dependency, `libcap-dev`, the user installed), which
a process is always allowed to do for a capability it already holds. Both bugs, and both fixes,
are documented in full in `nfs/upf/src/datapath.cpp`'s own comments, not just here.

**Real bug 2, found immediately after: `bpf_ringbuf_reserve` needs a genuine compile-time
constant.** Once veth/TUN creation started working, `bpf_object__load` reached the real BPF
verifier for the first time -- and it rejected the program: `R2 is not a known constant` on the
`bpf_ringbuf_reserve` call. The masking idiom (`tpdu_len &= 2047`) this project's original,
untested version used gives the verifier a provable *range*, which is sufficient for
`bpf_probe_read_kernel`'s size argument but NOT for `bpf_ringbuf_reserve`'s -- that helper requires
an actual literal/constant on this kernel/libbpf combination, confirmed by the verifier's own
rejection message, not assumed from documentation. Fixed by switching to the standard pattern real
eBPF codebases use for this exact situation: a fixed-size `struct tpdu_record { __u16 length;
unsigned char data[1500]; }`, always reserving `sizeof(*rec)` (a real compile-time constant), with
`length` telling the consumer how many of `data`'s bytes are the genuine T-PDU. Both
`gtpu_decap.bpf.c` and `datapath.cpp`'s consumer were updated to match.

**What IS now live-verified, for real, that the original version of this ADR could not confirm:**
- The BPF *verifier* accepts the program (after the fix above) -- `bpf_object__load` succeeds.
- The veth pair (`upf-n3`/`upf-n3-peer`) and TUN device (`upf-tun0`) are created for real; `ip
  link show upf-n3` confirms `xdpgeneric` mode with a real attached program (`prog/xdp id 682`
  observed).
- The BPF ring buffer and its polling thread start successfully.
- **Real control-plane integration, triggered by a real PFCP exchange, not a synthetic test of
  the map alone**: a hand-crafted-but-spec-correct PFCP Association Setup followed by a real
  Session Establishment Request (the same message shape ADR-0042's real `smf` sends) was sent to
  the live, privileged `upf` process. UPF allocated a real F-TEID (`0x1`) and its own log confirms
  `Sx Session established` -- and per `main.cpp`'s existing Stage 3 wiring, this real code path
  calls `datapath->register_teid(0x1)`, successfully inserting it into the live BPF hash map (no
  error logged, and the subsequent behavior below is consistent with the insert having succeeded).

**What is NOT yet verified -- the one remaining, specific gap.** A hand-crafted GTP-U test packet
(`gtpu_test.py`, spec-correct per TS 29.281, sent with the real allocated TEID) was sent toward
`upf-n3` from outside the process, forced across the real veth wire via `SO_BINDTODEVICE` (needed
because a naive send to `upf-n3`'s own address gets short-circuited by Linux's local-delivery
route, confirmed by RX counters not moving at all on the first attempt -- a real finding about
*how to test this*, not about the datapath itself). RX packet counters on `upf-n3` DID increase
after switching to `SO_BINDTODEVICE`, confirming packets physically reach the interface. But **ARP
resolution between the two veth peers fails** (`ip neigh show` reports `FAILED`/`INCOMPLETE` for
`upf-n3-peer -> upf-n3`), and no decapsulated T-PDU ever reached `upf-tun0` (0 RX packets
throughout). This was investigated at length: the XDP program's own logic passes ARP through
untouched at its very first check (`eth->h_proto != ETH_P_IP` -> `XDP_PASS`, before any GTP-U-
specific logic runs) and is very unlikely to be the cause; `arp_ignore`/`arp_filter` sysctls on
`upf-n3` are unset (0, not blocking); a documented real quirk of generic/SKB-mode XDP on veth
devices exists (SKB cloning can cause the XDP hook to be skipped for some packets, found via
research, not assumed) but does not obviously explain an ARP responder failing outright. Whether
this is an artifact of this specific sandboxed dev environment's network/veth handling, a firewall
rule this session's unprivileged shell could not inspect (`iptables`/`nft` both required `sudo`
that a follow-up diagnostic request was not completed for), or a genuine bug in this project's own
datapath setup was not conclusively root-caused before this session's priorities moved to Phase 4.
**This is disclosed as a real, open, unresolved item -- not silently dropped.**

**Consequence (superseded by the section below):** Phase 3's control-plane arc (Stages 0-3,
ADR-0040-ADR-0042) and this stage's own BPF program correctness (verifier acceptance) and
control-plane wiring (real TEID registration from a real PFCP exchange) were genuinely
live-verified at this point. The single remaining unverified claim was narrow and specific:
whether a real GTP-U packet, once it reaches `upf-n3`, is actually decapsulated and delivered to
`upf-tun0` end-to-end.

### Resolution: the ARP gap, root-caused and fixed (2026-08-09)

The user explicitly instructed that Phase 3 must be fully completed and live-verified before any
Phase 4 work continued (Phase 4/CHF scaffolding that had already started in that same session was
paused, left uncommitted, and resumed only after this section's verification was obtained). This
section records the real root cause and fix.

**Diagnostic access, itself a real obstacle.** Root-causing this needed real packet captures
(`tcpdump`) and privileged interface reconfiguration (`ip`, `bpftool`), none of which this
project's own shell environment had. `setcap`-granting `cap_net_raw` to a copy of `tcpdump` under
the repo's own `build/` directory (rather than `/usr/bin/tcpdump` directly -- confirmed for real
that `setcap` silently fails to persist on `/usr`, apparently a filesystem/mount characteristic of
this environment, while it works normally under `/home`) unblocked live packet capture. Privileged
`ip`/`bpftool` reconfiguration needed a one-time, explicitly user-approved, narrowly-scoped
passwordless-sudo grant (`visudo`, `NOPASSWD` for exactly `/usr/sbin/ip`, `/usr/sbin/bpftool`,
`/usr/sbin/setcap` -- nothing broader) after several rounds of manually-run `sudo` commands proved
unreliable to verify secondhand (a real, disclosed lesson: several early "ran it, succeeded"
confirmations turned out, on direct re-check, not to have taken effect -- resolved by verifying
every privileged step directly rather than trusting a secondhand report of success).

**Real finding 1: `ip link set dev X xdp off` (no mode) silently no-ops against a generic-mode
attachment.** Both `ip link set dev upf-n3 xdp off` and `bpftool net detach xdp dev upf-n3`
returned exit 0 with zero effect on a program attached via `XDP_FLAGS_SKB_MODE` (generic mode,
confirmed via `bpftool link show` returning empty -- i.e. not a `bpf_link`, ruling out that
hypothesis) -- `ip link set dev upf-n3 xdpgeneric off` (explicitly naming the mode) is what
actually detached it. A real iproute2 behavior, not a bug in this project's own code, but the
kind of tooling gotcha that ate significant diagnostic time before being isolated.

**Real finding 2, the actual root cause: two same-namespace routes to the same /30.**
`Datapath::create()` (`nfs/upf/src/datapath.cpp`) created BOTH veth ends (`upf-n3` AND
`upf-n3-peer`) in the same (default/init) network namespace, each with an address in the same
`10.99.0.0/30`. `ip route show` confirmed two separate `proto kernel scope link` routes for the
identical prefix, one per interface -- a genuinely degenerate, ambiguous configuration that does
not arise in normal veth usage (where at least one end is always moved into a separate namespace,
which is the entire reason veth pairs exist). Live packet capture on both interfaces proved the
ARP *request* crossed the wire correctly (visible on `upf-n3` via `tcpdump`, 3 real retries) but no
ARP *reply* was ever generated -- and, decisively, the same failure was reproduced with the XDP
program fully detached, ruling out the XDP program (its own logic or its generic/SKB-mode
attachment) as the cause entirely, isolating it to the routing-table ambiguity.

**Fix.** `kN3PeerIface` (`upf-n3-peer`) is now moved into its own network namespace
(`upf-n3-peer-test-ns`) immediately after veth creation, via `ip netns add` + `ip link set ...
netns ...`, with its address assigned inside that namespace (`ip -n upf-n3-peer-test-ns addr
add ...`). This structurally removes the overlapping-route ambiguity rather than working around a
symptom -- the standard fix for exactly this class of problem. `ip netns add` needed two
capabilities this binary didn't already ambient-raise: `CAP_SYS_ADMIN` (for the
`unshare(CLONE_NEWNET)` the command performs internally -- confirmed via a real `EPERM` with only
`CAP_NET_ADMIN` raised) and `CAP_DAC_OVERRIDE` (for creating the bind-mount target file under
`/run/netns/`, confirmed to be `root:root` mode `0755` -- a plain DAC check, unrelated to
`CAP_SYS_ADMIN`, confirmed via a real `EACCES`/"Permission denied" once `CAP_SYS_ADMIN` alone was
already in place). `ensure_datapath_caps_ambient()` (renamed from `ensure_net_admin_ambient()`)
now raises all three; the `setcap` grant on the built `upf` binary itself was correspondingly
widened to `cap_net_admin,cap_sys_admin,cap_bpf,cap_dac_override+eip`. The destructor additionally
runs `ip netns del upf-n3-peer-test-ns` on shutdown.

Since `kN3PeerIface` only exists as a same-host stand-in for real N3/gNB traffic (which this
project doesn't have yet -- see the disclosed NGAP PDU Session Resource Setup gap), this fix is
scoped entirely to `Datapath::create()`'s own test-injection side; `kN3Iface` (the interface a real
gNB's traffic would eventually arrive on) is unaffected and stays in the default namespace, exactly
where a real-deployment N3 NIC would be.

**Full end-to-end live verification obtained, with a real (not synthetic) TEID.** The complete
stack (nrf, udm, udr, ausf, pcf, smf, amf, upf) was started, then a real `nr-gnb`/`nr-ue` run
performed a genuine Initial Registration (including a real SQN resynchronization) and PDU Session
Establishment against it. `smf`'s log: `N4 Session Establishment succeeded for pduSessionId 1, UPF
F-SEID=0x1, allocated uplink F-TEID=0x1`; `upf`'s log: `allocated F-TEID 0x1 for PDR ID 1`. A
spec-correct GTP-U G-PDU test packet (`gtpu_test.py`, TS 29.281-correct header) carrying that exact
TEID (`0x1`) was then sent from inside `upf-n3-peer-test-ns` toward `upf-n3`. Result:
- `ip neigh show` inside the peer namespace: `10.99.0.1 dev upf-n3-peer lladdr 3a:fa:34:e8:6f:98
  REACHABLE` -- ARP resolution now succeeds.
- `upf`'s own log: `upf-datapath: delivered decapsulated T-PDU (44 bytes) to upf-tun0` -- an exact
  byte-count match against the real T-PDU size the test script sent, emitted only after this
  code's own `write()` return-value check (`written != tpdu_len` would have logged a warning
  instead) confirmed a complete, successful write to the TUN device.

An attempt to also independently read the delivered bytes back from `upf-tun0` in a second process
was tried and correctly failed -- a TUN device delivers to whichever single file descriptor
originally opened it via `TUNSETIFF` (the running `upf` process itself), not to a second, later
attacher; this is a structural property of TUN devices, not a gap in the evidence above.

**Consequence:** Phase 3 (Stages 0-4, control plane through datapath) is now fully live-verified
end to end with no open runtime-correctness questions: real PFCP codec, real UPF NF, real
SMF-as-PFCP-client via real `Nnrf_NFDiscovery`, real N4 Session Establishment triggered by a real
PDU session, a real XDP program that passes the verifier, and now real GTP-U decapsulation and
delivery to a TUN device, driven by a TEID that came from the real control-plane path rather than
being inserted for the test. Every gap this ADR previously disclosed as open is now closed.

## ADR-0044: Phase 4 Stage 0/1 -- CHF (Nchf_ConvergedCharging_Create), real N40 wiring, and a genuine sbi-codegen bug fix

**Date:** 2026-08-09
**Status:** Accepted.

**Context:** Phase 4 (Charging + TM Forum SID/BSS layer) begins. Scope for this turn, shown to and
approved by the user before any code was written (per CLAUDE.md's "show the procedure list, get
approval first" rule): a new `nfs/chf/` skeleton plus real `Nchf_ConvergedCharging_Create`
(`POST /chargingdata`) only, wired to SMF over N40 at PDU Session Establishment -- the same trigger
point Stage 3's real N4 Session Establishment uses (ADR-0042). Update/Release, the
`chargingNotification` callback, `Nchf_OfflineOnlyCharging`, `Nchf_SpendingLimitControl`, and the
TM Forum SID/BSS mapping layer are deliberately deferred to separate future turns -- the SID/BSS
layer specifically needs its own turn because CLAUDE.md requires `docs/CHARGING_MAPPING.md` (an
explicit 3GPP-CDR-field -> SID-entity -> TMF-API-resource table) to exist *before* any mapping
code, which this turn's pure-3GPP-side scope doesn't need yet.

**Real API surface, confirmed from the actual R19 YAML, not assumed.** None of the three CHF-
related YAML files (`TS32291_Nchf_ConvergedCharging.yaml`, `TS32291_Nchf_OfflineOnlyCharging.yaml`,
`TS29594_Nchf_SpendingLimitControl.yaml`) use an `operationId` field at all -- confirmed by
grepping all three (zero matches), a genuine property of how these particular charging specs are
authored, not a search miss. `Nchf_ConvergedCharging`'s real paths: `POST /chargingdata` (Create),
`POST /chargingdata/{ChargingDataRef}/update`, `POST /chargingdata/{ChargingDataRef}/release`, plus
a `chargingNotification` callback. This turn implements only the first.

**A genuine, pre-existing `tools/sbi-codegen` bug, found and fixed before CHF's DTOs would even
compile.** Adding `TS32291_Nchf_ConvergedCharging.yaml` to the codegen pilot set (`libs/sbi-
generated/CMakeLists.txt`) caused a real compile failure unrelated to CHF's own schema: `TS28541_
NrNrm.hpp`'s `using TaiList = std::vector<Tai>;` referenced a `Tai` type that no longer existed
under that bare name. Root cause: `schema_to_ir.py`'s `Converter._disambiguate()` correctly renames
every `ObjectType` field's `TypeRef` when a schema name collides across multiple files (real,
already-handled cases per ADR-0017) -- but `AliasType.cpp_underlying` (used for the array-of-named-
type case, e.g. `std::vector<Foo>`) was flattened to a plain string at construction time, *before*
disambiguation could know the element type's final name, and was never rewritten afterward. CHF's
YAML transitively pulled in a second/third file also defining `Tai` (`TS28623_GenericNrm.yaml`,
which `TaiList` actually `$ref`s), triggering a collision that hadn't existed in the smaller pilot
set before -- disambiguation correctly renamed every colliding `Tai` to `Tai_<file>`, but `TaiList`'s
alias string still said the now-nonexistent bare `Tai`. Fixed by giving `AliasType` a structured
`element_ref: TypeRef | None` field (populated alongside the existing flattened string in the array
branch of `_convert_one`), having `_disambiguate` rewrite that ref and regenerate `cpp_underlying`
from it when the element type gets renamed, and having `render.py`'s `_referenced_names` walk the
structured ref instead of regex-tokenizing the string. Real, reproducible, fixed at the generator
level (never hand-patch generated code) -- see `tools/sbi-codegen/sbi_codegen/{ir,schema_to_ir,
render}.py`.

**CHF (`nfs/chf/`).** New NF, port 7784, mirrors `nfs/pcf/src/main.cpp`'s skeleton shape (NRF
registration/heartbeat lifecycle, OAuth2, TLS 1.3+mTLS). `ChargingDataRefAllocator`
(`nfs/chf/src/stores.hpp`) is deliberately just a mutex-protected ID generator, not a full resource
store: Create doesn't need to read anything back this turn (unlike PCF's `AmPolicyStore`, which
backs a real `GET`), so a full store is deferred to the Update/Release turn that will actually need
one. Disclosed simplifications (stated in `nfs/chf/src/main.cpp`'s own file header too): no real
rating/quota engine (`multipleUnitInformation` is never populated -- schema-valid, not a real
charging decision, same category of gap as PCF's fixed-default policy, ADR-0028);
`invocationSequenceNumber` in the response echoes the request's value rather than assigning an
independent CHF-side sequence, because the YAML carries no field-level description distinguishing
the two and no normative TS 32.291 text is vendored in this repo to check -- echoing is the least-
invented choice, disclosed rather than picked silently; no persistence across restarts.

**A new shared utility, not CHF-private.** `ChargingDataRequest`/`Response`'s `invocationTimeStamp`
needed OpenAPI's `format: date-time` (RFC 3339) -- a genuinely different wire format from
`sbi_headers.hpp`'s existing `format_sender_timestamp` (RFC 7231 IMF-fixdate, for the
`3gpp-Sbi-Sender-Timestamp` *header*, not a JSON body field). New `libs/sbi-core/include/sbi_core/
datetime.hpp` + `src/datetime.cpp` (`format_rfc3339`), same small-single-purpose-utility precedent
as `uuid.hpp` -- kept in `sbi_core` rather than private to CHF since any future NF with a
`DateTime`-typed JSON field needs the identical formatting.

**SMF wired as a real N40 client (`nfs/smf/src/main.cpp`).** A `chf_client`/`chf_oauth` pair (same
one-client-per-NF-per-thread pattern as the existing `pcf_client`/`amf_client`), using a hardcoded
base URL (`kChfBase`) -- matching this file's own existing convention for PCF/AMF, not the
`Nnrf_NFDiscovery` path Stage 2's UPF discovery used (that was explicitly called out as the "first
real use of this NRF capability" in ADR-0041; every other NF-to-NF call in this file uses a
hardcoded base URL, before and after). `perform_n40_charging_data_create` sends only the three
mandatory `ChargingDataRequest` fields plus `subscriberIdentifier` -- `pDUSessionChargingInformation`
is deliberately left unset, since CHF's own rating engine doesn't exist yet to use it, so sending
it would be padding, not real content. Called right after the existing N4 Session Establishment
call in `CreateSMContext`'s handler, with the same best-effort/non-fatal discipline (logged on
failure, doesn't block the SM context's own 201) -- there is no real billing/quota dependency yet
for a charging-data failure to correctly block on, the same reasoning already applied to N4 and
N1N2MessageTransfer.

**Live verification, real interop between two independently-built processes.** Full stack (nrf,
udm, udr, ausf, pcf, chf, upf, smf, amf) started, then a real `nr-gnb`/`nr-ue` run performed Initial
Registration (including a real SQN resynchronization) and PDU Session Establishment. `smf`'s log:
`N4 Session Establishment succeeded ... allocated uplink F-TEID=0x1` immediately followed by
`Nchf_ConvergedCharging_Create succeeded for pduSessionId 1`. Verified independently on CHF's own
side too (not just trusting SMF's side of the claim), via each NF's own Prometheus counter,
scraped directly: `chf_charging_data_create_total{otel_scope_name="chf"} 1` and
`smf_chf_charging_data_create_total{otel_scope_name="smf"} 1` -- both real, both 1, confirming CHF
genuinely received and answered exactly one real `Nchf_ConvergedCharging_Create` call.

**Consequence:** Phase 4 Stage 0/1 is real, live-verified, and complete for its approved scope. Next
turns (separate, each needing its own approval per CLAUDE.md): Update/Release wired to session
modification/teardown, then `docs/CHARGING_MAPPING.md` before any TM Forum SID/BSS mapping code.

## ADR-0045: TM Forum SID mapping -- `docs/CHARGING_MAPPING.md` + `libs/bss-sid/` first slice

**Date:** 2026-08-10
**Status:** Accepted.

**Context:** CLAUDE.md's charging-domain rule requires `docs/CHARGING_MAPPING.md` (3GPP CDR field
-> SID entity -> TMF API resource, ambiguous mappings marked TODO and asked about, never silently
invented) to exist and be reviewed *before* any SID/BSS mapping code. No TM Forum Open API spec
files are vendored in this repo (unlike the 3GPP OpenAPI YAML) -- every field name and API number
in this ADR and the mapping doc was checked against a real TM Forum source, not recalled from
memory, same arms-length-reference-oracle discipline as NGAP/PFCP/GTP-U (ADR-0016/ADR-0031/
ADR-0039).

**Process note, disclosed plainly.** The research for this was delegated to a forked subagent with
instructions to research only and report back so the coordinator could write the mapping doc
itself. The fork instead wrote, committed, and pushed `docs/CHARGING_MAPPING.md` directly
(`9cd6b71`) before hitting an unrelated API connectivity error mid-summary. The content was
reviewed in full afterward and found sound (real citations throughout, honest scope limits, no
fabricated mappings) -- kept rather than redone, but the process deviation (writing/committing
without the intended review-before-commit step) is recorded here rather than left unremarked.

**Finding: CLAUDE.md's own TMF API list was incomplete.** Checking each in-scope SID entity
(Product, Service, Resource, Customer, Party, Agreement, ProductOffering, ProductPrice,
AppliedCustomerBillingRate, CustomerBill, BalanceTopUp) against TM Forum's real API directory found
four with no home in CLAUDE.md's stated TMF620/622/632/635/637/666/676/678/727 list: `Service`
(TMF633 Service Catalog / TMF638 Service Inventory), `Resource` (TMF639 Resource Inventory),
`Agreement` (TMF651), `BalanceTopUp` (TMF654 Prepay Balance -- confirmed via TM Forum's own data
model site and the TMF654 API user guide PDF). Asked, not silently resolved: the user chose to
extend the list. CLAUDE.md now states TMF620/622/632/633/635/637/638/639/651/654/666/676/678/727 --
`Service` gets both TMF633 and TMF638 (catalog and inventory), mirroring the catalog/inventory split
already present for `Product` (TMF620/TMF637) rather than arbitrarily picking one. Also corrected in
passing: TMF727 is **Service Usage Management**, not "Product Offering Qualification" as an earlier
reference implied -- that's TMF679, not in scope. `PROMPT.md` (the original brief CLAUDE.md
condenses) still states the narrower original list -- disclosed as a deliberate, flagged divergence
in `docs/CHARGING_MAPPING.md` itself (`PROMPT.md` reads as the user's own original text, not edited
without being asked) rather than silently left inconsistent.

**Mapping scope: only what's real today.** The mapping table covers exactly the 3GPP fields SMF's
`Nchf_ConvergedCharging_Create` call actually sends (Phase 4 Stage 0/1, ADR-0044):
`subscriberIdentifier`, `nfConsumerIdentification`, `invocationTimeStamp`,
`invocationSequenceNumber`. Every other field on `ChargingDataRequest`/`Response` (~20
`*ChargingInformation` blocks, `multipleUnitInformation`, etc.) is unpopulated by any NF in this
codebase, so mapping it now would mean inventing a shape for data that doesn't exist in any real
request -- deferred, listed explicitly as future work rather than silently dropped. Of the fields
actually sent, only `subscriberIdentifier` maps to a SID entity at all (`nfConsumerIdentification`
is 3GPP network-function provenance, not billing-domain data; the timestamp/sequence fields are
protocol bookkeeping, not standalone SID entities) -- so this turn's real, buildable slice is
exactly one mapping: SUPI -> TM Forum `Party`.

**Two inline TODOs resolved with real reasoning, not arbitrarily:**
- SUPI storage on TMF632 `Individual`: **`individualIdentification`** (`identificationType="SUPI"`),
  not `partyCharacteristic` -- `individualIdentification` is TM Forum's purpose-built extensibility
  point for strongly-typed external identifiers (the same shape passport/national-ID numbers use);
  a SUPI is a primary structured network identifier, not a supplementary characteristic.
- Future `chargingId` representation on `AppliedCustomerBillingRate`: **`characteristic`** array
  entry (name="chargingId"), not a custom top-level field -- `characteristic` is TM Forum's
  standard, spec-conformant extensibility mechanism; a non-standard top-level field would break
  conformance against the official TMF678 schema, which CLAUDE.md's own "swappable for a commercial
  stack" ODA-boundary goal depends on staying valid. Documented only -- no `AppliedCustomerBillingRate`
  is ever produced by this codebase yet (no rating engine exists), so there's nothing to attach this
  to today.

**`libs/bss-sid/`, a new, CHF-independent library.** Hand-written (not codegen'd -- no TMF632
OpenAPI YAML is vendored, same "hand-roll it, cite the real spec" precedent PFCP/GTP-U already
established for protocols/APIs with no local spec file). Models only `Individual.id` +
`individualIdentification` (confirmed real fields via TM Forum's actual TMF632 v4.0.0 swagger,
fetched live) -- not the ~25-field full `Individual` schema, since nothing in this codebase has
data for or a consumer of the rest yet (CLAUDE.md's "no speculative abstraction" rule).
`map_supi_to_individual` deliberately never fabricates an `Individual.id`: no real Party-management
store/ID-allocator exists in this codebase, so inventing one would misrepresent this as more
complete than it is. Deliberately has zero dependency on `sbi_core` -- a TM Forum Open API resource
is a conceptually separate ecosystem from the 3GPP SBI stack `sbi_core` serves, and CLAUDE.md's own
"BSS layer could be swapped for a commercial stack" goal argues for that independence structurally,
not just by convention.

**Wired into CHF, not left dormant.** `nfs/chf/src/main.cpp`'s `Nchf_ConvergedCharging_Create`
handler now builds the `bss_sid::Individual` from `subscriberIdentifier` and logs it -- proving
CHF's internal charging record is genuinely SID-shaped, which is as much of the mapping as has a
real, unambiguous field to build it from today. Not yet exposed via any real TMF632 REST surface or
persisted to a Party store -- neither exists in this codebase, and building either now would be
speculative given nothing consumes it yet.

**Live-verified, not just unit-tested.** 5 new `gtest` cases (`tests/conformance/test_bss_sid.cpp`:
SUPI mapping, `id` left unset, JSON round-trip, `id` correctly omitted when unset rather than
emitted as `null`, `IndividualIdentification` round-trip) -- all pass, 122/122 total, zero
regressions. Beyond unit tests: a real `nr-gnb`/`nr-ue` PDU Session Establishment was driven against
the full live stack, and `chf`'s own log shows the real, correctly-shaped output: `mapped
subscriberIdentifier to TM Forum SID Individual:
{"individualIdentification":[{"identificationId":"imsi-999700000000001","identificationType":"SUPI"}]}`
-- built from a real SUPI extracted from a real registration, not a synthetic test value, and with
`id` correctly absent from the JSON (matching the unit test's own assertion).

**Consequence:** `docs/CHARGING_MAPPING.md`'s prerequisite is satisfied, reviewed, and its two
resolvable TODOs closed. The first real slice of SID/BSS mapping code exists, is tested, and is
live-verified. Every other row in the mapping table remains correctly deferred (no real 3GPP data
populated for them yet) -- next turns, each needing their own approval: a real rating engine (which
would make `AppliedCustomerBillingRate`/`multipleUnitInformation` mappable), Update/Release wired
to session teardown, and eventually a real TMF632 (or other TMF) REST surface if/when this system
needs one.

## ADR-0046: Nchf_ConvergedCharging_Release wired to ReleaseSMContext

**Date:** 2026-08-10
**Status:** Accepted.

**Context:** ADR-0044 deferred Update/Release to separate turns. Of the two, only Release has a
real, already-existing trigger point today: `ReleaseSMContext` already calls PCF's `DeleteSMPolicy`
on session teardown (ADR-0029); `UpdateSMContext` does not call PCF at all yet, so there is no real
trigger for `Nchf_ConvergedCharging_Update` to attach to without inventing one -- Update stays
deferred, Release does not.

**CHF side.** `ChargingDataRefAllocator` (a bare ID generator, ADR-0044) is upgraded to
`ChargingDataStore`, which now tracks which refs are currently active -- needed so Release can
correctly 404 an unknown or already-released `ChargingDataRef` (TS 32.291's real semantics) rather
than accepting anything. New route `POST /chargingdata/{ChargingDataRef}/release`: parses the
request body as `ChargingDataRequest` (real spec shape, `requestBody` required) for mandatory-field
validation parity with Create, checks the ref is active, returns 204 (real spec response) or a 404
`ProblemDetails`.

**SMF side.** `perform_n40_charging_data_create` now returns the allocated `ChargingDataRef`
(parsed from CHF's `location` header, same extraction pattern already used for PCF's `smPolicyId`)
instead of a bare bool. The ref -- and, newly, the session's `supi` (never previously stored in
`SmContextStore`, needed so Release can populate `subscriberIdentifier` without re-deriving it) --
are merged into the already-stored SM context via a read-modify-write (`SmContextStore::update`
replaces the whole entry, so a naive second `update` call would have clobbered the `smPolicyId`/
`policy` fields Create's PCF call already wrote). New `perform_n40_charging_data_release`, called
from `ReleaseSMContext`'s handler with the same best-effort/non-fatal discipline as the existing
`DeleteSMPolicy` call right above it -- local session release must not get stuck on CHF being
unreachable. Skipped (not sent with a fabricated ref) if either `chargingDataRef` or `supi` is
missing from the stored context, e.g. because Create's own N40 call never succeeded for that
session.

**Live-verified, both the success and the failure path.** Full stack + a real `nr-gnb`/`nr-ue` PDU
Session Establishment produced a real `ChargingDataRef` (`chg-1`, `smf`'s log:
`Nchf_ConvergedCharging_Create succeeded for pduSessionId 1, ChargingDataRef=chg-1`). No NF in this
codebase currently triggers `ReleaseSMContext` automatically (no UE-initiated deregistration flow
exists yet, a disclosed gap in the same category as several others already in this project) -- so
the release was triggered directly against SMF's real endpoint (`POST /nsmf-pdusession/v1/
sm-contexts/smctx-1/release`, real mTLS client cert, HTTP 204), same "provide the trigger a real
upstream flow doesn't exist for yet" pattern this project's own integration tests already use.
Result: `smf`'s log confirms `Nchf_ConvergedCharging_Release succeeded for ChargingDataRef=chg-1`,
and both NFs' own independent Prometheus counters agree:
`chf_charging_data_release_total{otel_scope_name="chf"} 1` and
`smf_chf_charging_data_release_total{otel_scope_name="smf"} 1`. The failure path was also verified
for real, not assumed: releasing `chg-1` a second time correctly returned a 404 `ProblemDetails`
(`"No active charging data resource chg-1"`) rather than silently succeeding or crashing.

**Consequence:** `Nchf_ConvergedCharging_Create` and `_Release` are both real and live-verified,
covering PDU session establishment through teardown's charging lifecycle. `Nchf_ConvergedCharging_
Update`, the `chargingNotification` callback, and everything else deferred in ADR-0044/ADR-0045
remain deferred, each needing a real trigger or real data to exist first.

## ADR-0047: `bss/product-catalog` -- a real, standalone TMF620 Product Catalog service

**Date:** 2026-08-10
**Status:** Accepted.

**Context:** User-requested review found a real gap: PROMPT.md's charging principles state "every
SID entity, NRM object and IOC element in scope must be CONFIGURABLE in the charging model --
product and tariff definition is data, not code", but no product/tariff data model existed
anywhere in this repo. CHF's `Nchf_ConvergedCharging_Create` has always returned an empty grant
(disclosed since ADR-0044, "no real rating/quota engine") -- there was nothing to rate *against*.

**Scope, approved before implementation.** Real `ProductOffering`/`ProductOfferingPrice`
Create/Get/List/Delete (real TMF620 PATCH-for-update and the `/listener/*` event-notification
callbacks deferred -- not needed to prove the data model is real and usable) against a real,
in-memory store. Explicitly NOT: wiring CHF to actually consult this catalog when rating a charging
event (a real rating engine -- separate, larger, not-yet-approved scope), and NOT a GUI --
`PROMPT.md`'s own Phase 7 design (confirmed by re-reading it directly, in response to the user
asking for "GUIs... in each domain similar to CHF") is **one generic JSON-schema-driven console
built later**, explicitly "so new NFs need no UI code" -- not bespoke per-NF/per-domain GUIs, and
not scheduled until after Phase 5/6. The user, after this was flagged, confirmed sticking to that
documented plan rather than pulling GUI work forward or applying TM Forum SID to non-charging NFs
(AMF/SMF/PCF/AUSF's own operational configuration, if it needs a real data model, is a real 3GPP
standard -- TS 28.541 NRM/IOC -- not TM Forum SID, which is a business/billing concept with no
equivalent for those NFs; the two are named separately in PROMPT.md's own DATA MODEL line, not
interchangeable).

**Why a standalone service, not code inside `nfs/chf`.** CLAUDE.md's own stated goal -- "align to
TM Forum ODA component boundaries so the BSS layer could be swapped for a commercial stack" --
argues structurally for a real, separate component here, the same reasoning `libs/bss-sid`'s own
file header already gives for staying independent of `sbi_core`. New top-level `bss/` directory
(distinct from `nfs/`, since this is not a 3GPP Network Function -- no NRF registration, no
`Nnrf_NFManagement`, nothing in TS 23.501's NF taxonomy describes it) holds standalone,
independently-deployable BSS/ODA-layer services; `libs/bss-sid/` (existing) holds the shared DTOs/
mapping code a service like this one links against.

**Real fields, directly verified -- not trusted secondhand.** Rather than relying on the earlier
research fork's summarized field lists, this turn downloaded TM Forum's actual TMF620 v4.1.0
swagger JSON (`github.com/tmforum-apis/TMF620_ProductCatalog`) and parsed it directly with Python
before writing any DTO -- confirming exact real field names/types for `ProductOffering`,
`ProductOfferingPrice`, `ProductOfferingPriceRef`, and TM Forum's common `Money`
(`unit`/`value`)/`TimePeriod` (`startDateTime`/`endDateTime`)/`Quantity` (`amount`/`units`) types,
and confirming the real schema marks **no field required at all** on either resource -- every
`std::optional` in `libs/bss-sid/include/bss_sid/product.hpp` reflects that exactly, not a
simplification. Real paths/methods also confirmed directly: `GET`/`POST /productOffering`,
`GET`/`PATCH`/`DELETE /productOffering/{id}` (same shape for `/productOfferingPrice`), real
`basePath: /tmf-api/productCatalogManagement/v4/`.

**Security: mTLS only, no OAuth2.** Reuses this lab's existing CA (`scripts/gen-lab-pki.sh`) for
real TLS 1.3 + mTLS -- same infrastructure every 3GPP NF uses, genuine transport security, not
"none". Does NOT layer OAuth2 bearer-token verification on top the way every 3GPP NF does: there is
no NRF-issued token source for a non-3GPP ODA component, and building a parallel BSS-side OAuth2/
OIDC stack is out of scope for "data model + API first" -- disclosed as a narrower security
boundary than the 3GPP NFs have, not silently omitted.

**Live-verified, real HTTP interop, not just unit tests.** 5 new unit tests
(`tests/conformance/test_bss_sid_product.cpp`) for the DTOs (Money/TimePeriod JSON shape,
`ProductOfferingPrice` round-trip, `ProductOffering` referencing prices by id, empty-array
omission) -- 127/127 total, zero regressions. Beyond that: the live service was driven with real
mTLS `curl` calls -- created a real `ProductOfferingPrice` (5GB/month, $20, real `id`/`href`
assigned by the server), created a `ProductOffering` referencing it, listed it, retrieved it by id,
deleted it (204), confirmed a second delete correctly 404s, and confirmed a request **with no
client certificate at all fails outright** (curl reports connection failure, not just a 401) --
proving mTLS is genuinely enforced, not just configured. Both new Prometheus counters
(`product_catalog_offering_create_total`, `product_catalog_offering_price_create_total`) confirmed
via direct scrape after the real calls.

**Consequence:** Product/tariff definitions are now genuinely configurable data (a real store
behind a real API), closing the gap PROMPT.md's charging principles named. CHF still does not
consult this catalog -- that wiring is a real rating engine, a distinct, larger, not-yet-approved
scope. No GUI exists or is planned before Phase 7, per the user's explicit confirmation.

## ADR-0048: Real rating engine -- CHF consults bss/product-catalog to grant real units

**Date:** 2026-08-10
**Status:** Accepted.

**Context:** ADR-0047 built a real, configurable product/tariff catalog but left it unconsulted --
CHF's `Nchf_ConvergedCharging_Create` still always returned an empty grant. This turn closes that
gap: CHF becomes a real HTTP client of `bss/product-catalog` and returns a real `GrantedUnit`
derived from real catalog data, approved before implementation given the several genuine design
decisions involved (rating-group assignment, subscriber-to-product selection, unit conversion).

**Real schema confirmed first.** Checked `TS32291_Nchf_ConvergedCharging.yaml` directly:
`MultipleUnitUsage.ratingGroup` is the one mandatory field (confirmed via the schema's own
`required:` block) -- neither SMF nor CHF populated or read it before this turn.

**SMF now sends a real `multipleUnitUsage`.** `nfs/smf/src/main.cpp`'s
`perform_n40_charging_data_create` adds one `MultipleUnitUsage` entry with a fixed
`kDefaultRatingGroup = 1` -- no real service-to-rating-group mapping exists in this codebase (that
is TS 32.298/32.299 charging-characteristics configuration, out of scope here), so every PDU
session is charged under one fixed group, disclosed rather than invented as something smarter.
`requestedUnit` is deliberately omitted: this build has no real traffic-volume estimator to request
a specific amount against, so CHF grants a full quota from its own rate-plan lookup instead of
SMF asking for one -- a real, disclosed design choice, not an oversight.

**CHF's real rating engine (`build_rating_grant`, `nfs/chf/src/main.cpp`).** Queries
`bss/product-catalog` (mTLS only, no OAuth2 -- product-catalog has no NRF-issued token source,
same trust boundary ADR-0047 already established) for the first `Active`+`isSellable`
`ProductOffering` with a price, fetches its first referenced `ProductOfferingPrice`, and converts
`unitOfMeasure` into a real `GrantedUnit`. No real per-subscriber product assignment exists (no
customer/subscription store) -- "whichever offering is first in the catalog" is the real, disclosed
simplification, same category as PCF's fixed-default policy (ADR-0028). Unit conversion is real
but deliberately narrow: TS 32.291's `GrantedUnit` has no generic "amount + unit" field the way
TMF620's `Quantity` does (`totalVolume`/`uplinkVolume`/`downlinkVolume` are raw octet counts,
`time` is raw seconds -- confirmed via their `Uint64`/`Uint32` typing with no accompanying unit
field) -- only `"GB"`/`"MB"` (decimal, matching 3GPP's own octet-counting convention, not binary
GiB/MiB) convert to `totalVolume`; any other unit string falls back to `serviceSpecificUnits`
carrying the raw amount unconverted. A narrow, real, disclosed conversion -- not a general
unit-aware rating engine. If the catalog is unreachable or has no matching offering, Create still
succeeds with an empty grant (the same fallback this build has always had), not a hard failure --
matching every other best-effort external-dependency call in this project.

**Live-verified with real seeded data, not a synthetic value.** Full stack (nrf, udm, udr, ausf,
pcf, chf, upf, smf, amf, product-catalog) started; a real `ProductOfferingPrice` ("10GB Monthly
Data", $25/month, `unitOfMeasure={amount:10, units:"GB"}`) and a real `ProductOffering` ("5G
Standard Plan") referencing it were seeded via real mTLS `curl` calls to the live catalog service.
A real `nr-gnb`/`nr-ue` PDU Session Establishment then triggered the real chain: SMF's N4 Session
Establishment, SMF's `Nchf_ConvergedCharging_Create` (now carrying `multipleUnitUsage`), CHF's real
catalog lookup, and a real grant. `chf`'s own log: `rating engine granted 10000000000 octets from
ProductOffering '5G Standard Plan' / ProductOfferingPrice '10GB Monthly Data'` -- the exact correct
conversion (10 GB × 10^9 = 10,000,000,000 octets). Verified at the wire level too, not just via
logs: a direct `curl` call to CHF's real `Nchf_ConvergedCharging_Create` endpoint returned
`{"multipleUnitInformation":[{"grantedUnit":{"totalVolume":10000000000},"ratingGroup":1}]}` in the
actual HTTP response body. The new `chf_rating_grant_total` Prometheus counter confirmed
independently via direct scrape (`1`, matching the one real grant issued).

**Consequence:** CHF now makes a genuine, catalog-derived charging decision instead of always
returning an empty grant -- the last major disclosed gap from ADR-0044 ("no real rating/quota
engine") is closed for the online-charging-quota-grant case specifically. Still deferred: quota
*consumption* tracking and re-authorization (this grants once at session establishment and never
checks whether it was used up), real per-subscriber product assignment (needs a customer/
subscription store that doesn't exist), and `Nchf_ConvergedCharging_Update` (which would be the
real trigger for a mid-session re-authorization) -- each a distinct, larger, not-yet-approved scope.

## ADR-0049: Commercialization mandate -- exceed free5GC, carrier-grade testing

**Date:** 2026-08-10
**Status:** Accepted. Mandatory, user-directed, recorded plainly per explicit instruction ("no
compromise") -- this ADR states the requirement honestly, including what is and is not true today,
rather than either softening it or claiming premature compliance.

**Context:** The user's stated intent for this project is commercialization. Two concrete
requirements follow, added to `PROMPT.md`'s Section 2 (Reality check) in the same commit that adds
this ADR:

1. **Performance and reliability must exceed free5GC** (the real, existing open-source 5GC this
   project has cited as a scale comparator since the project's original bootstrap brief) -- not
   merely match it.
2. **CHF and every other component must be tested as carrier-grade products**, against real
   standards and frameworks, not just this project's own conformance tests against the 3GPP
   OpenAPI schemas.

**Honest current state, stated plainly, not softened.** As of this ADR, **zero benchmarking of any
kind has been performed** in this codebase -- no throughput measurement, no latency measurement, no
concurrent-session capacity testing, no comparison against free5GC or any other implementation.
Asserting a performance/reliability claim without that measurement would be exactly the kind of
unearned "carrier-grade" label `PROMPT.md`'s own Section 2 already warns against (a rule already in
force before this ADR, now reinforced, not contradicted, by it). Known, already-disclosed
architectural debt that stands between this codebase and a meaningful performance claim: the
synchronous HTTP/2 client (tracked since ADR-0009), no load-balancing/clustering/high-availability
across NF instances (every NF today is a single, un-replicated process), no benchmarking harness or
load-generation infrastructure (Phase 8's planned "synthetic traffic generator" is the intended
home for this, not started), no chaos/fault-injection testing, no soak testing.

**Carrier-grade testing framework: real candidates named, none yet selected.** Evaluated but not
adopted (a real "evaluate before picking" decision deferred to its own future turn, matching
ADR-0004's precedent, not guessed at here): the ETSI NFV-TST (pre-deployment testing) and NFV-REL
(resiliency requirements) specification series, and standard telecom high-availability conventions
(e.g. "five nines" uptime) as industry context, not yet a number this project has committed to.
Which framework(s) actually apply, and how they'd integrate with this project's existing
conformance-test-per-procedure discipline (`docs/TRACEABILITY.md`), is real, open scope.

**What this changes, and what it doesn't.** This is a mandatory goal the project now carries
forward -- future architecture decisions (e.g. whether to finally close the synchronous-HTTP-client
debt item, whether/when to build real HA/clustering, when to build the benchmarking harness) should
be made with this mandate in view. It does **not** retroactively make any existing component
carrier-grade or benchmarked-superior-to-free5GC -- no such claim is made anywhere in this codebase
as of this ADR. `PROMPT.md`'s own existing Section 2 reality check (multi-engineer, multi-year
program; "carrier-grade" is a destination reached through conformance and soak testing, not a label
applied at commit time) remains fully in force and is not contradicted by adding this mandate --
the mandate states the destination; Section 2's honesty about the distance to it stands.

**Consequence:** No code changes in this ADR -- this is a governance/goal-setting record, the same
category as ADR-0009 (raising the project's target from lab-grade to production-intent). Concrete
next steps, each needing its own future turn and approval: select a real carrier-grade test
framework, close the synchronous-HTTP-client debt item, build real benchmarking/load-generation
infrastructure, and only then produce a real, evidence-based comparison against free5GC -- not
before.

## ADR-0050: Quota consumption tracking/re-authorization, Stage 0 -- SMF's real bidirectional PFCP peer

**Date:** 2026-08-10
**Status:** Accepted.

**Context:** ADR-0048 built a real rating engine that grants a quota once, at session
establishment, but never tracks consumption or re-authorizes -- a real, disclosed gap. Closing it
properly requires TS 29.244's real Usage Reporting Rule (URR) mechanism: UPF counts usage and sends
an **unsolicited** Sx Session Report Request when a threshold is crossed. This is a real, 7-stage
effort (comparable in size to the original PFCP/UPF work), staged and approved before
implementation, same discipline as every other multi-stage effort in this project. Researched
directly from the real, vendored `specs/PFCP/29244-e30.pdf` before any code: TS 29.244 §7.5.2.4
(Create URR IE, real type=6), §7.5.8.3 (Usage Report IE, real type=80), and Annex C.2.1.1's real
worked example ("Online charging with intermediate and final quotas"), which this project's Stage
1-6 plan follows directly rather than reconstructing the flow from first principles.

**The real architectural gap this stage closes.** SMF's PFCP code (ADR-0041/ADR-0042) used a fresh
`io_context`+socket *per call*, blocking on `receive_from()` for that one request's response only.
This works for CP-initiated request/response (Association Setup, Session Establishment) but cannot
receive an unsolicited message at all -- UPF has no stable address to send a Session Report Request
to, and even if it did, nothing would be listening for it outside an active outbound call.

**Real IEs added to `pfcp_core`** (`ie.hpp`/`header.hpp`/`session_ies.hpp`/`.cpp`), every byte
layout confirmed against the real spec PDF, not assumed: `SessionReportRequest`/`Response` message
types (56/57, confirmed via Table 7.3-1), `CreateUrr`/`UsageReport` grouped IE types (6/80), and the
child IEs Stage 1-3 will need: `UrrId` (81), `UrSeqn` (104), `MeasurementMethod` (62, VOLUM bit),
`ReportingTriggers` (37, VOLTH/VOLQU bits), `VolumeThreshold`/`VolumeQuota`/`VolumeMeasurement` (31/
73/66 -- confirmed independently to share one byte layout, one shared codec function), `ReportType`
(39, USAR bit), `UsageReportTrigger` (63, VOLTH/VOLQU bits, a different bit assignment than
Reporting Triggers despite the similar name -- confirmed independently, not assumed identical).
Only the fields Annex C.2.1.1's real call flow needs are modeled (volume-based Total Volume
threshold/quota) -- not time-based/event-based measurement or the many other optional Create
URR/Usage Report fields this project has no real use for yet. 12 new unit tests
(`tests/conformance/test_pfcp_core.cpp`), byte-exact against the real spec figures.

**New `nfs/smf/src/pfcp_peer.hpp`/`.cpp`: `PfcpPeer`.** One persistent socket, bound to a new,
disclosed-as-lab-only port (`pfcp_core::kSmfCpFunctionPfcpPort` = 8806 -- real PFCP has no spec-
assigned CP-function port the way UPF's 8805 is IANA-assigned; real deployments convey this
out-of-band, same as every other hardcoded-per-NF-port convention already in this lab), replacing
every per-call ephemeral socket. One dedicated receive thread dispatches every incoming datagram by
message type: a `*Response` matching an outstanding request's sequence number is handed to that
caller via a `condition_variable` (keyed by sequence number, now genuinely unique per logical
request via `allocate_sequence_number()` -- the old code could safely hardcode `sequence_number=1`
everywhere since each call had its own private socket; a shared socket needs real uniqueness); an
unsolicited Session Report Request is handed to a caller-installed handler. The handler is a
post-construction setter (`set_session_report_handler`), not a constructor parameter -- it needs to
capture the `PfcpPeer` itself by reference (to send the ack), which would otherwise be a reference
to a not-yet-constructed object; the setter sidesteps that cleanly. This turn's handler is
architecture-proof only: acknowledges with a schema-valid `Cause=RequestAccepted`, doesn't yet parse
real Usage Report content or call `Nchf_ConvergedCharging_Update` (Stage 3), and echoes the
request's own SEID in the response header rather than looking up the session's real UP-side SEID
(no per-session PFCP state store exists yet) -- disclosed simplifications for later stages to close,
not oversights.

**Live-verified, both the regression and the new capability.** Full stack + a real `nr-gnb`/`nr-ue`
PDU Session Establishment confirmed zero regression: Association Setup and N4 Session Establishment
both still succeed, first attempt, on the new shared-socket architecture (`smf`'s log unchanged in
substance from every prior stage's own verification). Then, independently, a hand-crafted-but-real
Sx Session Report Request (TS 29.244-correct header, Report Type IE with the USAR bit) was sent
directly to SMF's new port 8806 -- `smf`'s own log confirms the real handler ran (`received real Sx
Session Report Request from 127.0.0.1 (seq=12345)`), and the sending script received a real,
correctly-typed Sx Session Report Response (message type 57) back, proving the receive-dispatch and
fire-and-forget-response code paths both work for real, not just in theory.

**Consequence:** The real architectural blocker to quota-consumption tracking is closed. Next:
Stage 1 (SMF sends a real Create URR, derived from CHF's ADR-0048 grant, as part of N4 Session
Establishment), then Stages 2-6 per the approved plan.

### Stage 1 (2026-08-10): SMF provisions a real Create URR from CHF's real grant

**Real schema checked first, not assumed.** `Create PDR`'s own real field table (TS 29.244
§7.5.2.2) confirms it has an optional `URR ID` field ("present if a measurement action shall be
applied to packets matching this PDR") -- the real mechanism a PDR gets associated with a URR for
measurement, same `UrrId` IE type Stage 0 already added.

**Reordered CreateSMContext's handler**: `Nchf_ConvergedCharging_Create` now runs *before* N4
Session Establishment (previously after) -- TS 29.244 Annex C.2.1.1's real call flow requests
credit first, then provisions the UP function with the resulting quota (its steps 1 then 2); the
old order couldn't have included a real grant-derived URR in the same Session Establishment
Request. `perform_n40_charging_data_create` now returns a `ChargingDataCreateResult` (charging
data ref + the real parsed `GrantedUnit.totalVolume`, when CHF's response includes one) instead of
just the ref.

**`perform_n4_session_establishment` provisions a real URR** when a grant is present: `URR ID`
referenced from the uplink PDR, `Measurement Method` (VOLUM), `Reporting Triggers` (VOLTH+VOLQU),
`Volume Threshold` = 90% of the grant, `Volume Quota` = the full grant -- the exact 90/100 ratio
Annex C.2.1.1's own worked example uses, not an arbitrary choice.

**Live-verified with the real seeded catalog data from ADR-0048's own test plan.** A real
`nr-gnb`/`nr-ue` PDU Session Establishment against a real 10GB/$25 seeded plan produced: `smf`'s
log -- `Nchf_ConvergedCharging_Create succeeded ... granted total volume=10000000000 octets` then
`provisioning URR 1 for pduSessionId 1: threshold=9000000000 octets, quota=10000000000 octets`
(exactly 90%/100% of the real grant) then `N4 Session Establishment succeeded`. UPF -- an
independently-built process with no knowledge of Stage 1's changes beyond parsing whatever IEs
arrive -- accepted the Session Establishment Request containing the new Create URR IE without
rejecting it (`upf`'s own log: `Sx Session established from 127.0.0.1`), real proof the encoding is
wire-correct, not just internally self-consistent. (UPF's eBPF datapath itself did not start this
particular run -- an unrelated, already-disclosed capability-grant issue from a rebuild earlier in
this session wiping `setcap`, not a Stage 1 regression; the PFCP control-plane flow under test here
does not depend on the datapath being up.) 137/137 tests pass, zero regressions.

**Consequence:** UPF now receives everything it needs to measure and report usage -- it just
doesn't act on the URR yet (Stage 2's job: real per-TEID byte counting and the real unsolicited
Session Report Request when the threshold is crossed).

### Stage 2 (2026-08-10): UPF real per-TEID byte counting + real unsolicited Session Report Request

**BPF-side: real in-kernel counting, not a userspace estimate.** `gtpu_decap.bpf.c` gained a
`urr_map` (TEID -> `struct urr_state`: threshold/quota/running total/two one-shot report latches)
and a `usage_report_ringbuf`. On every matched G-PDU, `__sync_fetch_and_add` atomically adds the
real T-PDU length to the running total (a real atomic, not merely correct-on-one-CPU today --
forward-compatible with multi-queue/multi-CPU XDP where it would matter); crossing Volume
Quota/Volume Threshold pushes a `usage_report_event` (checked quota before threshold, since a
single large burst could cross both in one packet) and the latch stops it from repeating on every
subsequent packet -- TS 29.244 Annex C.2.1.1's own real behaviour is for UP to keep forwarding
after a Volume Threshold report, so the counter keeps climbing past the point that already fired.
**Real, disclosed gap, not silently different:** Annex C.2.1.1 has UP function stop forwarding once
Volume Quota is reached (until a new quota arrives); this stage does NOT implement that stop --
doing so before Stage 5 exists to ever provision a fresh quota would strand every session
permanently the first time this is tested. Revisit once Stage 5 lands.

**`datapath.hpp`/`.cpp`: a second BPF map wired into the *same* ring-buffer poll loop.**
`Datapath::create()` now takes a `UsageReportHandler` (invoked on the datapath's own polling
thread), registers `usage_report_ringbuf` via `ring_buffer__add()` onto the existing `ring_buffer`
manager (one polling thread services both maps -- no second thread), and a new
`Datapath::register_urr(teid, threshold, quota)` writes the real per-TEID state UPF's control
plane parses out of a Create URR. One real implementation snag: the ring-buffer callback needs the
complete `Datapath::Impl` type (to reach the handler stashed on it), but `Impl` is a private nested
type -- a free function can't name it from outside. Fixed by making the callback a `static` member
function of `Impl` itself rather than adding a friend declaration.

**`nfs/upf/src/main.cpp`: parses the real Create URR, remembers what a report needs, sends it.**
`build_session_establishment_response_ies` now also decodes `CreateUrr`'s child `UrrId`/
`VolumeThreshold`/`VolumeQuota` (URR ID read off the wire, not assumed to always be SMF's `1`) and
calls `register_urr` alongside the existing `register_teid`. A new `TeidSessionStore` (mutex-
guarded -- written by `run_pfcp_lifecycle`'s main thread on Session Establishment, read and its
per-URR UR-SEQN counter advanced by the datapath's polling thread when a report fires) remembers,
per TEID, exactly what a Session Report Request needs to be addressed and correlated: SMF's real
sender endpoint, the session's CP F-SEID, and the URR ID. The SMF endpoint needs no separate
discovery -- Stage 0's `PfcpPeer` sends every request (including the Session Establishment Request
that reaches this code) from the same persistent socket it also listens on, so `sender` on receipt
already **is** SMF's real, addressable peer. A new `ReportSender` (its own dedicated UDP socket,
mutex-protected `send`) lets the datapath's thread fire the report without touching
`run_pfcp_lifecycle`'s own receive socket concurrently. Two new `pfcp_core` encoders were needed
(UPF is now the encoder, not just the decoder, of these two IEs -- the reverse direction from every
prior stage): `encode_report_type_usage_report()` and `encode_usage_report_trigger_volth()`/
`_volqu()`, added with round-trip unit tests. The header's Sequence Number field uses UPF's own new
node-level counter (`next_pfcp_sequence_number`), deliberately NOT the same value as UR-SEQN --
TS 29.244 gives Sequence Number and UR-SEQN different scopes (per-message node-level correlator vs.
per-URR-lifetime counter) and conflating them was considered and rejected while writing this.

**Live-verified end to end, all real.** Full stack + real `nr-gnb`/`nr-ue`, with a deliberately tiny
seeded grant (`bss/product-catalog` ProductOfferingPrice `unitOfMeasure={amount: 0.000001,
units: "GB"}` -> 1,000 real octets, so a handful of real packets could practically cross it) so this
run's crossing is reachable without sending gigabytes: `smf`'s log --
`granted total volume=1000 octets` -> `provisioning URR 1 ... threshold=900 octets, quota=1000
octets`; `upf`'s log -- `registered URR 1 for TEID 0x1: threshold=900 quota=1000 octets`. A 25-packet
real GTP-U burst (44 real T-PDU octets each, 1,100 cumulative) sent from the isolated peer namespace
(same mechanism ADR-0043's own live verification established) produced, in order: `upf-datapath:
delivered decapsulated T-PDU` x25 (decapsulation itself unaffected), then at real total=924 octets
`upf: sent Sx Session Report Request to 127.0.0.1 for TEID 0x1: total=924 octets, trigger=VOLTH`,
then at real total=1012 octets `... trigger=VOLQU` -- **exactly one of each**, confirming the
one-shot latches work under continued post-crossing traffic, not just once by luck. `smf`'s log
independently confirms real receipt of both, via Stage 0's already-proven handler: `received real
Sx Session Report Request from 127.0.0.1 (seq=1)` then `(seq=2)`. 140/140 tests pass (3 new for this
stage's new UPF-side encoders: `EncodeReportTypeUsageReportRoundTrips`,
`EncodeUsageReportTriggerVolthRoundTrips`, `EncodeUsageReportTriggerVolquRoundTrips`) -- zero
regressions, up from Stage 1's 137/137.

**Consequence:** UPF now genuinely measures real usage and reports it, unsolicited, to a real SMF
that receives it -- the exact real capability ADR-0048's rating engine was missing. SMF's handler
still only architecture-proof-acks (Stage 0's disclosed scope); it does not yet parse the real
Usage Report content or call `Nchf_ConvergedCharging_Update` -- that is Stage 3's job next.

### Stage 3 (2026-08-10): SMF parses the real Usage Report and calls Nchf_ConvergedCharging_Update

**Real spec path confirmed before writing any code.** The vendored
`specs/5G_APIs-REL-19/TS32291_Nchf_ConvergedCharging.yaml` has a real
`POST /chargingdata/{ChargingDataRef}/update` path (verbatim, sharing Create's own
`ChargingDataRequest`/`ChargingDataResponse` schemas), and `MultipleUnitUsage.usedUnitContainer`
(→`UsedUnitContainer.localSequenceNumber`/`totalVolume`) is the real, schema-correct place to
report consumed usage back -- confirmed by reading the YAML directly, not assumed from the Create
side's shape.

**SMF's handler now genuinely decodes the report it receives.** Stage 0's handler only
acknowledged unconditionally; it now decodes `ReportType` (confirms the USAR bit), finds the
`UsageReport` grouped IE, and decodes `UrrId`/`UrSeqn`/`UsageReportTrigger`/`VolumeMeasurement`
from inside it -- all real `pfcp_core` decoders Stage 0/2 already added for exactly this. The Sx
Session Report Response is still sent unconditionally afterward, regardless of whether the
resulting CHF call succeeds -- PFCP acknowledgment and the SBI/N40 call are different protocol
layers, and a CHF outage must not leave UPF's own report unacknowledged.

**New `CpSeidSessionStore`: resolving a report's SEID back to a session.** A Session Report
Request's header SEID is the same `cp_seid` `perform_n4_session_establishment` generated and sent
as the CP F-SEID at Session Establishment (UPF's Stage 2 code echoes it back verbatim, per TS
29.244's addressing rule this file already relies on elsewhere). `perform_n4_session_establishment`
now returns that `cp_seid` on success (was plain `bool`) so `CreateSMContext`'s handler can register
it, alongside the real SUPI and `ChargingDataRef`, in a new mutex-guarded `CpSeidSessionStore` --
written on the ioc thread, read on `PfcpPeer`'s own receive thread (same cross-thread-store
reasoning as `nfs/upf/src/main.cpp`'s `TeidSessionStore`, Stage 2). The store also tracks a real,
per-session `invocationSequenceNumber` counter for TS 32.291's "strictly increasing per invocation"
requirement (Create used `1`; Update calls get `2`, `3`, ...). **Disclosed, pre-existing gap NOT
fixed by this stage:** `perform_n40_charging_data_release` still hardcodes
`invocationSequenceNumber=2` rather than sharing this counter -- if both an Update and a Release
land on the same `ChargingDataRef`, the real strictly-increasing requirement could be violated.
Release predates this stage; fixing it was out of Stage 3's approved scope, flagged here rather
than silently left unnoticed.

**A dedicated CHF client for `PfcpPeer`'s own thread.** The Session Report handler runs on
`PfcpPeer`'s receive thread, not the HTTP/2 server's `ioc` thread that the route handlers'
`chf_client` is only safe to touch from (this file's own established one-client-per-thread
discipline, same reasoning Stage 0's own ADR text used for AMF's NGAP-thread AUSF client). A
second, dedicated `chf_report_client`/`chf_report_oauth` pair was added rather than sharing the
existing one.

**New `perform_n40_charging_data_update`.** Same shape as `_create`/`_release`: builds a real
`ChargingDataRequest` with one `MultipleUnitUsage` (the session's fixed `kDefaultRatingGroup`,
same simplification Create already carries -- no real per-service rating-group mapping exists in
this codebase) carrying one `UsedUnitContainer` (`localSequenceNumber` = the real UR-SEQN this
report carried, `totalVolume` = the real consumed octets), POSTs it to CHF, and logs the real
outcome. A non-200 (expected: CHF has no Update handler yet, Stage 4's job) is logged as an
explicitly-expected, disclosed state, not misreported as a bug in this stage's own request.

**Live-verified end to end.** Same small-seeded-grant setup as Stage 2 (1,000 real octets), same
25-packet real GTP-U burst. `smf`'s log: `received real Sx Session Report Request ... (seq=1)`
immediately followed by `CHF Nchf_ConvergedCharging_Update returned status 404 for
ChargingDataRef=chg-1 (expected until ADR-0050 Stage 4 implements CHF's Update endpoint)`, then the
same pair for `(seq=2)` -- both real Usage Reports (VOLTH and VOLQU) were genuinely decoded, both
resolved to the real session via `CpSeidSessionStore`, both produced a real HTTP/2 POST that
genuinely reached CHF (confirmed: CHF's own generic server returned a real, unregistered-route 404,
not a connection failure) with no crash, deadlock, or interference with the rest of SMF's request
handling. 140/140 tests pass, zero regressions.

**Consequence:** SMF now genuinely closes the loop from "UPF measured real usage" to "CHF was told
about it" -- the request is real, spec-correct, and reaches CHF; CHF just doesn't yet have anything
to say back. Stage 4 (CHF implements the real Update endpoint: applies the reported usage, issues a
follow-on grant) is next.

### Stage 4 (2026-08-10): CHF implements real Nchf_ConvergedCharging_Update

**Real route added, same discipline as Create/Release.** `POST /chargingdata/{ChargingDataRef}/
update` (confirmed verbatim in the vendored `TS32291_Nchf_ConvergedCharging.yaml`, shared
`ChargingDataRequest`/`ChargingDataResponse` schemas). Validates the ref is still active first --
same 404 convention Release already established, but via a new non-destructive
`ChargingDataStore::is_active()` (Release's own `release()` removes the ref as a side effect of
checking it, which Update must not do).

**Real content, not just a schema-valid echo.** Logs the actual reported usage
(`multipleUnitUsage[].usedUnitContainer[].totalVolume`/`localSequenceNumber`) SMF's Stage 3 call
now genuinely carries -- CHF's own evidence the full loop closed, not a placeholder. Re-
authorizes by calling the same `build_rating_grant` catalog-lookup rating engine Create already
uses, returning a fresh `GrantedUnit` in `multipleUnitInformation`, HTTP 200 (not 201 -- the
resource already exists, this updates it, per the YAML's own response code).

**Disclosed, real simplifications, stated plainly rather than presented as a full OCS:** no
balance/wallet deduction against what was already consumed -- this build has no such store
(`docs/CHARGING_MAPPING.md`'s own noted TMF654 Prepay Balance gap); the fresh grant is always the
catalog's full price-plan amount again, not a remaining-balance-aware amount. No differentiation
between a Volume-Threshold report and a Volume-Quota-exhaustion report -- both re-authorize
identically, and this isn't fixable on CHF's side alone: SMF's own Stage 3 code doesn't forward
that distinction as a real `Trigger` in the request body either (a real, disclosed gap on the SMF
side, out of this stage's scope to fix).

**Live-verified end to end, the full loop closed for real.** Same small-seeded-grant (1,000
octets) setup, same real `nr-gnb`/`nr-ue` session, same 25-packet GTP-U burst as Stages 2-3.
`chf`'s log: `Update for ChargingDataRef=chg-1 reports ratingGroup=1 used 924 octets
(localSequenceNumber=1)` immediately followed by a real fresh `rating engine granted 1000 octets`
-- then the same pair for `used 1012 octets (localSequenceNumber=2)`. `smf`'s log confirms real
success this time (not the Stage 3 404): `Nchf_ConvergedCharging_Update succeeded for
ChargingDataRef=chg-1, reported 924 octets used` then `... reported 1012 octets used`. 140/140
tests pass, zero regressions.

**Consequence:** the entire quota-consumption-tracking loop this 7-stage effort set out to build is
now real end to end: UPF measures real usage → reports it unsolicited to SMF → SMF decodes it and
calls CHF → CHF applies it and re-authorizes. What remains: Stage 5 (SMF pushes the new quota back
to UPF via a real Session Modification, so the datapath's own `urr_map` state reflects the
re-authorized quota rather than staying latched at the original one) and Stage 6 (a dedicated
end-to-end live-verification pass across all six stages together, plus a documentation summary).
