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
real TLS 1.3 + mTLS, real signed JWT) plus manual `curl` verification recorded below. Every
Namf_Communication operationId is now implemented -- `CreateUEContext`/`RelocateUEContext`/
`CancelRelocateUEContext` were initially deferred (multipart/related-only, ADR-0019) then built
once `sbi_core::multipart` landed (ADR-0020).

| Procedure | TS clause / operationId | Test |
|---|---|---|
| CreateUEContext | `PUT /namf-comm/v1/ue-contexts/{ueContextId}` (multipart/related) | Integration test (`CreateUEContextOverMultipartThenEBIAssignmentAndRelease`, real 201 + real `UeContextCreatedData` deserialization) + (`RelocateAndCancelRelocateUEContextOverMultipart`, wrong-content-type 400 case) |
| ReleaseUEContext | `POST /namf-comm/v1/ue-contexts/{ueContextId}/release` | Integration test, both branches: `MissingUeContextIs404...` (404) and `CreateUEContextOverMultipartThenEBIAssignmentAndRelease` (204 on a real context, then 404 on double-release) |
| EBIAssignment | `POST /namf-comm/v1/ue-contexts/{ueContextId}/assign-ebi` | Integration test (`CreateUEContextOverMultipartThenEBIAssignmentAndRelease`, real 200 on a real context) |
| UEContextTransfer | `POST /namf-comm/v1/ue-contexts/{ueContextId}/transfer` | Not exercised by any test yet -- implemented, unverified beyond compiling. Disclosed gap |
| RegistrationStatusUpdate | `POST /namf-comm/v1/ue-contexts/{ueContextId}/transfer-update` | Not exercised by any test yet -- implemented, unverified beyond compiling. Disclosed gap |
| RelocateUEContext | `POST /namf-comm/v1/ue-contexts/{ueContextId}/relocate` (multipart/related) | Integration test (`RelocateAndCancelRelocateUEContextOverMultipart`, real 201 + real `UeContextRelocatedData` deserialization, plus 404 on a nonexistent context) |
| CancelRelocateUEContext | `POST /namf-comm/v1/ue-contexts/{ueContextId}/cancel-relocate` (multipart/related) | Integration test (`RelocateAndCancelRelocateUEContextOverMultipart`, real 204) |
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

## multipart/related codec infrastructure (consumed by AMF and SMF)

