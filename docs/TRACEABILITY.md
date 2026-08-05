# Spec Traceability

One row per implemented procedure: which TS clause it comes from, which source file implements it,
and which test proves it. Per CLAUDE.md's Definition of Done, every NF procedure gets an entry here
before it's considered complete.

## NRF (Phase 2, first NF)

All rows below: source `specs/5G_APIs-REL-19/TS29510_Nnrf_NFManagement.yaml` /
`TS29510_Nnrf_NFDiscovery.yaml` / `TS29510_Nnrf_AccessToken.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`), implemented in `nfs/nrf/src/main.cpp` +
`nfs/nrf/src/registry.cpp`, proven by `tests/integration/test_hello_nf_registration.cpp`
(real subprocess-to-subprocess, real TLS 1.3 + mTLS, real signed JWT) plus manual `curl`
verification recorded in `docs/DECISIONS.md` ADR-0012 (tamper rejection) and this file's
"Manual verification" section below.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| RegisterNFInstance | `PUT /nnrf-nfm/v1/nf-instances/{nfInstanceID}` (NFManagement.yaml) | Integration test (register step) + manual curl (201, discovery finds it after) |
| GetNFInstance | `GET /nnrf-nfm/v1/nf-instances/{nfInstanceID}` (NFManagement.yaml) | Integration test (`wait_for_nrf` probe) |
| UpdateNFInstance (heartbeat) | `PATCH /nnrf-nfm/v1/nf-instances/{nfInstanceID}`, RFC 6902 JSON Patch (NFManagement.yaml) | Integration test (heartbeat step) -- patch is actually applied via `nlohmann::json::patch()`, not accepted-and-ignored like Phase 0's stub-nrf |
| DeregisterNFInstance | `DELETE /nnrf-nfm/v1/nf-instances/{nfInstanceID}` (NFManagement.yaml) | Integration test (deregister step) |
| GetNFInstances | `GET /nnrf-nfm/v1/nf-instances` (NFManagement.yaml) | Manual curl only -- no automated test yet, disclosed gap |
| CreateSubscription | `POST /nnrf-nfm/v1/subscriptions` (NFManagement.yaml) | Manual curl (201, notification delivery verified separately -- see below) |
| UpdateSubscription | `PATCH /nnrf-nfm/v1/subscriptions/{subscriptionID}` (NFManagement.yaml) | Not exercised by any test yet -- implemented, unverified beyond compiling. Disclosed gap. |
| RemoveSubscription | `DELETE /nnrf-nfm/v1/subscriptions/{subscriptionID}` (NFManagement.yaml) | Not exercised by any test yet -- implemented, unverified beyond compiling. Disclosed gap. |
| SearchNFInstances | `GET /nnrf-disc/v1/nf-instances?target-nf-type=...` (NFDiscovery.yaml) | Manual curl (registered an SMF, searched target-nf-type=SMF, got it back) |
| OAuth2 access token issuance | `POST /oauth2/token` (AccessToken.yaml), real ES256-signed JWT | Integration test (every step uses a real token) + manual curl tamper-rejection check (401, `ProblemDetails{"detail":"invalid signature"}`) + standalone round-trip test (see ADR-0012) |

**Manual verification (2026-08-05, not captured in an automated test -- recorded here so it's not
lost):**
```
$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7777/oauth2/token \
    -d "grant_type=client_credentials&nfInstanceId=test-nf-1&scope=nnrf-nfm&targetNfType=NRF"
-> 200, real ES256 JWT

$ curl ... -X PUT https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/test-nf-1 \
    -H "authorization: Bearer <token>" -d '{"nfInstanceId":"test-nf-1","nfType":"SMF",...}'
-> 201

$ curl ... "https://127.0.0.1:7777/nnrf-disc/v1/nf-instances?target-nf-type=SMF&requester-nf-type=AMF" \
    -H "authorization: Bearer <token>"
-> 200, {"nfInstances":[{...test-nf-1...}],"validityPeriod":60}

$ curl ... -H "authorization: Bearer <token>tampered" ...
-> 401, {"detail":"invalid signature","status":401,"title":"Unauthorized"}

Subscription notification delivery: verified at the libs/sbi-core::http2::Client level in
isolation (POST to a plain-HTTP test listener succeeded, body delivered, 204 received back) --
NOT re-verified through the full NRF register->notify chain in the same session due to an
inconclusive manual test (see docs/DECISIONS.md ADR-0015 note on this). Disclosed as needing a
proper automated test, not asserted as fully proven end-to-end.
```

## AMF (Phase 2, second NF)

