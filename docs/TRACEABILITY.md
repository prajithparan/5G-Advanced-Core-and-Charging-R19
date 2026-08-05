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
