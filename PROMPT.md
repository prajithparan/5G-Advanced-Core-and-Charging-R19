# Claude Code Master Prompt — 5G-Advanced (R19) Core Ecosystem in C++


## SECTION 0 — Paste this first (bootstrap prompt)

```
You are the lead architect and implementer for an open-source 5G-Advanced
(3GPP Release 19) service-based core network ecosystem, written in modern C++.

Read this entire brief, then DO NOT write application code yet. Instead:
1. Ask me up to 8 clarifying questions where the brief is genuinely ambiguous.
2. Propose a repository layout and a phased delivery plan.
3. Create CLAUDE.md capturing the conventions below so future sessions inherit them.

=== PROJECT GOAL ===
Build a modular, standards-faithful 5G Core (5GC) implementation where every
Network Function's northbound API is GENERATED from the official 3GPP OpenAPI
YAML definitions (forge.3gpp.org/rep/all/5G_APIs, REL-19 branch), with a
TM Forum SID-aligned charging/BSS domain, a JSON-driven GUI, and AI/ML
pipelines wired into NWDAF. Target: a lab-grade, spec-traceable reference
implementation — not a production carrier core.

=== SOURCE OF TRUTH (STRICT) ===
- 3GPP OpenAPI YAML (REL-19) is the ONLY source for API shapes, paths, schemas,
  and enums. Never hand-write a DTO that the YAML can generate.
- If a YAML file is unavailable offline, STOP and ask me to supply it. Do NOT
  invent field names, TS numbers, or reference points. Fabrication is the single
  worst failure mode in this project.
- Every generated NF must carry a header comment citing its TS number and the
  exact YAML file + commit/branch it was generated from.
- Stage-2 behaviour references: TS 23.501 (architecture), TS 23.502 (procedures),
  TS 23.503 (policy), TS 23.288 (NWDAF), TS 33.501 (security), TS 32.240/32.290/
  32.291 (charging), TS 29.500/29.501 (SBI framework principles).

=== NON-NEGOTIABLE ENGINEERING RULES ===
- C++20 minimum (C++23 where the toolchain allows). No raw new/delete; RAII
  everywhere; std::expected or tl::expected for recoverable errors; exceptions
  only at API boundaries.
- Every NF is an independent binary + a shared library, buildable standalone.
- No NF may include another NF's private headers. NFs talk ONLY over SBI.
- 100% of SBI traffic is HTTP/2 with JSON payloads, per TS 29.500: correct
  ProblemDetails on errors, 3gpp-Sbi-* headers, OAuth2 tokens from NRF.
- Test-first for protocol logic: every procedure gets a test derived from the
  TS 23.502 call flow it implements.
- All third-party dependencies must be OSI-approved open source. No proprietary
  SDKs, no vendor lock-in, no closed binaries.

=== MANDATED TECH STACK (all open source) ===
Build/tooling:  CMake 3.28+, Ninja, vcpkg (manifest mode), clang-format,
                clang-tidy, sanitizers (ASan/UBSan/TSan) in CI
Codegen:        openapi-generator (cpp-restsdk / cpp-pistache server targets) OR
                a custom Jinja-based generator if the official templates prove
                too lossy — evaluate BOTH in Phase 1 and recommend one with
                evidence, do not guess
HTTP/2 + SBI:   nghttp2 (core), Boost.Beast or Pistache for the service layer,
                libcurl (client), OpenSSL 3.x (TLS 1.3, mTLS)
JSON:           nlohmann/json (ergonomics, GUI/config) + simdjson (hot parse
                paths, telemetry ingest). Benchmark before choosing per path.
Async/runtime:  Boost.Asio (or libuv), a lock-free MPMC queue, thread-per-core
                where it measurably helps
PFCP/N4 + UP:   libpfcp-style codec (implement if none suitable), DPDK or VPP
                or eBPF/XDP for the UPF datapath — evaluate and justify
Storage:        Redis/Valkey (UDSF, session cache), PostgreSQL (UDR),
                ClickHouse (CDR/analytics), Kafka or Redpanda (event bus)
Observability:  OpenTelemetry C++, Prometheus exporter, spdlog, Grafana
Testing:        GoogleTest, Catch2, gMock, libFuzzer for codec fuzzing
GUI:            JSON-schema-driven. Backend exposes REST+WebSocket JSON; the
                front end renders dynamically from JSON Schema (React + JSON
                Forms, or Dear ImGui + nlohmann/json for a native operator
                console). Propose both, recommend one.
AI/ML:          ONNX Runtime (C++ inference in-process), optionally NVIDIA
                Triton for served models, MLflow for tracking, Kubeflow or
                Flyte for pipelines, Kafka for the feature stream
Deployment:     Docker + Compose for the lab, Helm charts for k8s, all
                reproducible from a single `make lab-up`

=== SCOPE: NETWORK FUNCTIONS (Release 19) ===
Tier 1 (must, Phase 2-3):
  NRF, AMF, SMF, UPF, PCF, UDM, UDR, AUSF, NSSF, NEF, SCP, BSF, CHF
Tier 2 (Phase 4):
  NWDAF (AnLF + MTLF), DCCF, ADRF, MFAF, NSACF, TSCTSF, EASDF, UCMF, SMSF,
  5G-EIR, LMF, GMLC, NSSAAF, AAnF, UDSF, SEPP (vSEPP/hSEPP + N32)
Tier 3 (Phase 5, R17-R19 features):
  MB-SMF / MB-UPF / MBSF / MBSTF (5MBS), AIOTF (Ambient IoT, TS 23.369),
  PKMF / PAnF / SLPKMF (ProSe + SL positioning), IMS AS + MF (Data Channel),
  MSGin5G, MNPF, PIN server, SEAL/SEALDD, ADAES, CAPIF, EIF (N110-N114)

Reference-point coverage target: every reference point defined in TS 23.501
clause 4.2.7 that is in scope for the implemented NFs — N1 through N115.
The numbering is SPARSE by design: N25, N39, N53-54, N64, N69, N72-79 and N98
were never assigned; N44-49 and N100-109 are reserved to TS 32.240; N90-95 to
TS 23.503. Do not invent numbers to fill gaps.

=== CHARGING DOMAIN: 3GPP + TM FORUM SID ===
Implement CHF per TS 32.290/32.291 (Nchf_ConvergedCharging,
Nchf_SpendingLimitControl, Nchf_OfflineOnlyCharging) with N40 (SMF), N28 (PCF),
N41/N42 (AMF, home/visited).

Then add a SID-aligned BSS abstraction layer that maps 3GPP charging events onto
TM Forum Information Framework (SID) entities — Product, Service, Resource,
Customer, Party, Agreement, ProductOffering, ProductPrice, AppliedCustomerBilling
Rate, CustomerBill, BalanceTopUp — and expose them via TM Forum Open APIs:
  TMF620 Product Catalog, TMF622 Product Ordering, TMF632 Party Management,
  TMF635 Usage Management, TMF637 Product Inventory, TMF666 Account Management,
  TMF676 Payment Management, TMF678 Customer Bill Management,
  TMF635/TMF727 usage + rating hooks
Deliver an explicit, reviewable mapping table: 3GPP CDR field -> SID entity ->
TMF Open API resource. Where a mapping is genuinely ambiguous, mark it TODO and
ask me — do NOT silently invent a mapping. Align to TM Forum ODA component
boundaries so the BSS layer could be swapped for a commercial stack.

=== AI PIPELINES (NWDAF-centric) ===
- NWDAF split into AnLF (analytics logic) and MTLF (model training logic) per
  TS 23.288, with Nnwdaf_EventsSubscription, AnalyticsInfo, DataManagement,
  MLModelProvision, MLModelTraining, MLModelMonitor.
- Data plane: NFs emit events -> Kafka -> feature store -> training (Python
  sidecar is acceptable for training ONLY) -> ONNX artifact -> C++ inference
  in AnLF via ONNX Runtime. Inference must be in-process C++, not a Python call.
- Ship at least three working analytics: (1) NF load prediction,
  (2) abnormal-behaviour/anomaly detection, (3) slice SLA / service-experience
  prediction. R19 additions to feature: energy-efficiency analytics and a
  vertical-federated-learning (VFL) hook.
- Model governance: every model versioned in MLflow with training data lineage,
  and an explicit drift-monitoring path via Nnwdaf_MLModelMonitor.
- Optional agentic layer: an MCP server exposing read-only NF state and analytics
  as tools, so an LLM assistant can query the core. Read-only by default; any
  write/config action requires explicit human approval in the loop.

=== DEFINITION OF DONE (per NF) ===
1. API generated from the R19 YAML, with the source file + branch cited
2. NRF registration/discovery/heartbeat working, OAuth2 token validated
3. All mandatory procedures from TS 23.502 for that NF implemented + tested
4. ProblemDetails error handling per TS 29.500
5. OpenTelemetry spans + Prometheus metrics emitted
6. Conformance test against the generated OpenAPI schema (request AND response)
7. Docker image + Compose entry + Helm chart
8. A spec-traceability doc: procedure -> TS clause -> source file -> test

=== YOUR WORKING STYLE FOR THIS PROJECT ===
- Work in small, reviewable increments. Never generate more than one NF per turn.
- Before writing any NF, show me the procedure list from TS 23.502 you intend to
  implement and let me approve it.
- When the spec is ambiguous or the YAML is missing, STOP AND ASK. A question
  costs me a minute; a fabricated field costs me a week of review.
- Keep a running docs/DECISIONS.md (ADR format) for every architectural choice,
  including the ones you rejected and why.
- Flag honestly when something is a stub, a simplification, or not conformant.
  I am a telecom architect and will spot it — tell me first.
```

