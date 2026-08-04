# CLAUDE.md — 5G-Advanced (R19) Core Ecosystem

This file is the standing brief for every session working in this repository.
It condenses `PROMPT.md`. If this file and `PROMPT.md` ever disagree, treat
that as a bug — flag it, don't silently pick one.

## Project goal

A modular, standards-faithful 5G Core (5GC) implementation in modern C++
where every Network Function's northbound API is **generated** from the
official 3GPP OpenAPI YAML (forge.3gpp.org/rep/all/5G_APIs, REL-19 branch),
with a TM Forum SID-aligned charging/BSS domain, a JSON-schema-driven GUI,
and AI/ML pipelines wired into NWDAF. Target: a production-grade,
spec-traceable reference implementation (raised from the original
lab-grade scope — see ADR-0009 in `docs/DECISIONS.md`).

## Source of truth (strict)

- 3GPP OpenAPI YAML (REL-19) is the ONLY source for API shapes, paths,
  schemas, and enums. Never hand-write a DTO the YAML can generate.
- If a YAML file is unavailable offline: **stop and ask**. Never invent
  field names, TS numbers, or reference points. Fabrication is the single
  worst failure mode on this project.
- Every generated NF carries a header comment citing its TS number and the
  exact YAML file + commit/branch it was generated from.
- Stage-2 behaviour references: TS 23.501 (architecture), TS 23.502
  (procedures), TS 23.503 (policy), TS 23.288 (NWDAF), TS 33.501 (security),
  TS 32.240/32.290/32.291 (charging), TS 29.500/29.501 (SBI framework).
- Where stage-3 YAML is genuinely missing for an R19 item (e.g. parts of
  AIOTF), implement against stage-2 and mark the gap explicitly — never
  invent an API to fill it.

## Non-negotiable engineering rules

- C++20 minimum, C++23 where the toolchain allows.
- No raw `new`/`delete`; RAII everywhere; `std::expected` / `tl::expected`
  for recoverable errors; exceptions only at API boundaries.
- Every NF is an independent binary + shared library, buildable standalone.
- No NF includes another NF's private headers. NFs talk ONLY over SBI.
- 100% of SBI traffic is HTTP/2 + JSON per TS 29.500: correct
  `ProblemDetails` on errors, `3gpp-Sbi-*` headers, OAuth2 tokens from NRF.
- Test-first for protocol logic: every procedure gets a test derived from
  the TS 23.502 call flow it implements.
- All third-party dependencies must be OSI-approved open source. No
  proprietary SDKs, no vendor lock-in, no closed binaries.

## Mandated tech stack

- **Build/tooling**: CMake 3.28+, Ninja, vcpkg (manifest mode),
  clang-format, clang-tidy, sanitizers (ASan/UBSan/TSan) in CI.
- **Codegen**: openapi-generator (cpp-restsdk / cpp-pistache targets) OR a
  custom Jinja generator — evaluated with evidence in Phase 1, not guessed.
- **HTTP/2 + SBI**: nghttp2 (core), Boost.Beast or Pistache (service
  layer), libcurl (client), OpenSSL 3.x (TLS 1.3, mTLS).
- **JSON**: nlohmann/json (ergonomics/GUI/config) + simdjson (hot parse
  paths/telemetry) — benchmark before choosing per path.
- **Async/runtime**: Boost.Asio (or libuv), lock-free MPMC queue,
  thread-per-core where it measurably helps.
- **PFCP/N4 + UP**: libpfcp-style codec (implement if none suitable);
  DPDK, VPP, or eBPF/XDP for the UPF datapath — evaluate and justify.
- **Storage**: Redis/Valkey (UDSF, session cache), PostgreSQL (UDR),
  ClickHouse (CDR/analytics), Kafka or Redpanda (event bus).
- **Observability**: OpenTelemetry C++, Prometheus exporter, spdlog,
  Grafana.
- **Testing**: GoogleTest, Catch2, gMock, libFuzzer for codec fuzzing.
- **GUI**: JSON-schema-driven. Backend exposes REST+WebSocket JSON; front
  end renders dynamically from JSON Schema (React + JSON Forms, or Dear
  ImGui + nlohmann/json for a native console) — both proposed, one
  recommended in Phase 7.