All rows below: source `specs/5G_APIs-REL-19/TS29518_Namf_Communication.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`), implemented in `nfs/amf/src/main.cpp`, proven by
`tests/integration/test_amf_namf_communication.cpp` (real subprocess-to-subprocess: `nrf` + `amf`,
real TLS 1.3 + mTLS, real signed JWT) plus manual `curl` verification recorded below. Deferred
(multipart/related-only, not implemented): `CreateUEContext`, `RelocateUEContext`,
`CancelRelocateUEContext` -- see `docs/DECISIONS.md` ADR-0019.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| ReleaseUEContext | `POST /namf-comm/v1/ue-contexts/{ueContextId}/release` | Integration test (`MissingUeContextIs404...`, 404 branch) + manual curl. "Found" branch unverified -- see ADR-0019 |
| EBIAssignment | `POST /namf-comm/v1/ue-contexts/{ueContextId}/assign-ebi` | Manual curl only (404 branch). "Found" branch unverified -- see ADR-0019 |
| UEContextTransfer | `POST /namf-comm/v1/ue-contexts/{ueContextId}/transfer` | Not exercised by any test yet -- implemented, unverified beyond compiling (same "found"-branch gap as the other three per-ueContextId ops; the 404 branch specifically was not separately curled for this operation). Disclosed gap |
| RegistrationStatusUpdate | `POST /namf-comm/v1/ue-contexts/{ueContextId}/transfer-update` | Not exercised by any test yet -- implemented, unverified beyond compiling. Disclosed gap |
| N1N2MessageTransfer | `POST /namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages` | Manual curl (400 on malformed body verified during development; 404-on-no-context path shares the same code as the other per-context ops) |
| N1N2MessageSubscribe | `POST /namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages/subscriptions` | Integration test (`N1N2SubscribeAndUnsubscribe`, real 201 + body) |
| N1N2MessageUnSubscribe | `DELETE /namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages/subscriptions/{subscriptionId}` | Integration test (`N1N2SubscribeAndUnsubscribe`, 204 then 404 on double-remove) |
| NonUeN2MessageTransfer | `POST /namf-comm/v1/non-ue-n2-messages/transfer` | Integration test (`AmfStatusChangeSubscribeAndNonUeN2Transfer`, real 200 + body) |
| NonUeN2InfoSubscribe | `POST /namf-comm/v1/non-ue-n2-messages/subscriptions` | Manual curl only (201) -- no automated test yet, disclosed gap |
| NonUeN2InfoUnSubscribe | `DELETE /namf-comm/v1/non-ue-n2-messages/subscriptions/{n2NotifySubscriptionId}` | Manual curl only -- no automated test yet, disclosed gap |
| AMFStatusChangeSubscribe | `POST /namf-comm/v1/subscriptions` | Integration test (`AmfStatusChangeSubscribeAndNonUeN2Transfer`, real 201 + Location header) |
| AMFStatusChangeUnSubscribe | `DELETE /namf-comm/v1/subscriptions/{subscriptionId}` | Manual curl only -- no automated test yet, disclosed gap |
| AMFStatusChangeSubscribeModfy | `PUT /namf-comm/v1/subscriptions/{subscriptionId}` | Not exercised by any test yet -- implemented, unverified beyond compiling. Disclosed gap |
| OAuth2 registration/heartbeat as an SBI client | N/A (`nfs/amf/src/main.cpp`'s `run_nrf_lifecycle`, dedicated thread per ADR-0019) | Manual verification below (`amf: registered with NRF (HTTP 201)` log line) + every integration test above implicitly depends on it succeeding |

**Manual verification (2026-08-05, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/amf/amf &
[amf] [info] amf: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7778/namf-comm/v1/ue-contexts/imsi-999700000000001/release \
    -H "authorization: Bearer <token, scope=namf-comm, targetNfType=AMF>" \
    -d '{"ngapCause":{"group":0,"value":0}}'
-> 404, {"detail":"No UE context with id imsi-999700000000001",...}

$ curl ... -X POST https://127.0.0.1:7778/namf-comm/v1/ue-contexts/imsi-1/n1-n2-messages/subscriptions \
    -d '{"n1NotifyCallbackUri":"https://example.com/n1"}'
-> 201, {"n1n2NotifySubscriptionId":"n1n2sub-1"}, Location header present

$ curl ... -X POST https://127.0.0.1:7778/namf-comm/v1/subscriptions \
    -d '{"amfStatusUri":"https://example.com/amfstatus"}'
-> 201, {"amfStatusUri":"https://example.com/amfstatus"}

$ curl ... -X POST https://127.0.0.1:7778/namf-comm/v1/non-ue-n2-messages/transfer \
    -d '{"n2Information":{"n2InformationClass":"PWS"}}'
-> 200, {"result":"N2_INFO_TRANSFER_INITIATED"}

$ curl http://127.0.0.1:9465/metrics | grep ^amf_
-> real Prometheus counters, e.g. amf_non_ue_n2_message_transfer_total{...} 1
```

## Codegen infrastructure (Phase 1)

Not a procedure row (this is infrastructure, not a business-logic procedure), noted separately:
`tools/sbi-codegen` generates C++ DTOs + JSON serializers for the full transitive `$ref` closure of
`TS29510_Nnrf_NFManagement.yaml`, `TS29510_Nnrf_NFDiscovery.yaml`, `TS29510_Nnrf_AccessToken.yaml`,
`TS29518_Namf_Communication.yaml`, and `TS29571_CommonData.yaml` (1092 types across 20 source YAML
files -- grew from Phase 1's original 1076/22/3-file set when NRF's own work added
NFDiscovery.yaml and AccessToken.yaml as additional codegen root files, since AccessTokenRsp and
SubscriptionData weren't reachable via the original three files' `$ref` chains -- see
`docs/DECISIONS.md` ADR-0010). Every generated file carries a header comment citing its source TS
number(s), YAML filename(s), and commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`. Proven by
`tests/conformance/test_round_trip.cpp` (C++ round-trip: construct -> to_json -> from_json ->
equality, including an anyOf-open-enum value outside the known list, and `AmfId`'s pattern
validation) plus `tests/conformance/validate_structural_conformance.py` (the emitted JSON's field
names checked against the real OpenAPI schema's `required`/`properties`, not a hand-copied
expectation). NRF is the first NF to actually consume these generated types (`libs/sbi-generated`
linked into `nfs/nrf`).

## Transport security (pre-Phase-2)

Not a procedure row (infrastructure). `libs/sbi-core`'s HTTP/2 server and client require TLS 1.3 +
mTLS (`docs/DECISIONS.md` ADR-0011, superseding ADR-0005's h2c-only Phase 0 state) --
`scripts/gen-lab-pki.sh` generates a lab CA and per-NF certs, and the integration test proves the
full flow works over a real mTLS handshake, not h2c. Proven additionally by manual `openssl
s_client` checks recorded in ADR-0011 (mTLS actually rejects clients without a cert; ALPN
negotiates `h2`).

## Deployment infrastructure (Phase 2, NRF)

Not a procedure row. `deploy/docker/nrf.Dockerfile` (multi-stage Ubuntu 24.04 build), actually
built and produced a working image in this environment (`docker build -f
deploy/docker/nrf.Dockerfile -t 5gc-nrf:test .` succeeded -- see `docs/DECISIONS.md` ADR-0014 for
the two real build issues hit and fixed along the way, not just anticipated). `deploy/docker/
docker-compose.yml` and `deploy/helm/nrf/` (Chart.yaml + deployment/service templates) exist but
are not proven by this session's testing beyond `docker build` succeeding for the image itself --
`docker compose up` and `helm install`/`helm template` were not run. Disclosed gap, not silently
assumed to work.

## Deployment infrastructure (Phase 2, AMF)

Not a procedure row. `deploy/docker/amf.Dockerfile` (multi-stage Ubuntu 24.04 build, mirrors
`nrf.Dockerfile`), actually built in this environment (`docker build -f deploy/docker/amf.Dockerfile
-t 5gc-amf:test .` -- see `docs/DECISIONS.md` ADR-0019 for the result and the shared-PKI bug found
and fixed in `deploy/docker/docker-compose.yml` along the way). `deploy/helm/amf/` exists (mirrors
`deploy/helm/nrf/`) but, like NRF's chart, is not proven by `helm install`/`helm template` this
session -- and additionally has a disclosed, unresolved gap of its own: no shared-PKI mechanism
between separate Helm releases (see `deploy/helm/amf/Chart.yaml`'s description and ADR-0019).
`docker compose up` (both `nrf` and `amf` containers actually mTLS-registering with each other) was
NOT run this session -- same disclosed-not-silently-assumed gap ADR-0014 already recorded for NRF
alone.

## RAN/UE simulator infrastructure (still not wired to any NF)

Not a procedure row (external test tool, not an implemented NF). `simulators/ransim/` wraps
UERANSIM v3.3.0 (commit `6bf5a1a96aaef6ae8778b9d8b477ac6e2bbf8156`, AGPL-3.0, arms-length external
process -- see `docs/DECISIONS.md` ADR-0016). Actually fetched and built in this environment
(`simulators/ransim/fetch-and-build.sh` produced real `nr-gnb`/`nr-ue`/`nr-cli` binaries, `nr-gnb
--version` reports `v3.3.0`, matching the pinned tag). Manually ran `nr-gnb -c config/gnb.yaml`:
it correctly attempts a real SCTP connection to `127.0.0.5:38412` and gets `Connection refused` --
the expected, disclosed state. **Still not wired to any NF**: AMF's `Namf_Communication` SBI
surface now exists (this session, see the AMF section above), but its NGAP/N2 termination
(TS 38.413, SCTP transport -- a separate, larger effort, deliberately deferred per ADR-0016) does
not. This simulator still cannot register a UE against anything in this repository.