---

## SECTION 1 — Phase prompts (run one per session)

### Phase 0 — Foundations
```
Set up the skeleton only: CMake + vcpkg manifest, clang-format/clang-tidy config,
CI (build + sanitizers + tests), docs/DECISIONS.md, docs/TRACEABILITY.md, and a
libs/sbi-core library exposing: HTTP/2 server + client, JSON (de)serialization,
ProblemDetails, 3gpp-Sbi-* header handling, OAuth2 client-credentials, structured
logging, OTel tracing. Include a "hello NF" that registers with a stub NRF.
No 5G business logic yet. Show me the dependency graph before you build.
```

### Phase 1 — Codegen spine
```
Ingest the 3GPP R19 OpenAPI YAMLs from ./specs/ (I will place them there).
Evaluate openapi-generator's C++ targets vs a custom Jinja generator on THREE
real files: TS29510_Nnrf_NFManagement, TS29518_Namf_Communication, and
TS29571_CommonData. Report concretely: what each loses (oneOf/anyOf, nullable,
allOf composition, discriminators, patterns, external $refs). Recommend one with
evidence. Then build `tools/sbi-codegen` producing headers + serializers +
server stubs + client SDKs, wired into CMake as a build step. Add a round-trip
schema-conformance test.
```

### Phase 2 — Control-plane core
```
Implement NRF first (it is the dependency root), then AMF, SMF, UDM, UDR, AUSF,
PCF — one per turn, in that order. For each: show me the TS 23.502 procedure list
for approval, then implement, then test. End state: UE registration
(TS 23.502 §4.2.2.2.2) and PDU session establishment (§4.3.2.2.1) working
end-to-end against a simulated gNB and UE.
```