Not a procedure row (shared infrastructure, RFC 2046/2387 -- not a 3GPP-specific format). See
`docs/DECISIONS.md` ADR-0020 for why this was built (SMF's `CreateSMContext`, the actual
AMF-triggered PDU Session Establishment trigger, turned out to be multipart/related-ONLY, same
wall AMF's `CreateUEContext` hit first). `libs/sbi-core/include/sbi_core/multipart.hpp` +
`src/multipart.cpp`, proven by 8 unit tests (`tests/conformance/test_multipart.cpp`: encode-then-
parse round-trips including genuinely opaque binary bytes, a hand-crafted body shaped like
`CreateUEContext`'s real wire format, 4 malformed-input rejection cases) and, more importantly,
proven end-to-end over real HTTP/2 by both consumers: `tests/integration/test_amf_namf_communication.cpp`'s
`CreateUEContextOverMultipartThenEBIAssignmentAndRelease`/`RelocateAndCancelRelocateUEContextOverMultipart`
and `tests/integration/test_smf_pdu_session.cpp`'s `FullSmContextLifecycleOverRealHttp2` all
construct real multipart wire bytes via this codec, send them over real TLS 1.3 + mTLS to a
running `amf`/`smf` process, and confirm the NF's own use of the same codec parses them correctly.
One disclosed, unverified assumption: the `Content-Id` bracket convention (see ADR-0020) has no
real external SBI peer in this lab to interop-test against yet.

## SMF (Phase 2, third NF)

All rows below: source `specs/5G_APIs-REL-19/TS29502_Nsmf_PDUSession.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`), implemented in `nfs/smf/src/main.cpp` +
`nfs/smf/src/sm_context_store.cpp`, proven by `tests/integration/test_smf_pdu_session.cpp` (real
subprocess-to-subprocess: `nrf` + `smf`, real TLS 1.3 + mTLS, real signed JWT, real multipart/
related `CreateSMContext`) plus manual `curl` verification recorded below. Scope: the `/sm-contexts`
collection only -- see `docs/DECISIONS.md` ADR-0021 for what's deferred (`/pdu-sessions`,
`SendMoData`/`TransferMoData`, `Nsmf_EventExposure.yaml`, `Nsmf_NIDD.yaml`) and why.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| CreateSMContext | `POST /nsmf-pdusession/v1/sm-contexts` (multipart/related-only) | Integration test (`FullSmContextLifecycleOverRealHttp2`, real 201 + real `SmContextCreatedData` deserialization + Location header) + (`...NonMultipartCreateIs400`, wrong-content-type 400 case) |
| RetrieveSMContext | `POST /nsmf-pdusession/v1/sm-contexts/{smContextRef}/retrieve` | Integration test (`FullSmContextLifecycleOverRealHttp2`, real 200 with NO request body -- proves the spec's `required: false` is honored) + (`MissingSmContextIs404...`, 404 branch) |
| UpdateSMContext | `POST /nsmf-pdusession/v1/sm-contexts/{smContextRef}/modify` | Integration test (`FullSmContextLifecycleOverRealHttp2`, real 204 on a real context) |
| ReleaseSMContext | `POST /nsmf-pdusession/v1/sm-contexts/{smContextRef}/release` | Integration test (`FullSmContextLifecycleOverRealHttp2`, real 204 then 404 on double-release) |
| OAuth2 registration/heartbeat as an SBI client | N/A (`nfs/smf/src/main.cpp`'s `run_nrf_lifecycle`, dedicated thread per ADR-0006/ADR-0019) | Manual verification below (`smf: registered with NRF (HTTP 201)` log line) + every integration test above implicitly depends on it succeeding |

**Manual verification (2026-08-05, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/smf/smf &
[smf] [info] smf: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts \
    -H "authorization: Bearer <token, scope=nsmf-pdusession, targetNfType=SMF>" \
    -H 'content-type: multipart/related; boundary="testboundary123"; type="application/json"' \
    --data-binary $'--testboundary123\r\nContent-Type: application/json\r\n\r\n{"servingNfId":"amf-1","servingNetwork":{"mcc":"999","mnc":"70"},"anType":"3GPP_ACCESS","smContextStatusUri":"https://example.com/status"}\r\n--testboundary123--\r\n'
-> 201, {}, location: /nsmf-pdusession/v1/sm-contexts/smctx-1

$ curl ... -X POST https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/smctx-1/retrieve
-> 200, {"ueEpsPdnConnection":""}

$ curl ... -X POST https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/smctx-1/modify \
    -d '{"upCnxState":"ACTIVATED"}'
-> 204

$ curl ... -X POST https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/smctx-1/release
-> 204 (then 404 on a second call)

$ curl http://127.0.0.1:9466/metrics | grep ^smf_
-> real Prometheus counters, e.g. smf_create_sm_context_total{...} 1
```

## UDM (Phase 2, fourth NF)

All rows below: source `specs/5G_APIs-REL-19/TS29503_Nudm_UECM.yaml`,
`specs/5G_APIs-REL-19/TS29503_Nudm_SDM.yaml` (commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`),
implemented in `nfs/udm/src/main.cpp` + `nfs/udm/src/stores.cpp`, proven by
`tests/integration/test_udm_uecm_sdm.cpp` (real subprocess-to-subprocess: `nrf` + `udm`, real
TLS 1.3 + mTLS, real signed JWT) plus manual `curl` verification recorded below. Scope: the AMF
and SMF registration groups of `Nudm_UECM`, and `Nudm_SDM`'s subscriber-data-retrieval +
subscription operations -- see `docs/DECISIONS.md` ADR-0023 for what's deferred and why.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| 3GppRegistration | `PUT /nudm-uecm/v1/{ueId}/registrations/amf-3gpp-access` | Integration test (`AmfRegistrationLifecycle`, real 201 then idempotent 200 on replace) |
| Get3GppRegistration | `GET /nudm-uecm/v1/{ueId}/registrations/amf-3gpp-access` | Integration test (`AmfRegistrationLifecycle`, 200 then 404 after deregister) + (`MissingRegistrationIs404...`, 404/401 paths) |
| Update3GppRegistration | `PATCH /nudm-uecm/v1/{ueId}/registrations/amf-3gpp-access` (RFC 7396 merge-patch) | Integration test (`AmfRegistrationLifecycle`, confirms merge semantics: patched field changes, untouched fields survive) |
| deregAMF | `POST /nudm-uecm/v1/{ueId}/registrations/amf-3gpp-access/dereg-amf` | Integration test (`AmfRegistrationLifecycle`, real 204) |
| Registration (SMF) | `PUT /nudm-uecm/v1/{ueId}/registrations/smf-registrations/{pduSessionId}` | Integration test (`SmfRegistrationLifecycle`, real 201) |
| GetSmfRegistration | `GET /nudm-uecm/v1/{ueId}/registrations/smf-registrations` | Integration test (`SmfRegistrationLifecycle`, real collection-list) |
| RetrieveSmfRegistration | `GET /nudm-uecm/v1/{ueId}/registrations/smf-registrations/{pduSessionId}` | Integration test (`SmfRegistrationLifecycle`, 200 then 404 after delete) |
| UpdateSmfRegistration | `PATCH /nudm-uecm/v1/{ueId}/registrations/smf-registrations/{pduSessionId}` (RFC 7396 merge-patch) | Integration test (`SmfRegistrationLifecycle`, confirms merge semantics) |
| SmfDeregistration | `DELETE /nudm-uecm/v1/{ueId}/registrations/smf-registrations/{pduSessionId}` | Integration test (`SmfRegistrationLifecycle`, real 204) |
| GetAmData | `GET /nudm-sdm/v2/{supi}/am-data` | Integration test (`SdmDataRetrievalAndSubscriptions`, real 200, real deserialization) |
| GetSmfSelData | `GET /nudm-sdm/v2/{supi}/smf-select-data` | Integration test (`SdmDataRetrievalAndSubscriptions`, real 200) |
| GetSmData | `GET /nudm-sdm/v2/{supi}/sm-data` | Integration test (`SdmDataRetrievalAndSubscriptions`, real 200) |
| Subscribe | `POST /nudm-sdm/v2/{ueId}/sdm-subscriptions` | Integration test (`SdmDataRetrievalAndSubscriptions`, real 201 + Location header) |
| Unsubscribe | `DELETE /nudm-sdm/v2/{ueId}/sdm-subscriptions/{subscriptionId}` | Integration test (`SdmDataRetrievalAndSubscriptions`, 204 then 404 on double-unsubscribe) |
| OAuth2 registration/heartbeat as an SBI client | N/A (`nfs/udm/src/main.cpp`'s `run_nrf_lifecycle`, dedicated thread per ADR-0006/ADR-0019) | Manual verification below (`udm: registered with NRF (HTTP 201)` log line) + every integration test above implicitly depends on it succeeding |

**Manual verification (2026-08-06, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/udm/udm &
[udm] [info] udm: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X PUT https://127.0.0.1:7780/nudm-uecm/v1/imsi-999700000000001/registrations/amf-3gpp-access \
    -H "authorization: Bearer <token, scope=nudm-uecm, targetNfType=UDM>" \
    -d '{"amfInstanceId":"...","deregCallbackUri":"...","guami":{...},"ratType":"NR"}'
-> 201

$ curl ... -X PATCH .../registrations/amf-3gpp-access -H "content-type: application/merge-patch+json" \
    -d '{"guami":{"plmnId":{"mcc":"999","mnc":"70"},"amfId":"FEDCBA"}}'
-> 200, amfId changed, amfInstanceId/deregCallbackUri unchanged (real RFC 7396 merge, not replace)

$ curl ... "https://127.0.0.1:7780/nudm-sdm/v2/imsi-999700000000001/am-data" -H "authorization: Bearer <token, scope=nudm-sdm>"
-> 200, {} (disclosed stub -- no UDR yet, see ADR-0023)

$ curl http://127.0.0.1:9467/metrics | grep ^udm_
-> real Prometheus counters, e.g. udm_amf_registration_total{...} 1
```

## UDR (Phase 2, fifth NF)

All rows below: source `specs/5G_APIs-REL-19/TS29505_Subscription_Data.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`), implemented in `nfs/udr/src/main.cpp` +
`nfs/udr/src/stores.cpp`, proven by `tests/integration/test_udr_context_data.cpp` (real
subprocess-to-subprocess: `nrf` + `udr`, real TLS 1.3 + mTLS, real signed JWT) plus manual `curl`
verification recorded below. Scope: the `context-data` group's AMF 3GPP-access context and SMF
registration context resources -- see `docs/DECISIONS.md` ADR-0025 for what's deferred and why,
and for the RFC 6902-vs-RFC 7396 distinction from UDM.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| QueryAmfContext3gpp | `GET /nudr-dr/v2/subscription-data/{ueId}/context-data/amf-3gpp-access` | Integration test (`AmfContextLifecycle`, real 200) + (`MissingResourceIs404...`, 404/401 paths) |
| CreateAmfContext3gpp | `PUT /nudr-dr/v2/subscription-data/{ueId}/context-data/amf-3gpp-access` | Integration test (`AmfContextLifecycle`, real 201 then idempotent 204 on replace) |
| AmfContext3gpp | `PATCH /nudr-dr/v2/subscription-data/{ueId}/context-data/amf-3gpp-access` (RFC 6902 JSON Patch) | Integration test (`AmfContextLifecycle`, confirms patch semantics: only `/guami/amfId` changes, `amfInstanceId`/`deregCallbackUri` survive) |
| QuerySmfRegList | `GET /nudr-dr/v2/subscription-data/{ueId}/context-data/smf-registrations` | Integration test (`SmfRegistrationLifecycle`, real collection GET via generated `sbi_gen::SmfRegList`) |
| QuerySmfRegistration | `GET /nudr-dr/v2/subscription-data/{ueId}/context-data/smf-registrations/{pduSessionId}` | Integration test (`SmfRegistrationLifecycle`, 200 then 404 after delete) |
| CreateOrUpdateSmfRegistration | `PUT /nudr-dr/v2/subscription-data/{ueId}/context-data/smf-registrations/{pduSessionId}` | Integration test (`SmfRegistrationLifecycle`, real 201) |
| UpdateSmfContext | `PATCH /nudr-dr/v2/subscription-data/{ueId}/context-data/smf-registrations/{pduSessionId}` (RFC 6902 JSON Patch) | Integration test (`SmfRegistrationLifecycle`, adds `/smfSetId`, confirms `pduSessionId` survives) |
| DeleteSmfRegistration | `DELETE /nudr-dr/v2/subscription-data/{ueId}/context-data/smf-registrations/{pduSessionId}` | Integration test (`SmfRegistrationLifecycle`, real 204 then 404 on re-delete) |
| OAuth2 registration/heartbeat as an SBI client | N/A (`nfs/udr/src/main.cpp`'s `run_nrf_lifecycle`, dedicated thread per ADR-0006/ADR-0019) | Manual verification below (`udr: registered with NRF (HTTP 201)` log line) + every integration test above implicitly depends on it succeeding |

**Manual verification (2026-08-06, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/udr/udr &
[udr] [info] udr: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X PUT https://127.0.0.1:7781/nudr-dr/v2/subscription-data/imsi-999700000000001/context-data/amf-3gpp-access \
    -H "authorization: Bearer <token, scope=nudr-dr, targetNfType=UDR>" \
    -d '{"amfInstanceId":"...","deregCallbackUri":"...","guami":{...},"ratType":"NR"}'
-> 201

$ curl ... -X PATCH .../context-data/amf-3gpp-access -H "content-type: application/json-patch+json" \
    -d '[{"op":"replace","path":"/guami/amfId","value":"FEDCBA"}]'
-> 204, amfId changed, amfInstanceId/deregCallbackUri unchanged (real RFC 6902 patch, not merge)

$ curl http://127.0.0.1:9468/metrics | grep ^udr_
-> real Prometheus counters
```

## UDM: Nudm_UEAU extension (this turn, prep for AUSF)

All rows below: source `specs/5G_APIs-REL-19/TS29503_Nudm_UEAU.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`) for the SBI surface, `libs/aka-crypto` (TS 35.205/35.206
MILENAGE, TS 33.220 Annex B.2.0 generic KDF, TS 33.501 Annex A key derivations -- no OpenAPI YAML,
implemented from the public algorithm definitions per `docs/DECISIONS.md` ADR-0026) for the crypto,
implemented in `nfs/udm/src/main.cpp` + `nfs/udm/src/stores.cpp`, proven by
`tests/integration/test_udm_ueau.cpp` (real subprocess-to-subprocess: `nrf` + `udm`, real TLS 1.3 +
mTLS, real signed JWT) plus `tests/conformance/test_milenage.cpp` + `test_eap_aka_prime.cpp` (real
TS 35.207 Test Set 1 vectors, real RFC 5448/4187 packet round-trips) plus manual `curl` verification
recorded below. Scope: GenerateAuthData, ConfirmAuth, DeleteAuth only -- see ADR-0026 for what's
deferred and why, and for why this extends an already-committed NF instead of landing inside AUSF's
own (still-future) turn.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| GenerateAuthData (5G-AKA) | `POST /nudm-ueau/v1/{supiOrSuci}/security-information/generate-auth-data` | Integration test (`GenerateAuthDataFor5GAkaSubscriberProducesDistinctVectors`, real `Av5GHeAka` vector, two consecutive calls produce distinct RAND/AUTN/KAUSF) + conformance test (`Milenage.TS35207TestSet1`, `AkaKdf.*`, real published vectors) |
| GenerateAuthData (EAP-AKA') | `POST /nudm-ueau/v1/{supiOrSuci}/security-information/generate-auth-data` | Integration test (`GenerateAuthDataForEapAkaPrimeSubscriberReturnsEapAkaPrimeVector`, real `AvEapAkaPrime` vector) + conformance test (`EapAkaPrime.*`, real PRF'/MK derivation + packet round-trips) |
| ConfirmAuth | `POST /nudm-ueau/v1/{supi}/auth-events` | Integration test (`ConfirmAuthThenDeleteAuthLifecycle`, real 201 + Location header) |
| DeleteAuth | `PUT /nudm-ueau/v1/{supi}/auth-events/{authEventId}` (confirmed a PUT via the YAML, not a DELETE despite the operationId) | Integration test (`ConfirmAuthThenDeleteAuthLifecycle`, real 204 then 404 on re-delete) |
| Unknown-SUPI / tampered-token error paths | N/A | Integration test (`GenerateAuthDataUnknownSupiIs404AndTamperedTokenIs401`) |

**Manual verification (2026-08-06, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/udm/udm &
[udm] [info] udm: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7780/nudm-ueau/v1/imsi-999700000000001/security-information/generate-auth-data \
    -H "authorization: Bearer <token, scope=nudm-ueau, targetNfType=UDM>" \
    -d '{"servingNetworkName":"5G:mnc070.mcc999.3gppnetwork.org","ausfInstanceId":"..."}'
-> 200, real Av5GHeAka: {"authType":"5G_AKA","authenticationVector":{"autn":"...","avType":"5G_HE_AKA",
   "kausf":"<64 hex chars>","rand":"<32 hex chars>","xresStar":"<32 hex chars>"},"supi":"imsi-999700000000001"}
   (field byte-lengths verified programmatically: autn/rand/xresStar 16 bytes, kausf 32 bytes)

$ curl ... -X POST .../imsi-999700000000002/security-information/generate-auth-data -d '{...}'
-> 200, real AvEapAkaPrime: {"authType":"EAP_AKA_PRIME","authenticationVector":{"avType":"EAP_AKA_PRIME",
   "ckPrime":"...","ikPrime":"...","rand":"...","xres":"<16 hex chars>"},"supi":"imsi-999700000000002"}

$ curl http://127.0.0.1:9467/metrics | grep udm_generate_auth_data_total
-> udm_generate_auth_data_total{otel_scope_name="udm"} 3   # real Prometheus counter, incremented per call
```

## AUSF (Phase 2, sixth NF)

All rows below: source `specs/5G_APIs-REL-19/TS29509_Nausf_UEAuthentication.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`) for the SBI surface, `libs/aka-crypto` for the crypto,
implemented in `nfs/ausf/src/main.cpp` + `nfs/ausf/src/stores.cpp`, proven by
`tests/integration/test_ausf_ue_authentication.cpp` (real subprocess-to-subprocess: `nrf` + `udm` +
`ausf`, real TLS 1.3 + mTLS, real signed JWT -- the first integration test in this build where one
NF's handler makes a real business-logic call to a second NF, not just to NRF) plus manual `curl`
verification recorded below. Scope: the `ue-authentications` resource group only -- see
`docs/DECISIONS.md` ADR-0027 for what's deferred and why, the mid-implementation KAUSF/EAP-AKA'
question resolved with the user, and a real pre-existing `libs/aka-crypto` bug (from ADR-0026's
turn) this turn's cross-process testing caught and fixed.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| UEAuthenticate (5G-AKA) | `POST /nausf-auth/v1/ue-authentications` | Integration test (`FiveGAkaSuccessfulAuthenticationCrossChecksHxresAndKseaf`, real `Av5gAka`, HXRES* independently re-derived and compared byte-for-byte) |
| UEAuthenticate (EAP-AKA') | `POST /nausf-auth/v1/ue-authentications` | Integration test (`EapAkaPrimeSuccessfulAuthenticationCrossChecksMacKseafAndMsk`, real base64 EAP-AKA' Request/Challenge packet, AT_MAC independently re-verified) |
| Confirm5gAkaAuthentication | `PUT /nausf-auth/v1/ue-authentications/{authCtxId}/5g-aka-confirmation` | Integration test (`FiveGAkaSuccessfulAuthenticationCrossChecksHxresAndKseaf`, KSEAF independently re-derived and compared; `FiveGAkaWrongResStarIsAuthenticationFailure`, wrong RES* -> `AUTHENTICATION_FAILURE`, no KSEAF, same HTTP 200 either way per the YAML) |
| Delete5gAkaAuthenticationResult | `DELETE /nausf-auth/v1/ue-authentications/{authCtxId}/5g-aka-confirmation` | Integration test (`FiveGAkaSuccessfulAuthenticationCrossChecksHxresAndKseaf`, real 204 then 404 on re-delete) |
| EapAuthMethod | `POST /nausf-auth/v1/ue-authentications/{authCtxId}/eap-session` | Integration test (`EapAkaPrimeSuccessfulAuthenticationCrossChecksMacKseafAndMsk`, real EAP-Success packet decoded and checked, KSEAF/MSK independently re-derived and compared; `EapAkaPrimeWrongResIsAuthenticationFailure`, wrong RES -> real EAP-Failure packet) |
| DeleteEapAuthenticationResult | `DELETE /nausf-auth/v1/ue-authentications/{authCtxId}/eap-session` | Integration test (`EapAkaPrimeSuccessfulAuthenticationCrossChecksMacKseafAndMsk`, real 204) |
| UEAuthenticationsDeregister | `POST /nausf-auth/v1/ue-authentications/deregister` | Integration test (`DeregisterRemovesContextThenSecondDeregisterIs404`, real 204 then 404) |
| Unknown-SUPI / tampered-token error paths | N/A | Integration test (`UnknownSupiIs404AndTamperedTokenIs401`, UDM's 404 mapped through to AUSF's documented "User does not exist in the HPLMN" wording) |
| OAuth2 registration/heartbeat as an SBI client | N/A (`nfs/ausf/src/main.cpp`'s `run_nrf_lifecycle`) | Manual verification below (`ausf: registered with NRF (HTTP 201)` log line) + every integration test above implicitly depends on it succeeding |
| AUSF -> UDM `GenerateAuthData` as an SBI client | N/A (separate `http2::Client`/`OAuth2Client` pair used from route handlers) | Every `POST /ue-authentications` integration test above -- these only pass if the real call to a real `udm` process, over real mTLS with a real token, succeeds |

**Manual verification (2026-08-07, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/udm/udm &   # then ./nfs/ausf/ausf &
[ausf] [info] ausf: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7782/nausf-auth/v1/ue-authentications \
    -H "authorization: Bearer <token, scope=nausf-auth, targetNfType=AUSF>" \
    -d '{"supiOrSuci":"imsi-999700000000001","servingNetworkName":"5G:mnc070.mcc999.3gppnetwork.org"}'
-> 201, Location: /nausf-auth/v1/ue-authentications/authctx-1
   {"5gAuthData":{"autn":"bcf8c5087f4eb9b9041d060c59dcdca0","hxresStar":"25fef92aa5815fde22cd537b69169adb",
   "rand":"bbfe5fb338759e64d80ad99d279e17dd"},"_links":{"5g-aka":{"href":".../authctx-1/5g-aka-confirmation"}},
   "authType":"5G_AKA","servingNetworkName":"5G:mnc070.mcc999.3gppnetwork.org"}
   (this is a real call to the real udm process running alongside -- not a stub)

$ curl ... -X DELETE .../authctx-1/5g-aka-confirmation
-> 204

$ curl ... -X POST .../ue-authentications/deregister -d '{"supi":"imsi-999700000000001"}'
-> 404, {"detail":"No authentication context for supi imsi-999700000000001",...}  # already deleted above

$ curl http://127.0.0.1:9469/metrics | grep ^ausf_
-> ausf_ue_authentications_total{otel_scope_name="ausf"} 1   # real Prometheus counter
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

## Deployment infrastructure (Phase 2, SMF)

Not a procedure row. `deploy/docker/smf.Dockerfile` (multi-stage Ubuntu 24.04 build, mirrors
`amf.Dockerfile`), actually built in this environment (`docker build -f deploy/docker/smf.Dockerfile
-t 5gc-smf:test .` succeeded, ~1239s -- mostly compiling the now-larger generated DTO translation
unit, TS29502_Nsmf_PDUSession.yaml having grown the type count to 1240). `deploy/docker/
docker-compose.yml` updated with an `smf` service (added to `pki-init`'s shared-volume
provisioning, same pattern as ADR-0019). `deploy/helm/smf/` (mirrors `deploy/helm/amf/`, including
the same disclosed cross-chart shared-PKI gap noted in `deploy/helm/smf/Chart.yaml`) -- not proven
by `helm install`/`helm template`, same disclosed gap as NRF/AMF's charts. `docker compose up`
(all three containers actually mTLS-registering with each other) was NOT run this session -- same
disclosed-not-silently-assumed gap ADR-0014 already recorded for NRF alone. See
`docs/DECISIONS.md` ADR-0021.

## Deployment infrastructure (Phase 2, UDM)

Not a procedure row. `deploy/docker/udm.Dockerfile` (multi-stage Ubuntu 24.04 build, mirrors
`smf.Dockerfile`), actually built in this environment (`docker build -f deploy/docker/udm.Dockerfile
-t 5gc-udm:test .` succeeded). `deploy/docker/docker-compose.yml` updated with a `udm` service
(added to `pki-init`'s shared-volume provisioning, same pattern as ADR-0019). `deploy/helm/udm/`
(mirrors `deploy/helm/smf/`, including the same disclosed cross-chart shared-PKI gap noted in
`deploy/helm/udm/Chart.yaml`) -- not proven by `helm install`/`helm template`, same disclosed gap
as NRF/AMF/SMF's charts. `docker compose up` (all four containers actually mTLS-registering with
each other) was NOT run this session -- same disclosed-not-silently-assumed gap ADR-0014 already
recorded for NRF alone. See `docs/DECISIONS.md` ADR-0023.

## Deployment infrastructure (Phase 2, UDR)

Not a procedure row. `deploy/docker/udr.Dockerfile` (multi-stage Ubuntu 24.04 build, mirrors
`udm.Dockerfile`), built in this environment (`docker build -f deploy/docker/udr.Dockerfile -t
5gc-udr:test .`). `deploy/docker/docker-compose.yml` updated with a `udr` service (added to
`pki-init`'s shared-volume provisioning, same pattern as ADR-0019). `deploy/helm/udr/` (mirrors
`deploy/helm/udm/`, including the same disclosed cross-chart shared-PKI gap noted in
`deploy/helm/udr/Chart.yaml`) -- not proven by `helm install`/`helm template`, same disclosed gap
as NRF/AMF/SMF/UDM's charts. `docker compose up` (all five containers actually mTLS-registering
with each other) was NOT run this session -- same disclosed-not-silently-assumed gap ADR-0014
already recorded for NRF alone. See `docs/DECISIONS.md` ADR-0025.

## Deployment infrastructure (Phase 2, AUSF)

Not a procedure row. `deploy/docker/ausf.Dockerfile` (multi-stage Ubuntu 24.04 build, mirrors
`udr.Dockerfile`), built in this environment (`docker build -f deploy/docker/ausf.Dockerfile -t
5gc-ausf:test .`). `deploy/docker/docker-compose.yml` updated with an `ausf` service (added to
`pki-init`'s shared-volume provisioning; the first NF service whose `depends_on` includes another
NF besides `nrf` -- `udm`, since AUSF's own handlers call it). `deploy/helm/ausf/` (mirrors
`deploy/helm/udr/`, including the same disclosed cross-chart shared-PKI gap noted in
`deploy/helm/ausf/Chart.yaml`) -- not proven by `helm install`/`helm template`, same disclosed gap
as NRF/AMF/SMF/UDM/UDR's charts. `docker compose up` (all six containers actually mTLS-registering
with each other, and AUSF actually calling UDM inside the compose network) was NOT run this
session -- same disclosed-not-silently-assumed gap ADR-0014 already recorded for NRF alone. See
`docs/DECISIONS.md` ADR-0027.

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