- **AI/ML**: ONNX Runtime (in-process C++ inference), optionally NVIDIA
  Triton, MLflow (tracking), Kubeflow or Flyte (pipelines), Kafka
  (feature stream). Training may use a Python sidecar; inference must be
  in-process C++, never a Python call at runtime.
- **Deployment**: Docker + Compose (lab), Helm charts (k8s), all
  reproducible from a single `make lab-up`.

## Scope: Network Functions (Release 19)

- **Tier 1** (must, Phase 2-3): NRF, AMF, SMF, UPF, PCF, UDM, UDR, AUSF,
  NSSF, NEF, SCP, BSF, CHF.
- **Tier 2** (Phase 4): NWDAF (AnLF + MTLF), DCCF, ADRF, MFAF, NSACF,
  TSCTSF, EASDF, UCMF, SMSF, 5G-EIR, LMF, GMLC, NSSAAF, AAnF, UDSF, SEPP
  (vSEPP/hSEPP + N32).
- **Tier 3** (Phase 5, R17-R19 features): MB-SMF/MB-UPF/MBSF/MBSTF
  (5MBS), AIOTF (Ambient IoT, TS 23.369), PKMF/PAnF/SLPKMF
  (ProSe + SL positioning), IMS AS + MF (Data Channel), MSGin5G, MNPF,
  PIN server, SEAL/SEALDD, ADAES, CAPIF, EIF (N110-N114).
- **Reference points**: every reference point in TS 23.501 §4.2.7 that is
  in scope for the implemented NFs, N1-N115. The numbering is sparse by
  design (N25, N39, N53-54, N64, N69, N72-79, N98 never assigned;
  N44-49/N100-109 reserved to TS 32.240; N90-95 to TS 23.503). Never
  invent numbers to fill gaps.

## Charging domain: 3GPP + TM Forum SID

- CHF per TS 32.290/32.291 (`Nchf_ConvergedCharging`,
  `Nchf_SpendingLimitControl`, `Nchf_OfflineOnlyCharging`) with N40 (SMF),
  N28 (PCF), N41/N42 (AMF, home/visited).
- SID-aligned BSS layer mapping 3GPP charging events onto TM Forum SID
  entities (Product, Service, Resource, Customer, Party, Agreement,
  ProductOffering, ProductPrice, AppliedCustomerBillingRate, CustomerBill,
  BalanceTopUp), exposed via TMF620/622/632/635/637/666/676/678/727.
- Deliverable before any mapping code: `docs/CHARGING_MAPPING.md` — an
  explicit, reviewable table of 3GPP CDR field -> SID entity -> TMF API
  resource. Ambiguous mappings are marked TODO and asked about, never
  silently invented. Align to TM Forum ODA component boundaries so the
  BSS layer could be swapped for a commercial stack.

## AI pipelines (NWDAF-centric)

- NWDAF split into AnLF (analytics logic) and MTLF (model training logic)
  per TS 23.288: `Nnwdaf_EventsSubscription`, `AnalyticsInfo`,
  `DataManagement`, `MLModelProvision`, `MLModelTraining`,
  `MLModelMonitor`.
- Data plane: NFs emit events -> Kafka -> feature store -> training
  (Python sidecar, training ONLY) -> ONNX artifact -> in-process C++
  inference in AnLF via ONNX Runtime.
- At least three working analytics: (1) NF load prediction, (2)
  abnormal-behaviour/anomaly detection, (3) slice SLA / service-experience
  prediction. R19 additions: energy-efficiency analytics, a
  vertical-federated-learning (VFL) hook.
- Every model versioned in MLflow with training-data lineage and an
  explicit drift-monitoring path via `Nnwdaf_MLModelMonitor`.
- Optional agentic layer: an MCP server exposing read-only NF state and
  analytics as tools. Read-only by default; any write/config action
  requires explicit human approval in the loop.

## Definition of done (per NF)

1. API generated from the R19 YAML, with source file + branch cited.
2. NRF registration/discovery/heartbeat working, OAuth2 token validated.
3. All mandatory TS 23.502 procedures for that NF implemented + tested.
4. `ProblemDetails` error handling per TS 29.500.
5. OpenTelemetry spans + Prometheus metrics emitted.
6. Conformance test against the generated OpenAPI schema (request AND
   response).
7. Docker image + Compose entry + Helm chart.
8. Spec-traceability doc entry: procedure -> TS clause -> source file ->
   test.

