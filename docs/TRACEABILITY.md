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

## SMF: PCF wiring (SM Policy Association Establishment/Termination, this turn)

Not new procedures -- extends CreateSMContext/ReleaseSMContext above with a real call to a real
PCF, source `specs/5G_APIs-REL-19/TS29512_Npcf_SMPolicyControl.yaml` (commit
`bca84b60a37773133bcae97e5c6c0d10a93b47b6`) for the client-side call shape, implemented in
`nfs/smf/src/main.cpp`, proven by the updated `tests/integration/test_smf_pdu_session.cpp` (now
real subprocess-to-subprocess: `nrf` + `smf` + `pcf`) plus manual `curl` verification recorded
below. See `docs/DECISIONS.md` ADR-0029 for the AMF-side deferral this turn also decided.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| CreateSMContext now really calls PCF's CreateSMPolicy | TS 23.502 §4.3.2.2.1 | Integration test (`FullSmContextLifecycleOverRealHttp2`, queries PCF directly after CreateSMContext and confirms a real `SmPolicyControl` with matching supi/dnn and a `default` session rule exists -- not just that SMF returned 201) |
| CreateSMContext fails closed if PCF is unreachable | N/A (this build's own fail-closed policy, ADR-0029) | Integration test (`CreateSMContextFailsClosedWhenPcfUnreachable`, SMF spawned with no PCF process at all -> real 500, not a silent success) |
| CreateSMContext requires supi/pduSessionId/dnn/sNssai | N/A (disclosed narrowing vs. SmContextCreateData's own schema, ADR-0029) | Integration test (`CreateSMContextRequiresSupiPduSessionIdDnnAndSNssai`, the old ADR-0020-era minimal body now gets a real 400) |
| ReleaseSMContext now really calls PCF's DeleteSMPolicy (best-effort) | TS 23.502 §4.3.2.2.1 | Integration test (`FullSmContextLifecycleOverRealHttp2`, queries PCF directly after ReleaseSMContext and confirms the SM Policy is really gone -- 404, not just that SMF returned 204) |
| SMF -> PCF as an SBI client (OAuth2 token, mTLS) | N/A (`nfs/smf/src/main.cpp`'s second `http2::Client`/`OAuth2Client` pair) | Every PCF-dependent assertion above implicitly depends on this succeeding for real |

**Manual verification (2026-08-07, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/pcf/pcf &   # then ./nfs/smf/smf &

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts \
    -H "authorization: Bearer <token, scope=nsmf-pdusession, targetNfType=SMF>" \
    -H 'content-type: multipart/related; boundary=boundary123' \
    --data-binary $'--boundary123\r\nContent-Type: application/json\r\n\r\n{"servingNfId":"...",
    "servingNetwork":{"mcc":"999","mnc":"70"},"anType":"3GPP_ACCESS","smContextStatusUri":"...",
    "supi":"imsi-999700000000001","pduSessionId":5,"dnn":"internet","sNssai":{"sst":1}}\r\n--boundary123--\r\n'
-> 201, {"pduSessionId":5,"sNssai":{"sst":1}}, location: /nsmf-pdusession/v1/sm-contexts/smctx-1

$ curl ... "https://127.0.0.1:7783/npcf-smpolicycontrol/v1/sm-policies/smpolicy-1" \
    -H "authorization: Bearer <token, scope=npcf-smpolicycontrol, targetNfType=PCF>"
-> 200, real SmPolicyControl: {"context":{"dnn":"internet","notificationUri":
   "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/smctx-1/pcf-notify","pduSessionId":5,
   "pduSessionType":"IPV4","sliceInfo":{"sst":1},"supi":"imsi-999700000000001"},"policy":{...}}
   (this is a real call to the real pcf process running alongside -- not a stub)

$ curl ... -X POST https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/smctx-1/release -d '{}'
-> 204

$ curl ... "https://127.0.0.1:7783/npcf-smpolicycontrol/v1/sm-policies/smpolicy-1" -H "authorization: Bearer <pcf token>"
-> 404, {"detail":"No SM policy smpolicy-1",...}   # confirms DeleteSMPolicy was really called

$ curl http://127.0.0.1:9466/metrics | grep ^smf_pcf_
-> smf_pcf_sm_policy_create_total{otel_scope_name="smf"} 1
-> smf_pcf_sm_policy_delete_total{otel_scope_name="smf"} 1
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

## PCF (Phase 2, seventh NF)

All rows below: source `specs/5G_APIs-REL-19/TS29507_Npcf_AMPolicyControl.yaml` +
`TS29512_Npcf_SMPolicyControl.yaml` (commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`),
implemented in `nfs/pcf/src/main.cpp` + `nfs/pcf/src/stores.cpp`, proven by
`tests/integration/test_pcf_policy_control.cpp` (real subprocess-to-subprocess: `nrf` + `pcf`, real
TLS 1.3 + mTLS, real signed JWT) plus manual `curl` verification recorded below. Scope: the
`CreateIndividualAMPolicyAssociation`/`ReadIndividualAMPolicyAssociation`/
`DeleteIndividualAMPolicyAssociation`/`ReportObservedEventTriggersForIndividualAMPolicyAssociation`
group and the `CreateSMPolicy`/`GetSMPolicy`/`UpdateSMPolicy`/`DeleteSMPolicy` group -- see
`docs/DECISIONS.md` ADR-0028 for what's deferred and why (including AMF/SMF not calling this PCF
yet) and for the disclosed fixed-default policy decisioning.

| Procedure | TS clause / operationId | Test |
|---|---|---|
| CreateIndividualAMPolicyAssociation | `POST /npcf-am-policy-control/v1/policies` | Integration test (`AmPolicyAssociationFullLifecycle`, real 201 + Location, default session AMBR applied) |
| ReadIndividualAMPolicyAssociation | `GET /npcf-am-policy-control/v1/policies/{polAssoId}` | Integration test (`AmPolicyAssociationFullLifecycle`, 200 then 404 after delete) + (`MissingResourceIs404...`) |
| ReportObservedEventTriggersForIndividualAMPolicyAssociation | `POST /npcf-am-policy-control/v1/policies/{polAssoId}/update` | Integration test (`AmPolicyAssociationFullLifecycle`, real `PolicyUpdate` response, confirms the update is really persisted via a follow-up GET, not just echoed) |
| DeleteIndividualAMPolicyAssociation | `DELETE /npcf-am-policy-control/v1/policies/{polAssoId}` | Integration test (`AmPolicyAssociationFullLifecycle`, real 204 then 404 on re-delete) |
| CreateSMPolicy | `POST /npcf-smpolicycontrol/v1/sm-policies` | Integration test (`SmPolicyFullLifecycleUsesRequestSuppliedDefaults`, real 201 body is `SmPolicyDecision` only per the YAML, decision reflects the request's own `subsSessAmbr`/`subsDefQos`) + (`SmPolicyWithoutSubscribedDefaultsUsesFixedFallback`, confirms the documented 1 Gbps/5QI-9 fallback when the request supplies neither) |
| GetSMPolicy | `GET /npcf-smpolicycontrol/v1/sm-policies/{smPolicyId}` | Integration test (`SmPolicyFullLifecycleUsesRequestSuppliedDefaults`, real `SmPolicyControl` `{context, policy}` body, 200 then 404 after delete) |
| UpdateSMPolicy | `POST /npcf-smpolicycontrol/v1/sm-policies/{smPolicyId}/update` | Integration test (`SmPolicyFullLifecycleUsesRequestSuppliedDefaults`, real 200 with a re-derived `SmPolicyDecision`) |
| DeleteSMPolicy | `POST /npcf-smpolicycontrol/v1/sm-policies/{smPolicyId}/delete` | Integration test (`SmPolicyFullLifecycleUsesRequestSuppliedDefaults`, real 204 then 404 on re-delete) |
| Unknown-resource / tampered-token error paths | N/A | Integration test (`MissingResourceIs404AndTamperedTokenIs401`) |
| OAuth2 registration/heartbeat as an SBI client | N/A (`nfs/pcf/src/main.cpp`'s `run_nrf_lifecycle`) | Manual verification below (`pcf: registered with NRF (HTTP 201)` log line) + every integration test above implicitly depends on it succeeding |

**Manual verification (2026-08-07, not captured in an automated test -- recorded here so it's not
lost):**
```
$ ./nfs/nrf/nrf &   # then ./nfs/pcf/pcf &
[pcf] [info] pcf: registered with NRF (HTTP 201)

$ curl --http2 --cacert certs/ca/ca.crt --cert certs/hello-nf/cert.pem --key certs/hello-nf/key.pem \
    -X POST https://127.0.0.1:7783/npcf-smpolicycontrol/v1/sm-policies \
    -H "authorization: Bearer <token, scope=npcf-smpolicycontrol, targetNfType=PCF>" \
    -d '{"supi":"imsi-999700000000001","pduSessionId":5,"pduSessionType":"IPV4","dnn":"internet",
        "notificationUri":"https://example.com/notify","sliceInfo":{"sst":1}}'
-> 201, Location: /npcf-smpolicycontrol/v1/sm-policies/smpolicy-1
   {"sessRules":{"default":{"authDefQos":{"5qi":9,"arp":{"preemptCap":"NOT_PREEMPT",
   "preemptVuln":"NOT_PREEMPTABLE","priorityLevel":8}},"authSessAmbr":{"downlink":"1 Gbps",
   "uplink":"1 Gbps"},"sessRuleId":"default"}},"suppFeat":""}

$ curl ... "https://127.0.0.1:7783/npcf-smpolicycontrol/v1/sm-policies/smpolicy-1"
-> 200, {"context":{...real echoed SmPolicyContextData...},"policy":{...same decision as above...}}

$ curl http://127.0.0.1:9470/metrics | grep ^pcf_sm_policy_create_total
-> pcf_sm_policy_create_total{otel_scope_name="pcf"} 1   # real Prometheus counter
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

## Deployment infrastructure (Phase 2, PCF)

Not a procedure row. `deploy/docker/pcf.Dockerfile` (multi-stage Ubuntu 24.04 build, mirrors
`ausf.Dockerfile`), built in this environment (`docker build -f deploy/docker/pcf.Dockerfile -t
5gc-pcf:test .`). `deploy/docker/docker-compose.yml` updated with a `pcf` service (added to
`pki-init`'s shared-volume provisioning; `depends_on` is just `nrf`, since PCF doesn't call any
other NF this turn -- see ADR-0028's deferred AMF/SMF wiring). `deploy/helm/pcf/` (mirrors
`deploy/helm/ausf/`, including the same disclosed cross-chart shared-PKI gap noted in
`deploy/helm/pcf/Chart.yaml`) -- not proven by `helm install`/`helm template`, same disclosed gap
as NRF/AMF/SMF/UDM/UDR/AUSF's charts. `docker compose up` and `helm install`/`helm template` were
NOT run this session -- same disclosed-not-silently-assumed gap ADR-0014 already recorded for NRF
alone. See `docs/DECISIONS.md` ADR-0028.

## AMF NGAP/N2 + NAS-5GS Registration and PDU Session Establishment (staged plan, Stages 0-5, plus ADR-0036/ADR-0037/ADR-0038)

**As of ADR-0038, the full table below is verified by a single real, unmodified `nr-ue` run,
first attempt, zero retries or failures anywhere in the entire procedure -- both directions** -- not
just each row independently. Real `nrf`/`udr`/`udm`/`ausf`/`pcf`/`smf`/`amf` plus real
`nr-gnb`/`nr-ue`: NG Setup → Initial Registration → AuthenticationFailure (SQN out of range) →
SQN-resync AuthenticationRequest accepted → SecurityModeCommand/Complete verified →
RegistrationAccept sent → AM Policy Association established with PCF → PDU Session Establishment
Request verified → SM context established with SMF → real PDU Session Establishment Accept built
from PCF's actual QoS decision → delivered to AMF via `Namf_Communication` `N1N2MessageTransfer` →
forwarded to the UE via `DownlinkNASTransport`. `nr-ue` logged `Initial Registration is successful`,
then `Sending PDU Session Establishment Request`, then `PDU Session Establishment Accept received`
and `PDU Session establishment is successful PSI[1]` -- the complete procedure, both directions,
with no gap left unimplemented. This supersedes every "Not reachable live (SQN gap)" note this
table previously carried, and closes the PDU Session Establishment Accept gap ADR-0036/ADR-0037
both disclosed.

| Procedure | TS clause / message | Test |
|---|---|---|
| NG Setup | TS 38.413 `NGSetupRequest`/`NGSetupResponse` | Real `nr-gnb` interop (real SCTP association, real NG Setup succeeds in both AMF's and `nr-gnb`'s own logs) |
| InitialUEMessage -> NAS RegistrationRequest decode | TS 38.413 `InitialUEMessage`; TS 24.501 §8.2.6 `RegistrationRequest`, §9.11.3.4 5GS Mobile Identity (null-scheme SUCI) | Real `nr-ue` interop (real SUPI `imsi-999700000000001` extracted from a real UE) + unit test `NasCodec.DecodesRegistrationRequestNullSchemeSuci` (hand-constructed vector) + 2 negative unit tests |
| AMF -> AUSF `POST /ue-authentications` | TS 29.509 `Nausf_UEAuthentication` (`AuthenticationInfo`/`UEAuthenticationCtx`) | Real call succeeds against real AUSF during the same `nr-ue` interop run (real 5G-AKA vector returned) |
| NAS AuthenticationRequest encode -> DownlinkNASTransport | TS 24.501 §8.2.1, §9.11.3.16/§9.11.3.15 (RAND Type-3, AUTN Type-4) | Real `nr-ue` interop (UE decodes it correctly, computes a real response) + unit test `NasCodec.EncodesAuthenticationRequestWithCorrectIeLayout` |
| NAS AuthenticationResponse/Failure decode | TS 24.501 §8.2.8/§8.2.7 | Real `nr-ue` interop, both outcomes: real `AuthenticationFailure` (mmCause=SYNCH_FAILURE, real AUTS) on first contact, then real `AuthenticationResponse` (RES\*) accepted on the SQN-resync retry -- see ADR-0037. 4 unit tests |
| SQN resynchronization (`resynchronizationInfo` plumbed AMF->AUSF->UDM; UDM `resync_sqn`; Milenage f1\*/f5\*) | TS 33.102 §6.3.3/Annex C.3; TS 29.503/29.509 `ResynchronizationInfo_Nudm_UEAU` | Real `nr-ue` interop: real AUTS verified, real UDM SQN reset, real resynced vector accepted by a real UE on retry, first attempt. Milenage f1\*/f5\* also cross-checked via standalone harness against UERANSIM's real compiled `milenage.c` (80/80 matches). 3 unit tests. See ADR-0037 |
| AMF -> AUSF `PUT .../5g-aka-confirmation` | TS 29.509 `ConfirmationData`/`ConfirmationDataResponse` | Real call succeeds against real AUSF during the same `nr-ue` interop run |
| KAMF derivation | TS 33.501 Annex A.7 (FC=0x6D) | Real `nr-ue` interop (correct KAMF is a precondition of every subsequent verified message) + unit test `AkaKdf.KamfDerivationIsDeterministicAndInputDependent`. A real SUPI-format bug (full `"imsi-"`-prefixed string fed to the KDF instead of bare digits) was caught and fixed here -- see ADR-0037 |
| KNASenc/KNASint derivation | TS 33.501 Annex A.8 (FC=0x69) | Real `nr-ue` interop (a derivation bug would fail every MAC verify below, none did) |
| 128-NEA2/128-NIA2 (AES-128-CTR / AES-128-CMAC) | TS 33.401 Annex B.1.3/B.2.3 (reused by TS 33.501 for the 5G algorithm set) | Real `nr-ue` interop, plus standalone cross-check harness against UERANSIM's real, independent `eea2.cpp`/`eia2.cpp` (40/40 byte-exact matches, 20 random trials each) -- see ADR-0034. 4 unit tests |
| NAS SecurityModeCommand encode / SecurityModeComplete decode | TS 24.501 §8.2.25/§8.2.26 | Real `nr-ue` interop, verified OK first attempt. A real bug (missing NAS sequence-number-byte MAC prefix, TS 24.501's actual `nas_enc::ComputeMac` construction) was found and fixed here -- the root cause of every prior stage's SecurityModeCommand failure -- see ADR-0037. 6 unit tests |
| NAS RegistrationAccept encode / RegistrationComplete decode | TS 24.501 §8.2.7/§8.2.6 | RegistrationAccept: real `nr-ue` interop, accepted first attempt. RegistrationComplete decode: **confirmed via real interop that a real UE never sends this message** for this build's minimal RegistrationAccept content (no GUTI/NSSAI change) -- TS 24.501's conditional-send rule, confirmed against UERANSIM's own `receiveInitialRegistrationAccept`. `decode_registration_complete` is kept (4 unit tests, spec-correct) but is not called by any production handler -- see ADR-0037 |
| AMF -> PCF `POST /npcf-am-policy-control/v1/policies` (`CreateIndividualAMPolicyAssociation`) | TS 29.507 `Npcf_AMPolicyControl` | Real `nr-ue` interop: real HTTP 201 + genuine `PolicyAssociation` from real PCF, first attempt, immediately after RegistrationAccept is sent (ADR-0037's phase-machine fix). Also previously verified directly via `curl` with a genuine NRF-issued token -- see ADR-0035 |
| NAS UlNasTransport decode (PDU Session Establishment Request routing) | TS 24.501 §8.2.10, §9.11.3.5 (payload container type), §9.11.3.41 (PDU session ID), §9.11.2.8 (S-NSSAI), §9.11.2.1a (DNN, TS 23.003 §9.1 label encoding) | Real `nr-ue` interop: pduSessionId=1, dnn=internet correctly extracted, MAC verified OK first attempt. 4 unit tests (`NasCodec.DecodeUlNasTransport*`). The 5GSM payload container itself (the PDU Session Establishment Request's own content) is deliberately never decoded -- see ADR-0036 |
| AMF -> SMF `POST /nsmf-pdusession/v1/sm-contexts` (`CreateSMContext`, now including real `n1SmMsg`) | TS 29.502 `Nsmf_PDUSession`, `multipart/related` | Real `nr-ue` interop: real HTTP 201 + genuine `SmContextCreatedData` from real SMF, first attempt -- also confirming SMF's own internal call to PCF succeeded as part of the same request. AMF now forwards the real captured payload-container bytes as `n1SmMsg` (previously dropped) -- see ADR-0038 |
| 5GSM PDU Session Establishment Request decode (header only) / Accept encode | TS 24.501 §8.3.1/§8.3.5, §9.11.4.13 (QoS rules), §9.11.4.14 (session-AMBR) | Real `nr-ue` interop: the real UE decoded and accepted this build's Accept content (`PDU Session Establishment Accept received` -> `PDU Session establishment is successful PSI[1]`), not just a MAC-verified opaque blob -- QFI/session-AMBR sourced from PCF's real `SmPolicyDecision`, not fabricated. 7 unit tests (`tests/conformance/test_nas_5gsm_codec.cpp`). See ADR-0038 |
| SMF -> AMF `POST /namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages` (`N1N2MessageTransfer`) | TS 29.518 `Namf_Communication` | Real `nr-ue` interop: real HTTP 200 from real AMF, first attempt, on two independent clean runs. AMF's pre-existing stub (JSON-only, no real delivery) replaced with a real `multipart/related` handler + `NgapUeRegistry` cross-thread handoff to the live NGAP association. See ADR-0038 |
| NAS DlNasTransport encode (delivering the Accept to the UE) | TS 24.501 §8.2.9 | Real `nr-ue` interop: the real UE received and correctly decoded/deciphered/MAC-verified this message before decoding the 5GSM Accept inside it. Unit test `NasCodec.EncodesDlNasTransportWithCorrectEnvelopeAndDecryptsToExpectedInner`. See ADR-0038 |

The PDU Session Establishment Accept gap ADR-0036/ADR-0037 both disclosed (AMF had nothing real to
send back to the UE after `CreateSMContext`, so a real `nr-ue` would retransmit under `T3580` and
eventually give up) is closed by the four rows above -- ADR-0038. Real end-to-end interop, first
attempt, zero retries anywhere in the chain, confirmed on two independent clean process sets.

Not a procedure row: `simulators/ransim/` wraps UERANSIM v3.3.0 (commit
`6bf5a1a96aaef6ae8778b9d8b477ac6e2bbf8156`, AGPL-3.0, arms-length external process -- see
`docs/DECISIONS.md` ADR-0016), used as both the real interop test client for the rows above and,
separately, as a read-only reference oracle (its vendored `src/lib/nas`/`src/lib/crypt` source,
never copied into this project's own artifacts) for every NAS-5GS/NEA2/NIA2 byte-layout decision
in this table. `ue.yaml`'s `op` field and three missing UAC fields were fixed as real config bugs
found along the way (ADR-0030/ADR-0032) -- this config file had never actually been run against
real `nr-ue` before this staged effort.

## UPF PFCP/N4 (staged plan, Phase 3 Stage 0-4, ADR-0039/ADR-0040/ADR-0041/ADR-0042/ADR-0043)

**Stage 4 (eBPF/XDP datapath), fully resolved 2026-08-09** (see ADR-0043 in full, including its
final "Resolution: the ARP gap, root-caused and fixed" section): live testing confirmed the BPF
verifier accepts the program, real control-plane integration works end to end (a real PFCP Session
Establishment genuinely registers a TEID in the live BPF map), and -- after root-causing a real
same-namespace overlapping-route bug (`Datapath::create()` put both veth ends in the same network
namespace, giving the kernel two `proto kernel scope link` routes for the same `/30`, which broke
ARP replies; fixed by moving the peer end into its own namespace) -- a real GTP-U test packet
carrying a TEID allocated by a genuine PFCP N4 Session Establishment (triggered by a real
`nr-gnb`/`nr-ue` PDU session) was actually decapsulated and delivered to the TUN device. Every row
below now meets the "real interop, actually observed" bar this document holds every other stage
to.

PFCP (TS 29.244) has no OpenAPI YAML -- see ADR-0039 for the real spec source used instead (the
official 3GPP TS 29.244 V14.3.0 PDF, vendored at `specs/PFCP/29244-e30.pdf`).

**As of ADR-0042, a real PDU Session Establishment (real `nr-ue` -> real `amf` -> real `smf`)
creates a real N4 session on real `upf`, end to end, first attempt, zero regressions** -- the same
"two independently-built processes genuinely interoperating over real wire bytes" bar the AMF/NGAP
table above met once real `nr-ue` interop landed, now met for the N4 side too.

| Procedure | TS clause / message | Test |
|---|---|---|
| PFCP message header encode/decode (node-related and session-related forms) | TS 29.244 §7.2.2 | 5 unit tests (`tests/conformance/test_pfcp_core.cpp`, `PfcpHeader.*`) -- byte-exact layout checks against the real spec figures, round-trips, malformed-input rejection |
| PFCP Information Element TLV encode/decode, including grouped IEs | TS 29.244 §8.1.1/§7.2.3.3 | 2 unit tests (`PfcpIe.*`) plus 5 for common IEs (`PfcpCommonIes.*`) plus 10 for session IEs including a dedicated grouped-IE round-trip test (`PfcpSessionIes.*`) -- see ADR-0042 |
| UPF -> NRF `PUT /nnrf-nfm/v1/nf-instances/{id}` (`NFType=UPF`, real `upfInfo`) | TS 29.510 `Nnrf_NFManagement` | Real interop: real NRF, real HTTP 201, confirmed in `nrf`'s own log (`registered new NF instance ... type=UPF`) |
| SMF -> NRF `GET /nnrf-disc/v1/nf-instances?target-nf-type=UPF` (`SearchNFInstances`) | TS 29.510 `Nnrf_NFDiscovery` | Real interop: real HTTP 200 from real NRF, real UPF `ipv4Addresses` extracted -- confirmed in `smf`'s own log (`discovered UPF at 127.0.0.1 via Nnrf_NFDiscovery`). First real use of this NRF capability anywhere in this project; every other NF-to-NF call still uses a hardcoded base URL -- see ADR-0041 |
| Sx Heartbeat Request/Response | TS 29.244 §7.4.2 | Real interop, verified two ways: (1) a hand-crafted PFCP datagram (ADR-0040) and (2) implicitly exercised by the same codec paths Association Setup below proves against a real independent peer |
| Sx Association Setup Request/Response | TS 29.244 §7.4.4.1/§7.4.4.2 | Real interop between two independently-built processes: real `smf` (client) and real `upf` (server), neither aware of the other's internals. `smf`'s log: `PFCP Sx Association established with UPF at 127.0.0.1`; `upf`'s own independently-generated log: `Sx Association Setup accepted from 127.0.0.1`. Node ID/Cause/Recovery Time Stamp/CP-and-UP Function Features all decoded correctly on both sides. See ADR-0041 |
| Sx Session Establishment Request/Response (one uplink PDR/FAR: Source Interface=Access, F-TEID CH-allocation, FAR Apply Action=FORW/Destination Interface=Core) | TS 29.244 §7.5.2/§7.5.3 | Real interop, triggered by a real `nr-ue` PDU Session Establishment via real `amf`->`smf`->`upf`. `smf`'s log: `N4 Session Establishment succeeded for pduSessionId 1, UPF F-SEID=0x1, allocated uplink F-TEID=0x1`, correctly ordered before the PDU Session Establishment Accept delivery (matching TS 23.502's real step order). `upf`'s own independently-generated log: `allocated F-TEID 0x1 for PDR ID 1` / `Sx Session established from 127.0.0.1`. See ADR-0042 |
| BPF program load + verifier acceptance (`gtpu_decap.bpf.c`) | -- (kernel/BPF infrastructure, not a 3GPP procedure) | **Live-verified 2026-08-09**: `bpf_object__load` succeeds against a real, privileged `upf` process after fixing a real verifier rejection (`bpf_ringbuf_reserve` needs a compile-time-constant size). See ADR-0043 |
| veth pair + TUN device creation, XDP attach (generic/SKB mode) | -- (this build's own datapath infrastructure) | **Live-verified 2026-08-09**: `ip link show upf-n3` confirms `xdpgeneric` mode with a real attached program. See ADR-0043 |
| Real TEID registration from a real PFCP Session Establishment into the live BPF map | TS 29.244 §7.5.2 (trigger) | **Live-verified 2026-08-09**: a real Association Setup + Session Establishment sent to the live, privileged `upf` allocated F-TEID `0x1` and (per the existing Stage 3 code path) called `datapath->register_teid(0x1)` against the real, running BPF map -- no synthetic map poke, the real control-plane path. See ADR-0043 |
| GTP-U (TS 29.281) header parse + TEID match + decapsulation, actual packet delivery to TUN | TS 29.281 §5.1 (header), Table 6.1-1 (G-PDU=255), §4.4.2.3 (UDP port 2152) | **Live-verified 2026-08-09**: after fixing the same-namespace overlapping-route bug (see ADR-0043's resolution section), a real GTP-U test packet carrying TEID `0x1` (allocated by the real N4 Session Establishment above, not a synthetic value) was sent from an isolated peer namespace. ARP resolved (`ip neigh show`: `10.99.0.1 ... REACHABLE`) and `upf`'s own log confirms `delivered decapsulated T-PDU (44 bytes) to upf-tun0` -- an exact byte-count match, logged only after a checked, successful `write()` to the TUN device |

**Disclosed gaps**: only an uplink PDR/FAR is created -- the downlink direction needs the gNB's
real N3 GTP-U endpoint (TEID+IP), which requires NGAP PDU Session Resource Setup (still not
implemented), disclosed in `nfs/upf/src/main.cpp`'s own header comment (ADR-0042). The datapath
(ADR-0043) itself has no open runtime-correctness questions left as of 2026-08-09: BPF verifier
acceptance, control-plane TEID registration, and actual GTP-U decapsulation-and-delivery are all
live-verified against real, independently-generated evidence (two separate NFs' own logs, real
`ip neigh`/`tcpdump` output). No real N3 traffic from an actual gNB reaches this path yet (the PDU
Session Resource Setup gap above) -- that remains the honest boundary of what's proven, not the
datapath's own correctness.