### Phase 3 — User plane
```
Implement N4/PFCP (TS 29.244) and the UPF datapath. Evaluate eBPF/XDP vs VPP vs
DPDK against my lab constraint (single Ubuntu 24.04 workstation, no SR-IOV NIC
assumed) and recommend one. Support GTP-U encap/decap, N3/N6/N9, QER/FAR/PDR/URR
rules, and usage reporting to CHF. Include a packet-level test harness.
```

### Phase 4 — Charging + TM Forum SID
```
Implement CHF (TS 32.290/32.291) with N40/N28/N41/N42, CDR generation to
ClickHouse, and the SID-aligned BSS layer with the TMF Open APIs listed in the
brief. Deliver docs/CHARGING_MAPPING.md — the 3GPP field -> SID entity -> TMF API
table — for my review BEFORE implementing the mapping code. Flag every ambiguous
mapping rather than guessing.
```

### Phase 5 — NWDAF + AI pipelines
```
Implement NWDAF AnLF + MTLF per TS 23.288, the Kafka event pipeline, the
training sidecar, ONNX Runtime in-process inference, and the three analytics.
Add MLflow tracking and the Nnwdaf_MLModelMonitor drift path. Then the R19
energy-efficiency analytics and the VFL hook.
```

### Phase 6 — R19 feature NFs
```
Implement the Tier 3 NFs, one per turn, prioritising: AIOTF (Ambient IoT,
TS 23.369), the 5MBS quartet, EASDF, TSCTSF, NSACF, SEPP with N32-c/N32-f
(PRINS), and CAPIF. Same definition-of-done applies.
```

### Phase 7 — GUI + operations console
```
Build the JSON-schema-driven operator GUI: topology view mirroring my R19
architecture poster, live NF status from NRF, session/subscriber browsers,
analytics dashboards, and a charging/BSS view. Backend REST+WebSocket JSON;
front end renders from JSON Schema so new NFs appear without UI code changes.
```

### Phase 8 — Lab packaging
```
Single-command lab bring-up (`make lab-up`), Helm charts, a synthetic traffic
generator, a conformance report generator, and a getting-started guide targeting
a single Ubuntu 24.04 workstation.
```

---

## SECTION 2 — Reality check before you start

Be aware of the scale you are commissioning:

- A conformant multi-NF 5GC is a **multi-engineer, multi-quarter** program.
  Open5GS and free5GC each represent years of work — and neither covers R19.
- The realistic outcome of this prompt is a **high-quality, spec-traceable
  reference implementation** of a meaningful subset — excellent for a lab,
  demos, thought leadership, and interop experiments; not a carrier-grade core.
- **Strongly consider studying Open5GS (C) and free5GC (Go) first.** Either
  could be a starting point rather than a blank page. Ask Claude Code to produce
  a build-vs-fork analysis as its very first deliverable — that one decision
  will dominate the project's economics.
- **Ambient IoT (AIOTF) and other R19 items may have incomplete stage-3 YAML.**
  Where stage-3 is missing, implement against stage-2 and mark the gap
  explicitly rather than inventing an API.
- Your lab laptop (MX450, CUDA 12.6) is fine for control-plane development and
  ONNX inference; the UPF datapath and any serious model training will want the
  larger lab tier.

## SECTION 3 — Guardrails worth repeating to the model

Paste this whenever output starts drifting:

```
Reminder of the rules for this project:
1. Never invent a TS number, reference point, API path, or JSON field. If it is
   not in the YAML or the spec in front of you, ask me.
2. One NF (or one subsystem) per turn. Show the procedure list for approval
   before implementing.
3. Tell me explicitly what is a stub, what is simplified, and what is not
   conformant. Do not let me discover it in review.
4. Every architectural decision goes in docs/DECISIONS.md with the rejected
   alternatives.
5. If you are unsure whether something is correct, say so plainly rather than
   producing confident code.
```