## Working style for this project

- Work in small, reviewable increments. Never generate more than one NF
  (or one subsystem) per turn.
- Before writing any NF, show the TS 23.502 procedure list to be
  implemented and get approval first.
- When the spec is ambiguous or YAML is missing: **stop and ask**. A
  question costs a minute; a fabricated field costs a week of review.
- Keep `docs/DECISIONS.md` (ADR format) current for every architectural
  choice, including rejected alternatives and why.
- Flag honestly what is a stub, a simplification, or non-conformant.
  The user is a telecom architect and will spot it — say so first, don't
  let it be discovered in review.

## Guardrails (repeat when output starts drifting)

1. Never invent a TS number, reference point, API path, or JSON field.
   If it's not in the YAML or spec in hand, ask.
2. One NF (or one subsystem) per turn. Show the procedure list for
   approval before implementing.
3. State explicitly what is a stub, what is simplified, and what is not
   conformant.
4. Every architectural decision goes in `docs/DECISIONS.md`, including
   rejected alternatives.
5. If unsure whether something is correct, say so plainly rather than
   producing confident code.

## Project decisions (resolved at kickoff)

- **Build strategy**: Greenfield. No fork of Open5GS or free5GC; they are
  reference reading only, not a starting codebase.
- **Spec source**: R19 OpenAPI YAML confirmed present, archive commit
  `bca84b60a37773133bcae97e5c6c0d10a93b47b6`, branch REL-19, release
  status Frozen, API version March 2026. 531 files. To be extracted into
  `specs/5G_APIs-REL-19/` with the commit hash recorded in
  `docs/DECISIONS.md` and `docs/TRACEABILITY.md`.
- **Phase 2 order**: full brief order — NRF -> AMF -> SMF -> UDM -> UDR ->
  AUSF -> PCF — ending with UE registration (TS 23.502 §4.2.2.2.2) and PDU
  session establishment (§4.3.2.2.1) end-to-end. No narrowed slice.
- **Hosting/license**: public GitHub repository, Apache-2.0 license
  (patent grant matters for a standards-adjacent project with likely
  corporate forks/contributors).
- **Dev environment**: bare-metal Ubuntu 24.04, MX450 GPU, CUDA 12.6.
  Not WSL2/VM. UPF datapath and serious model training will still want a
  larger lab tier when the time comes (see Reality check below).
- **CI**: GitHub-hosted free runners. Sanitizer (ASan/UBSan/TSan) and
  libFuzzer jobs must be designed to fit free-tier time/resource limits —
  keep them fast and targeted rather than exhaustive; revisit if runners
  become a bottleneck.
- **Cadence**: long-running, multi-session project worked in small
  increments — one NF or one subsystem per turn, matching the working
  style rules above. Not an accelerated single-shot demo.
- **AI/ML compute**: local MX450 is not the ceiling — larger training
  compute is expected later. Design the training-sidecar interface
  (Phase 5) to be swappable (e.g. pluggable backend/executor) rather than
  hardcoding for toy-scale local training only.

## Reality check

- A conformant multi-NF 5GC is a multi-engineer, multi-quarter program.
  Open5GS and free5GC each represent years of work, and neither covers
  R19. The target is production-grade (ADR-0009), which is a
  substantially larger undertaking than the project's original lab-grade
  framing — treat every phase's Definition of Done as the real bar, not
  an aspiration: full procedure coverage, real TLS/mTLS, no permanent
  stubs. A single solo session will not get there in one pass; this is
  still built incrementally, phase by phase, NF by NF — the destination
  changed, not the pace.
- Build-vs-fork (study/fork Open5GS (C) or free5GC (Go) vs. greenfield)
  is a first-order decision affecting project economics — resolved
  greenfield (ADR-0001).
- Dev machine (MX450, CUDA 12.6) is fine for control-plane dev and ONNX
  inference; UPF datapath and serious model training will want a larger
  lab tier.
- Known debt against the production-grade bar, tracked in
  `docs/DECISIONS.md` ADR-0009: Phase 0's h2c-only transport (no
  TLS/mTLS yet), stub-nrf's unsigned fake OAuth2 token, the synchronous
  HTTP/2 client, and ~40 outstanding `clang-tidy` style warnings. None
  of these are acceptable as a final state anymore.
