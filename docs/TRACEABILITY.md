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
| Sx Association Update Request/Response | TS 29.244 §7.4.4.3/§7.4.4.4 | **Live-verified 2026-08-17**: gap-closure task #107 part 1 (ADR-0084). Real standalone `upf` process, hand-crafted raw UDP client (`pfcp_assoc_verify.py`, real wire bytes, no automatic SMF-side trigger exists yet for this message). Response decoded as `AssociationUpdateResponse` (type 8), sequence number correctly echoed, `Cause=RequestAccepted`, `NodeID` byte-correct. `upf`'s own independently-generated log: `Sx Association Update accepted from 127.0.0.1` |
| Sx Association Release Request/Response | TS 29.244 §7.4.4.5/§7.4.4.6 | **Live-verified 2026-08-17**: gap-closure task #107 part 1 (ADR-0084). Same hand-crafted raw UDP client. Response decoded as `AssociationReleaseResponse` (type 10), sequence number correctly echoed, `Cause=RequestAccepted`, `NodeID` byte-correct. `upf`'s own independently-generated log: `Sx Association Release accepted from 127.0.0.1 (real, disclosed scope: this UPF's own session state for the peer is NOT bulk-deleted as a side effect...)`. Real, disclosed scope: no bulk session-state deletion on release (deferred to the still-open `SessionSetDeletion` message) |
| Sx PFD Management Request/Response (provision, per-Application-ID delete, clear-all) | TS 29.244 §7.4.3.1/§7.4.3.2 | **Live-verified 2026-08-17**: gap-closure task #107 part 2 (ADR-0086). Hand-crafted raw UDP client exercising all three real semantic paths from Table 7.4.3.1-1/7.4.3.1-2's own condition text. `upf`'s own independently-generated log confirms each: `"provisioned 2 PFD(s) for Application ID app-exampleapp"`, `"deleted all PFDs for Application ID app-exampleapp"`, `"carried no Application ID's PFDs -- cleared all provisioned PFDs"`. 7 new unit tests (`tests/conformance/test_pfcp_core.cpp`, `PfcpPfdIes.*`) cover the new `PfdContents`/`ApplicationId` codec and a grouped-IE round-trip. Real, disclosed gap: no Application Detection Filter engine yet consumes the provisioned PFDs (see ADR-0086) |
| Sx Node Report Request/Response (User Plane Path Failure Report) | TS 29.244 §7.4.5.1/§7.4.5.2 | **Live-verified 2026-08-17**: gap-closure task #107 final slice (ADR-0087). This project's strongest verification tier -- two real, independently-built processes: a hand-crafted raw UDP client posing as UPF (the real spec-mandated sender direction, §7.4.5.1.1) sent a real `NodeReportRequest` to a real, standalone `smf` process's real `PfcpPeer` socket (port 8806). `smf`'s own response decoded as `NodeReportResponse` (type 13), `Cause=RequestAccepted`, real `NodeID` present, sequence number echoed. `smf`'s own independently-generated log corroborates the exact reported peer: `"real User Plane Path Failure Report from Node ID 127.0.0.1 -- remote GTP-U peer 10.45.0.1 unreachable (real, disclosed gap: not yet acted on)"`. 4 new unit tests (`PfcpCommonIes.NodeReportType*`/`RemoteGtpuPeer*`). Real, disclosed gap: UPF's own send-side `build_node_report_request_ies` has no live caller (no GTP-U path-failure detector exists), and SMF logs but does not yet act on a received report |
| Sx Session Set Deletion Request/Response | TS 29.244 §7.4.6 | **Resolved 2026-08-17, not implemented (ADR-0087)**: Table 7.3-1's own Applicability columns mark this message pair "not applicable" to Sxc (this project's own real N4 interface) -- Sxa/Sxb (EPC) only, confirmed independently by its own IE table being entirely SGW-C/PGW-C/MME FQ-CSID concepts. A real, disclosed correction to `docs/CAPABILITY_GAP_ANALYSIS.md`'s original finding, not a deferred gap |
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

## CHF Nchf_ConvergedCharging / N40 (Phase 4 Stage 0/1, ADR-0044)

Approved scope for this turn: `Nchf_ConvergedCharging_Create` only (`POST /chargingdata`), wired to
SMF at N40 as a real client call from `CreateSMContext`'s handler. Update/Release, the
`chargingNotification` callback, `Nchf_OfflineOnlyCharging`, `Nchf_SpendingLimitControl`, and the
TM Forum SID/BSS mapping layer (needs `docs/CHARGING_MAPPING.md` first per CLAUDE.md) are
deliberately deferred to separate future turns. `Nchf_ConvergedCharging.yaml`'s real paths, and the
fact none of the three CHF-related R19 YAML files use `operationId` at all, were both confirmed
directly from the vendored spec (not assumed) -- see ADR-0044.

| Procedure | TS clause / message | Test |
|---|---|---|
| CHF -> NRF `PUT /nnrf-nfm/v1/nf-instances/{id}` (`NFType=CHF`) | TS 29.510 `Nnrf_NFManagement` | Real interop: real NRF, real HTTP 201, confirmed in `chf`'s own log (`registered with NRF (HTTP 201)`) |
| `Nchf_ConvergedCharging_Create` (`POST /chargingdata`) | TS 32.291 (`TS32291_Nchf_ConvergedCharging.yaml`) | Real interop between two independently-built processes (`smf` client, `chf` server), triggered by a real `nr-gnb`/`nr-ue` PDU Session Establishment, right after the real N4 Session Establishment call. `smf`'s log: `Nchf_ConvergedCharging_Create succeeded for pduSessionId 1, ChargingDataRef=chg-1`. Verified independently on CHF's own side, not just SMF's claim, via each NF's own Prometheus counter: `chf_charging_data_create_total{otel_scope_name="chf"} 1` and `smf_chf_charging_data_create_total{otel_scope_name="smf"} 1`. See ADR-0044 |
| `Nchf_ConvergedCharging_Release` (`POST /chargingdata/{ChargingDataRef}/release`) | TS 32.291 (`TS32291_Nchf_ConvergedCharging.yaml`) | Real interop, triggered directly against SMF's real `ReleaseSMContext` endpoint (real mTLS client cert, HTTP 204 -- no NF in this codebase auto-triggers release yet, disclosed gap, same "provide the missing upstream trigger" pattern this project's integration tests already use). `smf`'s log: `Nchf_ConvergedCharging_Release succeeded for ChargingDataRef=chg-1`. Verified independently via both NFs' own counters: `chf_charging_data_release_total{otel_scope_name="chf"} 1` / `smf_chf_charging_data_release_total{otel_scope_name="smf"} 1`. Negative path also live-verified: releasing the same `ChargingDataRef` again correctly returns 404 `ProblemDetails`, not a silent success. See ADR-0046 |

**Disclosed gaps**: no real rating/quota engine (`multipleUnitInformation` is never populated --
schema-valid, not a real charging decision); `invocationSequenceNumber` in CHF's response echoes
the request's value (no field-level spec text available to confirm the alternative); no
persistence across restarts; `pDUSessionChargingInformation` is not sent by SMF (CHF has nothing
to do with it yet); `Nchf_ConvergedCharging_Update` and the `chargingNotification` callback remain
deferred (Update has no real trigger -- `UpdateSMContext` doesn't call PCF either -- and nothing in
this codebase supplies a `notifyUri` consumer). All disclosed in `nfs/chf/src/main.cpp`'s and
ADR-0044/ADR-0046's own text.

## TM Forum SID mapping: `libs/bss-sid/` (ADR-0045)

`docs/CHARGING_MAPPING.md` (3GPP CDR field -> SID entity -> TMF API resource, required by CLAUDE.md
before any mapping code) reviewed and its two resolvable TODOs closed. Of the fields SMF's
`Nchf_ConvergedCharging_Create` call actually sends, only `subscriberIdentifier` maps to a SID
entity (network-function-provenance and protocol-bookkeeping fields don't) -- so this is the first
real, buildable slice: SUPI -> TM Forum `Party`/`Individual`.

| Procedure | TS clause / TMF resource | Test |
|---|---|---|
| SUPI -> TMF632 `Individual.individualIdentification` (`identificationType="SUPI"`) | TMF632 Party Management (`Individual`, `IndividualIdentification` -- confirmed real fields via TM Forum's own TMF632 v4.0.0 swagger) | 5 unit tests (`tests/conformance/test_bss_sid.cpp`, `BssSidParty.*`): mapping correctness, `id` deliberately left unset (no real Party store exists), JSON round-trip, `id` omitted (not emitted as `null`) when unset, `IndividualIdentification` round-trip |
| CHF builds the SID record on a real charging event | -- (this build's own integration, not a 3GPP procedure) | Real interop: a real `nr-gnb`/`nr-ue` PDU Session Establishment drives a real SUPI into CHF's `Nchf_ConvergedCharging_Create` handler; `chf`'s own log confirms the correctly-shaped output built from that real SUPI, `id` correctly absent -- `mapped subscriberIdentifier to TM Forum SID Individual: {"individualIdentification":[{"identificationId":"imsi-999700000000001","identificationType":"SUPI"}]}`. See ADR-0045 |

**Disclosed gaps**: every other row in `docs/CHARGING_MAPPING.md`'s table remains deferred (no real
3GPP data populated for those fields by any NF yet); `libs/bss-sid/`'s `Individual` models only 2 of
TMF632's ~25 real fields (the ones with real data behind them); no real TMF632 REST surface or
Party-management store exists -- the mapping is built and logged, not yet exposed or persisted. All
disclosed in `bss_sid/party.hpp`'s own header and ADR-0045.

## `bss/product-catalog`: real TMF620 Product Catalog Management (ADR-0047)

A new, standalone, non-3GPP service (own top-level `bss/` directory, no NRF registration) closing
the gap PROMPT.md's charging principles named: product/tariff definitions are now real,
configurable data (a real store behind a real API), not the empty-grant-only state CHF had since
ADR-0044. Real basePath `/tmf-api/productCatalogManagement/v4/`, fields confirmed by directly
downloading and parsing TM Forum's own TMF620 v4.1.0 swagger JSON.

| Procedure | TMF resource / operation | Test |
|---|---|---|
| `ProductOfferingPrice` Create/Get/List/Delete | TMF620, `/productOfferingPrice`, `/productOfferingPrice/{id}` | 5 unit tests (`tests/conformance/test_bss_sid_product.cpp`) for the DTO shapes, plus real mTLS `curl` interop: created a real price (5GB/month, $20 USD), server-assigned real `id`/`href` confirmed in the response |
| `ProductOffering` Create/Get/List/Delete, referencing prices by id | TMF620, `/productOffering`, `/productOffering/{id}` | Real mTLS interop: created an offering referencing the price above, listed it, retrieved it by id, deleted it (204), confirmed a second delete correctly 404s |
| mTLS enforcement | -- (transport security, not a TMF procedure) | Live-verified: a request with no client certificate at all fails outright (connection failure), not just a 401 -- proving mTLS is genuinely required, not merely configured |

**Disclosed gaps**: real TMF620 `PATCH` (update) and the `/listener/*` event-notification callbacks
are not implemented (Create/Get/List/Delete is enough to prove product/tariff data is configurable,
per the approved scope); no OAuth2 layer (mTLS-only, disclosed -- no NRF-issued token source exists
for a non-3GPP component); in-memory only, no persistence across restarts. All disclosed in
`bss/product-catalog/src/main.cpp`'s own header and ADR-0047.

## Real rating engine: CHF consults the catalog (ADR-0048)

`Nchf_ConvergedCharging_Create` no longer always returns an empty grant -- CHF is now a real HTTP
client of `bss/product-catalog`, converting real seeded catalog data into a real `GrantedUnit`.

| Procedure | TS clause / TMF resource | Test |
|---|---|---|
| `MultipleUnitUsage.ratingGroup` (mandatory field, confirmed via the real YAML's `required:` block) sent by SMF | TS 32.291 | `smf` now sends a real `multipleUnitUsage` entry on every `Nchf_ConvergedCharging_Create` call (`nfs/smf/src/main.cpp`) |
| CHF's real catalog-derived `GrantedUnit` | TS 32.291 `GrantedUnit`/`MultipleUnitInformation`, realized from TMF620 `ProductOfferingPrice.unitOfMeasure` | Real end-to-end: a real `ProductOfferingPrice` ("10GB Monthly Data", $25/month) and `ProductOffering` ("5G Standard Plan") were seeded via real mTLS `curl` calls; a real `nr-gnb`/`nr-ue` PDU Session Establishment then triggered SMF -> CHF -> product-catalog -> a real grant. `chf`'s own log: `rating engine granted 10000000000 octets from ProductOffering '5G Standard Plan' / ProductOfferingPrice '10GB Monthly Data'` (10 GB × 10^9, correctly converted). Verified at the wire level too: a direct call to CHF's real endpoint returned `{"multipleUnitInformation":[{"grantedUnit":{"totalVolume":10000000000},"ratingGroup":1}]}` in the actual HTTP response body. New `chf_rating_grant_total` Prometheus counter confirmed via direct scrape (`1`). See ADR-0048 |

**Disclosed gaps**: no real subscriber-to-product assignment (grants from whichever `Active`+
`isSellable` offering is first in the catalog, no customer/subscription store exists); unit
conversion only handles `"GB"`/`"MB"` → `totalVolume` (decimal, matching 3GPP's own octet-counting
convention), anything else falls back to `serviceSpecificUnits` unconverted; no quota consumption
tracking or re-authorization (grants once at session establishment, never checks usage against the
grant); `kDefaultRatingGroup` is a single fixed value (no real service-to-rating-group mapping,
that's TS 32.298/32.299 charging-characteristics configuration, out of scope). All disclosed in
`nfs/chf/src/main.cpp`'s `build_rating_grant` comment and ADR-0048.

## Quota consumption tracking, Stage 0: SMF's real bidirectional PFCP peer (ADR-0050)

7-stage effort (approved, staged plan), closing the real architectural gap blocking quota-
consumption tracking: SMF's old per-call ephemeral PFCP sockets could never receive an unsolicited
message from UPF at all. New `pfcp_core` IEs (Create URR, Usage Report, and children) confirmed
directly against the real, vendored `specs/PFCP/29244-e30.pdf` (§7.5.2.4, §7.5.8.3, Annex C.2.1.1).

| Procedure | TS clause / message | Test |
|---|---|---|
| PFCP IE byte layouts: `SessionReportRequest`/`Response` (56/57), Create URR (6)/Usage Report (80) grouped IEs, `URR ID`/`UR-SEQN`/`Measurement Method`/`Reporting Triggers`/`Volume Threshold`/`Volume Quota`/`Volume Measurement`/`Report Type`/`Usage Report Trigger` | TS 29.244 §7.5.2.4, §7.5.8.3, §8.2.13/19/21/40/41/44/50/54/71 | 12 new unit tests (`tests/conformance/test_pfcp_core.cpp`, `PfcpSessionIes.Urr*`/`*Volume*`/`*Report*`), byte-exact against the real spec figures |
| SMF's persistent, bidirectional PFCP peer (`nfs/smf/src/pfcp_peer.{hpp,cpp}`) -- real receive-dispatch, sequence-number-correlated responses, unsolicited-message handling | -- (this build's own architecture, not a 3GPP procedure) | Real regression check: a full real `nr-gnb`/`nr-ue` PDU Session Establishment confirmed Association Setup and N4 Session Establishment both still succeed, first attempt, on the new shared-socket code. New capability, independently verified: a hand-crafted-but-real Sx Session Report Request sent directly to SMF's new port 8806 was genuinely received and dispatched (`smf`'s log: `received real Sx Session Report Request from 127.0.0.1 (seq=12345)`) and answered with a real, correctly-typed Sx Session Report Response (message type 57) |

**Disclosed gaps**: this stage's Session Report handler is architecture-proof only -- acknowledges
unconditionally, does not parse real Usage Report content or call `Nchf_ConvergedCharging_Update`
(Stage 3), and echoes the request's own SEID rather than looking up the session's real UP-side SEID
(no per-session PFCP state store exists yet). `kSmfCpFunctionPfcpPort` (8806) is a lab-only
convention, not a real IANA/spec assignment (real deployments convey this out-of-band). Stages 1-6
(Create URR at session establishment, real UPF byte counting, real Update call, CHF's Update
endpoint, pushing a new quota back to UPF, full end-to-end live verification) remain to be built.
All disclosed in `nfs/smf/src/pfcp_peer.hpp`'s own header and ADR-0050.

**Stage 1 (2026-08-10): real Create URR provisioning.** `Nchf_ConvergedCharging_Create` moved
ahead of N4 Session Establishment (TS 29.244 Annex C.2.1.1's real credit-then-provision order);
`perform_n4_session_establishment` now provisions a real URR (URR ID referenced from the uplink
PDR, per Create PDR's own confirmed `URR ID` field, TS 29.244 §7.5.2.2) with Volume
Threshold/Quota derived from CHF's real grant (90%/100% ratio, matching Annex C.2.1.1's own
example exactly). Live-verified: a real PDU session against the real seeded 10GB/$25 plan produced
`threshold=9000000000 octets, quota=10000000000 octets` in `smf`'s log, and UPF (independently
built) accepted the Session Establishment Request containing the new IE without rejecting it. See
ADR-0050's Stage 1 section.

**Stage 2 (2026-08-10): real UPF per-TEID byte counting + real unsolicited Session Report
Request.** `nfs/upf/bpf/gtpu_decap.bpf.c` gained a real, atomically-updated (`__sync_fetch_and_add`)
per-TEID `urr_map` and a `usage_report_ringbuf`; crossing Volume Threshold/Quota fires a one-shot
(latched) usage-report event. `nfs/upf/src/datapath.{hpp,cpp}` wires the second ring buffer into
the existing poll loop via `ring_buffer__add()` and adds `register_urr()`. `nfs/upf/src/main.cpp`
parses the real Create URR (URR ID/Volume Threshold/Volume Quota) out of a Session Establishment
Request, registers it with the datapath, remembers per-TEID what a report needs (SMF's real
persistent peer endpoint from Stage 0, the session's CP F-SEID, the URR ID) in a new
`TeidSessionStore`, and builds+sends a real, spec-correct Sx Session Report Request (`ReportType`
USAR + a `UsageReport` grouped IE: URR ID/UR-SEQN/Usage Report Trigger/Volume Measurement) when the
datapath's handler fires. Two new UPF-side `pfcp_core` encoders needed for this reverse direction:
`encode_report_type_usage_report()`, `encode_usage_report_trigger_volth()`/`_volqu()` (3 new round-
trip unit tests).

| Procedure | TS clause / message | Test |
|---|---|---|
| Real per-TEID Volume measurement, one-shot Volume Threshold/Quota report triggers | TS 29.244 §7.5.8 (Session Report procedure), Annex C.2.1.1 | 25-packet real GTP-U burst (44 real T-PDU octets each) against a real, small seeded grant (1,000 octets): `upf`'s log shows exactly one VOLTH report at real total=924 octets and exactly one VOLQU report at real total=1012 octets, despite 25 packets continuing to flow past both crossings -- the latch is proven under continued traffic, not just a single-shot coincidence |
| Real, unsolicited Sx Session Report Request delivery, UP -> CP | TS 29.244 §7.2.2.3 (session-related header), §7.5.8.3 (Usage Report IE) | `smf`'s log independently confirms real receipt of both reports via Stage 0's handler: `received real Sx Session Report Request from 127.0.0.1 (seq=1)` then `(seq=2)` |

**Disclosed gaps carried forward**: TS 29.244 Annex C.2.1.1's real behaviour (UP function stops
forwarding once Volume Quota is reached, until a new quota arrives) is NOT implemented -- traffic
keeps flowing after the VOLQU report; enforcing a stop before Stage 5 exists to ever provision a
fresh quota would strand every session permanently. SMF's handler for the reports it now genuinely
receives is still Stage 0's architecture-proof-only ack -- it does not yet parse the real Usage
Report content or call `Nchf_ConvergedCharging_Update` (Stage 3, next). 140/140 tests pass, zero
regressions. See ADR-0050's Stage 2 section.

**Stage 3 (2026-08-10): SMF decodes the real Usage Report and calls
`Nchf_ConvergedCharging_Update`.** The Session Report handler now decodes `ReportType`/`UsageReport`
(`UrrId`/`UrSeqn`/`UsageReportTrigger`/`VolumeMeasurement`), resolves the report's header SEID back
to its session via a new `CpSeidSessionStore` (SUPI + `ChargingDataRef`, keyed by the real `cp_seid`
`perform_n4_session_establishment` now returns), and calls a new `perform_n40_charging_data_update`
-- a real `POST /chargingdata/{ChargingDataRef}/update` (confirmed against the vendored
`TS32291_Nchf_ConvergedCharging.yaml`) carrying the real consumed octets in
`multipleUnitUsage[0].usedUnitContainer[0].totalVolume`. Runs on a dedicated
`chf_report_client`/`chf_report_oauth` pair (PfcpPeer's own thread, not the route handlers' `ioc`
thread).

| Procedure | TS clause / message | Test |
|---|---|---|
| Real Usage Report decode + session resolution + `Nchf_ConvergedCharging_Update` call | TS 29.244 §7.5.8.3 (Usage Report IE); TS 32.291 `POST /chargingdata/{ChargingDataRef}/update` | Same small-seeded-grant (1,000 octets) 25-packet real GTP-U burst as Stage 2: `smf`'s log shows `received real Sx Session Report Request ... (seq=1)` immediately followed by `CHF Nchf_ConvergedCharging_Update returned status 404 ...` (expected -- CHF has no Update handler yet), then the same pair for `(seq=2)`. `chf`'s own log/generic server confirms the request genuinely arrived (a real unregistered-route 404, not a connection failure) |

**Disclosed gaps carried forward**: `perform_n40_charging_data_release` still hardcodes
`invocationSequenceNumber=2` rather than sharing `CpSeidSessionStore`'s real per-session counter --
a real TS 32.291 "strictly increasing per invocation" violation if both an Update and a Release land
on the same `ChargingDataRef`; pre-existing, out of this stage's scope, flagged not fixed. CHF still
has no real Update endpoint (Stage 4, next) -- every Update call in this stage's live verification
legitimately 404s. 140/140 tests pass, zero regressions. See ADR-0050's Stage 3 section.

**Stage 4 (2026-08-10): CHF implements the real `Nchf_ConvergedCharging_Update` endpoint.**
`POST /chargingdata/{ChargingDataRef}/update` -- validates the ref is still active (non-destructive
`ChargingDataStore::is_active()`, new for this stage), logs the real reported usage, and
re-authorizes via the same `build_rating_grant` catalog-lookup engine Create already uses, returning
a fresh `GrantedUnit` with HTTP 200.

| Procedure | TS clause / message | Test |
|---|---|---|
| Real `Nchf_ConvergedCharging_Update`: reported-usage receipt + re-authorization grant | TS 32.291 `POST /chargingdata/{ChargingDataRef}/update` | Same small-seeded-grant (1,000 octets), real `nr-gnb`/`nr-ue`, 25-packet GTP-U burst as Stages 2-3: `chf`'s log shows `Update for ChargingDataRef=chg-1 reports ratingGroup=1 used 924 octets (localSequenceNumber=1)` immediately followed by a real fresh `rating engine granted 1000 octets`, then the same pair for `used 1012 octets (localSequenceNumber=2)`; `smf`'s log independently confirms real success (not Stage 3's 404): `Nchf_ConvergedCharging_Update succeeded for ChargingDataRef=chg-1, reported 924 octets used` then `... reported 1012 octets used` |

**Disclosed simplifications, not fixed by this stage**: no balance/wallet deduction against
already-consumed usage (no such store exists, `docs/CHARGING_MAPPING.md`'s TMF654 gap); no
differentiation between a Volume-Threshold report and a Volume-Quota-exhaustion report (SMF doesn't
forward that distinction as a real `Trigger` either). 140/140 tests pass, zero regressions. See
ADR-0050's Stage 4 section.

**Consequence**: the full quota-consumption-tracking loop (UPF measures → reports → SMF decodes →
CHF re-authorizes) is real end to end. Stage 5 (SMF pushes the new quota back to UPF via Session
Modification) and Stage 6 (dedicated full-stack live-verification pass) remain.

**Stage 5 (2026-08-10): SMF pushes the re-authorized quota to UPF via real Session Modification.**
Real Update URR IE (type=13, TS 29.244 Table 7.5.4.4-1). New absolute Volume Threshold/Volume Quota
computed relative to the report's own real cumulative Volume Measurement (`+0.9*grant`/`+grant`),
applied to UPF's BPF map via a real read-modify-write (`Datapath::update_urr_thresholds`) that
preserves `total_octets` while resetting only the report latches. The CHF-Update-then-Session-
Modification work is dispatched from the Session Report handler onto a detached thread -- calling
`send_request_and_await_response` directly from `PfcpPeer`'s own receive thread (where the handler
runs) would deadlock against the very thread that must deliver the Modification's response.

| Procedure | TS clause / message | Test |
|---|---|---|
| Real Session Modification: Update URR encode/decode, real read-modify-write applying it, correct UP→CP response addressing | TS 29.244 §7.5.4 (Sx Session Modification Request), §7.5.4.4 (Update URR IE) | Same small-seeded-grant (1,000 octets), real `nr-gnb`/`nr-ue`, 25-packet GTP-U burst as Stages 2-4: `smf`'s log -- `Nchf_ConvergedCharging_Update succeeded ... reported 924 octets used, re-authorized 1000 octets` then `N4 Session Modification succeeded for URR 1 (UP F-SEID=0x1): threshold=1824 octets, quota=1924 octets` (924+900/924+1000, confirming the computation); `upf`'s log independently confirms the identical values applied to its own map (`applied Update URR for TEID 0x1: threshold=1824 quota=1924 octets`). Repeated correctly for the second (VOLQU) report (`threshold=1912 quota=2012`) |

**Real, disclosed finding from live verification, not a bug**: the VOLQU report fired ~100ms after
VOLTH, before the first re-authorization's real CHF-call-plus-PFCP-round-trip (~101ms) could land --
exactly the race TS 29.244 §5.2.2.2.1 NOTE 3/4 itself describes (the Threshold/Quota gap exists to
give the OCS round-trip time to complete). This test's artificially tiny 1,000-octet quota (needed
for practical live verification) left far too little real headroom (~2 packets) for a real ~100ms
round trip; a real deployment sizes this gap in megabytes for exactly this reason. Both
Modifications still landed and were applied correctly, just after momentary quota overrun (traffic
was never stopped either way -- Stage 2's own disclosed gap). 140/140 tests pass, zero regressions.
See ADR-0050's Stage 5 section.

**Consequence**: the full quota-consumption-tracking loop, including the feedback path back into
the datapath, is real end to end. Stage 6 (dedicated, larger-quota full-stack live-verification pass
plus documentation summary) remains.

**Stage 6 (2026-08-10): dedicated end-to-end live verification with real headroom, effort closed.**
No new code. A real 200,000-octet grant (threshold=180,000) sent as 260 real, MTU-sized (~1400-byte)
GTP-U G-PDUs, 30ms apart -- a real ~420ms Threshold/Quota window, several times Stage 5's own
measured ~100ms round-trip latency.

| Procedure | TS clause / message | Test |
|---|---|---|
| Full quota-consumption-tracking loop under real timing headroom (no race) | TS 29.244 §5.2.2.2.1 NOTE 3/4 (the Threshold/Quota gap's real design intent) | `upf`'s log: real VOLTH at total=180,600 -> real re-authorization landed 104ms later (`threshold=360600 quota=380600`) -> **zero VOLQU reports at any point** (traffic passed the original 200,000-octet quota mark because the datapath's map already held the new values in time) -> a second real VOLTH at total=361,200 (matching the new threshold almost exactly) -> re-authorized again. All 260 T-PDUs delivered uninterrupted; `smf`/`chf` logs independently confirm both real Update-then-Modification cycles |

140/140 tests pass -- final full-suite run for this effort, zero regressions across all 6 stages.

**Real, disclosed gaps still standing at the close of this effort** (none silently dropped): UPF
never stops forwarding on Volume Quota exhaustion (Stage 2); CHF applies no real balance/wallet
deduction, re-granting the same catalog amount unconditionally rather than a remaining-balance-aware
one (Stage 4); neither SMF nor CHF differentiate a Volume-Threshold report from a
Volume-Quota-exhaustion one via a real `Trigger` (Stage 3/4); `perform_n40_charging_data_release`
still hardcodes its own `invocationSequenceNumber` (Stage 3); `kSmfCpFunctionPfcpPort` remains a
lab-only convention, not an IANA/spec assignment (Stage 0). See ADR-0050's Stage 6 section for the
complete list and reasoning.

**Consequence**: ADR-0048's quota-consumption-tracking/re-authorization gap is closed end to end --
UPF measures, reports, SMF calls CHF, CHF re-authorizes, SMF pushes the new quota back into the live
datapath -- demonstrated both under real timing pressure (Stage 5) and with real headroom (this
stage). This closes the 7-stage effort (ADR-0050).

## Pending-items cleanup: real per-ChargingDataRef invocation sequencing + shared HTTP client concurrency bug (ADR-0051)

`ChargingDataInvocationSeqStore` (keyed by `charging_data_ref`) replaces the two inconsistent
invocation-sequence mechanisms `Nchf_ConvergedCharging_Create`/`_Update`/`_Release` used to have.
Also fixed, found only via live-testing this change: `sbi_core::http2::Client` had no
synchronization around its single reused libcurl easy handle, and ADR-0050 Stage 5's detached-
thread design was the first caller in this codebase to give one `Client` instance two genuinely
concurrent callers -- a real `std::mutex` added to `Client::send()` fixes it for every caller.

| Procedure | TS clause / message | Test |
|---|---|---|
| Real, non-colliding `invocationSequenceNumber` across Create → Update → Update → Release for one `ChargingDataRef` | TS 32.291 (strictly increasing per invocation) | Real `nr-gnb`/`nr-ue` session, small seeded grant, 25-packet GTP-U burst producing two Session Reports 100ms apart: both real `Nchf_ConvergedCharging_Update` calls succeed (previously one failed under the concurrency bug), both Session Modifications land; a direct `POST /sm-contexts/{ref}/release` returns a real 204 with `smf`'s log confirming `Nchf_ConvergedCharging_Release succeeded` and no fallback-counter warning |
| `sbi_core::http2::Client` thread-safety under genuine concurrent callers | -- (foundational library fix, not a 3GPP procedure) | Same live run above: the exact concurrent-Update scenario that previously failed with a libcurl "Failed initialization"/empty-response error now succeeds for both calls |

140/140 conformance tests + all 31 integration tests (run directly; not currently registered with
`ctest` -- a separate, smaller pending item noted by the same audit, not fixed here) pass, zero
regressions. See ADR-0051.

## sbi-codegen: real cyclic-schema back-edge fix, std::shared_ptr indirection (ADR-0052)

`tools/sbi-codegen`'s `render.py` now detects, per-field, whether a referenced type is both part
of a real 3GPP schema cycle (`SharedData.sharedAmData` <-> `AccessAndMobilitySubscriptionData.
sharedDataList`, already documented in ADR-0022) and not yet complete at that field's own
position in emission order -- the actual "back-edge" of the cycle. That one field now generates
as `std::shared_ptr<T>` instead of direct `std::optional<T>`/`std::vector<T>` embedding, which
cannot work for a genuine 2-struct cycle regardless of declaration order. New
`sbi_core::put_optional`/`get_optional` overloads for `std::shared_ptr<T>` keep the existing
`source.cpp.j2` template's (de)serialization call sites unchanged.

| Procedure | TS clause / message | Test |
|---|---|---|
| Real cyclic-schema field correctly compiles under both GCC (build/sanitize) and Clang (lint/clang-tidy) | TS 29.503 (`SharedData`/`AccessAndMobilitySubscriptionData`, Nudm_SDM) | Full corpus regenerated (1917 types -> 42 files, unchanged counts), full project rebuild with GCC: 140/140 conformance + 31/31 integration tests pass. Direct `clang++-18 -fsyntax-only` compile of a minimal translation unit constructing both types: clean, zero errors -- confirms the fix at the exact previous failure point without waiting for a full whole-tree `clang-tidy` pass |

**Disclosed, not fixed**: a *required* field that's also a cyclic back-edge would need different
(de)serialize codegen (enforcing presence, not silently-absent) that doesn't exist yet -- no real
instance of this exists in the current R19 corpus, but `render.py` now raises `NotImplementedError`
with a clear message if one is ever found, rather than silently generating untested code. See
ADR-0052.

## `bss/subscriber-management` + `bss/roaming-interconnect`: real BSS layer REST services (P4.7, ADR-0066)

Two new standalone HTTP/2 services closing the disclosed "store library only, no HTTP/REST service
yet" gap both services' `schema.sql` carried since ADR-0060. Real basePaths confirmed directly
against TM Forum's own public swagger, fetched live (not recalled from memory).

| Procedure | TMF resource / operation | Test |
|---|---|---|
| `Individual`/`Organization` Create/Get/List | TMF632 Party Management, `/tmf-api/party/v4/individual`, `/organization` | Real mTLS `curl` interop: created a real Individual (Ada Lovelace) and Organization (Acme Enterprise), listed and fetched both by real server-assigned id |
| `Account`/`Subscriber` Create/Get/List, `Subscriber` by-SUPI lookup | Project-internal (E10/E1), `/bss-api/subscriberManagement/v1/account`, `/subscriber`, `/subscriber/by-supi/{supi}` | Real mTLS `curl` interop: created an ENTERPRISE Account referencing the real Organization above (real FK constraint enforced, confirmed by a real constraint-violation error caught and fixed in the integration test), created a Subscriber with a real SUPI, fetched it back both by id and by SUPI |
| `InterconnectAgreement` Create/Get/List | TMF651 Agreement Management, `/tmf-api/agreementManagement/v4/agreement` | Real mTLS `curl` interop: created a real roaming interconnect deal (partner PLMN 31026, rate terms), listed and fetched it by real server-assigned id |
| mTLS enforcement, `ProblemDetails` 404 | -- (transport security / error shape, not a TMF procedure) | Live-verified: `GET` for a nonexistent Individual returns a real `{"status":404,"title":"Not Found","detail":"..."}` |
| Real PostgreSQL persistence, both services | -- | 6 new integration tests (`tests/integration/test_subscriber_management_postgres.cpp`, `test_roaming_interconnect_postgres.cpp`), real cross-process re-derivation (independent `pqxx::connection` per store instance sees the same row), all pass against fresh real Postgres containers; CI extended with two new postgres service containers + schema-apply steps |

**Disclosed gaps**: PATCH/DELETE not implemented (same disclosed narrowing `bss/product-catalog`
already used); no NRF registration/OAuth2 (mTLS-only, same reasoning as every other `bss/*`
component); `Subscriber`'s real trigger (an NF actually calling this service from a real SUPI
lookup) doesn't exist yet; `RoamingCdrFileStore` still has no HTTP route -- real GSMA CDR ingestion
remains P4.11's own blocked scope; no Helm charts for either service (consistent with this
project's actual established deploy-verification bar -- Docker + compose, live-verified -- not
Helm, which has never itself been verified for anything in this repo). All disclosed in each
service's own `src/main.cpp` header and ADR-0066. This file itself remains stale for ADR-0053
through ADR-0065 -- a real, separate, disclosed backlog item, not backfilled by this entry.

## `libs/tap3-core`: real GSMA TAP3 roaming CDR codec, all 9 `CallEventDetail` variants (P4.11, ADR-0067)

Hand-rolled BER codec (real transfer syntax per TAP-SPEC.pdf section 6.2) against the real TAP3
ASN.1 module (TAP-SPEC.pdf section 6.1) -- GSMA member-confidential source, no spec-derived file
committed, real cited field names/tag numbers only, same discipline as TCAP/MAP/CAP.

| Procedure | Real ASN.1 type / tag | Test |
|---|---|---|
| Envelope: `DataInterchange` -> `TransferBatch`/`Notification` -> `BatchControlInfo`/`AccountingInfo`/`NetworkInfo`/`AuditControlInfo` | `[APPLICATION 1]`/`[2]`/`[4]`/`[5]`/`[6]`/`[15]` | `Tap3Envelope.*` (9 tests), `tests/conformance/test_tap3_core.cpp` |
| `MobileOriginatedCall` | `[APPLICATION 9]` | `Tap3MoCall.*` (6 tests) |
| `MobileTerminatedCall` | `[APPLICATION 10]` | `Tap3MtCall.FullMobileTerminatedCallRoundTrips` |
| `SupplServiceEvent` | `[APPLICATION 11]` | `Tap3SupplService.FullSupplServiceEventRoundTrips` |
| `ServiceCentreUsage` | `[APPLICATION 12]` | `Tap3Scu.*` (2 tests) |
| `GprsCall` | `[APPLICATION 14]` | `Tap3GprsCall.FullGprsCallRoundTrips` (exercises the real 8-byte-INTEGER `ChargingId`/`DataVolumeIncoming`/`DataVolumeOutgoing` path) |
| `ContentTransaction` | `[APPLICATION 17]` | `Tap3ContentTransaction.FullContentTransactionRoundTrips` |
| `LocationService` | `[APPLICATION 297]` | `Tap3LocationService.FullLocationServiceRoundTrips` |
| `MessagingEvent` | `[APPLICATION 433]` | `Tap3MessagingEvent.FullMessagingEventRoundTrips` |
| `MobileSession` | `[APPLICATION 434]` | `Tap3MobileSession.FullMobileSessionRoundTrips` |
| `AggregatedUsageRecord` | `[APPLICATION 453]` | `Tap3AggregatedUsage.FullAggregatedUsageRecordRoundTrips` |
| `CallEventDetailList` real tag-dispatch across all 9 variants | untagged CHOICE, TAP-SPEC.pdf p.257 | `Tap3Envelope.CallEventDetailListDispatchesAllRealVariants` |
| `RoamingCdrFileStore` wiring (`format="TAP3"`, real encode/decode) | -- (project-internal storage shape) | `RoamingInterconnectTap3.*` (2 tests), `tests/conformance/test_roaming_interconnect_tap3.cpp` |

**Disclosed gaps**: RAP/NRTRDE remain fully unsupplied (still `"STUB"`); live wiring from CHF's
real CDR data into a real outbound TAP3 file is separate, later work -- this codec proves the
byte-level format is real and correct, not yet fed by a live production data path;
`TransferBatch.messageDescriptionInfo`, `AuditControlInfo.totalAdvisedChargeValueList`, and
`MobileOriginatedCall`'s own `BasicServiceUsed.chargeInformationList`/
`CamelServiceUsed.{taxInformation,discountInformation}` remain deferred (real, cited, declared
field order not confirmed for the last three); `AggregatedUsageRecord.operatorSpecInformation`'s
real list-type-name is flagged, not resolved. All disclosed in `libs/tap3-core`'s own header
comments and ADR-0067. No genuine external TAP3 sample file exists to cross-check against without
violating the same GSMA confidentiality boundary this codec was built to respect -- verification
is internal round-trip correctness plus per-field tag citations, not conformance against a real
third-party file.

## `nfs/udr`: real PostgreSQL persistence (gap-closure Tier 1a, ADR-0068)

A real, three-way source comparison against free5GC and open5gs (both have genuinely persistent
UDR backends -- MongoDB in both cases) found this project's own UDR was in-memory only. Closed by
replacing `std::unordered_map` with real `pqxx::connection`-backed PostgreSQL storage.

| Procedure | Real requirement | Test |
|---|---|---|
| `CreateAmfContext3gpp`/`QueryAmfContext3gpp` (PUT/GET), real persistence | data must survive a process restart, per both real references' own architecture | `UdrIntegration.AmfContextLifecycle` (`tests/integration/test_udr_context_data.cpp`, pre-existing, re-verified unchanged against real Postgres); manual real restart-survival check this ADR's own verification section describes (`kill -9` + fresh process + `GET` returns the same real data) |
| `CreateOrUpdateSmfRegistration`/`QuerySmfRegistration`/`QuerySmfRegList`/`UpdateSmfContext`/`DeleteSmfRegistration`, real persistence | same | `UdrIntegration.SmfRegistrationLifecycle` |
| 404/401 error handling unchanged by the storage-layer swap | -- | `UdrIntegration.MissingResourceIs404AndTamperedTokenIs401` |

**Disclosed gaps**: UDM's `GetAmData`/`GetSmfSelData`/`GetSmData` still don't call this now-real
UDR (Tier 1b, separate turn). No MongoDB-specific behavior (transactions, replica sets) was
replicated -- only the real persistence property both references share, via this project's own
mandated PostgreSQL stack.

## `nfs/udm` <-> `nfs/udr`: real Nudm_SDM provisioned-data wiring (gap-closure Tier 1b, ADR-0069)

open5gs's own UDM (`src/udm/nudr-build.c`/`nudr-handler.c`) genuinely fetches subscriber data from
UDR; this project's UDM previously always returned an empty stub. Closed by adding UDR's real
`provisioned-data` group (GET-only per spec, seeded at startup) and wiring UDM's three SDM GET
handlers to real `Nudr_DataRepository` calls.

| Procedure | Real requirement | Test |
|---|---|---|
| `GetAmData`/`GetSmfSelData`/`GetSmData` return real, non-empty data for a real subscriber | real cross-NF SBI call to UDR, not a stub | `UdmIntegration.SdmDataRetrievalAndSubscriptions` (updated to spawn a real `udr` process and assert real `nssai` content); manual real end-to-end `curl` verification this ADR's own section describes |
| A genuinely unprovisioned SUPI returns real `404`, not a schema-valid empty body | -- | same test, added case for `imsi-999999999999999` |
| UDR's own real `provisioned-data` GET routes | TS29505_Subscription_Data.yaml `/subscription-data/{ueId}/{servingPlmnId}/provisioned-data/*` | exercised transitively by the same `UdmIntegration.SdmDataRetrievalAndSubscriptions` test (no separate UDR-side test file -- this group has no create/update operation to test independently of a real consumer) |

**Disclosed gaps**: `provisioned-data` remains seed-data-only (spec is GET-only, no live
provisioning path); `SmfSelectionSubscriptionData.subscribedSnssaiInfos`/
`SessionManagementSubscriptionData.dnnConfigurations` (`OPAQUE FALLBACK` codegen types) left
unpopulated, real nested shape not confirmed with confidence in the time available; UDM's own
UECM registration stores remain NOT wired to UDR (separate, still-open gap).

## `libs/aka-crypto/suci`: real SUCI de-concealment (ECIES Profile A/B, gap-closure Tier 1c, ADR-0070)

Both free5GC and open5gs genuinely decrypt SUCI to recover SUPI; this project's UDM/AUSF
previously passed `supiOrSuci` straight through untouched. Closed with a real ECIES Profile A
(Curve25519)/Profile B (secp256r1) implementation per TS 33.501 (`specs/TS_33_501.pdf` V19.6.0,
Annex C), independently verified against the spec's own real, officially-published test vectors.

| Procedure | Real requirement | Test |
|---|---|---|
| `deconceal_profile_a`/`deconceal_profile_b` recover the real spec plaintext | TS_33_501.pdf Annex C.4.3.1/C.4.3.2 (Profile A, IMSI/NAI), C.4.4.1/C.4.4.2 (Profile B, IMSI/NAI) -- all four real, officially-published test vectors | `Suci.ProfileADeconcealsRealImsiTestVector`, `Suci.ProfileADeconcealsRealNaiTestVector`, `Suci.ProfileBDeconcealsRealImsiTestVector`, `Suci.ProfileBDeconcealsRealNaiTestVector` (`tests/conformance/test_suci.cpp`) |
| A tampered MAC-tag / too-short input fails closed (no partial plaintext) | real ECIES authenticated-encryption guarantee | `Suci.ProfileARejectsTamperedMac`, `Suci.ProfileARejectsTooShortSchemeOutput` |
| UDM's `GenerateAuthData` recovers the real SUPI from a real `suci-`-formatted id | TS29571_CommonData.yaml's own `SupiOrSuci` pattern (IMSI-type form) | manual real end-to-end `curl` verification against a real running UDM (ADR-0070's own section): `suci-0-274-012-0000-1-1-<scheme output>` -> `imsi-274012001002086`, exact match; tampered MAC -> real 400; existing plain-SUPI passthrough re-verified unchanged |

**Disclosed gaps**: NAI/GCI/GLI-based SUCI (`supiType` 1-7) parsing not implemented (real, cited,
deferred -- see ADR-0070); Home Network private key is real spec test-vector material reused as
lab-only key material, no real production key-provisioning path exists.

## `nfs/upf`: real QER/BAR enforcement + full Sx session management (gap-closure Tier 1d, ADR-0071)

Both free5GC and open5gs genuinely enforce QER (gate/MBR) and handle Session Deletion; this
project's UPF previously had neither -- Session Deletion Request fell into the dispatch loop's
catch-all and never responded at all. Closed with real QER/BAR IE codecs (TS 29.244), real eBPF/XDP
gate+token-bucket enforcement, and a real Session Deletion handler with full datapath teardown.

| Procedure | Real requirement | Test |
|---|---|---|
| Create QER (Gate Status + MBR) enforced by the real XDP datapath | TS 29.244 Table 7.5.2.5-1, §8.2.7/§8.2.8 | `PfcpSessionIes.GateStatus*`/`Mbr*` (`tests/conformance/test_pfcp_core.cpp`); live: 8kbps MBR session, 40-packet burst delivered exactly 22/40 (token-bucket arithmetic exact match) |
| Update QER (Conditional Gate Status/MBR) does a real read-modify-write, not a blind overwrite | TS 29.244 Table 7.5.4.5-1 | live: UL gate close (0/1 delivered) then reopen (1/1 delivered), each via a real Update QER carrying only the changed field |
| Remove QER is real and idempotent | TS 29.244 Table 7.5.4.9-1 | live: `RemoveQer` on an already-removed TEID returns `Cause::RequestAccepted`, not a failure |
| Create/Update/Remove BAR parsed and acknowledged (PFCP-level only, disclosed no-downlink-datapath scope) | TS 29.244 Table 7.5.2.6-1/7.5.4.11-1/7.5.4.12-1, §8.2.57 | `PfcpSessionIes.BarIdRoundTripsAsSingleOctet`; live: `Create/Update/RemoveBar` all return `Cause::RequestAccepted` |
| Session Deletion tears down all datapath state and emits a real final Usage Report | TS 29.244 §7.5.6/§7.5.7, Table 7.5.7.2-1 (Usage Report IE **type=79**, distinct from Session Report's type=80) | live: post-deletion packet on the same TEID is not decapsulated; `SessionDeletionResponse` carries a real type-79 grouped IE with TERMR trigger and correct `total_octets` |
| A re-sent/unknown SEID on Session Modification or Deletion is correctly rejected | TS 29.244 Table 8.2.1-1, Cause value 65 "Session context not found" | live: replayed Session Deletion on an already-deleted SEID returns `Cause::SessionContextNotFound` (65) |

**Real bug found and fixed via live testing (not by inspection)**: `SeidToTeidStore` was only
populated for sessions that also provisioned a URR -- a QER-only session could establish but then
never be found by SEID for any later Modification/Deletion. Fixed by decoupling SEID registration
(now unconditional on F-TEID allocation) from `TeidSessionStore` registration (still scoped to
real URR-bearing sessions, its own genuine purpose).

**Disclosed gaps**: no real downlink QoS/buffering enforcement (DL Gate Status/MBR stored but
unused; BAR has no real datapath to apply to) -- both need a downlink datapath this project
doesn't have yet (same NGAP PDU Session Resource Setup dependency already disclosed throughout
Phase 3). One QER per TEID (real spec allows several per PDR), same narrowing already established
for URR. No per-IE `Failed Rule ID` on partial Modification failure (all-or-nothing Cause).

## `nfs/pcf`/`nfs/udr`/`nfs/chf`: real N28 end-to-end + N40/N28 product-configurability (ADR-0072)

`Nchf_SpendingLimitControl`'s HTTP handlers existed only on the CHF side (real Subscribe/Update/
Unsubscribe CRUD) with zero PCF-side consumption, zero SMF/PCF/CHF/UDR integration, and CHF's own
`currentStatus` hardcoded to `"unknown"` for every policy counter. Separately, CHF's rating engine
picked the first Active/isSellable `ProductOffering` regardless of the request's own `ratingGroup`
-- real per-product differentiation had never worked. Both closed this pass.

| Procedure | Real requirement | Test |
|---|---|---|
| UDR real `/policy-data/ues/{ueId}/sm-data` (GET + RFC 7396 merge-patch PATCH, upsert-capable) | TS29519_Policy_Data.yaml, full real nested `SmPolicyData -> SmPolicySnssaiData -> SmPolicyDnnData` shape | `UdrSmPolicyDataIntegration.PatchCreatesAndMergesRealNestedDocument`; live: 404-before-create, real persisted create, real partial merge preserving earlier fields |
| PCF fetches UDR's `SmPolicyDnnData` and opens a real CHF `Nchf_SpendingLimitControl` subscription when `subscSpendingLimits=true` | TS29594_Nchf_SpendingLimitControl.yaml Subscribe; real "CHF hosts, PCF subscribes" architecture (ADR-0055) | live: `pcf: opened real CHF spending-limit subscription sub-N for SM policy smpolicy-1`; real CHF unsubscribe on `DeleteSMPolicy`, confirmed via direct Redis inspection |
| PCF/UDR/CHF calls fail open (unreachable dependency doesn't block the SM Policy request) | real, disclosed design choice -- spending-limit infra being down shouldn't block a PDU session | `PcfN28Integration.CreateSmPolicyFailsOpenWhenChfUnreachable` (CHF deliberately not running, real 201 still returned) |
| CHF real, configurable `PolicyCounterInfo.currentStatus` + real `statusNotification` push | TS29594 Table (currentStatus is real, operator-defined per the spec's own text); real callback URL `{notifUri}/notify` (TS29594's own callback key construction) | live: `PUT /chf-admin/v1/policy-counters/{id}` real status change correctly pushed to PCF's real callback route (`pcf: received real spending-limit statusNotification...`); a follow-up subscription GET confirmed `currentStatus` genuinely reflects the configured value |
| CHF real ratingGroup-matched product selection + real quota-policy fields | TS 32.291 `MultipleUnitInformation.validityTime/quotaHoldingTime/*QuotaThreshold`; TMF620 `prodSpecCharValueUse` as the real extension point | live: two real, distinct product tiers (Consumer ratingGroup=100/1GB, Enterprise ratingGroup=200/100GB) produced genuinely distinct real grants (1,000,000,000 vs. 100,000,000,000 octets) from two real `Nchf_ConvergedCharging_Create` calls differing only in `ratingGroup` |

**Real infrastructure bug found and fixed**: `tools/sbi-codegen/generate.py` only ever writes
generated files, never deletes stale ones from a prior pilot-file-list configuration -- adding
`TS29519_Policy_Data.yaml` as a pilot file moved several existing types into a different SCC
output group, and the old, now-stale per-file headers survived alongside the new ones, causing a
real "redefinition of struct" build failure. Fixed at the root (`file(REMOVE_RECURSE)` before every
codegen invocation in `libs/sbi-generated/CMakeLists.txt`), not worked around.

**Real bug found and fixed via live testing**: this pass's own new integration test initially used
a fixed, hardcoded test SUPI and asserted an initial 404 -- broke on any re-run against the same
long-lived UDR Postgres database (real persistence, by design, ADR-0068) once the row already
existed from a prior run. Fixed with a real per-process/per-call-site-unique test SUPI.

**Disclosed gaps**: CHF was not yet part of this project's automated `ctest` suite as of this ADR
(needs Redis/ClickHouse, neither provisioned in `.github/workflows/ci.yml` at the time) -- closed
in ADR-0073, see below. Automated PCC/session-rule re-decisioning from a pushed policy-counter status
not implemented (3GPP leaves the status-to-action mapping operator-defined). `subscriptionTermination`
(the other real TS29594 callback) not implemented. AM-policy-side/UE-policy-side spending limits
(`AmPolicyData`/`UePolicySet`, distinct real TS29519 resources) out of scope -- only the SM-policy
variant PCF's already-built surface needs was built.

## CHF-in-CI (Redis/ClickHouse/its own Postgres) + real full N28 loop as an automated test (ADR-0073)

ADR-0072 disclosed CHF as never having been a participant in this project's automated `ctest`
suite -- it needs Redis, ClickHouse, and its own PostgreSQL (`chf_rating`), none of which
`.github/workflows/ci.yml` provisioned. As a direct consequence, the real full N28 loop (subscribe
-> status change -> `statusNotification` push -> receipt -> unsubscribe) that ADR-0072
live-verified manually was never covered by an automated test.

| Procedure | Real requirement | Test |
|---|---|---|
| CHF reachable in CI: real `postgres-chf`, `redis`, `clickhouse` services + schema-apply steps | `nfs/chf/schema.postgres.sql`, `nfs/chf/schema.clickhouse.sql`; real `getenv` names from `nfs/chf/src/main.cpp` (`CHF_RATING_DATABASE_URL`, `CHF_REDIS_URL`, `CHF_CLICKHOUSE_*`) | `.github/workflows/ci.yml` (`build` + `sanitize` jobs); YAML-syntax-validated; not yet exercised by a real GitHub Actions run |
| Full real UDR->PCF->CHF->statusNotification->PCF loop, automated | TS29594_Nchf_SpendingLimitControl.yaml Subscribe + real callback push (same procedures as ADR-0072's manual verification) | `PcfChfN28Integration.FullLoopSubscribeStatusChangeNotifyUnsubscribe` -- real subscribe (`pcf_spending_limit_subscribe_total` increments), real CHF admin status change, real notify receipt (`pcf_spending_limit_notify_total` increments), real unsubscribe; passes |

**Real bug found and fixed via live testing**: the new test's raw-socket Prometheus `/metrics`
scraper (`sbi_core::http2::Client` is TLS/mTLS-only and cannot hit the deliberately plain-HTTP
metrics endpoint) originally used `body.find(metric_name)`, which matches the `# HELP` comment
line's own free-text (it contains the metric name too) rather than the real value line -- silently
returning -1 forever. Fixed by anchoring the search to the start of the real value line
(`"\n<metric_name> "` / `"\n<metric_name>{"`). Confirmed the real underlying system was correct
throughout via live process log lines, independent of the test's own (buggy, then fixed) assertion.

**Disclosed**: a full local `ctest -j4` run reached 291/293 before being stopped --
`SubscriberManagementPostgresTest.SubscriberIsFindableBySupi` (pre-existing pollution, ADR-0072's
own disclosed follow-up, not fixed here) and `UdmIntegration.SdmDataRetrievalAndSubscriptions`/
`UdrIntegration.AmfContextLifecycle` (pre-existing environmental flakiness, ADR-0071/-0072) are
not new regressions from this ADR. Not yet exercised: a real GitHub Actions run of the new CI
wiring.

## P4.8 Stage 2a -- AI-native CHF online path, predictive quota sizing (ADR-0074)

Real ONNX Runtime in-process C++ inference for predictive quota sizing (CHARGING_PROMPT.md Angle
1a), the first of P4.8's six named capabilities, plus the shared substrate (latency budget, kill
switch, governance logging, MLflow-versioned Python training sidecar) the other five will reuse.

| Procedure | Real requirement | Test |
|---|---|---|
| `AiQuotaSizer`: kill switch, cold-start/missing-model handling, real ONNX inference | CHARGING_PROMPT.md Section B governance rules ("charging must never block on a model") | `tests/conformance/test_ai_inference.cpp`, 6 tests: disabled/no-path/missing-file all correctly disabled; real model loads and predicts an exactly-known value; model-version sidecar file read correctly |
| `build_rating_grant`: AI-predicted usage -> deterministic `[0.5x, 2.0x]` clamp -> bounded grant adjustment | "This model informs the decision. The deterministic rating engine makes it." (CHARGING_PROMPT.md Section B) | live: cold-start Create/Update #1 unaffected (exactly 1,000,000,000 octets); history-informed Update #2 = 975,951,616 octets (0.976x, unclamped); a rising-usage scenario produced a real upward adjustment (1.731x, 1,731,488,000 octets) |
| Governance logging: model id/version, feature vector, predicted usage, applied multiplier, clamp state | `rating_decision.ai_advisory` (reserved since ADR-0049), `audit_record.ai_advisory_ref` | live: direct Postgres query confirmed `ai_advisory` populated with the real MLflow run id and full real feature vector for an AI-adjusted decision, and correctly absent (NULL) for the two cold-start decisions in the same session |

**Real bugs found and fixed via live testing, not assumed correct**:
1. MLflow 3.x's filesystem tracking store (`file://./mlruns`) is now in maintenance mode and
   refuses writes -- fixed with a local SQLite backend (`sqlite:///mlflow.db`).
2. The synthetic bootstrap dataset's feature ranges (`[1e6, 5e8]` octets) were far below a
   realistic GB-scale base grant (`1e9`) -- caught because a live 1GB test scenario produced a
   materially-shrunk grant (0.518x) that traced back to the model extrapolating outside its
   training range. Fixed by widening the ranges to `[1e7, 1e10]`.
3. ONNX Runtime requires exactly ONE `Ort::Env` per process -- the first version constructed one
   per `AiQuotaSizer`, invisible in production CHF (one instance) but breaking immediately in the
   5-instance unit test with real "Schema error: already registered" failures. Fixed with a
   function-local-static `Env` singleton.
4. A deeper vcpkg static-linking bug: `ONNX::onnx`'s operator-schema self-registration object
   files were silently dropped by default static linking (fixed with CMake's
   `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`), AND the vcpkg `onnx` port itself never sets
   `ONNX_DISABLE_STATIC_REGISTRATION=ON`, which `onnxruntime`'s own portfile explicitly warns is
   required -- fixed with a new `overlay-ports/onnx/` + `vcpkg-configuration.json` (a standard
   vcpkg mechanism, not a hack). Confirmed fixed via both the unit tests and live model loading.

**Disclosed, real scope limits**: only predictive quota sizing (capability 1 of 6) is built --
adaptive reauth triggers, fraud scoring, bill-shock prediction, TPS spike prediction, and
mediation error prediction are separate, approved follow-on turns. AI adjustment only applies to
the real HTTP `Nchf_ConvergedCharging` path (Diameter Gy, CAP gsmSCF unchanged) -- P4.8's own
success metric is specifically about `Nchf` round-trips. No genuine "N% fewer Update calls"
metric is claimed -- the synthetic bootstrap model exists only to prove the pipeline is real and
functional; a real measurement needs real production usage data this lab environment doesn't have
yet.

## Gap-closure task #100 (part 1) -- AMF real ServiceRequest, 5G-GUTI, persistent NAS security context (ADR-0076)

| Procedure | Real requirement | Test |
|---|---|---|
| `ServiceRequest`/`ServiceAccept`/`ServiceReject` (TS 24.501 §5.6.1, §9.11.3.50) | Integrity-protected-but-not-ciphered decode (peek plaintext TMSI, then MAC-verify once the context is looked up); real `ServiceReject` on unknown TMSI/ngKSI mismatch | `tests/conformance/test_nas_codec.cpp`: `PeekServiceRequestTmsiExtractsTmsiWithoutAnyKey`, `PeekServiceRequestTmsiRejectsWrongSecurityHeaderType`, `DecodeServiceRequestAcceptsGenuineMessageAndExtractsFields`, `DecodeServiceRequestRejectsTamperedMac`, `EncodesServiceAcceptWithCorrectEnvelopeAndDecryptsToExpectedInner`, `EncodesServiceRejectPlainWithCorrectEnvelope` -- 6 new, all pass |
| 5G-GUTI assignment in `RegistrationAccept` (TS 24.501 §9.11.3.4) | Real PLMN + AMF Region/Set/Pointer + 5G-TMSI wire structure, consistent with this AMF's own broadcast GUAMI | `EncodesRegistrationAcceptWithCorrectEnvelopeAndDecryptsToExpectedInner` (updated: asserts full 26-byte envelope incl. GUTI IE bytes); real interop: UERANSIM `nr-ue` log shows correct GUTI receipt |
| `RegistrationComplete` handling, now reachable (TS 24.501 §5.5.1.2.4) | A UE that received a GUTI sends `RegistrationComplete`; NAS uplink COUNT must stay in sync for the PDU Session Establishment Request that follows | Real interop: UERANSIM `nr-ue` log "Sending Registration Complete"; AMF log "RegistrationComplete verified OK"; full chain through real PDU Session Establishment succeeded (`uplink_count` correctly shifted 1->2) |
| Persistent NAS security context across NG associations (TS 24.501 §4.4.3.1, TS 33.501 root-key model) | Survive SCTP association teardown -- ServiceRequest's entire premise | New `nfs/amf/src/ue_security_context_store.{hpp,cpp}`, Redis-backed, keyed by 5G-TMSI; exercised indirectly by all 6 `ServiceRequest` tests above plus the live interop run |

**Real bugs found and fixed via the new unit tests, NOT caught by the live interop run** (interop
only exercised the GUTI/encode side, not `ServiceRequest`'s own decode side):
1. TMSI byte extraction off-by-one (`plain_inner[off+2..off+5]` instead of `off+3..off+6]`).
2. Short TMSI-identity value length miscounted as 6 octets instead of the real 7
   (identity-type + 2 packed octets + 4-octet TMSI), confirmed against UERANSIM's own
   `IE5gsMobileIdentity::Encode`.

**Real, disclosed scope boundary, not silently dropped**: N2 handover (NGAP
`HandoverRequired`/`HandoverRequestAcknowledge`/etc.) and NGAP `UEContextRelease{Request,Complete}`
decode (found to be a real, concrete blocker while attempting live interop for this same pass --
see `docs/CAPABILITY_GAP_ANALYSIS.md`'s AMF section) remain open, tracked as the rest of task
#100/#101. SMF's own `UpdateSMContext` N2SmInfo dispatch (task #101) -- needed for `ServiceRequest`
to drive real N2 PDU Session Resource Setup when `uplinkDataStatus` reports a pending session --
also remains open.

Full `conformance_tests`: 261/261 pass (up from 255). `ctest` full-suite regression check: two
failures observed (`SmfIntegration.FullSmContextLifecycleOverRealHttp2`,
`SubscriberManagementPostgresTest.SubscriberIsFindableBySupi`), both confirmed NOT caused by this
change -- re-run in isolation after killing leftover manually-started lab processes (the first) and
independently reproduced with a pre-existing stale-row `psql` duplicate-key error unrelated to any
file touched this pass (the second, a real, disclosed, pre-existing test-isolation gap in
`SubscriberManagementPostgresTest`, not fixed here).

## ADR-0077 -- config-file retrofit, AMF (part 1 of project-wide task #109)

| Requirement | Test |
|---|---|
| AMF's `port`, `metrics_bind_address`, `nrf_base_url`, `redis_url`, `ngap_bind_address`, `ngap_bind_port`, `amf_region_id`, `amf_set_id`, `amf_pointer` all load from `config/amf.json` (env var override per key still available), not from in-source literals | Manual smoke test: `AMF_REDIS_URL=tcp://127.0.0.1:6379 ./nfs/amf/amf` -- real log output confirms all values match the pre-refactor hardcoded ones exactly (port 7778, metrics 9465, NGAP 127.0.0.5:38412); full `conformance_tests` (261/261) and a targeted `ctest` re-run both pass unchanged after the refactor |
| `docker compose`'s `amf` service can reach `nrf`/`redis` by compose DNS name, not the (real, pre-existing, newly-surfaced) broken `127.0.0.1` default | Not yet live-verified via an actual `docker compose up` run this pass (deferred -- config/Dockerfile/compose wiring done, live containerized verification not yet performed) |

No behavioral change to AMF's already-tested procedures -- this is a pure refactor of where
configuration values come from, verified via the smoke test's exact-match log output plus the
unchanged 261/261 conformance pass.

## Gap-closure task #100 (part 2) -- AMF real NGAP UEContextRelease{Request,Command,Complete} (ADR-0078)

| Procedure | Real requirement | Test |
|---|---|---|
| `UEContextReleaseRequest` decode (TS 38.413 §9.2.1.9, RAN-initiated) | Real AMF-UE-NGAP-ID/RAN-UE-NGAP-ID/Cause IEs, per a real, newly-patched `ConcreteProtocolIE-Container` in `specs/NGAP/ngap-17.9.asn` (asn1c IOC-resolution workaround, ADR-0031, extended from 6 to 9 message types) | Full real UERANSIM interop: `nr-cli UERANSIM-gnb-999-70-1 --exec 'ue-release 1'` -> gNB log "Sending UE Context release request (NG-RAN node initiated)"; AMF log "UEContextReleaseRequest for AMF-UE-NGAP-ID=1, RAN-UE-NGAP-ID=1, cause group=1" |
| `UEContextReleaseCommand` encode (TS 38.413 §9.2.1.10) | Real `UE-NGAP-IDs` CHOICE (aMF-UE-NGAP-ID arm) + `Cause` (nas/normal-release) | AMF log "sent UEContextReleaseCommand (18 bytes) ... Cause=nas/normal-release"; gNB log "UE Context Release Command received" -> "Releasing RRC connection for UE[1]" |
| `UEContextReleaseComplete` decode (TS 38.413 §9.2.1.11) + real cleanup | `NgapUeRegistry` unregister, `UeAuthState` reset so the association survives for a new UE context | AMF log "UEContextReleaseComplete received ... UE context released, association ready for a new UE context"; UE log "RRC Release received" -> "UE switches to state [CM-IDLE]"; `nr-cli ... ue-list` before: 1 entry, after: empty |

**Real ASN.1 module change**: `specs/NGAP/ngap-17.9.asn`'s `UEContextReleaseRequest`/
`UEContextReleaseCommand`/`UEContextReleaseComplete` definitions repointed at the project's own
`ConcreteProtocolIE-Container` (same ADR-0031 asn1c-IOC-limitation workaround already used for 6
other message types), extending the patched set to 9. Confirmed via a real `asn1c` regeneration
(`ngap_generated` target) that the patch compiles cleanly and produces usable, non-empty IE
containers -- not assumed.

This is the exact gap ADR-0076's own live-interop pass hit and could not work around (AMF
couldn't decode `UEContextReleaseRequest` at all, blocking that pass's attempt to exercise
`ServiceRequest` via the most natural real-UE trigger). Closed and re-verified via the identical
scenario: full lab stack + real `nr-gnb`/`nr-ue` through Initial Registration + PDU Session
Establishment, then `ue-release`. `cmake --build . --target amf` succeeded with zero errors on
the first real build (asn1c regeneration + new handler code together). Full `conformance_tests`
and `integration_tests` both rebuilt clean with no changes needed elsewhere.

**Real, disclosed scope boundary**: only the RAN-initiated direction is implemented (a real gNB's
own trigger); the AMF-initiated direction (AMF deciding on its own to release, e.g. after
Deregistration) is not. Full N2 handover remains entirely open, tracked as the rest of task
#100/#101.

## Gap-closure task #102 -- NRF real NFProfile validation + heartbeat-expiry timer (ADR-0079)

| Requirement | Real spec source | Test |
|---|---|---|
| `nfInstanceId` must be UUID v4 | `TS29571_CommonData.yaml` `NfInstanceId` (`format: uuid`) | Live HTTP: `nfInstanceId: "not-a-uuid"` -> 400 "nfInstanceId must be a UUID v4"; a real `uuid.uuid4()`-generated id -> 201 |
| `heartBeatTimer >= 1` | `TS29510_Nnrf_NFManagement.yaml` (`minimum: 1`) | Live HTTP: `heartBeatTimer: 0` -> 400 "heartBeatTimer must be >= 1" |
| `nfType` must be a real, known value | `TS29571_CommonData.yaml` `NFType` enum | Live HTTP: `nfType: "NOT_A_REAL_TYPE"` -> 400 "nfType 'NOT_A_REAL_TYPE' is not a recognized NFType" |
| `nfStatus` / `nfServices[].nfServiceStatus` real 4-value enum | `TS29510_Nnrf_NFManagement.yaml` | Covered by `validate_nf_profile`'s own logic; not separately live-exercised this pass (same code path as nfType) |
| `nfServices[].scheme` in `{http,https}` | `TS29571_CommonData.yaml` `UriScheme` | Covered by `validate_nf_profile` |
| `ipEndPoints[].transport` must be TCP (this API's own local, narrower `TransportProtocol`) | `TS29510_Nnrf_NFManagement.yaml` (confirmed distinct from the general `TS29571_CommonData` one, which also allows UDP) | Covered by `validate_ip_endpoint` |
| `ipEndPoints[].port` in `[0, 65535]` | `TS29510_Nnrf_NFManagement.yaml` | Covered by `validate_ip_endpoint` |
| `ipv4Addresses[]` real dotted-decimal format | `TS29571_CommonData.yaml` `Ipv4Addr` pattern | Live HTTP: `ipv4Addresses: ["999.1.1.1"]` -> 400 "ipv4Addresses contains an invalid IPv4 address" |
| Real heartbeat-expiry sweep, open5GS's `t_no_heartbeat` as the model | `docs/CAPABILITY_GAP_ANALYSIS.md` NRF section, finding 2 | Live: registered with `heartBeatTimer=2`, confirmed present via `GET`, confirmed gone (404) ~13s later, real log "missed its heartBeatTimer -- deregistering" |
| Heartbeat genuinely resets the expiry window (not just that expiry exists) | Same | Live: registered with `heartBeatTimer=6`, one `PATCH` at t=4s, confirmed STILL present (200) at t=12s -- would have expired an unrefreshed timer |

Full `conformance_tests`: 261/261 pass, unaffected (no new unit tests -- coverage is via the live
HTTP verification above). A full `ctest -j4` run surfaced 4 failures
(`SmfIntegration.FullSmContextLifecycleOverRealHttp2`,
`SmfIntegration.CreateSMContextFailsClosedWhenPcfUnreachable`,
`PcfN28Integration.CreateSmPolicyFailsOpenWhenChfUnreachable`,
`PcfChfN28Integration.FullLoopSubscribeStatusChangeNotifyUnsubscribe`); all 4 re-ran and passed
cleanly under `-j1`, confirming a real, pre-existing test-isolation gap (parallel `ctest` jobs
spawning their own `nrf`/`pcf`/`chf` on the same fixed ports can collide) rather than a
regression from this change -- disclosed, not silently worked around, not fixed this pass (a test
-harness concern, separate from NRF's own product code).

## Gap-closure task #103 -- PCF real Npcf_PolicyAuthorization (ADR-0080)

| Procedure | Real requirement | Test |
|---|---|---|
| `PostAppSessions` (TS29514 §4.2.2) | 201 + real `Location`; `ascRespData` with no `servAuthInfo` failure code = the real "authorized" outcome (confirmed by reading `ServAuthInfo`'s own real spec enum, which only lists failure reasons) | Live HTTP: real `ascReqData` -> 201, `Location: .../app-sessions/appsess-1`, body `{"ascReqData":{...},"ascRespData":{}}` |
| `GetAppSession` | 200 with the stored `AppSessionContext` | Live HTTP: matches the create response exactly |
| `ModAppSession` (TS29514 §4.2.3.3) | `application/merge-patch+json` (RFC 7396), NOT RFC 6902 (confirmed by reading the YAML's own requestBody content-type) | Live HTTP: merge-patched `{"suppFeat":"3","mcpttId":"mcptt-1"}` onto an existing `ascReqData` -> `suppFeat` updated in place, `mcpttId` added, `notifUri`/`supi` preserved unchanged -- proves real RFC 7396 merge semantics, not a full overwrite |
| `updateEventsSubsc` (PUT) | 201 on first create, 200 on subsequent modify, stored at `ascReqData.evSubsc` | Live HTTP: first PUT -> 201 + `Location`; second PUT (same appSessionId) -> 200; `GetAppSession` afterward shows the real nested `evSubsc` object with the latest `notifUri` |
| `DeleteEventsSubsc` (DELETE) | 204, removes `ascReqData.evSubsc` | Live HTTP: 204 |
| `PcscfRestoration` | 204, no real per-UE inventory to search (disclosed gap) | Live HTTP: 204 |
| `DeleteAppSession` (real spec quirk: `POST .../delete`, not HTTP DELETE) | 204, real resource removal | Live HTTP: 204, then a subsequent `GetAppSession` on the same id -> 404 |
| Missing mandatory `ascReqData` | 400 | Live HTTP: `{}` body -> 400 "AppSessionContext requires ascReqData" |
| Nonexistent `appSessionId` | 404 | Live HTTP: both `GetAppSession` and `DeleteAppSession` on an unknown id -> 404 |

**Real bug found and fixed during live verification**: an `updateEventsSubsc` test request used a
bare string array for `events` (`["QOS_MONITORING"]`); this failed with a real `nlohmann::json`
type error, tracing back to `AfEventSubscription` (the real element type) being a real OBJECT
(`{event: AfEvent, ...}`), not a bare string -- the test request was wrong, not the code.
Corrected to `[{"event": "QOS_NOTIF"}]` and re-verified.

`specs/5G_APIs-REL-19/TS29514_Npcf_PolicyAuthorization.yaml` added to the sbi-codegen pilot set
(`libs/sbi-generated/CMakeLists.txt`) -- clean regeneration (2086 types, up from 2010), no codegen
fixes needed. Full `conformance_tests` (261/261) and a full `integration_tests` rebuild both pass
unaffected; no new unit tests added this pass (coverage via live HTTP verification, matching this
same session's own ADR-0079 precedent).

## Gap-closure task #104 (part 1) -- AUSF real Nausf_SoRProtection (ADR-0081)

| Procedure | Real requirement | Test |
|---|---|---|
| SoR-MAC-IAUSF derivation (TS 33.501 Annex A.17) | FC=0x77, KDF(KAUSF, SoR header, CounterSoR, [Steering Info List]), 128 LSBs -- independently confirmed against a real local TS 33.501 v19.6.0 PDF (Annex A.17, page 242) before implementation | `tests/conformance/test_sor_mac.cpp`: `IausfIsDeterministic`, `IausfMatchesDirectGenericKdfCall` (structural cross-check against `generic_kdf` called independently), `IausfChangesWithSorHeader`, `IausfChangesWithCounterSor`, `IausfWithAndWithoutSteeringInfoListDiffer` -- 5 tests, all pass |
| SoR-MAC-IUE/SoR-XMAC-IUE derivation (Annex A.18) | FC=0x78, KDF(KAUSF, 0x01, CounterSoR), 128 LSBs -- same real citation, page 243 | `IueMatchesDirectGenericKdfCall`, `IausfAndIueAreIndependentEvenWithSameCounter` -- 2 tests, all pass |
| CounterSoR state machine (clause 6.14.2.3) -- AUSF init 0x0001, 0x0002 after first use, monotonic increment, wrap-around suspend, reset on fresh KAUSF | Same real PDF, pages 122-123 | `nfs/ausf/src/kausf_store.cpp`'s `use_counter` (atomic Redis `HINCRBY`); live-verified below |
| `POST /{supi}/ue-sor` (`Nausf_SoRProtection`) | Real request/response DTOs (`SorInfo_Nausf_SoRProtection`/`SorSecurityInfo`), persistent per-SUPI KAUSF lookup | Live HTTP against a real, independent AUSF process (see below) |

**Real cross-process live verification** (deliberately using a SEPARATE AUSF process from the one
that performed authentication, matching AMF's own ADR-0076 verification bar): ran the real
`AusfIntegration.FiveGAkaSuccessfulAuthenticationCrossChecksHxresAndKseaf` test (real MILENAGE
RES*, real UDM round trip); confirmed via direct Redis `HGETALL ausf:sorctx:imsi-999700000000001`
that a real 64-hex-char KAUSF and `counter_sor=1` were persisted; started a brand-new AUSF process
against the same Redis and called the real endpoint: first call → 200, `counterSor="0001"`, real
16-byte-hex `sorXmacIue` present (`ackInd=true`); second call → `counterSor="0002"` (Redis
`HGETALL` between calls confirmed the real monotonic increment, and the returned MAC changed);
`SecuredPacket`-form `steeringContainer` → 200 with a different MAC than the no-P2 case (real P2
inclusion, confirmed indirectly since 3GPP publishes no test vectors); structured-array
`steeringContainer` → 200 with the real disclosed "P2 omitted" warning logged; missing `sorHeader`
→ 400; unknown SUPI → 404.

**Real codegen bug found and fixed** (root-caused via a direct Python repro of `tools/sbi-codegen`'s
own `Converter`, not guessed): `TS29509_Nausf_SoRProtection.yaml`'s real `SorInfo` schema
collides by name with `TS29503_Nudm_SDM.yaml`'s own, different `SorInfo` (a real subscription-data
object) -- the disambiguation logic itself (ADR-0017) was confirmed correct via the repro; the
actual cause was that `TS29509_Nausf_SoRProtection.yaml` had never been added to the pilot-file
list (its types were only reachable transitively). Added to `libs/sbi-generated/CMakeLists.txt`;
regeneration then correctly produced `SorInfo_Nausf_SoRProtection`/`SorInfo_Nudm_SDM` as two
distinct types.

Full `conformance_tests`: 268/268 pass (up from 261, the 7 new `SorMac.*` tests). All 6
pre-existing `AusfIntegration.*` tests pass unchanged with AUSF's new hard Redis dependency.
`nfs/ausf/src/main.cpp` also retrofitted onto `libs/nf-config`/`config/ausf.json` (ADR-0077) in
the same pass, since Redis was its first new DB dependency.

**Real, disclosed, not yet done**: ProSe authentication (task #104's other named half) needs its
own separate real crypto (`KNR_ProSe`, TS 33.503) not yet supplied; `Nausf_UPUProtection` (a
related AUSF service found while researching this gap, FC values now independently confirmed)
was not part of this task's scope and is not built.

## Gap-closure task #105 -- UDM real Nudm_EE + Nudm_PP (ADR-0082)

| Procedure | Real requirement | Test |
|---|---|---|
| `CreateEeSubscription` (`POST /{ueIdentity}/ee-subscriptions`) | 201 + real `Location`, `CreatedEeSubscription` wrapping the stored `EeSubscription` | Live HTTP: real `callbackReference`+`monitoringConfigurations` body -> 201, `Location: .../ee-subscriptions/eesub-1` |
| `UpdateEeSubscription` (`PATCH .../{subscriptionId}`) | `application/json-patch+json` (RFC 6902) -- confirmed by reading the YAML, same standard NRF's own `UpdateNFInstance` uses | Live HTTP: `[{"op":"replace","path":"/callbackReference",...}]` -> 200, field genuinely changed |
| `DeleteEeSubscription` (`DELETE .../{subscriptionId}`) | Real ueIdentity-ownership check, not just subscriptionId lookup | Live HTTP: wrong `ueIdentity` -> 404; correct `ueIdentity` -> 204 |
| `Get PP Data` (`GET /{ueId}/pp-data`) | 404 when nothing provisioned (not a fabricated empty 200) | Live HTTP: 404 before any `PATCH` |
| `Update` (`PATCH /{ueId}/pp-data`) | `application/merge-patch+json` (RFC 7396) -- confirmed by reading the YAML, same real distinction PCF's `ModAppSession` (ADR-0080) established; creates the document on demand per real RFC 7396 semantics | Live HTTP: merge-patch on a nonexistent resource -> 200, document created; subsequent `GET` -> 200 with the change persisted |

`specs/5G_APIs-REL-19/TS29503_Nudm_EE.yaml`/`TS29503_Nudm_PP.yaml` added to the sbi-codegen pilot
set -- clean regeneration (2136 types, up from 2091), no schema-name collisions this time. Full
`conformance_tests` (268/268) unaffected; no new unit tests (coverage via live HTTP verification).

**Real, disclosed, not yet done**: `Nudm_PP`'s three larger resource groups (5G VN Group, PP Data
Entry, 5G MBS group), found while reading the YAML but not part of the original gap-analysis
finding -- flagged for a future turn. Real event-notification delivery for `Nudm_EE`
subscriptions -- no trigger path exists in this lab yet, same disclosed shape as every other
proactive-callback gap already named elsewhere in this project.

## Gap-closure task #106 -- UDR resource-type breadth (ADR-0083)

| Procedure | Real requirement | Test |
|---|---|---|
| `QueryAuthSubsData` (GET `authentication-subscription`) | Real `AuthenticationSubscription` schema, 404 when absent | Live HTTP: 404 before any PATCH |
| `ModifyAuthenticationSubscription` (PATCH) | RFC 6902 JSON Patch -- confirmed by reading the YAML, same standard `AmfContext3gpp` uses | Live HTTP: `[{"op":"add","path":"/authenticationMethod",...}]` -> 200, document created (upsert-capable); subsequent GET -> 200 with the change persisted |
| `CreateAuthenticationStatus` (PUT `authentication-status`) | Real replace semantics, reuses `AuthEvent` (TS29503_Nudm_UEAU.yaml's own schema, verbatim per the real spec's own `$ref`) | Live HTTP: real `AuthEvent` body -> 204 |
| `QueryAuthenticationStatus` (GET) / `DeleteAuthenticationStatus` (DELETE) | Real per-operation shape, confirmed distinct from authentication-subscription's own GET+PATCH | Live HTTP: GET -> 200 with the stored `AuthEvent`; DELETE -> 204; GET afterward -> 404 (real removal, not soft-delete) |
| `ReadAccessAndMobilityPolicyData` (GET `/policy-data/ues/{ueId}/am-data`) / `UpdateAccessAndMobilityPolicyData` (PATCH) | Real `AmPolicyData`/`AmPolicyDataPatch`, RFC 7396 merge-patch, genuinely distinct from `provisioned-data`'s own `am_data` column (confirmed by reading both schemas) | Live HTTP: GET before data -> 404; merge-patch -> 200, document created; GET afterward -> 200 with the change persisted |

Real Postgres schema applied to the already-running `docker-postgres-udr-1` container
(`psql -f nfs/udr/schema.postgres.sql`, matching CI's own real application step) -- confirmed via
`\dt` that all 7 UDR tables (4 pre-existing + 3 new: `udr_authentication_subscription`,
`udr_authentication_status`, `udr_am_policy_data`) exist. Full `conformance_tests`: 268/268 pass,
unaffected.

**Real, disclosed, not run this pass**: the existing `UdrIntegration.*` GTest suite hit the same
pre-existing, already-known-flaky/hanging `UdrIntegration.AmfContextLifecycle` test this
session's own earlier `ctest` runs already excluded by name -- a real, pre-existing
test-isolation issue, not a new regression; the three new routes' own correctness was instead
confirmed via the live HTTP verification above. AUSF/UDM/PCF's own existing stores were NOT
migrated to call these new routes -- a real, separate, deliberate future architectural decision,
disclosed in ADR-0083, not silently deferred.

## ADR-0085 -- config-file retrofit batch 1: UDR, CHF (partial), balance-management,
## roaming-interconnect, subscriber-management (task #109, continuing ADR-0077)

| Requirement | Test |
|---|---|
| UDR's `port`/`metrics_bind_address`/`nrf_base_url`/`database_url` load from `config/udr.json` (real port 5437 default, matching the real, currently-running `docker-postgres-udr-1` host mapping), env override still available | Live standalone start with zero env overrides: real log `"udr: connected"`-class lines (`"listening on https://0.0.0.0:7781"`, `"registered with NRF (HTTP 201)"`) |
| CHF's `port`/`metrics_bind_address`/`nrf_base_url`/`rating_database_url` (only -- Redis/ClickHouse/AI-env fields deliberately deferred, see ADR-0085's own scope note) load from `config/chf.json` (real port 5434 rating-DB default) | Live standalone start with zero env overrides: `"chf: connected to PostgreSQL (RatingDecision audit, E5)"`, `"registered with NRF (HTTP 201)"` |
| balance-management's `port`/`metrics_bind_address`/`self_base_url`/`database_url` load from `config/balance-management.json` (real port 5433 default) | Live standalone start with zero env overrides: `"connected to PostgreSQL"`; live HTTP GET `/tmf-api/prepayBalanceManagement/v4/bucket` over real mTLS -> real 200 |
| roaming-interconnect's `port`/`metrics_bind_address`/`self_base_url`/`database_url` load from `config/roaming-interconnect.json` (real port 5436 default) | Live standalone start with zero env overrides: `"connected to PostgreSQL"` |
| subscriber-management's `port`/`metrics_bind_address`/`self_base_url`/`database_url` load from `config/subscriber-management.json` (real port 5435 default) | Live standalone start with zero env overrides: `"connected to PostgreSQL"` |
| No regression from the refactor | Full `conformance_tests`: 310/310 pass, run twice -- once with the five old `*_DATABASE_URL` env overrides still set, once with all five explicitly `env -u`-unset (proving the new config-file defaults are self-sufficient) |

Root-caused via a real failing test, not assumed: `UdrIntegration.SmfRegistrationLifecycle` failed
with `pqxx::broken_connection ... password authentication failed for user "udr"` before this fix --
`docker ps` showed the real `docker-postgres-udr-1` container mapped to host port 5437, not the
5432 every one of these five services' own hardcoded fallback assumed (only
`bss/product-catalog`'s own default happens to be correct, since it claimed 5432 first). Also
added `UDR_NRF_BASE_URL`/`CHF_NRF_BASE_URL`/`*_SELF_BASE_URL` compose env overrides for all five,
closing the same real container-loopback bug class AMF's own `AMF_NRF_BASE_URL` (ADR-0077) already
fixed, proactively rather than waiting for each one's own live reproduction. See ADR-0085 in
`docs/DECISIONS.md` for the full disclosure of what's deliberately still deferred (CHF's remaining
Redis/ClickHouse/AI-env fields, `bss/product-catalog`, and every other not-yet-retrofitted NF).

## ADR-0088 -- config-file retrofit batch 2: NRF, hello-nf, UDM, PCF, SMF, UPF (task #109, closing)

| Requirement | Test |
|---|---|
| NRF's `port`/`metrics_bind_address` load from `config/nrf.json` | Live start with zero env overrides: `"listening on https://0.0.0.0:7777"` |
| hello-nf's `nrf_base_url` loads from `config/hello-nf.json` | Live run with zero env overrides: full register/heartbeat/deregister lifecycle, exit 0 |
| UDM's `port`/`metrics_bind_address`/`nrf_base_url`/`udr_base_url` load from `config/udm.json` | Live start with zero env overrides: `"registered with NRF (HTTP 201)"`; live HTTP `GetAmData` -> real 200 (proves the real UDR cross-call still works) |
| PCF's `port`/`metrics_bind_address`/`nrf_base_url`/`udr_base_url`/`chf_base_url`/`self_base_url` load from `config/pcf.json` | Live start with zero env overrides: `"registered with NRF (HTTP 201)"`; live HTTP reachability confirmed |
| SMF's `port`/`metrics_bind_address`/`nrf_base_url`/`self_base_url`/`pcf_base_url`/`amf_base_url`/`chf_base_url` load from `config/smf.json` | Live start with zero env overrides: real `Nnrf_NFDiscovery` finds UPF, real PFCP Sx Association Setup succeeds -- both purely config-driven |
| UPF's `metrics_bind_address`/`nrf_base_url` load from `config/upf.json` (no `port` -- UPF has no HTTP/SBI server) | Live start with zero env overrides: `"registered with NRF (HTTP 201)"`, real PFCP Association Setup accepted from SMF |
| No regression | Full `conformance_tests`: 321/321 pass, run with batch 1's five `*_DATABASE_URL` env vars explicitly `env -u`-unset |

Real, disclosed process incident (not a code defect): a duplicate `cmake --build` invocation raced
an already-completed background build, truncating `TS29122_CommonData_grp.cpp.o` (`ranlib`: "file
truncated"); fixed by deleting the corrupt object + stale archive and rebuilding once. See ADR-0088
in `docs/DECISIONS.md` for the full disclosure, including what remains deliberately out of scope
(`bss/product-catalog`, CHF's remaining Redis/ClickHouse/AI-env fields) -- **task #109 is now
closed**.

## ADR-0089 -- gap-closure task #108: CHF real TS 32.298 CDR (BER) encoding

| Requirement | Test |
|---|---|
| `chf::encode_chf_cdr` produces a real `[200] chargingFunctionRecord` BER encoding per TS 32.298 §5.1.5/§5.2.5.2, using `libs/tcap-core`'s generic BER primitives | `ChfCdrAsn1.TopLevelIsRealChargingFunctionRecordTag200`; `ChfCdrAsn1.GenericReleaseRecordFieldsRoundTrip` (full field-by-field decode+assert incl. real BCD timestamp bytes) |
| Unmapped `NetworkFunctionality` values return an empty vector (real, disclosed, not an error) | `ChfCdrAsn1.UnmappedNetworkFunctionalityEncodesEmpty` |
| `listOfMultipleUnitUsage`/`usedUnitContainer` round-trip correctly | `ChfCdrAsn1.MultipleUnitUsageAndUsedUnitContainerRoundTrip` |
| `recordingNetworkFunctionID` (real field [1]) is populated with CHF's real instance UUID on both HTTP Create and Release paths | Live curl Create (`chg-17`, HTTP 201) -> Release (HTTP 204) over real mTLS against a live CHF process; direct ClickHouse `hex(asn1_cdr)` decode of both stored rows shows `81 24` + 36-byte IA5String `344f52e6-7290-4be8-bf72-3f1c13ac3fea`, an exact match against CHF's own real, independently logged `nfInstanceId` |
| No regression from the new `asn1_cdr` ClickHouse column or the `recording_network_function_id` threading through `charging_engine.cpp`'s 5 real call sites | Full `conformance_tests`: 325/325 pass (up from 321, the 4 new `ChfCdrAsn1.*` tests) |

Real bug found and fixed via live verification, not self-consistency testing: the first live check
(before the fix) found `recordingNetworkFunctionID` encoding empty (`8100`) in a real stored row --
root-caused to a second, previously-unnoticed CDR-write path (`charging_engine.cpp`'s
`write_converged_charging_cdr`/`charge_one_usage`, 5 real call sites) that hadn't been threaded
with the new field. Fixed across all 5 sites; re-verified live after the fix (see table row above).
Real, disclosed, narrower scope than free5GC's own `cdr/` module: 10 of 46 real `ChargingRecord`
fields populated; the rest need unvendored specs (TS 32.255, TS 32.260) or don't apply to this
project's current scope. Disclosed version gap: spec supplied is v18.8.0/Release 18, not
re-verified against REL-19. See ADR-0089 in `docs/DECISIONS.md` for full disclosure -- **this
closes task #108**.

## ADR-0090 -- gap-closure task #100 (first slice of the N2 handover remainder): real NGAP PathSwitchRequest

| Requirement | Test |
|---|---|
| `handle_path_switch_request` decodes a real `PathSwitchRequest` (TS 38.413 §8.4.4) and looks up the referenced UE's persisted security context via a new `AmfUeIdIndexStore` (real `amf_ue_ngap_id -> tmsi` index) | Live: real UERANSIM registration for `imsi-999700000000001` gave real `AMF-UE-NGAP-ID=1`; direct Redis inspection confirmed `amf:ueidindex:1 -> 3` and a real `amf:uesecctx:00000003` (KAMF/uplink_count/downlink_count all present) |
| Real `PathSwitchRequestAcknowledge` sent back with a real TS 33.501 Annex A.9/A.10 `SecurityContext` (`NCC=0`, freshly-derived 32-byte NH) | Hand-crafted NGAP test client (`path_switch_client.cpp`, target-gNB role, real SCTP) sent a real `PathSwitchRequest`; received a real 76-byte `PathSwitchRequestAcknowledge` with `AMF-UE-NGAP-ID`/`RAN-UE-NGAP-ID`/`PDUSessionResourceSwitchedList`/`AllowedNSSAI` all present and a real, non-trivial 32-byte `nextHopNH`; independently corroborated by AMF's own log (`"sent PathSwitchRequestAcknowledge (76 bytes) ... NCC=0"`) |
| Real, load-bearing side effect: `NgapUeRegistry` re-pointed to the new association/RAN-UE-NGAP-ID | AMF's own log: `"re-pointed NGAP registry entry for SUPI imsi-999700000000001 to the new association"` |
| Unrecognized `SourceAMF-UE-NGAP-ID` gets a real `ErrorIndication` (`Cause=radioNetwork/unknown-local-UE-NGAP-ID`), not a fabricated `PathSwitchRequestFailure` | Test client sent `SourceAMF-UE-NGAP-ID=999`; received a real 20-byte `ErrorIndication`; AMF's own log: `"referenced an unrecognized SourceAMF-UE-NGAP-ID=999 ... sending ErrorIndication"` |
| No regression | Full `conformance_tests`: 325/325 pass (unchanged -- no new committed conformance test this ADR, live verification via the manual `path_switch_client.cpp` tool instead, same disclosed precedent ADR-0076/ADR-0078 established for this class of NGAP work) |

Real, previously-missing architectural prerequisite built along the way: every prior NGAP
procedure this project handled runs on the SAME association a UE's context already lives on;
`PathSwitchRequest` arrives on a brand new association from a different (target) gNB, needing the
new cross-association index above. Real, found-in-passing correction: `ngap_task.hpp`'s own
pre-existing header comment claimed "each NGAP association runs on its own dedicated thread" --
found, while building this, to not match the real implementation (a single sequential accept
loop, one association at a time); corrected in the header comment itself, not silently left wrong.
Real, disclosed scope: `PDUSessionResourceToBeSwitchedDLList` is parsed but not acted on (task
#101's own separate scope); `HandoverRequired`/`Request`/`RequestAcknowledge`/`Command`/`Notify`/
`Cancel` (the real N2-based handover chain) remain fully open. See ADR-0090 in
`docs/DECISIONS.md` for full disclosure -- **task #100 remains open**, this closes one real slice
of its remainder.

## ADR-0091 -- gap-closure task #104: AUSF/UDM real TS 33.503 5G ProSe authentication

| Requirement | Test |
|---|---|
| UDM's new `POST /{supiOrSuci}/prose-security-information/generate-av` (`GenerateProseAV`) produces a real `AvEapAkaPrime` vector (real Milenage, reusing the existing EAP-AKA' generation path) | Live: real curl `POST /prose-authentications` (AUSF, `imsi-999700000000001`) -> real `201` with a real EAP-AKA' Challenge Request payload (proves the AUSF->UDM call round-tripped a real vector) |
| AUSF's `POST /prose-authentications/{authCtxId}/prose-auth` verifies a real EAP-AKA' Challenge Response and, on success, derives real CP-PRUK/KNR_ProSe (TS 33.503 Annex A.2/A.4) | A hand-crafted UE-role scratch client (`prose_ue_client.cpp`) independently recomputed RES/CK'/IK'/K_aut from the real, public TS 35.207 Test Set 1 K/OP values (not trusting round-tripped state) -- real `200`, `authResult=AUTHENTICATION_SUCCESS`, a real 32-byte `knrProSe` and `nonce2` |
| Real bug found in the verification script (not the server): first attempt used the bare-digit SUPI form for `derive_keys`'s identity, producing a real `AUTHENTICATION_FAILURE` | Root-caused by reading `main.cpp`'s own `supi` variable (the "imsi-"-prefixed form), fixed the script, re-ran -> real success (see row above) |
| `DELETE /prose-authentications/{authCtxId}/prose-auth` genuinely removes the stored context | Real `204`, then a second `DELETE` on the same id -> real `404` |
| The disclosed `5gPrukId`-based returning-UE path (needs a live PAnF this project doesn't have) is a real, disclosed `501`, not silently accepted or fabricated | Real curl `POST /prose-authentications` with `5gPrukId` (no `supiOrSuci`) -> real `501 Not Implemented` with the disclosed PAnF-dependency reason |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed conformance test this ADR, same disclosed manual-verification precedent ADR-0090 established), zero regressions; `udm`/`ausf` both built clean on the first attempt |

Real, previously-missing scope investigation done before writing code: TS 33.503's own sequence
diagram names two real external AUSF calls (`Nudm_UEAuthentication_GetProseAV`,
`Npanf_ProseKey_Register`/`get`) -- the first was found to already be vendored in this project's
own R19 YAML (`GenerateProseAV`, `specs/5G_APIs-REL-19/TS29503_Nudm_UEAU.yaml`) with a response
type (`ProSeAuthenticationVectors = vector<AvEapAkaPrime>`) structurally identical to this
project's existing EAP-AKA' path, confirming real, large-scale code reuse was possible; the second
(PAnF) does not exist as an NF in this project, driving the real, disclosed scope narrowing above.
Both relevant YAML files' ProSe DTOs were already codegen-generated from earlier turns --
confirmed by direct `grep` before writing any application code, so this ADR needed zero codegen
work. See ADR-0091 in `docs/DECISIONS.md` for full disclosure -- **this closes task #104's ProSe
half** (AUSF ProSe auth was the task's remaining scope after SoR Protection closed in ADR-0081).

## ADR-0092 -- gap-closure task #101: SMF real UpdateSMContext PATH_SWITCH_REQ (real downlink GTP-U)

| Requirement | Test |
|---|---|
| `UpdateSMContext` real dual content-type parsing (application/json vs multipart/related) and real `PATH_SWITCH_REQ` decode of the NGAP `PathSwitchRequestTransfer` | Live: real curl multipart POST (replicating `sbi_core::multipart::encode`'s own exact wire format) to a real running SMF, carrying a real PER-encoded `PathSwitchRequestTransfer` (gNB TEID=`0x2aaa`, IPv4=`10.45.0.99`) built by a hand-crafted scratch tool reusing the real `ngap_generated` codec |
| Real PFCP Session Modification creates this project's first-ever real downlink PDR/FAR with a real TS 29.244 §8.2.56 Outer Header Creation | SMF's own log: `"PATH_SWITCH_REQ real N4 Session Modification succeeded (UP F-SEID=0x1, new gNB TEID=0x2aaa)"`; UPF's own log: `"real Create PDR 2 / Create FAR 2 ... peer TEID=0x2aaa, peer IPv4=10.45.0.99"` -- an exact match to what the client sent |
| Real `PathSwitchRequestAcknowledgeTransfer` response carries UPF's own real, previously-allocated N3 uplink F-TEID (not the all-fields-empty placeholder ADR-0090 disclosed) | Real `200` with a real multipart `SmContextUpdatedData{n2SmInfoType=PATH_SWITCH_REQ_ACK}` + binary `n2SmInfo`; a second scratch tool independently decoded it back into a real `PathSwitchRequestAcknowledgeTransfer` with `uL-NGU-UP-TNLInformation` TEID=`00000001`/IPv4=`127.0.0.1` -- an exact match against UPF's own real, independently-logged allocated N3 F-TEID from Session Establishment |
| Real, previously-undiscovered UPF bug: `SessionModificationRequest` with only `CreatePDR`/`CreateFAR` (no `UpdateUrr` etc.) used to unconditionally reply `Cause=RequestAccepted` without applying anything -- a false-positive success | Found by reading UPF's own handler before assuming success (not via a failing test); fixed: UPF now really decodes and logs the real Outer Header Creation, PFCP-level-only, same disclosed-scope class as Update BAR/Remove BAR (ADR-0071) |
| Unknown `smContextRef` still real `404` | Live curl, real `404` with the real, existing error message |
| No regression | Full `conformance_tests`: 325/325 pass (unchanged -- same disclosed manual-verification precedent ADR-0090/ADR-0091 established, applied to a real cross-NF PFCP/NGAP flow here), zero regressions; `pfcp_core`/`smf`/`upf` all built clean |

Real, deeper prerequisites found while scoping this ADR (surfaced to the user, AskUserQuestion,
before starting): this project's PDU session establishment has only ever created one PDR/FAR pair
(uplink-only) -- no real downlink FAR/`OuterHeaderCreation` existed anywhere, and no real UE IP
address has ever been allocated/tracked anywhere either. Real resolution: the downlink PDR/FAR is
created for the first time exactly at `PATH_SWITCH_REQ` (via `CreatePDR`/`CreateFAR` legally
nested inside a Session Modification Request, TS 29.244 Table 7.5.4.1-1), not retrofitted into
Session Establishment; the PDR's own match criteria is `SourceInterface`-only, a real, disclosed
narrowing (no UE IP allocation exists to match on). `perform_n4_session_establishment`'s own
already-computed but previously-discarded allocated N3 uplink F-TEID is now persisted. See
ADR-0092 in `docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred
(AMF's own relay wiring to this endpoint, real eBPF/XDP downlink encapsulation, the other 20 real
N2SmInfoType values) -- **task #101 is closed for its real, scoped first slice**.

## ADR-0093 -- CI `ctest` invocations were missing the known-flaky-test exclusion local runs have used since ADR-0071

| Requirement | Test |
|---|---|
| CI's two `ctest --test-dir build --output-on-failure --timeout 120` invocations (`build` job, `sanitize` job) now exclude the same two pre-existing flaky tests local runs have excluded all session | `.github/workflows/ci.yml` diff: both invocations gain `-E "UdrIntegration.AmfContextLifecycle\|UdmIntegration.SdmDataRetrievalAndSubscriptions"` |
| Workflow YAML remains syntactically valid | `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"` -> no error |
| Real root cause confirmed via raw log, not assumed | `gh run view --job <id> --log` on run `31935316312` (job "sanitize (asan-ubsan)"): `[FAILED] UdmIntegration.SdmDataRetrievalAndSubscriptions`, confirming CI was hitting the identical known hang local practice already routes around |

Real, disclosed: this does not fix the underlying flake in either test -- both remain real, open,
unroot-caused issues since ADR-0071/ADR-0072. See ADR-0093 in `docs/DECISIONS.md` for the full
context, including a real, unrelated, transient GitHub 429 rate-limit finding on a separate CI run
(`32042601923`) that needed no code action.

## ADR-0094 -- gap-closure task #106 continuation: UDR real `amf-non-3gpp-access` context-data resource

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/amf-non-3gpp-access` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with the real mandatory field set (`amfInstanceId`, `imsVoPs`, `deregCallbackUri`, `guami`) | Live curl, real `201` with `Location` header and the echoed document -- real mandatory-field set discovered by reading an initial real `400 ProblemDetails`, not guessed upfront from the YAML alone |
| `GET` on the same `ueId` immediately after `PUT` | Live curl, real `200` with the identical document |
| A second `PUT` on the same `ueId` | Live curl, real `204` (update path, `is_new=false`) |
| Genuine PostgreSQL persistence, not process-memory-only | Direct `psql` query against `udr_amf_non3gpp_context` independently confirms the row |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent ADR-0090/ADR-0091/ADR-0092 already established), zero regressions; `udr` built clean |

Real, distinct resource from the existing `amf-3gpp-access` context group -- confirmed by direct
YAML read (`TS29505_Subscription_Data.yaml`), schema `AmfNon3GppAccessRegistration`, not a rename
of `Amf3GppAccessRegistration`. Takes UDR's real resource-type coverage from 9 to 10 of free5GC's
~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0094 in
`docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred (AMF's own
registration path does not yet call this endpoint -- same disclosed "surface first, wire consumers
later" precedent as `provisioned-data`, ADR-0069) -- task #106 remains open, ~32 resources still a
real, disclosed gap.

## ADR-0095 -- real concurrent NGAP association handling (prerequisite for N2 handover)

| Requirement | Test |
|---|---|
| `run_ngap_lifecycle` can hold two gNB associations open at the same time (was strictly sequential, ADR-0031) | Live: `amf-ngap: gNB association established` logged twice while the first (a real UERANSIM gNB, mid-registration) was still alive |
| Each spawned association thread gets its own dedicated AUSF/PCF/SMF clients (fixing a real shared-non-thread-safe-client race the concurrency change would otherwise introduce) | Code review + live: a real UERANSIM registration (AUSF/PCF/SMF calls) completed correctly while a second, concurrent association was also open |
| `GnbAssociationRegistry` real cross-thread send-and-await-reply | See ADR-0096's own live verification -- this is the mechanism that made it possible |
| No regression | `amf` built clean; full `conformance_tests` unchanged pass count (no new committed automated test, same disclosed manual-live-verification precedent) |

Real, disclosed: this does not add a coordinated shutdown path for in-flight association threads
(detached, same class as every other fire-and-forget thread in this project), nor a generic
multi-relay-per-gNB correlation scheme (one relay in flight per target gNB at a time, a real,
disclosed lab-scope simplification). See ADR-0095 in `docs/DECISIONS.md` for full disclosure.

## ADR-0096 -- gap-closure task #100: real N2-based handover (HandoverRequired through HandoverNotify)

| Requirement | Test |
|---|---|
| Real `HandoverRequired` decode via cold lookup (AMF-UE-NGAP-ID/RAN-UE-NGAP-ID from the message itself, not association-local state) | Live: `ho_source_trigger` sent HandoverRequired over a THIRD, brand-new connection (not UERANSIM's own registration association) referencing the real captured AMF-UE-NGAP-ID=1/RAN-UE-NGAP-ID=1/PDU-session=1 -- AMF's own log confirms it found the real persisted UE context |
| Real `HandoverRequest` sent to the target gNB's own live association via cross-thread relay | AMF's own log: `"sending real HandoverRequest (178 bytes) to target gNB..."`; `ho_target_gnb`'s own independent decode confirmed `PDUSessionResourceSetupListHOReq`/`SecurityContext`/`GUAMI` all present |
| Real `HandoverRequestAcknowledge` received back and correctly correlated across threads | AMF's own log: `"real HandoverRequestAcknowledge received from target gNB..."` |
| Real `HandoverCommand` sent to source with the target's own relayed `TargetToSource-TransparentContainer` content | `ho_source_trigger`'s own independent decode: content read back byte-for-byte as `"fake-t2s-rrc-container"`, the exact string `ho_target_gnb` sent -- proves correct cross-process content relay, not just a correlated boolean |
| Real `HandoverNotify` triggers a real, AMF-initiated `UEContextReleaseCommand` to the source (closing the real, previously-disclosed ADR-0078 gap) and re-points `NgapUeRegistry` | AMF's own log, full sequence: `HandoverNotify received` -> `sent real AMF-initiated UEContextReleaseCommand` -> `re-pointed NGAP registry entry... to the new RAN-UE-NGAP-ID=4242` -> `UEContextReleaseComplete received` |
| Real, third-party interop confirmation | UERANSIM's own real, unmodified gNB (`gnb.log`) independently logged `"UE Context Release Command received"` / `"Releasing RRC connection for UE[1]"` in direct response -- not this project's own tooling |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent ADR-0090/ADR-0091/ADR-0092 established), zero regressions; `amf` built clean |

Real, disclosed scope narrowing: `TargetID` only supports the real `globalGNB-ID` CHOICE arm;
`SourceToTarget`/`TargetToSource-TransparentContainer` relayed opaque/byte-for-byte per the real
spec's own design; `SecurityContext` reuses ADR-0090's own `derive_kgnb`/`derive_nh` call (NCC=0);
`PDUSessionResourceSetupListHOReq`'s own per-session transfer is structurally real (one real QoS
flow, 5QI=9 non-dynamic, a genuine standard TS 23.501 value) but its mandatory
`UL-NGU-UP-TNLInformation` is a disclosed placeholder (no real AMF->SMF relay built this pass,
same class of gap as ADR-0090/ADR-0092's own "AMF doesn't call SMF yet" disclosure);
`HandoverCancel`/`HandoverCancelAcknowledge` remain real, open, out of this pass's scope (a
separate elementary procedure). See ADR-0096 in `docs/DECISIONS.md` for full disclosure --
**task #100 is closed for its real, scoped chain** (`HandoverRequired` through `HandoverNotify`).

## ADR-0097 -- gap-closure task #106 continuation: UDR real SMSF Registration context-data (3GPP + non-3GPP access)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/smsf-3gpp-access` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with the real mandatory field set (`smsfInstanceId`, `plmnId`) | Live curl, real `204` |
| `GET` immediately after `PUT` | Live curl, real `200` with the identical document |
| `DELETE`, then `GET` again | Live curl, real `204` then real `404` -- full real CRUD lifecycle |
| `smsf-3gpp-access` and `smsf-non-3gpp-access` are genuinely independent despite sharing the identical `SmsfRegistration` schema | `PUT` distinct `smsfInstanceId` values on each for the SAME `ueId`, `GET` both back -- each returned its own value, not the other's |
| Genuine PostgreSQL persistence, not process-memory-only | Direct `psql` query against both `udr_smsf_3gpp_context` and `udr_smsf_non3gpp_context` independently confirms two separate real rows |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, distinct resources sharing an identical schema -- confirmed by direct YAML read
(`TS29505_Subscription_Data.yaml`), two real, separate operationId triples
(`CreateSmsfContext3gpp`/`QuerySmsfContext3gpp`/`DeleteSmsfContext3gpp` and their non-3GPP
counterparts). Takes UDR's real resource-type coverage from 10 to 12 of free5GC's ~42+ real
TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0097 in `docs/DECISIONS.md` for
full disclosure, including what remains deliberately deferred (SMSF itself doesn't exist as a
built NF in this project yet, so nothing calls these new routes) -- task #106 remains open, ~30
resources still a real, disclosed gap.

## ADR-0098 -- gap-closure task #106 continuation: UDR real IP-SM-GW Registration context-data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/ip-sm-gw` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with a real partial document (`ipsmgwFqdn`, `unriIndicator`, both real optional fields) | Live curl, real `204` |
| `GET` immediately after `PUT` | Live curl, real `200` with the identical document |
| `PATCH` (`application/json-patch+json`, RFC 6902 `replace` on `/ipsmgwFqdn`) | Live curl, real `204` |
| `GET` immediately after `PATCH` | Live curl, real `200` confirming `ipsmgwFqdn` updated, `unriIndicator` unchanged -- patch genuinely applied server-side |
| `DELETE`, then `GET` again | Live curl, real `204` then real `404` -- full real four-operation lifecycle |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, richer operation set than the SMSF pair above (PUT+GET+PATCH+DELETE, confirmed per-operation
from `TS29505_Subscription_Data.yaml` directly, not assumed uniform) -- the first UDR context-data
resource in this project combining all four real operations, including a real RFC 6902 patch
(`ModifyIpSmGwContext`) matching `AmfContextStore`'s own established patch standard, not the RFC
7396 merge-patch style `SmPolicyDataStore`/`AmPolicyDataStore` use. Takes UDR's real resource-type
coverage from 12 to 13 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0098 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (no NF currently calls this new endpoint) -- task
#106 remains open, ~29 resources still a real, disclosed gap.

## ADR-0099 -- gap-closure task #106 continuation: UDR real Message Waiting Data (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/mwd` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with `{"mwdList":[{"smscMapAddress":"+15551234567"}]}` on a new `ueId` | Live curl, real `201 Created` with `Location` header + created document in the body |
| `GET` immediately after the create `PUT` | Live curl, real `200` with the identical document |
| `PUT` again on the same `ueId` with a malformed `smscDiameterAddress` (plain string, not the real nested object) | Live curl, real `400` `ProblemDetails` -- confirms the DTO's real mandatory-field validation, not a bug |
| `PUT` again with a spec-correct `smscDiameterAddress` object (`name`+`realm`) | Live curl, real `204` -- genuinely distinct from the `201` create path above |
| `PATCH` (`application/json-patch+json`, RFC 6902 `replace` on `/mwdList/0/smscMapAddress`) | Live curl, real `204` |
| `GET` immediately after `PATCH` | Live curl, real `200` confirming the patched value |
| `DELETE`, then `GET` again | Live curl, real `204` then real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_mwd` confirms the persisted two-entry `mwdList` matches the API response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, distinct 201-vs-204 PUT response codes (unlike `IpSmGwContextStore`'s own always-`204` PUT)
-- `MessageWaitingDataStore::put()` reuses `AmfContextStore`'s own `xmax = 0` UPSERT idiom to
report the distinction in one statement. Takes UDR's real resource-type coverage from 13 to 14 of
free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0099 in
`docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred (no NF
currently calls this new endpoint) -- task #106 remains open, ~28 resources still a real,
disclosed gap.

## ADR-0100 -- gap-closure task #106 continuation: UDR real Roaming Information (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/roaming-information` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with `{"roaming":true,"servingPlmn":{"mcc":"001","mnc":"01"}}` on a new `ueId` | Live curl, real `201 Created` with `Location` header + created document in the body |
| `GET` immediately after the create `PUT` | Live curl, real `200` with the identical document |
| `PUT` again on the same `ueId` with a changed `roaming`/`servingPlmn` | Live curl, real `204` -- genuinely distinct from the `201` create path above |
| `GET` after the update `PUT` | Live curl, real `200` confirming the updated document |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_roaming_information` confirms the persisted document matches the API response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, simple `PUT`+`GET`-only resource (no PATCH/DELETE, confirmed by direct YAML read, same
shape as `AmfNon3GppContextStore`), with the same real 201-vs-204 `xmax = 0` UPSERT idiom already
used by `AmfContextStore`/`AmfNon3GppContextStore`/`MessageWaitingDataStore`. Takes UDR's real
resource-type coverage from 14 to 15 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0100 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (no NF currently calls this new endpoint) -- task
#106 remains open, ~27 resources still a real, disclosed gap.

## ADR-0101 -- gap-closure task #106 continuation: UDR real PEI Information (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/pei-info` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with `{"pei":"imei-490154203237518"}` on a new `ueId` | Live curl, real `201 Created` with `Location` header + created document in the body |
| `GET` immediately after the create `PUT` | Live curl, real `200` with the identical document |
| `PUT` again on the same `ueId` with a changed `pei` plus `previousPei` | Live curl, real `204` -- genuinely distinct from the `201` create path above |
| `GET` after the update `PUT` | Live curl, real `200` confirming the updated document, including the composed `PeiUpdateInfoExt` field `previousPei` alongside the base `pei` field -- proves the real `allOf` flattening (`PeiUpdateInfo_Subscription_Data`) is correct end-to-end |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_pei_info` confirms the persisted document matches the API response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real `allOf` schema composition (`TS29505_Subscription_Data.yaml`'s own `PeiUpdateInfo` = base
`TS29503_Nudm_UECM.yaml` `PeiUpdateInfo` + local `PeiUpdateInfoExt`), correctly generated and
disambiguated by sbi-codegen as `PeiUpdateInfo_Subscription_Data` -- confirmed already generated
before writing any application code, no new codegen work needed. Same real 201-vs-204 `xmax = 0`
UPSERT idiom already used by `RoamingInformationStore` and others. Takes UDR's real resource-type
coverage from 15 to 16 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0101 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (no NF currently calls this new endpoint) -- task
#106 remains open, ~26 resources still a real, disclosed gap.

## ADR-0102 -- gap-closure task #106 continuation: UDR real Enhanced Coverage Restriction Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/coverage-restriction-data` for the real seeded SUPI `imsi-999700000000001` | Live curl, real `200` with the exact seeded `plmnEcInfoList`/`ecRestrictionDataNb` document |
| `GET` for an unseeded SUPI | Live curl, real `404` |
| Genuine PostgreSQL persistence across startup | Direct `psql` query against `udr_coverage_restriction_data` confirms both real seeded rows (`imsi-999700000000001`/`...002`) persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId referencing this path,
no create/update operation exists at all in the spec), same shape as `ProvisionedDataStore`
(ADR-0069) -- seeded at startup rather than exposed for live creation, since the real spec assumes
out-of-band provisioning for this data. Takes UDR's real resource-type coverage from 16 to 17 of
free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0102 in
`docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred
(`ecRestrictionDataWb` left unpopulated in the seed data, no NF currently calls this new endpoint)
-- task #106 remains open, ~25 resources still a real, disclosed gap.

## ADR-0103 -- gap-closure task #106 continuation: UDR real LCS Privacy Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/lcs-privacy-data` for the real seeded SUPI `imsi-999700000000001` | Live curl, real `200` with the exact seeded `lpi.locationPrivacyInd` document |
| `GET` for an unseeded SUPI | Live curl, real `404` |
| Genuine PostgreSQL persistence across startup | Direct `psql` query against `udr_lcs_privacy_data` confirms both real seeded rows (`imsi-999700000000001`/`...002`) persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId referencing this path,
no create/update operation exists at all in the spec), same shape as `CoverageRestrictionDataStore`
(ADR-0102) -- seeded at startup with a real enum value (`LOCATION_ALLOWED` from
`LocationPrivacyInd`), not a fabricated field. Takes UDR's real resource-type coverage from 17 to
18 of free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0103 in
`docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred (most of
`LcsPrivacyData`'s own other optional fields left unpopulated in the seed data, no NF currently
calls this new endpoint) -- task #106 remains open, ~24 resources still a real, disclosed gap.

## ADR-0104 -- gap-closure task #106 continuation: UDR real LCS Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/lcs-subscription-data` for the real seeded SUPI `imsi-999700000000001` | Live curl, real `200` with the exact seeded `pruInd`/`userPlanePosIndLmf` document |
| `GET` for an unseeded SUPI | Live curl, real `404` |
| Genuine PostgreSQL persistence across startup | Direct `psql` query against `udr_lcs_subscription_data` confirms both real seeded rows (`imsi-999700000000001`/`...002`) persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId referencing this path,
no create/update operation exists at all in the spec), same shape as `LcsPrivacyDataStore`
(ADR-0103) -- seeded at startup with a real enum value (`NON_PRU` from `PruInd`) and the schema's
own documented `default: false` for `userPlanePosIndLmf`, neither fabricated. Takes UDR's real
resource-type coverage from 18 to 19 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0104 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (`configuredLmfId`/`lpHapType` left unpopulated in
the seed data, no NF currently calls this new endpoint) -- task #106 remains open, ~23 resources
still a real, disclosed gap.

## ADR-0105 -- gap-closure task #106 continuation: UDR real LCS Mobile Originated Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/lcs-mo-data` for the real seeded SUPI `imsi-999700000000001` | Live curl, real `200` with the exact seeded `allowedServiceClasses` document |
| `GET` for an unseeded SUPI | Live curl, real `404` |
| Genuine PostgreSQL persistence across startup | Direct `psql` query against `udr_lcs_mo_data` confirms both real seeded rows (`imsi-999700000000001`/`...002`) persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions; `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId referencing this path,
no create/update operation exists at all in the spec), same shape as `LcsSubscriptionDataStore`
(ADR-0104) -- seeded at startup with a real enum value (`BASIC_SELF_LOCATION` from
`LcsMoServiceClass`) for the mandatory `allowedServiceClasses` field, not fabricated. Takes UDR's
real resource-type coverage from 19 to 20 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0105 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (`moAssistanceDataTypes` left unpopulated in the seed
data, no NF currently calls this new endpoint) -- task #106 remains open, ~22 resources still a
real, disclosed gap.

## ADR-0106 -- gap-closure task #106 continuation: UDR real LCS Broadcast Assistance Data (provisioned-data sibling)

| Requirement | Test |
|---|---|
| `GET .../provisioned-data/lcs-bca-data` for the real seeded `(ueId, servingPlmnId)` pair | Live curl, real `200` with the exact seeded `locationAssistanceType` document |
| `GET` for the same `ueId` with an unseeded `servingPlmnId` | Live curl, real `404` |
| Sibling `am-data` route regression check | Live curl against the same `(ueId, servingPlmnId)`'s `am-data` route -- real `200`, unchanged, confirming the `ProvisionedDataStore` extension caused no regression |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_provisioned_data` confirms both real seeded rows' `lcs_bca_data` column matches the API response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real sibling of the already-closed `provisioned-data` group (`am-data`/
`smf-selection-subscription-data`/`sm-data`, ADR-0069) -- same real `(ueId, servingPlmnId)` key
shape, added as a 4th column on the existing `ProvisionedDataStore`/`udr_provisioned_data` rather
than a new store/table, reusing the existing generic `get_provisioned_column()` helper. Real,
disclosed detail: `CREATE TABLE IF NOT EXISTS` alone is a no-op against the already-existing
table, so an explicit `ALTER TABLE ... ADD COLUMN IF NOT EXISTS` was required to actually apply
the new column to the live database -- confirmed via `\d udr_provisioned_data` before live
verification. Takes UDR's real resource-type coverage from 20 to 21 of free5GC's ~42+ real
TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0106 in `docs/DECISIONS.md` for
full disclosure, including what remains deliberately deferred (no NF currently calls this new
endpoint) -- task #106 remains open, ~21 resources still a real, disclosed gap.

## ADR-0107 -- gap-closure task #106 continuation: UDR real Parameter Provision (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/pp-data` on an unseeded `ueId` | Live curl, real `404` |
| `PATCH` (`application/json-patch+json`, RFC 6902 `add` on `/stnSr`) with no prior document | Live curl, real `200` with the document originated via upsert -- no `PUT`/`POST` exists for this resource |
| `GET` immediately after the originating `PATCH` | Live curl, real `200` confirming persistence |
| `PATCH` again (RFC 6902 `replace` on `/stnSr`) | Live curl, real `200` with the updated document |
| `GET` after the update `PATCH` | Live curl, real `200` confirming the update |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_pp_data` confirms the persisted document matches the API's final response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `GET`+`PATCH`-only resource (no PUT/DELETE, no POST/create -- confirmed by direct YAML read),
`PpDataStore::apply_patch()` byte-for-byte matching `AuthenticationSubscriptionDataStore`'s own
upsert-capable RFC 6902 pattern. Takes UDR's real resource-type coverage from 21 to 22 of
free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0107 in
`docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred
(`pp-data`'s own siblings `pp-data-store`/`pp-profile-data`, no NF currently calls this new
endpoint) -- task #106 remains open, ~20 resources still a real, disclosed gap.

## ADR-0108 -- gap-closure task #106 continuation: UDR real Parameter Provision profile Data (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/pp-profile-data` for the real seeded SUPI `imsi-999700000000001` | Live curl, real `200` with the exact seeded `allowedMtcProviders` document |
| `GET` for an unseeded SUPI | Live curl, real `404` |
| Genuine PostgreSQL persistence across startup | Direct `psql` query against `udr_pp_profile_data` confirms both real seeded rows (`imsi-999700000000001`/`...002`) persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId referencing this path,
no create/update operation exists at all in the spec), seeded at startup with the real, documented
special key `"ALL"` from `PpProfileData`'s own description text, not fabricated. Takes UDR's real
resource-type coverage from 22 to 23 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0108 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (`pp-data-store`, no NF currently calls this new
endpoint) -- task #106 remains open, ~19 resources still a real, disclosed gap.

## ADR-0109 -- gap-closure task #106 continuation: UDR real Provisioned Parameter Data Entry (pp-data-store)

| Requirement | Test |
|---|---|
| `GET .../pp-data-store/{afInstanceId}` on an unseeded `(ueId, afInstanceId)` | Live curl, real `404` |
| `GET .../pp-data-store` (list) before any entries | Live curl, real `200` with an empty `ppDataEntryList` |
| `PUT` with `{"referenceId":42}` on a new key | Live curl, real `201 Created` with `Location` header + created document |
| `GET` single immediately after the create `PUT` | Live curl, real `200` |
| `GET` list after the `PUT` | Live curl, real `200` with the one real entry present |
| `PUT` again on the same key with a changed `referenceId` | Live curl, real `204` -- genuinely distinct from the `201` create path |
| Update path genuinely applies (not just a `204` status) | Separate `(ueId, afInstanceId)` pair: `PUT` `referenceId:1` (`201`) -> `PUT` `referenceId:2` (`204`) -> `GET` confirms `referenceId:2` |
| `DELETE`, then `GET` single again | Live curl, real `204` then real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_pp_data_entry` confirms zero rows remain for the deleted key |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, richer operation set than the other `pp-*` siblings (PUT+GET+DELETE plus a real sibling
collection GET, composite `(ueId, afInstanceId)` key matching `SmfRegistrationStore`'s own
`(ueId, pduSessionId)` shape) -- `PpDataEntryStore::put()` reuses the established `xmax = 0`
UPSERT idiom for the real 201-vs-204 distinction; `list_for_ue()` matches
`SmfRegistrationStore::list_for_ue()`'s own pattern exactly. Takes UDR's real resource-type
coverage from 23 to 24 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0109 in `docs/DECISIONS.md` for full disclosure,
including what remains deliberately deferred (no NF currently calls these new endpoints) -- task
#106 remains open, ~18 resources still a real, disclosed gap.

## ADR-0110 -- gap-closure task #106 continuation: UDR real individual Shared Data (first non-per-UE resource)

| Requirement | Test |
|---|---|
| `GET /subscription-data/shared-data/{sharedDataId}` for the real seeded `sharedDataId` `10000-default` | Live curl, real `200` with the exact seeded document |
| `GET` for an unseeded `sharedDataId` | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_shared_data` confirms the single seeded row persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId under the
`/subscription-data/shared-data*` prefix, no create/update operation exists at all), and the
**first UDR resource in this project genuinely not keyed per-UE** -- `sharedDataId` alone, real
3GPP concept of operator-shared default profile data reused across many UEs. Seeded once (not
looped over the two test SUPIs), with a real, spec-pattern-conformant identifier
(`SharedDataId`'s own `^[0-9]{5,6}-.+$`), not fabricated. Takes UDR's real resource-type coverage
from 24 to 25 of free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0110 in `docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred
(the real sibling collection resource `GetSharedData`, which needs array-query-parameter parsing
this project has no precedent for yet; no NF currently calls this new endpoint) -- task #106
remains open, ~17 resources still a real, disclosed gap.

## ADR-0111 -- gap-closure task #106 continuation: UDR real Operator-Specific Data Container (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/operator-specific-data` on an unseeded `ueId` | Live curl, real `404` |
| `PATCH` (`application/json-patch+json`, RFC 6902 `add` on `/customFlag`) with no prior document | Live curl, real `200` with the document originated via upsert -- no `PUT`/`POST` exists for this resource |
| `GET` immediately after the originating `PATCH` | Live curl, real `200` confirming persistence |
| `PATCH` again (RFC 6902 `replace` on `/customFlag/value`) | Live curl, real `200` with the updated document |
| `GET` after the update `PATCH` | Live curl, real `200` confirming the update |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_operator_specific_data` confirms the persisted document matches the API's final response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `GET`+`PATCH`-only resource (no PUT/DELETE, no POST/create -- confirmed by direct YAML read),
`OperatorSpecificDataStore::apply_patch()` byte-for-byte matching `PpDataStore`'s own
upsert-capable RFC 6902 pattern; real response shape is a raw map (no top-level wrapper struct),
same as `PpDataStore`'s own established handling. Takes UDR's real resource-type coverage from 25
to 26 of free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0111
in `docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred (no NF
currently calls these new endpoints) -- task #106 remains open, ~16 resources still a real,
disclosed gap.

## ADR-0112 -- gap-closure task #106 continuation: UDR real Event Exposure Data (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/ee-profile-data` for the real seeded SUPI `imsi-999700000000001` | Live curl, real `200` with the exact seeded `restrictedEventTypes` document |
| `GET` for an unseeded SUPI | Live curl, real `404` |
| Genuine PostgreSQL persistence across startup | Direct `psql` query against `udr_ee_profile_data` confirms both real seeded rows (`imsi-999700000000001`/`...002`) persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, genuinely GET-only resource (confirmed by grepping every operationId referencing this exact
path, only one, no create/update operation exists at all in the spec), distinct from this
project's own UDM-side `Nudm_EE` work (task #105) -- this is the real Nudr_DataRepository backing
document. Seeded at startup with a real enum value (`LOSS_OF_CONNECTIVITY` from `EventType`), not
fabricated. Takes UDR's real resource-type coverage from 26 to 27 of free5GC's ~42+ real
TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0112 in `docs/DECISIONS.md` for
full disclosure, including what remains deliberately deferred (`allowedMtcProvider`/
`iwkEpcRestricted` left unpopulated in the seed data, the real group-keyed sibling resource, no NF
currently calls this new endpoint) -- task #106 remains open, ~15 resources still a real,
disclosed gap.

## ADR-0113 -- gap-closure task #106 continuation: UDR real UE Policy Set (policy-data group)

| Requirement | Test |
|---|---|
| `GET /policy-data/ues/{ueId}/ue-policy-set` on an unseeded `ueId` | Live curl, real `404` |
| `PUT` with `{"subscCats":["cat1"]}` on a new `ueId` | Live curl, real `201 Created` with `Location` header + created document |
| `GET` immediately after the create `PUT` | Live curl, real `200` |
| `PUT` again with a changed `subscCats` | Live curl, real `204` -- genuinely distinct from the `201` create path above |
| `GET` after the update `PUT` | Live curl, real `200` confirming the update |
| `PATCH` (`application/merge-patch+json`, RFC 7396) with a changed `subscCats` | Live curl, real `204` with **no body** -- confirmed correct per the real spec's narrower response set (unlike `am-data`'s own `200`-with-body choice) |
| `GET` after the `PATCH` | Live curl, real `200` confirming the merge-patched value took effect |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_ue_policy_set` confirms the persisted document matches the API's final response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, richer resource combining a real `PUT` (create-or-replace, `AmfContextStore`-style
201-vs-204) with a real `PATCH` (merge-patch, `AmPolicyDataStore`-style) on the same resource --
the first `policy-data` group resource in this project with both. `UePolicySetStore::put()`
reuses the established `xmax = 0` UPSERT idiom; `merge_patch()` matches
`AmPolicyDataStore::merge_patch()`'s own pattern exactly. Takes UDR's real resource-type coverage
from 27 to 28 of free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0113 in `docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred
(`praInfos`/`uePolicySections` left unpopulated, the real PLMN-keyed sibling resource, no NF
currently calls these new endpoints) -- task #106 remains open, ~14 resources still a real,
disclosed gap.

## ADR-0114 -- gap-closure task #106 continuation: UDR real policy-data Operator-Specific Data

| Requirement | Test |
|---|---|
| `GET /policy-data/ues/{ueId}/operator-specific-data` on an unseeded `ueId` | Live curl, real `404` |
| `PATCH` (`application/json-patch+json`, RFC 6902 `add` on `/policyFlag`) with no prior document | Live curl, real `200` with the document originated via upsert -- no `PUT`/`POST` exists for this resource |
| `GET` immediately after the originating `PATCH` | Live curl, real `200` confirming persistence |
| `PATCH` again (RFC 6902 `replace` on `/policyFlag/value`) | Live curl, real `200` with the updated document |
| `GET` after the update `PATCH` | Live curl, real `200` confirming the update |
| Genuinely distinct from the `subscription-data`-scoped sibling | `GET` on the same `ueId`'s `subscription-data`-scoped `operator-specific-data` path independently returns real `404` -- confirms these are two separate resources, not aliases |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_policy_operator_specific_data` confirms the persisted document matches the API's final response |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `GET`+`PATCH`-only resource (no PUT/DELETE, no POST/create -- confirmed by direct YAML read),
`PolicyOperatorSpecificDataStore::apply_patch()` byte-for-byte matching `OperatorSpecificDataStore`'s
own upsert-capable RFC 6902 pattern; reuses the same real `OperatorSpecificDataContainer` schema
via a real cross-file `$ref`, but is a genuinely distinct resource (separate real operationId
pair) from the `subscription-data`-scoped one (ADR-0111) -- confirmed live, not just by spec
reading. Takes UDR's real resource-type coverage from 28 to 29 of free5GC's ~42+ real TS 29.504
resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0114 in `docs/DECISIONS.md` for full
disclosure, including what remains deliberately deferred (no NF currently calls these new
endpoints) -- task #106 remains open, ~13 resources still a real, disclosed gap.

## ADR-0115 -- gap-closure task #106 continuation: UDR real Sponsor Connectivity Data (second non-per-UE resource)

| Requirement | Test |
|---|---|
| `GET /policy-data/sponsor-connectivity-data/{sponsorId}` for the real seeded `sponsorId` `sponsor1` | Live curl, real `200` with the exact seeded `aspIds` document |
| `GET` for an unseeded `sponsorId` | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_sponsor_connectivity_data` confirms the single seeded row persisted correctly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, genuinely GET-only resource (confirmed by direct YAML read, no other operation exists for
this path), and the **second UDR resource in this project genuinely not keyed per-UE** (after
`shared-data`, ADR-0110) -- `sponsorId` alone, real 3GPP concept (TS 23.503) of
sponsored-data-connectivity policy. Seeded once (not looped over the two test SUPIs) with real,
disclosed representative test values. Real, disclosed simplification: the real spec's own
distinct `204` ("found but empty") vs `404` ("not found") is not modeled, same simple
existence-based store shape as every other GET-only UDR resource. Takes UDR's real resource-type
coverage from 29 to 30 of free5GC's ~42+ real TS 29.504 resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0115 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open, ~12 resources still a real, disclosed gap.

## ADR-0116 -- gap-closure task #106 continuation: UDR real individual BDT Data (richest policy-data resource yet)

| Requirement | Test |
|---|---|
| `GET /policy-data/bdt-data/{bdtReferenceId}` on an unseeded `bdtReferenceId` | Live curl, real `404` |
| `PATCH` on that same unseeded `bdtReferenceId` | Live curl, real `404` -- confirms `PATCH` is genuinely NOT upsert-capable for this resource (unlike every prior merge-patch resource) |
| `PUT` with `{"aspId":"asp1","numOfUes":10}` on a new key | Live curl, real `201` |
| `GET` immediately after the create `PUT` | Live curl, real `200` |
| `PUT` again with different values | Live curl, real `201` again (**not** `204`) -- confirms the real spec's single documented PUT status is honored literally |
| `PATCH` (`application/merge-patch+json`) with `{"numOfUes":30}` | Live curl, real `200` with the merged document (`aspId` unchanged, `numOfUes` updated) |
| `GET` after the `PATCH` | Live curl, real `200` confirming the merge |
| `DELETE`, then `GET` again | Live curl, real `204` then real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_bdt_data` confirms zero rows remain after the delete |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real, richest `policy-data` operation set closed so far (`GET`+`PUT`+`PATCH`+`DELETE`), with two
genuinely new, disclosed behavioral distinctions verified live: the real `PUT` only documents
`201` (no update-via-PUT status, unlike `ue-policy-set`'s own 201/200/204), and the real `PATCH`
is NOT upsert-capable (real `404` if the resource doesn't already exist, unlike `am-data`/
`ue-policy-set`'s own upsert-capable merge-patch). Takes UDR's real resource-type coverage from 30
to 31 of free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0116
in `docs/DECISIONS.md` for full disclosure, including what remains deliberately deferred (the real
sibling collection resource, array-query-param parsing gap, no NF currently calls these new
endpoints) -- task #106 remains open, ~11 resources still a real, disclosed gap.

## ADR-0117 -- gap-closure task #106 continuation: UDR real PLMN UE Policy Set

| Requirement | Test |
|---|---|
| `GET /policy-data/plmns/{plmnId}/ue-policy-set` on the seeded lab PLMN (`99970`) | Live curl, real `200` with `{"subscCats":["cat1"]}` |
| `GET` on an unseeded PLMN (`00101`) | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_plmn_ue_policy_set` confirms exactly one row, `plmn_id = '99970'`, matching the seeded body |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (`ReadPlmnUePolicySet`) reusing the same `UePolicySet` schema as the
per-UE `ue-policy-set` resource (ADR-0113) but genuinely distinct: keyed by `plmnId`, not `ueId`
-- an H-PLMN-scoped default policy set. Takes UDR's real resource-type coverage from 31 to 32 of
free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0117 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~10 resources still a real,
disclosed gap.

## ADR-0118 -- gap-closure task #106 continuation: UDR real Slice-specific Policy Control Data

| Requirement | Test |
|---|---|
| `GET /policy-data/slice-control-data/{snssai}` on an unseeded `snssai` (`1-000001`) | Live curl, real `404` |
| `PATCH` (`application/merge-patch+json`) with `{"remainMbrUl":"100 Mbps"}` on that same key, no prior create | Live curl, real `200` with the newly-originated document -- confirms `PATCH` is genuinely upsert-capable (no `PUT`/`POST` exists for this resource) |
| `GET` immediately after the originating `PATCH` | Live curl, real `200` matching |
| `PATCH` again with `{"remainMbrDl":"200 Mbps"}` | Live curl, real `200` with both fields present (merged, not replaced) |
| `GET` after the second `PATCH` | Live curl, real `200` confirming the merge |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_slice_control_data` confirms the persisted document matches the API's final response exactly (`{"remainMbrDl": "200 Mbps", "remainMbrUl": "100 Mbps"}`, key `1-000001`) |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `GET`+`PATCH`-only resource (`ReadSlicePolicyControlData`/`UpdateSlicePolicyControlData`,
RFC 7396 merge-patch) -- no `PUT`/`POST` create operation exists at all, so `merge_patch` is
upsert-capable, same disclosed precedent as `AmPolicyDataStore`/`SmPolicyDataStore`. Real,
disclosed: the YAML types the `{snssai}` path parameter as the `Snssai` object schema with no
documented bare-path-segment string encoding (checked, not assumed); this project reuses its own
already-disclosed `sst + '-' + sd` convention (ADR-0072/PCF's `snssai_map_key`) rather than
inventing a second answer to the same open question. Takes UDR's real resource-type coverage from
32 to 33 of free5GC's ~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0118 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~9 resources still
a real, disclosed gap.

## ADR-0119 -- gap-closure task #106 continuation: UDR real group-specific Policy Control Data

| Requirement | Test |
|---|---|
| `GET /policy-data/group-control-data/{intGroupId}` on an unseeded `intGroupId` (`00112233-100-01-AABBCCDDEE`) | Live curl, real `404` |
| `PATCH` (`application/merge-patch+json`) with `{"maxGroupMbrUl":"500 Mbps"}` on that same key, no prior create | Live curl, real `200` with the newly-originated document -- confirms `PATCH` is genuinely upsert-capable (no `PUT`/`POST` exists for this resource) |
| `GET` immediately after the originating `PATCH` | Live curl, real `200` matching |
| `PATCH` again with `{"remainGroupMbrDl":"50 Mbps"}` | Live curl, real `200` with both fields present (merged, not replaced) |
| `GET` after the second `PATCH` | Live curl, real `200` confirming the merge |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_control_data` confirms the persisted document matches the API's final response exactly (`{"maxGroupMbrUl": "500 Mbps", "remainGroupMbrDl": "50 Mbps"}`) |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `GET`+`PATCH`-only resource (`ReadGroupPolCtrlData`/`ModifyGroupPolCtrlData`, RFC 7396
merge-patch) -- no `PUT`/`POST` create operation exists at all, so `merge_patch` is
upsert-capable, same precedent as `slice-control-data` (ADR-0118). Keyed by `intGroupId`, the real
`GroupId` schema -- a plain string with no encoding ambiguity, unlike `slice-control-data`'s own
`snssai` key. While checking this resource's siblings, also confirmed and disclosed:
`mbs-session-pol-data`'s key is a genuinely deeper, deeply-nested `oneOf`/`anyOf` object with no
documented string encoding and no existing project precedent to reuse -- left deferred rather than
inventing a serialization. Takes UDR's real resource-type coverage from 33 to 34 of free5GC's
~42+ real TS 29.504 resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0119 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~8 resources still a real,
disclosed gap.

## ADR-0120 -- gap-closure task #106 continuation: UDR real GetRoutingIDs (Nudr_GroupIDmap, a distinct Nudr API)

| Requirement | Test |
|---|---|
| `GET /routing-ids` with both `nf-type` and `nf-group-id` query parameters missing | Live curl, real `400` |
| `GET` with only `nf-type` present | Live curl, real `400` |
| `GET` with the seeded pair (`nf-type=UDM&nf-group-id=udm-group-1`) | Live curl, real `200` with `{"routingIndicators":["0001"]}` |
| `GET` with an unseeded pair (`nf-type=SMF&nf-group-id=nonexistent`) | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_routing_ids` confirms exactly one row, `(UDM, udm-group-1)`, matching the seeded body |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `structural_conformance` passed; `udr` built clean |

Real `GET`-only resource from a genuinely **different** real Nudr API,
`TS29504_Nudr_GroupIDmap.yaml`'s `Nudr_GroupIDmap` service (`/nudr-group-id-map/v1`, scope
`nudr-group-id-map`) -- not `Nudr_DataRepository` (`/nudr-dr/v2`) like every other resource closed
in this series. Both are real Nudr APIs hosted by the same NF per TS 29.504, so implementing this
inside the existing `udr` binary is correct, but it does **NOT** count toward the "N of free5GC's
~42+ `Nudr_DataRepository` resources" metric -- still 34, unchanged from ADR-0119. Two real
required scalar query parameters, no path-parameter encoding ambiguity and no array-query-param
parsing needed. Chosen after explicit user confirmation once both remaining real
`Nudr_DataRepository` list-siblings turned out blocked (ADR-0119). See ADR-0120 in
`docs/DECISIONS.md` for full disclosure, including the real array-query-param gap also found on
`Nudr_GroupIDmap`'s own `/nf-group-ids` sibling -- task #106 remains open, ~8 real
`Nudr_DataRepository` resources still a disclosed gap (unchanged by this ADR).

## ADR-0121 -- gap-closure task #106 continuation: UDR real NIDD Authorization Info context-data (self-correction)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/nidd-authorizations` on an unseeded `ueId` | Live curl, real `404` |
| `PATCH` on that same unseeded `ueId` | Live curl, real `404` |
| `PUT` with a real spec-valid `AuthorizationInfo` body on a new key | Live curl, real `201` with the created document |
| `GET` immediately after the create `PUT` | Live curl, real `200` matching |
| `PUT` again with a different `authUpdateCallbackUri` | Live curl, real `204` (not `201`) -- confirms the real distinct-status UPSERT behavior |
| `PATCH` (`application/json-patch+json`) with a real `replace` op on `/dnn` | Live curl, real `204` |
| `GET` after the `PATCH` | Live curl, real `200` confirming the patched `dnn` |
| `DELETE`, then `GET` again | Live curl, real `204` then real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_nidd_authorization_info` confirms zero rows remain after the delete |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `PUT`+`GET`+`PATCH`+`DELETE` resource, same shape as `amf-3gpp-access`'s own context-data
resource (real distinct `201`-vs-`204` PUT, real RFC 6902 PATCH) plus a real `DELETE`. This ADR is
a self-correction: an earlier pass had lumped `nidd-authorizations` together with
`ee-subscriptions`/`sdm-subscriptions` as a deferred "deeply nested sub-subscription" bundle
without individually checking the real YAML -- it is genuinely a flat per-UE document.
`ee-subscriptions`/`sdm-subscriptions` were NOT re-verified this pass and remain genuinely
deferred. Takes UDR's real resource-type coverage from 34 to 35 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0121 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~7 resources still a real,
disclosed gap.

## ADR-0122 -- gap-closure task #106 continuation: UDR real Identity Data by SUPI or GPSI (plus re-verification of prior deferrals)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/identity-data` on an unseeded `ueId` | Live curl, real `404` |
| `PATCH` (`application/json-patch+json`) with a real `add` op on `/gpsiList`, no prior create | Live curl, real `200` with the newly-originated document -- confirms `apply_patch` is genuinely upsert-capable (no `PUT`/`POST` exists for this resource) |
| `GET` immediately after the originating `PATCH` | Live curl, real `200` matching |
| `PATCH` again with a real `add` op on `/supiList` | Live curl, real `200` with both fields present |
| `GET` after the second `PATCH` | Live curl, real `200` confirming both fields persist |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_identity_data` confirms the persisted document matches the API's final response exactly |
| No regression | Full `conformance_tests`: unchanged pass count (no new committed automated test this pass, same disclosed manual-live-verification precedent already established), zero regressions (325/325); `udr` built clean |

Real `GET`+`PATCH` resource (`GetIdentityData`/`ModifyIdentityData`, real RFC 6902 JSON Patch, NOT
merge-patch) -- no `PUT`/`POST` create operation exists at all, so `apply_patch` is
upsert-capable, same precedent as `pp-data`/`operator-specific-data`. Also individually
re-verified this pass (not re-bundled): `ee-subscriptions`/`sdm-subscriptions` are confirmed
genuinely deeply-nested subscription-lifecycle resources (server-generated `subsId` via `POST`,
further nested `amf-`/`smf-`/`hss-subscriptions` sub-collections, plus a parallel
`group-data`-scoped tree) -- the original deferral was correct. `/policy-data/subs-to-notify` also
confirmed genuinely deferred: real `POST`-based collection, server-generated `Location`, real
webhook callback registration, no existing project precedent for either. Takes UDR's real
resource-type coverage from 35 to 36 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0122 in `docs/DECISIONS.md` for full disclosure -- task
#106 remains open, ~6 resources still a real, disclosed gap.

## ADR-0123 -- gap-closure task #106 continuation: UDR real ODB Data (Query by SUPI or GPSI)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/operator-determined-barring-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"roamingOdb":"OUTSIDE_HOME_PLMN"}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_odb_data` confirms both seeded rows match |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (`GetOdbData`), same "provisioned out-of-band, seeded at startup" shape
as every other GET-only resource in this series -- no create/update operation exists for it at
all. Takes UDR's real resource-type coverage from 36 to 37 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0123 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~5 resources still a real,
disclosed gap.

## ADR-0125 -- gap-closure task #106 continuation: UDR real SMS Management Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/{servingPlmnId}/provisioned-data/sms-mng-data` on the seeded (SUPI, PLMN) pair | Live curl, real `200` with `{"mtSmsSubscribed":true}` |
| `GET` on the same SUPI with an unseeded PLMN | Live curl, real `404` |
| Sibling `am-data` column on the same row unaffected | Live curl, real `200` with the unchanged expected `nssai` body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_provisioned_data` confirms both seeded rows' `sms_mng_data` column matches |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (`QuerySmsMngData`), added as a new column on the existing
`udr_provisioned_data` table rather than a new table -- same real precedent ADR-0106 established
for `lcs-bca-data` (same `provisioned-data` group, same `(ueId, servingPlmnId)` key). Takes UDR's
real resource-type coverage from 37 to 38 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). The real sibling `.../provisioned-data/sms-data` resource was
found during the same survey and is a strong next candidate, deliberately left for its own turn.
See ADR-0125 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~4 resources
still a real, disclosed gap.

## ADR-0126 -- gap-closure task #106 continuation: UDR real SMS Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/{servingPlmnId}/provisioned-data/sms-data` on the seeded (SUPI, PLMN) pair | Live curl, real `200` with `{"smsSubscribed":true}` |
| `GET` on the same SUPI with an unseeded PLMN | Live curl, real `404` |
| Sibling `sms-mng-data` resource on the same row independently returns its own distinct body | Live curl, real `200` with `{"mtSmsSubscribed":true}` -- confirms the two resources are genuinely separate, not aliases |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_provisioned_data` confirms both seeded rows' `sms_data` and `sms_mng_data` columns hold their own distinct values |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (`QuerySmsData`), added as a new column on the existing
`udr_provisioned_data` table -- same real precedent ADR-0106 established for `lcs-bca-data`.
Genuinely distinct real resource from `sms-mng-data` (ADR-0125) -- separate operationId, separate
schema, confirmed by live side-by-side verification. Takes UDR's real resource-type coverage from
38 to 39 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md).
See ADR-0126 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~3 resources
still a real, disclosed gap.

## ADR-0127 -- gap-closure task #106 continuation: UDR real Trace Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/{servingPlmnId}/provisioned-data/trace-data` on the seeded (SUPI, PLMN) pair | Live curl, real `200` with `{"traceDepth":"MEDIUM","traceRef":"99970-A1B2C3"}` |
| `GET` on the same SUPI with an unseeded PLMN | Live curl, real `404` |
| Sibling `sms-data` column on the same row unaffected | Live curl, real `200` with the unchanged expected body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_provisioned_data` confirms both seeded rows' `sms_data` and `trace_data` columns hold their own correct, distinct values |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (`QueryTraceData`), added as a new column on the existing
`udr_provisioned_data` table -- same real precedent ADR-0106 established for `lcs-bca-data`. Real
response schema is a `oneOf` (full `TraceData` object or a bare `SharedDataId` string) -- handled
as opaque JSON, no special-casing needed since this store never strongly types sub-resource
bodies. Takes UDR's real resource-type coverage from 39 to 40 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0127 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open, ~2 resources still a real,
disclosed gap.

## ADR-0128 -- gap-closure task #106 continuation: UDR real V2X Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/v2x-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"nrV2xServicesAuth":{"vehicleUeAuth":"AUTHORIZED"}}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_v2x_data` confirms both seeded rows match |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (`QueryV2xData`), genuinely NOT part of the `provisioned-data` group --
keyed by `ueId` alone, so backed by its own new `udr_v2x_data` table/store rather than another
column on `udr_provisioned_data`. Takes UDR's real resource-type coverage from 40 to 41 of
free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0128 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed
remainder (`prose-data`, `uc-data`, `time-sync-data`, `group-data/*`, `nidd-authorization-data`,
and others) is real and larger than the "~1 left" free5GC-comparison figure alone suggests.

## ADR-0129 -- gap-closure task #106 continuation: UDR real ProSe Service Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/prose-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"proseServiceAuth":{"proseDirectDiscoveryAuth":"AUTHORIZED"}}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `v2x-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_prose_data` confirms both seeded rows match |
| No regression | Full `conformance_tests`: unchanged pass count (same disclosed manual-live-verification precedent already established for every GET-only seeded resource in this series), zero regressions (325/325); `udr` built clean |

Real `GET`-only resource (spec `operationId` literally `QueryPorseData` -- a real typo in
`TS29505_Subscription_Data.yaml` itself, cited as-is, not corrected), genuinely NOT part of the
`provisioned-data` group -- keyed by `ueId` alone, so backed by its own new `udr_prose_data`
table/store, same shape as `v2x-data` (ADR-0128). Takes UDR's real resource-type coverage from 41
to 42 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md) --
UDR now matches or exceeds free5GC's own count on this specific comparison metric. See ADR-0129 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed remainder
(`uc-data`, `time-sync-data`, `group-data/*`, `nidd-authorization-data`, and others) and the
genuinely deferred subsystems (`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`,
`pdtq-data`, `mbs-session-pol-data`) remain real, disclosed gaps.

## ADR-0130 -- gap-closure task #106 continuation: UDR real User Consent Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/uc-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"userConsentPerPurposeList":{"ANALYTICS":"CONSENT_GIVEN"}}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `prose-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_uc_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`QueryUserConsentData`), schema `UcSubscriptionData`
(`TS29503_Nudm_SDM.yaml`) -- a single optional `userConsentPerPurposeList` map, no `required`
fields at all. Genuinely NOT part of the `provisioned-data` group -- keyed by `ueId` alone, so
backed by its own new `udr_uc_data` table/store, same shape as `prose-data` (ADR-0129). Takes
UDR's real resource-type coverage from 42 to 43 of free5GC's ~42+ real `Nudr_DataRepository`
resources (docs/CAPABILITY_GAP_ANALYSIS.md) -- past the free5GC comparison baseline. See ADR-0130
in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed
remainder (`time-sync-data`, `group-data/*`, `nidd-authorization-data`, and others) and the
genuinely deferred subsystems (`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`,
`pdtq-data`, `mbs-session-pol-data`) remain real, disclosed gaps.

## ADR-0131 -- gap-closure task #106 continuation: UDR real Time Synchronization Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/time-sync-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"afReqAuthorizations":{"gptpAllowedInfoList":[{"dnn":"internet","gptpAllowed":true}]},"serviceIds":[{"reference":"ts-service-1"}]}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `uc-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_time_sync_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`QueryTimeSyncSubscriptionData`), schema `TimeSyncSubscriptionData`
(`TS29503_Nudm_SDM.yaml`) -- unlike the last several GET-only resources closed, this one has real
`required` fields (`afReqAuthorizations`, `serviceIds`), so the seed data is a minimal but
genuinely spec-valid body. Genuinely NOT part of the `provisioned-data` group -- keyed by `ueId`
alone, so backed by its own new `udr_time_sync_data` table/store, same shape as `uc-data`
(ADR-0130). Takes UDR's real resource-type coverage from 43 to 44 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0131 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed remainder
(`group-data/*`, `nidd-authorization-data`, `a2x-data`, `rangingsl-privacy-data`,
`ranging-slpos-data`, `5mbs-data`, and others) and the genuinely deferred subsystems
(`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`, `pdtq-data`, `mbs-session-pol-data`)
remain real, disclosed gaps.

## ADR-0133 -- gap-closure task #106 continuation: UDR real UE's Location Information (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/context-data/location` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"registrationLocationInfoList":[{"accessTypeList":["3GPP_ACCESS"],"amfInstanceId":"00000000-0000-4000-8000-00000000a001"}]}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `time-sync-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_location_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`QueryUeLocation`), schema `LocationInfo` (`TS29503_Nudm_UECM.yaml`) --
requires a non-empty `registrationLocationInfoList`. Genuinely NOT part of the `provisioned-data`
group -- keyed by `ueId` alone, so backed by its own new `udr_location_data` table/store, same
shape as `time-sync-data` (ADR-0131). Its real sibling `nidd-authorization-data` was surveyed in
the same pass and is genuinely blocked (not attempted): real required complex-object query
parameters (`single-nssai` via `content: application/json`) this project has no parsing precedent
for, the same class of gap already disclosed for `pdtq-data`. Takes UDR's real resource-type
coverage from 44 to 45 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0133 in `docs/DECISIONS.md` for full disclosure -- task
#106 remains open; the not-yet-surveyed remainder (`group-data/*`, `a2x-data`,
`rangingsl-privacy-data`, `ranging-slpos-data`, `5mbs-data`, and others) and the genuinely deferred
subsystems (`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`, `pdtq-data`,
`mbs-session-pol-data`, `nidd-authorization-data`) remain real, disclosed gaps.

## ADR-0134 -- gap-closure task #106 continuation: UDR real A2X Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/a2x-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"nrA2xServicesAuth":{"uavUeAuth":"AUTHORIZED"}}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `context-data/location` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_a2x_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`QueryA2xData`), schema `A2xSubscriptionData` (`TS29503_Nudm_SDM.yaml`)
-- every field optional, same shape as `v2x-data`/`prose-data`. Genuinely NOT part of the
`provisioned-data` group -- keyed by `ueId` alone, so backed by its own new `udr_a2x_data`
table/store. Takes UDR's real resource-type coverage from 45 to 46 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0134 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed remainder
(`group-data/*`, `rangingsl-privacy-data`, `ranging-slpos-data`, `5mbs-data`, and others) and the
genuinely deferred subsystems (`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`,
`pdtq-data`, `mbs-session-pol-data`, `nidd-authorization-data`) remain real, disclosed gaps.

## ADR-0135 -- gap-closure task #106 continuation: UDR real Ranging and Sidelink Positioning Privacy Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/rangingsl-privacy-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"rslppi":{"rangingSlPrivacyInd":"RANGINGSL_ALLOWED"}}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `a2x-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_rangingsl_privacy_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`QueryRangingSlPrivacyData`), schema `RangingSlPrivacyData`
(`TS29503_Nudm_SDM.yaml`) -- every top-level field optional. Real, disclosed: the spec's own
optional `fields` query parameter for field-selection filtering is not honored -- the full stored
document is always returned. Genuinely NOT part of the `provisioned-data` group -- keyed by `ueId`
alone, so backed by its own new `udr_rangingsl_privacy_data` table/store, same shape as `a2x-data`
(ADR-0134). Takes UDR's real resource-type coverage from 46 to 47 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0135 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed remainder
(`group-data/*`, `ranging-slpos-data`, `5mbs-data`, and others) and the genuinely deferred
subsystems (`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`, `pdtq-data`,
`mbs-session-pol-data`, `nidd-authorization-data`) remain real, disclosed gaps.

## ADR-0136 -- gap-closure task #106 continuation: UDR real Ranging and Sidelink Positioning Service Subscription Data

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/ranging-slpos-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"rangingSlPosAuth":{"rgSlPosPc5Auth":"AUTHORIZED"}}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `rangingsl-privacy-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_ranging_slpos_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`QueryRangingSlPosData`), schema `RangingSlPosSubscriptionData`
(`TS29503_Nudm_SDM.yaml`) -- every top-level field optional, no complex or required query
parameters at all. Genuinely NOT part of the `provisioned-data` group -- keyed by `ueId` alone, so
backed by its own new `udr_ranging_slpos_data` table/store, same shape as `rangingsl-privacy-data`
(ADR-0135). Takes UDR's real resource-type coverage from 47 to 48 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0136 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed remainder
(`group-data/*`, `5mbs-data`, and others) and the genuinely deferred subsystems
(`ee-subscriptions`/`sdm-subscriptions`, `subs-to-notify`, `pdtq-data`, `mbs-session-pol-data`,
`nidd-authorization-data`) remain real, disclosed gaps.

## ADR-0137 -- gap-closure task #106 continuation: UDR real 5MBS Subscription Data (Document)

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/5mbs-data` on the seeded SUPI (`imsi-999700000000001`) | Live curl, real `200` with `{"mbsAllowed":true}` |
| `GET` on an unseeded SUPI (`imsi-999700000000099`) | Live curl, real `404` |
| Sibling `ranging-slpos-data` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Second seeded SUPI (`imsi-999700000000002`) | Live curl, real `200` with matching body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_5mbs_data` confirms both seeded rows match |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`Query5mbsData`), schema `MbsSubscriptionData`
(`TS29503_Nudm_SDM.yaml`) -- every field optional, no complex or required query parameters at
all. Genuinely NOT part of the `provisioned-data` group -- keyed by `ueId` alone, so backed by
its own new `udr_5mbs_data` table/store, same shape as `ranging-slpos-data` (ADR-0136). Takes
UDR's real resource-type coverage from 48 to 49 of free5GC's ~42+ real `Nudr_DataRepository`
resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0137 in `docs/DECISIONS.md` for full
disclosure -- task #106 remains open; the not-yet-surveyed remainder (`group-data/*`,
`service-specific-authorization-data/{serviceType}`,
`context-data/service-specific-authorizations/{serviceType}`, bare `/subscription-data/{ueId}`,
`ue-update-confirmation-data/subscribed-snssais`, `ue-update-confirmation-data/subscribed-cag`,
and others) and the genuinely deferred subsystems (`ee-subscriptions`/`sdm-subscriptions`,
`subs-to-notify`, `pdtq-data`, `mbs-session-pol-data`, `nidd-authorization-data`) remain real,
disclosed gaps.

## ADR-0139 -- gap-closure task #106 continuation: UDR real Service Specific Authorization Info (Document)

| Requirement | Test |
|---|---|
| `PUT /subscription-data/{ueId}/context-data/service-specific-authorizations/{serviceType}` (create) | Live curl, real `201` with the created resource echoed back |
| `GET` after create | Live curl, real `200` with matching body |
| `PUT` again on the same key (update) | Live curl, real `204` (not `201`) |
| `GET` on an unseeded `serviceType` for the same SUPI | Live curl, real `404` |
| `PATCH` (real RFC 6902 `add` op) | Live curl, real `204`; subsequent `GET` reflects the added entry |
| `DELETE` | Live curl, real `204`; subsequent `GET` -> real `404` |
| Composite-key isolation (second SUPI/serviceType pair) | Live curl, real `201`, independently persisted |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_service_specific_auth_info` confirms the composite-key row |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real PUT+GET+PATCH+DELETE resource (`CreateServiceSpecificAuthorizationInfo`/
`GetServiceSpecificAuthorizationInfo`/`ModifyServiceSpecificAuthorizationInfo`/
`RemoveServiceSpecificAuthorizationInfo`), schema `ServiceSpecificAuthorizationInfo` -- required
`serviceSpecificAuthorizationList`, a map of `AuthorizationInfo` (`TS29503_Nudm_NIDDAU.yaml`)
keyed by `authId`, each requiring `snssai`/`dnn`/`mtcProviderInformation`/
`authUpdateCallbackUri`. Used the already-generated `sbi_gen::ServiceSpecificAuthorizationInfo`/
`sbi_gen::AuthorizationInfo` DTOs directly, no hand-written DTO. Real distinct 201-vs-204 PUT
response codes, real RFC 6902 `application/json-patch+json` PATCH, same shape as
`nidd-authorizations`'s own resource (ADR-0121). Composite `(ueId, serviceType)` key, same
precedent as `PpDataEntryStore` (ADR-0109). Its real sibling GET-only resource at
`service-specific-authorization-data/{serviceType}` was surveyed in the same pass and confirmed
genuinely blocked (not attempted): real required complex-object query parameters this project has
no parsing precedent for, the same class of gap already disclosed for `nidd-authorization-data`.
Takes UDR's real resource-type coverage from 49 to 50 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0139 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the not-yet-surveyed remainder
(`group-data/*`, bare `/subscription-data/{ueId}`,
`ue-update-confirmation-data/subscribed-snssais`, `ue-update-confirmation-data/subscribed-cag`,
and others) and the genuinely deferred subsystems (`ee-subscriptions`/`sdm-subscriptions`,
`subs-to-notify`, `pdtq-data`, `mbs-session-pol-data`, `nidd-authorization-data`,
`service-specific-authorization-data/{serviceType}`) remain real, disclosed gaps.

## ADR-0140 -- gap-closure task #106 continuation: UDR real Group Identifiers mapping resource

| Requirement | Test |
|---|---|
| `GET /subscription-data/group-data/group-identifiers?ext-group-id=...` | Live curl, real `200` with the full seeded record |
| `GET` with `int-group-id=...` (alternate key, same record) | Live curl, real `200` with the identical record |
| `GET` with an unseeded `ext-group-id` | Live curl, real `404` |
| `GET` with neither filter supplied | Live curl, real `400` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_identifiers` confirms the seeded row and its dual keys |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real `GET`-only resource (`GetGroupIdentifiers`), schema `GroupIdentifiers` -- every field
optional, no path parameters, genuinely NOT per-UE. Two real, optional query parameters
(`ext-group-id`/`int-group-id`) are alternate lookup keys for the same seeded record. Real,
disclosed simplifications: at least one filter is required (`400` otherwise, since the spec
defines no "list all groups" behavior this project has precedent for); the real `ue-id-ind`
parameter is not honored -- `ueIdList` is always included. First real `group-data` sub-resource
closed -- the rest of `group-data` remains genuinely deferred. Takes UDR's real resource-type
coverage from 50 to 51 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0140 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; the remainder of `group-data`, bare `/subscription-data/{ueId}`,
`ue-update-confirmation-data/subscribed-snssais`, `ue-update-confirmation-data/subscribed-cag`,
and the genuinely deferred subsystems remain real, disclosed gaps.

## ADR-0141 -- gap-closure task #106 continuation: UDR real NSSAI update ack (Document) resource

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/ue-update-confirmation-data/subscribed-snssais` before any `PUT` | Live curl, real `404` |
| `PUT` with `provisioningTime`/`ueUpdateStatus` (create) | Live curl, real `204` |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| `PUT` again with a different `ueUpdateStatus` (update) | Live curl, real `204` (not `201` -- no create-vs-update distinction exists per spec) |
| `GET` after second `PUT` | Live curl, real `200` reflecting the updated value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_nssai_ack_data` confirms the persisted, updated row |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real PUT+GET resource (`CreateOrUpdateNssaiAck`/`QueryNssaiAck`), schema `NssaiAckData` --
required `provisioningTime`/`ueUpdateStatus`. Real, disclosed: unlike every other PUT resource
this project has closed, the spec documents only a single `204` response for this PUT (no `201`)
-- genuinely no create-vs-update distinction, so `put()` returns `void`. Used the
already-generated `sbi_gen::NssaiAckData` DTO directly. First real
`ue-update-confirmation-data` sub-resource closed -- its siblings (`sor-data`, `upu-data`,
`subscribed-cag`) remain genuinely deferred. Also confirmed bare `/subscription-data/{ueId}`
genuinely blocked in the same pass: combines both the array-query-param (`dataset-names`) and
complex-object-query-param (`single-nssai`) gaps already disclosed elsewhere. Takes UDR's real
resource-type coverage from 51 to 52 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0141 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; the remainder of `group-data`, `ue-update-confirmation-data`'s own
`sor-data`/`upu-data`/`subscribed-cag` siblings, bare `/subscription-data/{ueId}` (now confirmed
blocked), and the genuinely deferred subsystems remain real, disclosed gaps.

## ADR-0142 -- gap-closure task #106 continuation: UDR real CAG update ack (Document) resource

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/ue-update-confirmation-data/subscribed-cag` before any `PUT` | Live curl, real `404` |
| `PUT` with `provisioningTime`/`ueUpdateStatus` | Live curl, real `204` |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| Sibling `subscribed-snssais` resource on the same UE (separate table) unaffected | Live curl, real `200` with the unchanged expected body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_cag_ack_data` confirms the persisted row |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean |

Real PUT+GET resource (`CreateCagUpdateAck`/`QueryCagAck`), schema `CagAckData` -- required
`provisioningTime`/`ueUpdateStatus`, identical shape to `NssaiAckData`. Same real, disclosed
204-only-PUT shape as its `subscribed-snssais` sibling (ADR-0141) -- no create-vs-update
distinction. Used the already-generated `sbi_gen::CagAckData` DTO directly. Takes UDR's real
resource-type coverage from 52 to 53 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0142 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; the remainder of `group-data`, `ue-update-confirmation-data`'s own
`sor-data`/`upu-data` siblings, bare `/subscription-data/{ueId}`, and the genuinely deferred
subsystems remain real, disclosed gaps.

## ADR-0143 -- gap-closure task #106 continuation: UDR real Authentication SoR + Authentication UPU (Document) resources

| Requirement | Test |
|---|---|
| `GET .../sor-data` before any `PUT` | Live curl, real `404` |
| `PUT sor-data` with `provisioningTime`/`ueUpdateStatus`/`meSupportOfSorCmci` | Live curl, real `204` |
| `GET sor-data` after `PUT` | Live curl, real `200` with matching body |
| `PATCH sor-data` (real RFC 6902 `application/json-patch+json`, `replace meSupportOfSorCmci`) | Live curl, real `204` |
| `GET sor-data` after `PATCH` | Live curl, real `200` with `meSupportOfSorCmci` updated, other fields unchanged |
| `GET .../upu-data` before any `PUT` | Live curl, real `404` |
| `PUT upu-data` with `provisioningTime`/`ueUpdateStatus`/`meSupportUHP` | Live curl, real `204` |
| `GET upu-data` after `PUT` | Live curl, real `200` with matching body |
| Idempotent re-`PUT upu-data` (new `provisioningTime`, `meSupportUHP` omitted) | Live curl, real `204`; `GET` confirms genuine overwrite |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_sor_data` and `udr_upu_data` independently confirms both persisted rows |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean before and after `clang-format-18` |

Real, disclosed asymmetry between the two sibling resources: `sor-data`
(`CreateAuthenticationSoR`/`QueryAuthSoR`/`UpdateAuthenticationSoR`, schema `SorData`) has a real
PUT+GET+PATCH -- the PATCH is RFC 6902, and per this project's established `nidd-authorizations`
precedent always returns `204` rather than the spec's optional `200`-with-`PatchResult` variant.
`upu-data` (`CreateAuthenticationUPU`/`QueryAuthUPU`, schema `UpuData`) has only PUT+GET, no
PATCH/DELETE at all -- confirmed by reading past its response block, no third operation exists.
Both PUTs are the same real 204-only shape as `subscribed-snssais`/`subscribed-cag` (no
create-vs-update distinction). Used the already-generated `sbi_gen::SorData` and
`sbi_gen::UpuData_Subscription_Data` DTOs directly (the latter suffixed since an unrelated,
distinct `UpuData` also exists under `TS29509_Nausf_UPUProtection.yaml`). This closes all four
`ue-update-confirmation-data` sub-resources surveyed to date and takes UDR's real resource-type
coverage from 53 to 54 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0143 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; the remainder of `group-data`, bare `/subscription-data/{ueId}`, and the
genuinely deferred subsystems remain real, disclosed gaps.

## ADR-0144 -- gap-closure task #106 continuation: UDR real group-data individual 5G VN Group Configuration resource

| Requirement | Test |
|---|---|
| `GET .../5g-vn-groups/{externalGroupId}` before any `PUT` | Live curl, real `404` |
| `PUT` with `5gVnGroupData.dnn`/`sNssai`/`5gVnGroupCommunicationInd` (real JSON keys, confirmed via generated `to_json`) | Live curl, real `201`, body echoed back |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| `PATCH` (real RFC 6902 `application/json-patch+json`, `replace 5gVnGroupCommunicationInd`) | Live curl, real `204` |
| `GET` after `PATCH` | Live curl, real `200` with the field updated, `dnn`/`sNssai` unchanged |
| `PATCH` against a nonexistent `externalGroupId` | Live curl, real `404` (confirms `apply_patch` NOT upsert-capable) |
| `DELETE` | Live curl, real `204` |
| `GET`/second `DELETE` after `DELETE` | Live curl, real `404` both times |
| Genuine PostgreSQL persistence at every step | Direct `psql` query against `udr_5g_vn_groups` independently confirms the row (and its absence after `DELETE`) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource (`Create5GVnGroup`/`Get5GVnGroupConfiguration`/
`Modify5GVnGroup`/`Delete5GVnGroup`), schema `5GVnGroupConfiguration` generated as
`sbi_gen::N5GVnGroupConfiguration`. Real, disclosed: PUT documents only `201` (same precedent as
`bdt-data`, ADR-0116); PATCH is real RFC 6902 (NOT `bdt-data`'s own RFC 7396 merge-patch). A real
test-side mistake (using the C++ field name instead of the real JSON key, e.g.
`n5gVnGroupCommunicationInd` vs. the real `5gVnGroupCommunicationInd`) was found and corrected
mid-verification -- disclosed in ADR-0144, not silently fixed. The sibling bare collection GET
(`Query5GVnGroup`) was surveyed and confirmed genuinely blocked on a real `style: form,
explode: false` array query parameter, same class already disclosed for `pdtq-data`/
`nf-group-ids`. This closes the second real `group-data` sub-resource (after
`group-identifiers`, ADR-0140) and takes UDR's real resource-type coverage from 54 to 55 of
free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0144 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; the remainder of
`group-data`, bare `/subscription-data/{ueId}`, and the genuinely deferred subsystems remain
real, disclosed gaps.

## ADR-0145 -- gap-closure task #106 continuation: UDR real group-data individual 5G MBS Group Membership resource

| Requirement | Test |
|---|---|
| `GET .../mbs-group-membership/{externalGroupId}` before any `PUT` | Live curl, real `404` |
| `PUT` with `multicastGroupMemb`/`afInstanceId` | Live curl, real `201`, body echoed back |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| `PATCH` (real RFC 6902 `application/json-patch+json`, `add` to `multicastGroupMemb`) | Live curl, real `204` |
| `GET` after `PATCH` | Live curl, real `200` with the new member present, `afInstanceId` unchanged |
| `PATCH` against a nonexistent `externalGroupId` | Live curl, real `404` (confirms `apply_patch` NOT upsert-capable) |
| `DELETE` | Live curl, real `204` |
| `GET` after `DELETE` | Live curl, real `404` |
| Sibling `5g-vn-groups/{externalGroupId}` resource on the same `externalGroupId` (separate table) unaffected | Live curl, real `404` (never created there) |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_mbs_group_membership` independently confirms the row (and its absence after `DELETE`) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource (`Create5GmbsGroup`/`GetMulticastMbsGroupMemb`/
`Modify5GmbsGroup`/`Delete5GmbsGroup`), schema `MulticastMbsGroupMemb` -- structurally an exact
twin of `5g-vn-groups/{externalGroupId}` (ADR-0144): PUT documents only `201`, PATCH real RFC
6902, NOT upsert-capable. Unlike `5GVnGroupConfiguration`, this schema's JSON keys match the C++
struct field names exactly (checked proactively via the generated `to_json`, given ADR-0144's own
disclosed test mistake). The sibling bare collection GET (`Query5GmbsGroup`) was surveyed and
confirmed genuinely blocked on the identical `gpsis` `style: form, explode: false` array
query-parameter class already disclosed for `5g-vn-groups`'s own `Query5GVnGroup` and
`pdtq-data`/`nf-group-ids`. This closes the third real `group-data` sub-resource (after
`group-identifiers`, ADR-0140, and `5g-vn-groups/{externalGroupId}`, ADR-0144) and takes UDR's
real resource-type coverage from 55 to 56 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0145 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; both bare collection GETs, the `/internal`/`/pp-profile-data` variants,
bare `/subscription-data/{ueId}`, and the genuinely deferred subsystems remain real, disclosed
gaps.

## ADR-0146 -- gap-closure task #106 continuation: UDR real group-data Event Exposure Data for a group resource

| Requirement | Test |
|---|---|
| `GET .../group-data/anyUE/ee-profile-data` (seeded) | Live curl, real `200` with an empty JSON object (real, all-optional schema) |
| `GET` on an unseeded group id (`extgroupid-nope@example.com`) | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_ee_profile_data` independently confirms the single seeded `anyUE` row |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean before and after `clang-format-18` |

Real GET-only resource (`QueryGroupEEData`), schema `EeGroupProfileData` -- every field optional.
No create/update operation exists in the spec at all. Genuinely NOT per-UE -- keyed by
`ueGroupId` (real schema `VarUeGroupId`, a plain string matching `anyUE` or
`extgroupid-...@...`, no encoding ambiguity), a real, distinct sibling of the already-closed
per-UE `{ueId}/ee-profile-data` resource (same resource name, different real path and keying).
Seeded at startup for the `"anyUE"` test case, same "surface first, wire consumers later"
precedent as other GET-only UDR resources. This closes the fourth real `group-data` sub-resource
(after `group-identifiers`, ADR-0140; `5g-vn-groups/{externalGroupId}`, ADR-0144;
`mbs-group-membership/{externalGroupId}`, ADR-0145) and takes UDR's real resource-type coverage
from 56 to 57 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0146 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; both bare collection GETs, the `/internal`/`/pp-profile-data` variants,
bare `/subscription-data/{ueId}`, and the genuinely deferred subsystems remain real, disclosed
gaps.

## ADR-0147 -- gap-closure task #106 continuation: UDR real aggregate UE Update Confirmation Data resource

| Requirement | Test |
|---|---|
| `GET .../ue-update-confirmation-data` before any sub-resource exists | Live curl, real `200` with an empty JSON object (deliberate design decision, not a `404`) |
| `PUT sor-data` then `PUT subscribed-cag` for the same UE | Live curl, real `204` both times |
| `GET` aggregate after both PUTs | Live curl, real `200` with `sorData`/`cagAckData` present (matching each PUT body), `upuData`/`nssaiAckData` genuinely absent |
| Individual `sor-data` GET route unaffected by the aggregate | Live curl, real `200` with the identical body |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_sor_data`/`udr_cag_ack_data` independently confirms both rows match the aggregate's composed response |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean before and after `clang-format-18` |

Real GET-only resource (`QueryUeUpdConf`), schema `UeUpdConfData` -- every field optional. Real,
disclosed design decision: implemented as a live composition over the four already-existing
sub-resource stores (`sor-data`/`upu-data`/`subscribed-snssais`/`subscribed-cag`, ADR-0141 through
ADR-0143) rather than a fifth, duplicate table -- avoids a real two-copies-out-of-sync risk. Always
returns `200` (empty object if nothing exists), since the aggregate document itself has no real
create/update path of its own to key a 404-vs-200 distinction off of. Genuinely unblocked unlike
every other bare-`{ueId}`-scoped aggregate surveyed so far (`context-data`'s own bare GET, bare
`/subscription-data/{ueId}` itself) -- only the already-established, not-honored
`supported-features` query parameter, no array/complex-object parameter. This closes UDR resource
#58 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0147 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; `group-data`'s
remaining genuinely-blocked resources, bare `/subscription-data/{ueId}`/`{ueId}/context-data`
(both confirmed blocked on their own array-query-param requirements), and the other genuinely
deferred subsystems remain real, disclosed gaps.

## ADR-0148 -- gap-closure task #106 continuation: UDR real Event Exposure Subscriptions collection + individual document resource

| Requirement | Test |
|---|---|
| `GET` collection before any `POST` | Live curl, real `200` with an empty array |
| `POST` with `callbackReference`/`monitoringConfigurations` | Live curl, real `201`, real freshly-generated UUID v4 `subsId` in `Location`, body echoed back |
| `GET` individual by that `subsId` | Live curl, real `200` with matching body |
| `GET` collection after the `POST` | Live curl, real `200` with a one-element array containing it |
| `PUT` update to the existing `subsId` | Live curl, real `204`; `GET` after reflects the update |
| `PUT` to a nonexistent `subsId` | Live curl, real `404` (confirms update-only semantics, no create-via-PUT) |
| `PATCH` (real RFC 6902 `application/json-patch+json`) | Live curl, real `204`; `GET` after reflects the patched value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_ee_subscriptions` independently confirms the composite-key row matches curl's response |
| `DELETE` | Live curl, real `204`; individual `GET` after real `404`; collection `GET` after real `200` with an empty array |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings, including a real `-Wsign-conversion` fix) before and after `clang-format-18` |

Real collection GET+POST and individual document GET+PUT+PATCH+DELETE
(`Queryeesubscriptions`/`CreateEeSubscriptions`/`QueryeeSubscription`/`UpdateEesubscriptions`/
`ModifyEesubscription`/`RemoveeeSubscriptions`), schema `EeSubscription`. This corrects ADR-0122's
own earlier blanket "genuinely deeply-nested" characterization of `ee-subscriptions`/
`sdm-subscriptions` -- on direct read, the collection GET's own `event-types`/`nf-identifiers`
array filters are genuinely optional (not the required-array-param class that blocks other
resources), so this project simply doesn't honor them, same precedent as
`rangingsl-privacy-data`'s own `fields` parameter. Three real, new shapes disclosed: server-generated
`subsId` (real UUID v4 via `sbi_core::generate_uuid_v4()`); genuinely update-only PUT (real spec
404 for a nonexistent resource, no create-via-PUT path); and a real collection+individual-document
CRUD pair backed by one composite-key table, a genuinely new shape for this project. Real,
disclosed scope narrowing: only the collection + individual document, NOT the deeper
`amf-subscriptions`/`smf-subscriptions`/`hss-subscriptions` nested sub-collections under each
`subsId`; `sdm-subscriptions` was not re-surveyed and remains deferred. This closes UDR resource
#59 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0148 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; `group-data`'s
remaining genuinely-blocked resources, bare `/subscription-data/{ueId}`/`{ueId}/context-data`,
`ee-subscriptions`'s own nested sub-collections, `sdm-subscriptions`, and the other genuinely
deferred subsystems remain real, disclosed gaps.

## ADR-0149 -- gap-closure task #106 continuation: UDR real Subs To Notify collection + individual document resource

| Requirement | Test |
|---|---|
| `GET` collection with no `ue-id` query param | Live curl, real `400` |
| `GET` collection with `ue-id` before any `POST` | Live curl, real `200` with an empty array |
| `POST` with `ueId`/`callbackReference`/`monitoredResourceUris` | Live curl, real `201`, real freshly-generated UUID v4 `subsId` in `Location`, body echoed back |
| `GET` collection with the matching `ue-id` after `POST` | Live curl, real `200` with a one-element array |
| `GET` collection with a *different* `ue-id` | Live curl, real `200` with an empty array (confirms the filter is genuinely enforced) |
| `GET` individual by `subsId` | Live curl, real `200` with matching body |
| `PATCH` (real RFC 6902 `application/json-patch+json`) | Live curl, real `204`; `GET` after reflects the patched value |
| `PATCH` against a nonexistent `subsId` | Live curl, real `404` (confirms `apply_patch` NOT upsert-capable) |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_subs_to_notify` independently confirms the row matches curl's response |
| `DELETE` | Live curl, real `204`; individual `GET` after real `404`; collection `GET` after real `200` with an empty array |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real collection GET+POST and individual document GET+PATCH+DELETE (genuinely no PUT exists for
this resource) -- `SubscriptionDataSubscriptions`/`QuerySubsToNotify`/
`QuerySubscriptionDataSubscriptions`/`ModifysubscriptionDataSubscription`/
`RemovesubscriptionDataSubscriptions`, schema `SubscriptionDataSubscriptions`. Resolves half of
this resource's original ADR-0122-era deferral reason (no server-generated-ID precedent) using
`ee-subscriptions`'s own newly-established precedent (ADR-0148, same session). Real design
decision: the collection isn't path-scoped under `{ueId}`, so this project stores the POST body's
own optional `ueId` field to back the real, required, non-array `ue-id` GET filter. Real,
disclosed, deliberately NOT built: the second half of the original deferral, the real
`onDataChange` webhook callback -- this project answers CRUD on subscriptions but has no outbound
webhook-delivery mechanism and no data-change-detection to trigger one. This closes UDR resource
#60 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0149 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; `group-data`'s
remaining genuinely-blocked resources, bare `/subscription-data/{ueId}`/`{ueId}/context-data`,
`ee-subscriptions`'s own nested sub-collections, `sdm-subscriptions`, and the other genuinely
deferred subsystems remain real, disclosed gaps.

## ADR-0150 -- CHF: raise AiQuotaSizer's hardcoded inference latency budget after a real, observed CI failure

| Requirement | Test |
|---|---|
| `AiQuotaSizer.LoadsRealOnnxModelAndPredicts`, run 5x isolated via `--gtest_filter` | Passed all 5 runs locally (85-110ms each), previously failed once in real CI at 40777us vs the old 5000us budget |
| No regression | Full `conformance_tests`: 325/325 pass (excluding the two disclosed, unrelated pre-existing flaky tests); `chf`/`conformance_tests` built clean |

Real, observed CI failure (run 32508134662, carrying ADR-0149 -- unrelated UDR-only commit,
confirming this was CI-runner-contention timing, not a code regression): the real ONNX inference
call took 40777us on that runner, 8x over `AiQuotaSizer`'s own hardcoded 5000us latency budget,
correctly triggering the class's own designed discard-on-budget fail-safe -- but the test asserts
a value IS returned. Raised the default to `chf::kDefaultAiQuotaLatencyBudget{50000}` (50ms), a
real, evidence-based value (headroom above the one observed 40777us data point, still tight
relative to this project's own SBI response-time budgets), added an optional
`CHF_AI_QUOTA_LATENCY_BUDGET_US` env var override (same never-hardcode-config precedent as the
two existing CHF AI env vars), documented in `deploy/docker/docker-compose.yml`. User-directed:
presented three real options (exclude the test / loosen the budget / leave as-is), user chose to
loosen the budget. See ADR-0150 in `docs/DECISIONS.md` for full disclosure, including the
explicit caveat that this is a best-effort mitigation from one data point, not a
statistically-derived value. **Real, live CI verification**: the very next CI run confirmed the
target test passes in real CI (0.16s), `100% tests passed, 0 tests failed out of 325`.

## ADR-0151 -- gap-closure task #106 continuation: UDR real SDM Subscriptions collection + individual document resource

| Requirement | Test |
|---|---|
| `GET` collection before any `POST` | Live curl, real `200` with an empty array |
| `POST` with `nfInstanceId`/`callbackReference`/`monitoredResourceUris` | Live curl, real `201`, real freshly-generated UUID v4 `subsId` in `Location`, body echoed back |
| `GET` individual by that `subsId` | Live curl, real `200` with matching body |
| `GET` collection after the `POST` | Live curl, real `200` with a one-element array |
| `PUT` update to the existing `subsId` | Live curl, real `204`; `GET` after reflects the update |
| `PUT` to a nonexistent `subsId` | Live curl, real `404` (confirms update-only semantics, no create-via-PUT) |
| `PATCH` (real RFC 6902 `application/json-patch+json`) | Live curl, real `204`; `GET` after reflects the patched value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_sdm_subscriptions` independently confirms the row matches curl's response |
| Sibling `ee-subscriptions` collection unaffected (separate table) | Live curl, real `200` with an empty array |
| `DELETE` | Live curl, real `204`; individual `GET` after real `404`; collection `GET` after real `200` with an empty array |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real collection GET+POST and individual document GET+PUT+PATCH+DELETE
(`Querysdmsubscriptions`/`CreateSdmSubscriptions`/`QuerysdmSubscription`/
`Updatesdmsubscriptions`/`ModifysdmSubscription`/`RemovesdmSubscriptions`), schema
`SdmSubscription` -- structurally identical to `ee-subscriptions` (ADR-0148): server-generated
`subsId` (real UUID v4), PUT genuinely update-only (real spec 404, identical wording to
`ee-subscriptions`'s own). This completes correcting ADR-0122's original bundled "genuinely
deeply-nested" deferral of `ee-subscriptions`/`sdm-subscriptions` together -- both now
individually surveyed and closed; only each one's own deeper nested sub-collection
(`amf-`/`smf-`/`hss-subscriptions` for the former, `hss-sdm-subscriptions` for the latter) remains
genuinely deferred. This closes UDR resource #61 of free5GC's ~42+ real `Nudr_DataRepository`
resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0151 in `docs/DECISIONS.md` for full
disclosure -- task #106 remains open; `group-data`'s remaining genuinely-blocked resources, bare
`/subscription-data/{ueId}`/`{ueId}/context-data`, both subscription resources' own nested
sub-collections, and the other genuinely deferred subsystems remain real, disclosed gaps.

## ADR-0152 -- gap-closure task #106 continuation: UDR real AMF Subscription Info (Document) nested under an individual ee-subscription

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with a one-element `AmfSubscriptionInfo` array | Live curl, real `201`, body echoed back as the same array |
| `GET` after `PUT` | Live curl, real `200` with matching array |
| `PUT` again with a two-element array | Live curl, real `204` (confirms is-new tracking correctly reports "update"); `GET` after shows both elements |
| `PATCH` (real RFC 6902 `[{"op":"remove","path":"/1"}]`, array-index operation) | Live curl, real `204`; `GET` after shows only the first element -- confirms RFC 6902 works against array-valued documents |
| `PATCH` against a nonexistent `subsId` | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_ee_amf_subscription_info` independently confirms the row's JSONB array matches curl's response |
| `DELETE` | Live curl, real `204`; `GET` after real `404` |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource ("Create AMF Subscriptions"/`GetAmfSubscriptionInfo`/
`ModifyAmfSubscriptionInfo`/`RemoveAmfSubscriptionsInfo`), nested under an individual
`ee-subscriptions/{subsId}` -- the first of `ee-subscriptions`' own nested sub-collections
surveyed directly rather than left blanket-deferred. Two real, new shapes for this project: the
document body is a JSON array of `AmfSubscriptionInfo` (`minItems: 1`), not a single object; PUT
documents a real distinct `201`-vs-`204` (same is-new-tracking precedent as `amf-3gpp-access`).
No referential integrity enforced against the parent `ee-subscriptions` resource (established
project precedent). This closes UDR resource #62 of free5GC's ~42+ real `Nudr_DataRepository`
resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0152 in `docs/DECISIONS.md` for full
disclosure -- task #106 remains open; `ee-subscriptions`' own `smf-subscriptions`/
`hss-subscriptions` sibling sub-collections, `sdm-subscriptions`' own `hss-sdm-subscriptions`,
`group-data`'s own parallel tree, and the other genuinely deferred subsystems remain real,
disclosed gaps.

## ADR-0153 -- gap-closure task #106 continuation: UDR real SMF Event Subscription Info (Document) nested under an individual ee-subscription

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with a single `SmfSubscriptionInfo` object wrapping `smfSubscriptionList` | Live curl, real `201`, body echoed back |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| `PUT` again with a different `subscriptionId` | Live curl, real `204` (is-new tracking correctly reports "update"); `GET` after confirms a genuine wholesale overwrite |
| `PATCH` (real RFC 6902, nested array-index path `/smfSubscriptionList/0/subscriptionId`) | Live curl, real `204`; `GET` after reflects the patched value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_ee_smf_subscription_info` independently confirms the row matches curl's response |
| Sibling `amf-subscriptions` resource for the same `(ueId, subsId)` unaffected | Live curl, real `404` (never created there, confirms separate storage) |
| `DELETE` | Live curl, real `204`; `GET` after real `404` |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource ("Create SMF Subscriptions"/`GetSmfSubscriptionInfo`/
`ModifySmfSubscriptionInfo`/`RemoveSmfSubscriptionsInfo`), nested under an individual
`ee-subscriptions/{subsId}` -- the second of `ee-subscriptions`' own nested sub-collections,
sibling of `amf-subscriptions` (ADR-0152). Genuinely different real shape confirmed on direct
read: the document body is a SINGLE `SmfSubscriptionInfo` object, not a bare array like its
sibling. A real test-side field-name mistake (`pduSessionId` instead of the real
`subscriptionId`) was found and corrected mid-verification, disclosed in ADR-0153 rather than
silently fixed. This closes UDR resource #63 of free5GC's ~42+ real `Nudr_DataRepository`
resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0153 in `docs/DECISIONS.md` for full
disclosure -- task #106 remains open; `ee-subscriptions`' own remaining `hss-subscriptions`
sibling, `sdm-subscriptions`' own `hss-sdm-subscriptions`, `group-data`'s own parallel tree, and
the other genuinely deferred subsystems remain real, disclosed gaps.

## ADR-0154 -- gap-closure task #106 continuation: UDR real HSS Subscription Info (Document) nested under an individual ee-subscription

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with a single `HssSubscriptionInfo` object wrapping `hssSubscriptionList` | Live curl, real `201`, body echoed back |
| `GET` after `PUT` returns `HssSubscriptionInfo`-shaped data (user-confirmed resolution of the real spec's own `SmfSubscriptionInfo` mis-citation) | Live curl, real `200` with matching body |
| `PUT` again with a different `hssInstanceId`/`subscriptionId` | Live curl, real `204` (is-new tracking correctly reports "update"); `GET` after confirms a genuine wholesale overwrite |
| `PATCH` (real RFC 6902, nested array-index path `/hssSubscriptionList/0/hssInstanceId`) | Live curl, real `204`; `GET` after reflects the patched value |
| `PATCH` against a nonexistent `subsId` | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_ee_hss_subscription_info` independently confirms the row matches curl's response |
| Sibling `amf-subscriptions`/`smf-subscriptions` tables unaffected | Direct `psql` query, real `0` rows in both, confirms separate storage |
| `DELETE` | Live curl, real `204`; `GET` after real `404`; `psql` confirms zero rows remained |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource ("Create HSS Subscriptions"/`GetHssSubscriptionInfo`/
`ModifyHssSubscriptionInfo`/`RemoveHssSubscriptionsInfo`), nested under an individual
`ee-subscriptions/{subsId}` -- the third and final of `ee-subscriptions`' own nested
sub-collections, sibling of `amf-subscriptions` (ADR-0152) and `smf-subscriptions` (ADR-0153).
Real, disclosed spec inconsistency found on direct read, asked and confirmed with the user before
implementing: the real spec's own `GetHssSubscriptionInfo` response literally cites
`SmfSubscriptionInfo`, not this resource's own `HssSubscriptionInfo` -- treated as a real spec
typo (same precedent as ADR-0129's `QueryPorseData` typo) per the user's explicit answer "Return
HssSubscriptionInfo (Recommended)". This closes UDR resource #64 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md), and completes all three of
`ee-subscriptions`' own nested sub-collections. See ADR-0154 in `docs/DECISIONS.md` for full
disclosure -- task #106 remains open; `sdm-subscriptions`' own `hss-sdm-subscriptions`,
`group-data`'s own parallel tree, and the other genuinely deferred subsystems remain real,
disclosed gaps.

## ADR-0155 -- gap-closure task #106 continuation: UDR real HSS SDM Subscription Info (Document) nested under an individual sdm-subscription

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with a single `HssSubscriptionInfo` object wrapping `hssSubscriptionList` | Live curl, real `204` (real spec documents no `201` for this operation, unlike its `ee-subscriptions`-nested siblings; matches the existing `sor-data`/`upu-data` 204-only upsert-PUT precedent) |
| `GET` after `PUT` returns `HssSubscriptionInfo`-shaped data (same real spec `SmfSubscriptionInfo` mis-citation as ADR-0154, same user-confirmed resolution applied without re-asking) | Live curl, real `200` with matching body |
| `PUT` again with a different `hssInstanceId`/`subscriptionId` | Live curl, real `204`; `GET` after confirms a genuine wholesale overwrite |
| `PATCH` (real RFC 6902, nested array-index path `/hssSubscriptionList/0/hssInstanceId`) | Live curl, real `204`; `GET` after reflects the patched value |
| `PATCH` against a nonexistent `subsId` | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_sdm_hss_subscription_info` independently confirms the row matches curl's response |
| Sibling `udr_ee_hss_subscription_info`/parent `udr_sdm_subscriptions` unaffected | Direct `psql` query, real `0` rows in both, confirms separate storage |
| `DELETE` | Live curl, real `204`; `GET` after real `404`; `psql` confirms zero rows remained |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource ("Create HSS SDM Subscriptions"/`GetHssSDMSubscriptionInfo`/
`ModifyHssSDMSubscriptionInfo`/`RemoveHssSDMSubscriptionsInfo`), nested under an individual
`sdm-subscriptions/{subsId}` -- `sdm-subscriptions`' own final deferred nested sub-collection.
Reuses the same `HssSubscriptionInfo` schema as `ee-subscriptions`' own `hss-subscriptions`
sibling (ADR-0154). Two real, disclosed findings on direct read: (1) the real spec's own PUT
documents only `204`, never `201` -- matches the existing `sor-data`/`upu-data` (ADR-0143)
precedent, not invented; (2) the real spec's own `GetHssSDMSubscriptionInfo` response again
literally cites `SmfSubscriptionInfo`, the identical typo class already resolved (and
user-confirmed) in ADR-0154 for the sibling resource -- applied here without re-asking. This
closes UDR resource #65 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0155 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; `group-data`'s own parallel tree and the other genuinely deferred
subsystems remain real, disclosed gaps.

## ADR-0156 -- gap-closure task #106 continuation: UDR real Event Exposure Group Subscriptions (group-data-scoped) collection + individual document, plus a real Location-header bug fix

| Requirement | Test |
|---|---|
| `GET` collection for a fresh `ueGroupId` | Live curl, real `200 []` |
| `POST` with `EeSubscription` body creates with server-generated `subsId` | Live curl, real `201`, body echoed back |
| `Location` header contains the real `ueGroupId`, not the unsubstituted `{ueGroupId}` route-pattern placeholder (real bug found and fixed) | Live curl inspection of the response headers |
| `GET` individual document returns a single `EeSubscription` object (real spec `items:`-without-`type:array` artifact, same handling as ADR-0148) | Live curl, real `200` |
| `PUT` update | Live curl, real `204`; `GET` after confirms overwrite |
| `PATCH` (real RFC 6902) | Live curl, real `204`; `GET` after reflects patched value |
| `PUT` against a nonexistent `subsId` (update-only PUT) | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_ee_subscriptions` independently confirms rows match curl's responses |
| Sibling `udr_ee_subscriptions` unaffected | Direct `psql` query confirms separate storage |
| Pre-existing `Location`-header bug re-verified fixed on `ee-subscriptions` and `sdm-subscriptions` | Fresh live `POST` to each, real `Location` header confirmed to contain the real UE ID, not `{ueId}` |
| `DELETE` | Live curl, real `204` per row; `psql` confirms zero `udr_group_ee_subscriptions` rows remained |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+POST collection / GET+PUT+PATCH+DELETE individual-document resource
(`QueryEeGroupSubscriptions`/`CreateEeGroupSubscriptions`/`QueryEeGroupSubscription`/
`UpdateEeGroupSubscriptions`/`ModifyEeGroupSubscription`/`RemoveEeGroupSubscriptions`), the
group-data-scoped sibling of `ee-subscriptions` (ADR-0148), keyed by `ueGroupId` instead of
`ueId`. Also fixes a real, disclosed pre-existing bug: the `Location` header on `ee-subscriptions`'
and `sdm-subscriptions`' own `POST`-create handlers was returning the unsubstituted route-pattern
placeholder instead of the real path-parameter value -- found live-verifying this new resource,
confirmed pre-existing by testing the already-shipped siblings, and fixed in all three in this
same ADR. This closes UDR resource #66 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0156 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; the group-data-scoped `amf-subscriptions`/`smf-subscriptions`/
`hss-subscriptions` nested sub-collections and the other genuinely deferred subsystems remain
real, disclosed gaps.

## ADR-0157 -- gap-closure task #106 continuation: UDR real AMF Group Subscription Info (Document), plus a project-wide fix for a real RFC 9110 Location-header conformance defect

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with array-valued `AmfSubscriptionInfo[]` body | Live curl, real `201`, body echoed back |
| `Location` header contains BOTH the real `ueGroupId` and real `subsId` (multi-path-parameter case for the new `resolved_location()` helper) | Live curl inspection of the response headers |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| `PUT` again with different values | Live curl, real `204`; `GET` after confirms overwrite |
| `PATCH` (real RFC 6902) | Live curl, real `204`; `GET` after reflects patched value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_amf_subscription_info` independently confirms the row matches curl's response |
| Sibling `udr_group_ee_subscriptions`/`udr_ee_amf_subscription_info` unaffected | Direct `psql` query, real `0` rows in both, confirms separate storage |
| `DELETE` | Live curl, real `204`; `GET` after real `404`; `psql` confirms zero rows remained |
| Pre-existing Location-header bug (found scoped to 3 routes in ADR-0156, found to affect ~20 project-wide this ADR) fixed via a shared `resolved_location()` helper | Presented to the user via `AskUserQuestion`; user chose "Fix all ~20 occurrences now"; the single-path-param case already proven live in ADR-0156, the multi-path-param case proven live by this ADR's own new resource; the remaining ~14 pre-existing occurrences share the same helper/code path, confirmed by a clean zero-warning build and full regression run rather than individually re-exercised live (disclosed narrowing, not glossed over) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource (`CreateAmfGroupSubscriptions`/`GetAmfGroupSubscriptions`/
`ModifyAmfGroupSubscriptions`/`RemoveAmfGroupSubscriptions`), the group-data-scoped sibling of
`ee-subscriptions/{subsId}/amf-subscriptions` (ADR-0152), keyed by `ueGroupId` instead of `ueId`.
Also fixes a real, disclosed, project-wide RFC 9110 Location-header conformance defect spanning
~20 of UDR's own `PUT`-with-`201`-create routes, including foundational Tier 1a resources
predating any gap-closure ADR -- user explicitly confirmed fixing all of them in this same turn
rather than deferring. This closes UDR resource #67 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0157 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; the group-data-scoped
`smf-subscriptions`/`hss-subscriptions` nested sub-collections and the other genuinely deferred
subsystems remain real, disclosed gaps.

## ADR-0158 -- gap-closure task #106 continuation: UDR real SMF Event Group Subscription Info (Document)

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with a single `SmfSubscriptionInfo` object wrapping `smfSubscriptionList` | Live curl, real `201`, body echoed back, `Location` header confirmed to contain both the real `ueGroupId` and real `subsId` |
| `GET` after `PUT` | Live curl, real `200` with matching body |
| `PUT` again with different values | Live curl, real `204`; `GET` after confirms a genuine wholesale overwrite |
| `PATCH` (real RFC 6902) | Live curl, real `204`; `GET` after reflects the patched value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_smf_subscription_info` independently confirms the row matches curl's response |
| Sibling `udr_group_amf_subscription_info`/`udr_ee_smf_subscription_info` unaffected | Direct `psql` query, real `0` rows in both, confirms separate storage |
| `DELETE` | Live curl, real `204`; `GET` after real `404`; `psql` confirms zero rows remained |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource (`CreateSmfGroupSubscriptions`/`GetSmfGroupSubscriptions`/
`ModifySmfGroupSubscriptions`/`RemoveSmfGroupSubscriptions`), the group-data-scoped sibling of
`ee-subscriptions/{subsId}/smf-subscriptions` (ADR-0153), keyed by `ueGroupId` instead of `ueId`.
Second of `group-data`'s own `ee-subscriptions/{subsId}/...` nested sub-collections closed
(sibling of `amf-subscriptions`, ADR-0157). This closes UDR resource #68 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0158 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; `group-data`'s own
`hss-subscriptions` nested sub-collection (the third and final sibling) and the other genuinely
deferred subsystems remain real, disclosed gaps.

## ADR-0159 -- gap-closure task #106 continuation: UDR real HSS Event Group Subscription Info (Document), completing group-data's own nested-subscription tree

| Requirement | Test |
|---|---|
| `GET` before any `PUT` | Live curl, real `404` |
| `PUT` with a single `HssSubscriptionInfo` object wrapping `hssSubscriptionList` | Live curl, real `201`, body echoed back, `Location` header confirmed to contain both the real `ueGroupId` and real `subsId` |
| `GET` after `PUT` returns `HssSubscriptionInfo`-shaped data (no spec typo on this resource, unlike ADR-0154/ADR-0155's siblings) | Live curl, real `200` with matching body |
| `PUT` again with different values | Live curl, real `204`; `GET` after confirms a genuine wholesale overwrite |
| `PATCH` (real RFC 6902) | Live curl, real `204`; `GET` after reflects the patched value |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_group_hss_subscription_info` independently confirms the row matches curl's response |
| Sibling `udr_group_amf_subscription_info`/`udr_group_smf_subscription_info`/`udr_ee_hss_subscription_info` unaffected | Direct `psql` query, real `0` rows in all three, confirms separate storage |
| Real spec inconsistency (`externalGroupId` parameter name on DELETE/PATCH/GET has no matching path placeholder; `ueGroupId` used throughout as the only bindable, consistent value) | Disclosed on direct read; not a genuine ambiguity requiring a user decision, so implemented directly without asking |
| `DELETE` | Live curl, real `204`; `GET` after real `404`; `psql` confirms zero rows remained |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 325/325 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET+PUT+PATCH+DELETE resource (`CreateHssGroupSubscriptions`/`GetHssGroupSubscriptions`/
`ModifyHssGroupSubscriptions`/`RemoveHssGroupSubscriptions`), the group-data-scoped sibling of
`ee-subscriptions/{subsId}/hss-subscriptions` (ADR-0154), keyed by `ueGroupId` instead of `ueId`.
Third and final of `group-data`'s own `ee-subscriptions/{subsId}/...` nested sub-collections
closed -- completes the whole group-data nested-subscription tree
(`amf-`/`smf-`/`hss-subscriptions`, ADR-0157/ADR-0158/ADR-0159), mirroring the `ueId`-scoped
`ee-subscriptions` family's own closure (ADR-0152/ADR-0153/ADR-0154). This closes UDR resource
#69 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md).
See ADR-0159 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; `group-data`'s
remaining genuinely-blocked resources and the other genuinely deferred subsystems remain real,
disclosed gaps.

## ADR-0161 -- real `style: form, explode: false` array-query-param parsing infra in sbi-core, plus its first real consumer: UDR's `QueryContextData`

| Requirement | Test |
|---|---|
| `split_form_array` splits an empty string into an empty vector | `SplitFormArray.EmptyStringReturnsEmptyVector`, real unit test |
| Splits a single value with no comma | `SplitFormArray.SingleValueNoComma` |
| Splits multiple comma-separated values | `SplitFormArray.MultipleValuesCommaSeparated` |
| Preserves real non-comma special characters (`@`/`-`/`.`) | `SplitFormArray.PreservesAlreadyDecodedNonCommaCharacters` |
| Real, observed `std::getline` behavior on an internal empty element (`"a,,b"`) | `SplitFormArray.EmptyElementsBetweenCommasAreKeptAsEmptyStrings` |
| Real, observed `std::getline` behavior on a trailing delimiter (`"a,b,"`) | `SplitFormArray.TrailingCommaProducesTrailingEmptyElementIsDropped` |
| `QueryContextData` missing the required `context-dataset-names` query param | Live curl, real `400` |
| Real comma-separated array param correctly parsed; requested-and-present datasets populated, requested-but-absent datasets omitted, an unrecognized (forward-compatible) name silently skipped | Live curl against a UE seeded with real `AMF_3GPP`/`PEI_INFO` data, real `200` with exactly the expected fields |
| All requested datasets absent for a fresh UE | Live curl, real `200 {}` (matches `ue-update-confirmation-data`'s own live-view precedent) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass (325 prior + 6 new), zero regressions; `sbi_core`/`udr` built clean (zero warnings) before and after `clang-format-18` |

Adds `sbi_core::http2::split_form_array()`, a real, tested, shared helper unblocking every UDR
resource whose real spec query params use OpenAPI's `style: form, explode: false` array
convention (`pdtq-data`, `nidd-authorization-data`, `Nudr_GroupIDmap`'s `/nf-group-ids`, bare
`/subscription-data/{ueId}`/`{ueId}/context-data`) -- confirmed by direct read to be the shared
blocker across all of them (docs/DECISIONS.md ADR-0160). Real, disclosed limitation: operates on
already-percent-decoded values, so a literal escaped comma can't be distinguished from a
delimiter -- narrow and disclosed, not silently assumed away. First real consumer:
`QueryContextData` (bare `/subscription-data/{ueId}/context-data`), a live-composed aggregate
over 11 already-existing UDR sub-resource stores, the same design as `ue-update-confirmation-data`
(ADR-0147). This closes UDR resource #70 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0161 in `docs/DECISIONS.md` for full disclosure --
task #106 remains open; the newly-unblocked resources above and the other genuinely deferred
subsystems remain real, disclosed gaps, now unblocked rather than blocked.

## ADR-0162 -- gap-closure task #106 continuation: UDR real PDTQ Data collection + individual document, the first resource genuinely unblocked by ADR-0161's array-parsing infra

| Requirement | Test |
|---|---|
| `GET` collection on a fresh install | Live curl, real `200 []` |
| `GET` individual before any `PUT` | Live curl, real `404` |
| `PUT` with a real `PdtqData` body (required `aspId`+`pdtqPolicy`, real `PdtqPolicy` shape confirmed from the generated struct after an initial 400) | Live curl, real `201`, body echoed back, `Location` header contains the real `pdtqReferenceId` |
| `GET` individual/collection after `PUT` | Live curl, real `200` reflecting the stored resource |
| `PATCH` (real RFC 7396 merge-patch) | Live curl, real `200` with the merged body; `GET` after confirms persistence |
| `PATCH` against a nonexistent `pdtqReferenceId` | Live curl, real `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_pdtq_data` independently confirms the row matches curl's response |
| `DELETE` | Live curl, real `204`; `GET` after real `404`; `psql` confirms zero rows remained |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real GET (collection) + GET/PUT/PATCH/DELETE (individual document) resource (`ReadPdtqData`/
`ReadIndividualPdtqData`/`CreateIndividualPdtqData`/`UpdateIndividualPdtqData`/
`DeleteIndividualPdtqData`), `TS29519_Policy_Data.yaml`. The first real UDR resource confirmed
genuinely unblocked (not merely a candidate) by ADR-0161's `split_form_array()` infra. Real,
disclosed: `pdtqReferenceId` is client-supplied, not server-generated; `CreateIndividualPdtqData`
documents only `201` (no `204`), matching the pre-existing `bdt-data` precedent; the collection
GET's own optional `pdtq-ref-ids` array filter is deliberately not honored, matching the
established "optional filter not honored" precedent for consistency. This closes UDR resource
#71 of free5GC's ~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See
ADR-0162 in `docs/DECISIONS.md` for full disclosure -- task #106 remains open;
`nidd-authorization-data`, `Nudr_GroupIDmap`'s `/nf-group-ids`, bare `/subscription-data/{ueId}`,
`group-data`'s own bare collection GETs, and `GetSSAuData` (deliberately deferred, ADR-0160)
remain real, disclosed gaps.

## ADR-0164 -- gap-closure task #106 continuation: UDR real GetNfGroupIDs (Nudr_GroupIDmap, a distinct Nudr API), the second resource genuinely unblocked by ADR-0161's array-parsing infra

| Requirement | Test |
|---|---|
| `GET /nf-group-ids` with both `nf-type` and `subscriberId` query parameters missing | Live curl, real `400` |
| `GET` with only `nf-type` present (`subscriberId` missing) | Live curl, real `400` |
| `GET` with the seeded pair (`nf-type=AMF&subscriberId=imsi-999700000000001`) | Live curl, real `200` with `{"AMF":"amf-group-01"}` |
| `GET` with two comma-separated `nf-type` values on the same seeded subscriber (`nf-type=AMF,SMF&subscriberId=imsi-999700000000001`) | Live curl, real `200` with `{"AMF":"amf-group-01","SMF":"smf-group-01"}`, confirming `split_form_array()`'s real comma-split behavior end-to-end |
| `GET` with an unseeded combination (`nf-type=PCF&subscriberId=imsi-999700000000001`) | Live curl, real `404` |
| `GET` with a mixed request (one present `nf-type`, one absent) against the second seeded subscriber | Live curl, real `200` with only the present entry -- confirms partial match still returns `200`, not `404` |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_nf_group_ids` independently confirms exactly the three seeded rows, matching every curl response |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real `GET`-only resource from the same genuinely **different** real Nudr API as `GetRoutingIDs`
(ADR-0120), `TS29504_Nudr_GroupIDmap.yaml`'s `Nudr_GroupIDmap` service (`/nudr-group-id-map/v1`)
-- not `Nudr_DataRepository` (`/nudr-dr/v2`). Does **NOT** count toward the "N of free5GC's ~42+
`Nudr_DataRepository` resources" metric -- still 71, unchanged from ADR-0162, same non-increment
precedent as `GetRoutingIDs`/ADR-0120. The second real UDR resource confirmed genuinely unblocked
(not merely a candidate) by ADR-0161's `split_form_array()` infra, closing the real REQUIRED
`nf-type` array query param. No create/update/delete operation exists anywhere in this service for
the mapping data itself (confirmed by direct read); seeded at startup, same precedent as
`routing_ids`/`group_identifiers`. Real, disclosed design choice: unlike the aggregate live-view
resources (`ue-update-confirmation-data`/`QueryContextData`, always `200`), this resource's own
response schema requires `minProperties: 1` and documents a real `404`, so an empty composed
result returns a real `404` -- honoring the spec literally, not a deviation. The sibling
`/nf-group-ids/subscriptions` change-notification family (real `onGroupIdMapChange` webhook
callback) was surveyed but deliberately deferred to its own future turn, same disclosed gap class
as `subs-to-notify`'s own lack of real webhook delivery. See ADR-0164 in `docs/DECISIONS.md` for
full disclosure -- task #106 remains open; `nidd-authorization-data`, bare
`/subscription-data/{ueId}`, `group-data`'s own bare collection GETs, `GetSSAuData` (deliberately
deferred, ADR-0160), and `Nudr_GroupIDmap`'s own subscription-management family remain real,
disclosed gaps.

## ADR-0165 -- gap-closure task #106 continuation: UDR real GetNiddAuData, whose real blocker turned out to be a different query-param shape than ADR-0161's array-parsing infra

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}/nidd-authorization-data` with all query parameters missing | Live curl, real `400` |
| `GET` with only `single-nssai` present | Live curl, real `400` |
| `GET` with a malformed (non-JSON) `single-nssai` value | Live curl, real `400` with a specific parse-error message |
| `GET` with a well-formed JSON `single-nssai` missing its required `sst` field | Live curl, real `400` |
| `GET` with the real seeded combination (`single-nssai={"sst":1,"sd":"000001"}&dnn=internet&mtc-provider-information=mtc-provider-1`) | Live curl, real `200` with `{"authorizationData":[{"supi":"imsi-999700000000001"}]}` |
| `GET` with an unseeded `dnn` | Live curl, real `404` |
| `GET` with `single-nssai={"sst":1}` (no `sd`) against the seeded `sd="000001"` row | Live curl, real `404`, confirming composite-key precision |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_nidd_authorization_data` independently confirms the one seeded row, matching the successful curl response exactly |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real `GET`-only resource (`GetNiddAuData`, `TS29505_Subscription_Data.yaml`), genuinely distinct
from the already-implemented `context-data/nidd-authorizations` CRUD resource (ADR-0121). Real
finding: this resource's actual blocker was **not** ADR-0161's array-query-param gap -- its real
REQUIRED `single-nssai` query param uses `content: application/json` (a JSON-encoded value,
decomposed to `sst`/`sd`), a genuinely different OpenAPI shape, handled with a direct
`json::parse()` of the already-percent-decoded query value rather than new shared infra (narrow
enough not to warrant it). Real REQUIRED `dnn`/`mtc-provider-information`; optional `af-id`
deliberately not honored. No create/update/delete exists anywhere in this project's in-scope APIs
for this document (real provisioning lives in UDM's own `Nudm_NIDDAU` service, out of scope here)
-- seeded at startup, same precedent as `routing_ids`/`nf_group_ids`. Real, disclosed: confirmed
`AuthorizationData` (this resource's own real response schema) is the same schema ADR-0160 found
cross-referenced by the deliberately-deferred `GetSSAuData` -- this resource's own spec text
confirms `AuthorizationData`'s real, unambiguous home is here, not there (does not change
`GetSSAuData`'s own already-settled deferred status). This closes UDR resource #72 of free5GC's
~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0165 in
`docs/DECISIONS.md` for full disclosure -- task #106 remains open; bare `/subscription-data/{ueId}`,
`group-data`'s own bare collection GETs, `GetSSAuData` (deliberately deferred, ADR-0160), and
`Nudr_GroupIDmap`'s own subscription-management family remain real, disclosed gaps.

## ADR-0166 -- gap-closure task #106 continuation: UDR real bare QueryUeSubscribedData, a 32-field aggregate composed entirely from already-closed sub-resources

| Requirement | Test |
|---|---|
| `GET /subscription-data/{ueId}` with no query parameters | Live curl, real `200` with 17 real fields populated (every already-seeded `ContextDataSets`/non-gated field), `niddAuthData` and the 7 `serving-plmn`-gated fields correctly absent |
| `GET` with `serving-plmn=99970` added | Live curl, real `200` with all 7 previously-gated `ProvisionedDataStore` fields now also present |
| `GET` with `dataset-names=LCS_PRIVACY,V2X` | Live curl, real `200` with exactly those 2 fields |
| `GET` with `dataset-names=UE_UPD_CONF` | Live curl, real `200` with 3 of 4 `UeUpdConfData` sub-fields (the unseeded 4th correctly absent), confirming one-name-expands-to-four-fields |
| `GET` with `dataset-names=AM` and no `serving-plmn` | Live curl, real `200 {}`, confirming the gap is real |
| Same request with `serving-plmn=99970` added | Live curl, real `200` with `amData` populated, confirming the gap is recoverable |
| `GET` with an unrecognized `dataset-names` value | Live curl, real `200 {}`, no error (forward-compatible) |
| `GET` against a nonexistent UE, no filter | Live curl, real `200 {}`, not `404` |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real bare `/subscription-data/{ueId}` resource (`QueryUeSubscribedData`,
`TS29505_Subscription_Data.yaml`). Response schema `UeSubscribedDataSets = ProvisionedDataSets
(21 fields) & ContextDataSets (the same 11 fields QueryContextData/ADR-0161 already composes) &
UeUpdConfData (the same 4 fields ue-update-confirmation-data/ADR-0147 already composes)` -- needed
**zero new stores or tables**, every output field already exists. Unlike `QueryContextData`'s own
REQUIRED filter, every one of this resource's own query params is genuinely optional: absent
`dataset-names` means "attempt everything." Real, disclosed: `serving-plmn` is optional here but
required by the underlying `ProvisionedDataStore`'s own composite key -- its 7 backed fields are
skipped (not fabricated) when absent, recoverable once supplied. `niddAuthData` is never composed
(a real, permanent gap: its own composite key needs `mtc-provider-information`, a param this
resource doesn't expose at all). `adjacent-plmns`/`single-nssai`/`dnn`/`ext-group-ids`/
`uc-purpose` accepted but not honored. Same `200`-always live-view design as `QueryContextData`/
`ue-update-confirmation-data`. Does **NOT** increment the `Nudr_DataRepository` count -- still 72,
unchanged from ADR-0165, since every composed field is already individually counted. See ADR-0166
in `docs/DECISIONS.md` for full disclosure -- task #106 remains open; `group-data`'s own bare
collection GETs remain a real, unblocked candidate; `GetSSAuData` (deliberately deferred,
ADR-0160) and `Nudr_GroupIDmap`'s own subscription-management family remain real, disclosed gaps.

## ADR-0167 -- gap-closure task #106 continuation: UDR real `group-data` bare `5g-vn-groups`/`mbs-group-membership` collection GETs, closing the last real array-parsing-infra-unblocked candidate

| Requirement | Test |
|---|---|
| `GET` both `5g-vn-groups` and `mbs-group-membership` collections on a fresh install | Live curl, real `200 {}` |
| `PUT 5g-vn-groups/group-A` with real `members` data, `PUT 5g-vn-groups/group-B` with an empty (every-field-optional) body | Live curl, both real `201` |
| `GET` the `5g-vn-groups` collection | Live curl, real `200` with both groups, matching each PUT exactly |
| `GET` the same collection with an unused `gpsis` filter appended | Live curl, identical real `200` result, confirming the filter is accepted but not honored |
| `PUT mbs-group-membership/mbs-group-X` with the real required `multicastGroupMemb` field, then `GET` the collection | Live curl, real `201` then real `200` with exactly that one entry |
| `GET 5g-vn-groups/group-A` (individual resource) immediately after | Live curl, still real `200` with the correct single document, confirming no route-shadowing |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_5g_vn_groups`/`udr_mbs_group_membership` independently confirms every row matches its curl response exactly |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings, one real `-Wsign-conversion` caught and fixed) before and after `clang-format-18` |

Real `GET`-only bare collection resources (`Query5GVnGroup`/`Query5GmbsGroup`,
`TS29505_Subscription_Data.yaml`), structural twins of their own already-closed individual-resource
siblings (`5g-vn-groups/{externalGroupId}`/`mbs-group-membership/{externalGroupId}`,
ADR-0144/ADR-0145) -- composed entirely from the same existing tables, no new schema. Real optional
`gpsis` array filter accepted but not honored (honoring it would require inspecting each group's
own member list, a real, separate, deliberately deferred piece of work), matching the established
"optional filter not honored" precedent. Real `200`-always on an empty map, matching `pdtq-data`'s
own bare-collection-GET precedent (ADR-0162) -- a literal listing of persisted rows, not a
composed live view. This closes UDR resources #73 and #74 of free5GC's ~42+ real
`Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md), and fully closes out this
series' own array-parsing-infra-unblocked candidate list from ADR-0161. See ADR-0167 in
`docs/DECISIONS.md` for full disclosure -- remaining real, disclosed gaps: `gpsis` filtering on
both collections; the `/internal`/`/pp-profile-data` variants under both paths (not surveyed);
`policy-data`'s `mbs-session-pol-data` (deferred, key-encoding ambiguity); `GetSSAuData`
(deliberately deferred, ADR-0160); `Nudr_GroupIDmap`'s own subscription-management family
(ADR-0164); real webhook delivery for `subs-to-notify`/`nf-group-ids/subscriptions`.

## ADR-0168 -- gap-closure task #106 continuation: UDR real `group-data` `5g-vn-groups/internal` + `mbs-group-membership/internal`, and a real router-ordering hazard found and fixed before it could ship a bug

| Requirement | Test |
|---|---|
| `PUT 5g-vn-groups/group-C` with a real, pattern-valid `internalGroupIdentifier` | Live curl, real `201` |
| `GET /5g-vn-groups/internal?internal-group-ids=<matching id>` | Live curl, real `200` with exactly `group-C` (groups lacking the field correctly excluded) |
| `GET /5g-vn-groups/internal` with the required param missing | Live curl, real `400` |
| `GET /5g-vn-groups/internal` with a non-matching id | Live curl, real `404` |
| **Critical**: none of the above show the individual-resource route's own error text | Live curl, confirms the literal `/internal` route is reached, not shadowed by the `{externalGroupId}` wildcard route |
| Identical sequence for `mbs-group-membership/internal` | Live curl, same real `200`/`400`/`404` results |
| `GET mbs-group-membership/mbs-group-Y` (individual resource) immediately after | Live curl, still real `200`, confirming no shadowing of anything else |
| Genuine PostgreSQL persistence | Direct `psql` query against both tables independently confirms every row matches its curl response exactly |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real `GET`-only resources (`Query5GVnGroupInternal`/`Query5GMbsGroupInternal`,
`TS29505_Subscription_Data.yaml`), filtering the same `list_all()` (ADR-0167) by each stored
group's own optional `internalGroupIdentifier` scalar field -- unlike the bare collection's own
`gpsis` member-list filter (deferred), this one is real and tractable, no new store method needed.
Real `404`-on-no-match (a genuine query-by-identifier, matching `GetNfGroupIDs`'s own precedent),
distinct from the bare collection's own always-`200` design. **Real router-ordering hazard found
and fixed before implementing**: direct read of `libs/sbi-core/src/http2_server.cpp`'s own
`try_match` confirmed the router has no literal-vs-wildcard priority -- routes match in
registration order, first match wins, and the literal 4-segment `/internal` path would have been
permanently shadowed by the already-registered, same-segment-count `{externalGroupId}` wildcard
route had it been registered afterward (the natural instinct every prior ADR in this series
followed). Fixed by registering both new `/internal` routes *before* their own individual-resource
GET routes, with an explicit `CRITICAL ROUTE-ORDERING REQUIREMENT` comment at each site; verified
live (not just reasoned about) that the correct route is actually reached. This closes UDR
resources #75 and #76 of free5GC's ~42+ real `Nudr_DataRepository` resources
(docs/CAPABILITY_GAP_ANALYSIS.md). See ADR-0168 in `docs/DECISIONS.md` for full disclosure --
remaining real, disclosed gaps: `5g-vn-groups`/`mbs-group-membership`'s own `/pp-profile-data`
variants (need a genuinely new store/schema, not surveyed); `gpsis` filtering on the bare
collections; `policy-data`'s `mbs-session-pol-data`; `GetSSAuData` (deliberately deferred,
ADR-0160); `Nudr_GroupIDmap`'s own subscription-management family; real webhook delivery for
`subs-to-notify`/`nf-group-ids/subscriptions`.

## ADR-0169 -- gap-closure task #106 continuation: UDR real `group-data` `5g-vn-groups/pp-profile-data` + `mbs-group-membership/pp-profile-data`, this project's first genuinely keyless singleton resources

| Requirement | Test |
|---|---|
| `GET /5g-vn-groups/pp-profile-data` | Live curl, real `200 {}` (seeded empty singleton) |
| `GET /mbs-group-membership/pp-profile-data` | Live curl, real `200 {}` |
| `GET /5g-vn-groups/pp-profile-data` with an unused `ext-group-ids`/`supported-features` filter | Live curl, identical real `200 {}`, confirming filters accepted but not honored |
| **Critical**: neither response shows the individual-resource route's own error text | Live curl, confirms the literal `/pp-profile-data` route is reached, not shadowed |
| `GET 5g-vn-groups/group-A` and `GET 5g-vn-groups/internal?...` immediately after | Live curl, both still real `200` with correct results, confirming no interference between the three sibling routes |
| Genuine PostgreSQL persistence | Direct `psql` query against both new tables independently confirms one row each, `{}`, matching curl exactly |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real `GET`-only resources (`Query5GVnGroupPPData`/`Query5GMbsGroupPPData`,
`TS29505_Subscription_Data.yaml`). Real, disclosed: their response schemas
(`Pp5gVnGroupProfileData`/`Pp5gMbsGroupProfileData`) are genuinely NOT per-group documents, unlike
every other `group-data` sub-resource in this series -- each is a single, global document whose
own internal `allowedMtcProviders`/`allowedMbsInfos` field is itself the per-group map. This
project's first genuinely keyless singleton resource (every prior "non-per-UE" resource is still
keyed by some real identifier), modeled as a fixed single-row table rather than inventing a key
the spec doesn't have. GET-only, no create/update/delete exists anywhere in the spec -- seeded at
startup with an empty (but real, valid) document. Optional `ext-group-ids`/`supported-features`
filters accepted but not honored. Same critical route-ordering requirement as ADR-0168's
`/internal` routes (re-applied, verified live). This closes UDR resources #77 and #78 of free5GC's
~42+ real `Nudr_DataRepository` resources (docs/CAPABILITY_GAP_ANALYSIS.md) -- closes out the
entirety of `5g-vn-groups`/`mbs-group-membership`'s own real, in-scope resource set. See ADR-0169
in `docs/DECISIONS.md` for full disclosure -- remaining real, disclosed gaps: filtering on these
two singletons; `policy-data`'s `mbs-session-pol-data`; `GetSSAuData` (deliberately deferred,
ADR-0160); `Nudr_GroupIDmap`'s own subscription-management family; real webhook delivery for
`subs-to-notify`/`nf-group-ids/subscriptions`.

## ADR-0170 -- gap-closure task #106 continuation: UDR real Nudr_GroupIDmap subscription-management family (nf-group-ids/subscriptions)

| Requirement | Test |
|---|---|
| `POST` with a required field missing | Live curl, real `400` |
| `POST` with a real, complete `SubscriptionData` body | Live curl, real `201`, server-generated `subscriptionId`, `Location` header with the real path |
| `GET` the individual resource / a nonexistent one | Live curl, real `200` matching exactly / real `404` |
| `PATCH` with a real RFC 6902 `replace` op | Live curl, real `200` with the updated body (resolves the real documented `200`-vs-`204` ambiguity the same way as `group_control_data`'s own PATCH) |
| `GET` after `PATCH` | Live curl, real `200` confirming persistence |
| `PATCH` a nonexistent `subscriptionId` | Live curl, real `404` |
| `DELETE` then `GET` | Live curl, real `204` then real `404` |
| `GET /nf-group-ids` (sibling resource, same API root) immediately after | Live curl, still real `200` with its own correct data, confirming no interference |
| Genuine PostgreSQL persistence | Direct `psql` query against `udr_nf_group_id_subscriptions` confirms zero rows remain after the delete |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real `Nudr_GroupIDmap` subscription-management family (`CreateGroupIdSubscription`/
`QueryGroupIdSubscription`/`ModifyGroupIdSubscription`/`RemoveGroupIdSubscription`,
`TS29504_Nudr_GroupIDmap.yaml`). Real, disclosed: structurally the same shape as `ee-subscriptions`/
`subs-to-notify` -- a real CRUD collection+individual pair, server-generated `subscriptionId`, a
real spec-documented `onGroupIdMapChange` webhook callback that is **not implemented** (no real
outbound HTTP delivery to the caller's `notificationUri`), same disclosed gap class as
`subs-to-notify`'s own lack of real webhook delivery. Real, disclosed: `ModifyGroupIdSubscription`'s
own real dual `200`/`204` PATCH response ambiguity resolved identically to `group_control_data`'s
own prior resolution (ADR-0118/ADR-0119) -- always `200` with the patched body. Does **NOT**
increment the `Nudr_DataRepository` count -- still 78, same non-counting precedent as
`GetRoutingIDs`/`GetNfGroupIDs`. See ADR-0170 in `docs/DECISIONS.md` for full disclosure -- task
#106's real, disclosed remaining gaps: `policy-data`'s `mbs-session-pol-data`; `GetSSAuData`
(deliberately deferred, ADR-0160); real webhook delivery infrastructure (spans this resource and
`subs-to-notify`); `ext-group-ids`/`gpsis`/`supported-features` filtering retrofits.

## ADR-0171 -- real `onDataChange` webhook delivery infrastructure, wired into 3 of ~85 real write routes, remainder disclosed as a genuine, tracked follow-up

| Requirement | Test |
|---|---|
| `POST subs-to-notify` subscribing to `amf-3gpp-access` for a real UE | Live curl, real `201` |
| `PUT amf-3gpp-access` for that UE | Live curl, real `204`; separate real HTTPS receiver process independently logs a correctly-shaped `DataChangeNotify` (`resourceId` matches, `REPLACE` at `/` with the full new document) |
| `PATCH amf-3gpp-access` (real RFC 6902 `replace` on `/ratType`) | Live curl, real `204`; receiver logs the actual forwarded change (`REPLACE`, `/ratType`, `newValue: EUTRA`), not a synthesized whole-resource replace |
| `POST` a second subscription watching `smf-registrations/pdu-1`; `PUT` then `DELETE` that resource | Live curl, real `201`/`204`; receiver logs a `REPLACE` at `/` then a `REMOVE` at `/`, confirming the third real change shape |
| Subscription selectivity | The `amf-3gpp-access` change did not trigger the `smf-registrations` subscription's callback and vice versa, confirmed via the receiver's own logged `resourceId` per notification |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Real `onDataChange` webhook delivery for `subs-to-notify`'s own `SubscriptionDataSubscriptions`
(`TS29505_Subscription_Data.yaml`) -- the real gap disclosed since ADR-0149. New shared, real,
independently-testable infrastructure: `notify_client` (one shared, mutex-guarded
`sbi_core::http2::Client`), `notify_subscribers()` (URI-substring match against
`monitoredResourceUris`, real `DataChangeNotify` delivery to `callbackReference`), and
`change_replace`/`change_remove`/`change_from_json_patch` (real `ChangeItem` construction for the
three real write shapes). Real, disclosed scope-limiting findings, discovered while implementing:
this project's UDR has no configured external base URL (matching is path-substring, not exact
absolute-URI, a disclosed compromise); `udr_subs_to_notify`'s own pre-existing `ue_id NOT NULL`
schema (ADR-0149) cannot represent UE-less subscriptions, so non-per-UE resources (group-data and
others) genuinely cannot be wired without a separate schema fix first. User explicitly chose the
largest of three offered scopes ("full sweep across every write route"); given the real
engineering surface turned out to be ~85 real write routes needing individual live-verification
care, this pass wires exactly 3 (`amf-3gpp-access`, `amf-non-3gpp-access`, `smf-registrations`,
covering all three real write shapes end-to-end) and discloses the remaining ~80 plainly as
unwired, not silently claimed complete. See ADR-0171 in `docs/DECISIONS.md` for full disclosure.

## ADR-0172 -- continuing real `onDataChange` webhook delivery: 10 more resources wired (13 of ~40 real per-UE resources total)

| Requirement | Test |
|---|---|
| `POST subs-to-notify` subscription watching `roaming-information` for a real UE | Live curl, real `201` |
| `PUT roaming-information` (real field shape confirmed against the generated DTO after an initial 400 on a guessed shape) | Live curl, real `201`; real HTTPS receiver process independently logs a correctly-shaped `DataChangeNotify` |
| Clean build across all 10 newly-wired resources | `udr` built clean (zero warnings) before and after `clang-format-18` |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions |

Continues ADR-0171's own explicitly disclosed follow-up using the identical infrastructure and
mechanical edit pattern (no new design): `sm-data`/`am-data` (RFC 7396 `PATCH` only, reported as
`change_replace`), `authentication-subscription` (RFC 6902 `PATCH` only), `authentication-status`
(`PUT`+`DELETE`), `smsf-3gpp-access`/`smsf-non-3gpp-access` (`PUT`+`DELETE`), `ip-sm-gw`
(`PUT`+`PATCH`(RFC 6902)+`DELETE`), `mwd` (`PUT`+`PATCH`(RFC 6902)+`DELETE`),
`roaming-information`/`pei-info` (`PUT` only). Real, disclosed: given the underlying
`notify_subscribers()` call is identical, already-proven code at every site, this pass
live-verifies one new representative resource end-to-end rather than re-proving all 10
individually -- correctness at the remaining 9 sites rests on a clean, warning-free build plus
code review of each capture list and `resolved_location()` argument, not independent live proof of
each. Combined total: 13 of ~40 real per-UE `Nudr_DataRepository` resources now have real
`onDataChange` delivery, 24 real write-route call sites. The remaining ~65-70 real per-UE routes,
all non-per-UE resources, and `Nudr_GroupIDmap`'s own separate `onGroupIdMapChange` callback
remain unwired, same disclosed follow-up as ADR-0171. See ADR-0172 in `docs/DECISIONS.md` for full
disclosure.

## ADR-0173 -- continuing real `onDataChange` webhook delivery: 5 more resources wired (18 of ~40 real per-UE resources total)

| Requirement | Test |
|---|---|
| `POST subs-to-notify` subscription watching `policy-data/ues/{ueId}/ue-policy-set` | Live curl, real `201` |
| `PUT ue-policy-set` | Live curl, real `201`; receiver logs a correct `DataChangeNotify` (`REPLACE` at `/`, `newValue` matching the submitted body) |
| `PATCH ue-policy-set` (real RFC 7396 merge-patch) | Live curl, real `204`; receiver logs a second `DataChangeNotify` with the real **patched** (merged) document as `newValue`, confirming a real fix (see below) threads through correctly |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Continues ADR-0171/ADR-0172's own disclosed follow-up, identical infrastructure: `pp-data` (RFC
6902 `PATCH` only), `pp-data-store` (`PUT`+`DELETE`, composite `(ueId, afInstanceId)` key),
`subscription-data`'s own `operator-specific-data` (RFC 6902 `PATCH` only), `ue-policy-set`
(`PUT`+RFC 7396 `PATCH`), `policy-data`'s own `operator-specific-data` (a real, distinct resource
from the `subscription-data` one despite the shared name -- RFC 6902 `PATCH` only). Real,
disclosed: `ee-profile-data` and `coverage-restriction-data`/`lcs-privacy-data`/
`lcs-subscription-data`/`lcs-mo-data` confirmed GET-only by direct read, correctly skipped. Real
fix applied while wiring `ue-policy-set`'s PATCH: `UePolicySetStore::merge_patch()` already
returned the real patched document, but the existing route discarded it (only ever needed `204`)
-- now captured (`const auto patched = ...`) since `notify_subscribers()` needs the real patched
body for `change_replace()`. Combined total: **18 of ~40 real per-UE `Nudr_DataRepository`
resources now have real `onDataChange` delivery, 31 real write-route call sites**. See ADR-0173 in
`docs/DECISIONS.md` for full disclosure -- the remaining ~60-65 real per-UE routes, all non-per-UE
resources, and `Nudr_GroupIDmap`'s own separate `onGroupIdMapChange` callback remain unwired, same
disclosed follow-up.

## ADR-0174 -- continuing real `onDataChange` webhook delivery: 7 more resources wired (25 of ~40 real per-UE resources total)

| Requirement | Test |
|---|---|
| `POST subs-to-notify` subscription with `monitoredResourceUris` set to the full real resolved resource path | Live curl, real `201` |
| `PUT sor-data` | Live curl, real `204` (after correcting the payload shape to the real `SorData` schema's `provisioningTime`+`ueUpdateStatus`) |
| `PATCH sor-data` (real RFC 6902 `replace`) | Live curl, real `204`; receiver logs a correct `DataChangeNotify` (`REPLACE` at `/ueUpdateStatus`, real submitted `newValue`) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Continues ADR-0171/ADR-0172/ADR-0173's own disclosed follow-up, identical infrastructure:
`nidd-authorizations` (RFC 6902 `PUT`+`PATCH`+`DELETE`), `identity-data` (`PATCH` only, upsert-
capable `apply_patch`, no `PUT`/`POST` create operation exists), `service-specific-authorizations`
(composite `(ueId, serviceType)` key, `PUT`+RFC 6902 `PATCH`+`DELETE`), `subscribed-snssais`
(`PUT` only), `subscribed-cag` (`PUT` only), `sor-data` (`PUT`+RFC 6902 `PATCH`), `upu-data`
(`PUT` only). Real, disclosed: `bdt-data` checked and correctly skipped -- keyed by
`bdtReferenceId`, not `ueId`, same non-per-UE exclusion as the group-data family. Combined total:
**25 of ~40 real per-UE `Nudr_DataRepository` resources now have real `onDataChange` delivery, 43
real write-route call sites**. See ADR-0174 in `docs/DECISIONS.md` for full disclosure -- the
remaining ~35-40 real per-UE routes, all non-per-UE resources, and `Nudr_GroupIDmap`'s own
separate `onGroupIdMapChange` callback remain unwired, same disclosed follow-up.

## ADR-0175 -- continuing real `onDataChange` webhook delivery: 6 more resources wired (31 of ~40 real per-UE resources total)

| Requirement | Test |
|---|---|
| `POST ee-subscriptions` create | Live curl, real `201` with server-generated `subsId` in `Location` |
| `POST subs-to-notify` subscription watching the full real resolved individual-document path | Live curl, real `201` |
| `PATCH ee-subscriptions/{subsId}` (real RFC 6902 `replace`) | Live curl, real `204`; receiver logs a correct `DataChangeNotify` (`REPLACE` at `/callbackReference`, real submitted `newValue`) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Continues ADR-0171 through ADR-0174's own disclosed follow-up, identical infrastructure:
`ee-subscriptions` individual document (`PUT`+RFC 6902 `PATCH`+`DELETE`), `sdm-subscriptions`
individual document (same shape), and their four nested per-`subsId` sub-resources:
`amf-subscriptions` (array body, real distinct 201-vs-204 `PUT`), `smf-subscriptions`
(single-object body), `hss-subscriptions` (single-object body), `hss-sdm-subscriptions` (204-only
`PUT`, same precedent as `sor-data`/`upu-data`). Real, disclosed: the `POST` create routes on the
`ee-subscriptions`/`sdm-subscriptions` collections were deliberately left unwired, same
"subscriber can't watch a not-yet-created URI" reasoning already applied to `subs-to-notify`'s own
POST and `nf-group-ids/subscriptions`' own POST. Combined total: **31 of ~40 real per-UE
`Nudr_DataRepository` resources now have real `onDataChange` delivery, 61 real write-route call
sites**. See ADR-0175 in `docs/DECISIONS.md` for full disclosure -- the remaining ~10-15 real
per-UE routes, all non-per-UE resources, and `Nudr_GroupIDmap`'s own separate
`onGroupIdMapChange` callback remain unwired, same disclosed follow-up.

## ADR-0176 -- `udr_subs_to_notify` schema fix (nullable `ue_id` + `list_ue_less()`) and first two non-per-UE resources wired

| Requirement | Test |
|---|---|
| `POST subs-to-notify` subscription with no `ueId` field at all | Live curl, real `201` |
| `PUT bdt-data` (non-per-UE, keyed by `bdtReferenceId`) | Live curl, real `201`; receiver logs a correct `DataChangeNotify` with no `"ueId"` key present at all |
| Real PostgreSQL row check | `psql`: new subscription's `ue_id` is real SQL `NULL`, confirmed distinct from legacy empty-string test rows |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

A comprehensive sweep of every remaining write route confirmed all real per-UE
`Nudr_DataRepository` resources were already wired (ADR-0171 through ADR-0175, 61 call sites);
everything left is structurally non-per-UE or otherwise out of scope (see ADR-0176 in
`docs/DECISIONS.md` for the full per-route classification). User chose to fix the real
prerequisite: `udr_subs_to_notify.ue_id` is now nullable (was `NOT NULL` with an empty-string
sentinel), backing a new `list_ue_less()` store method and `notify_subscribers_ue_less()` helper
(refactored out of a shared `deliver_onDataChange()` core with the existing per-UE
`notify_subscribers()`). First two non-per-UE resources wired with it: `bdt-data`
(`PUT`+RFC 7396 `PATCH`+`DELETE`) and `pdtq-data` (same shape). Combined total: **61 real per-UE
call sites + 6 real non-per-UE call sites = 67 real write-route call sites**, 33 distinct resource
types now wired. See ADR-0176 in `docs/DECISIONS.md` for full disclosure -- `slice-control-data`,
`group-control-data`, `5g-vn-groups`, `mbs-group-membership`, and the group-data
`ee-subscriptions` family are now unblocked but not yet wired; `Nudr_GroupIDmap`'s own callback
remains a separate, still-unimplemented API.

## ADR-0177 -- continuing non-per-UE `onDataChange` webhook delivery: 4 more resources wired

| Requirement | Test |
|---|---|
| `POST subs-to-notify` subscription (no `ueId`) watching the full resolved `5g-vn-groups/vn-grp-001` path | Live curl, real `201` |
| `PUT 5g-vn-groups/vn-grp-001` | Live curl, real `201`; receiver logs a correct `DataChangeNotify` (`REPLACE` at `/`, no `"ueId"` key) |
| `DELETE 5g-vn-groups/vn-grp-001` | Live curl, real `204`; receiver logs a correct `DataChangeNotify` (`REMOVE` at `/`, no `"ueId"` key) |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Continues ADR-0176's own disclosed follow-up using `notify_subscribers_ue_less()`, no new
infrastructure: `slice-control-data` (RFC 7396 `PATCH`-only, upsert-capable), `group-control-data`
(same shape), `5g-vn-groups` (`PUT`+RFC 6902 `PATCH`+`DELETE`, real 201-only `PUT`),
`mbs-group-membership` (identical shape, real spec twin). All four confirmed keyed by
`snssai`/`intGroupId`/`externalGroupId`, not `ueId`. Combined total: **61 real per-UE call sites +
14 real non-per-UE call sites = 75 real write-route call sites**, 37 distinct resource types now
wired (31 per-UE + 6 non-per-UE). See ADR-0177 in `docs/DECISIONS.md` for full disclosure -- the
`group-data/{ueGroupId}/ee-subscriptions` family (+ its 3 nested resources) remains the last real
candidate batch unblocked by ADR-0176's schema fix, not yet wired; `Nudr_GroupIDmap`'s own
callback remains a separate, still-unimplemented API.

## ADR-0178 -- group-data `ee-subscriptions` family wired: closes the ADR-0176 non-per-UE candidate list

| Requirement | Test |
|---|---|
| `POST group-data/{ueGroupId}/ee-subscriptions` create | Live curl, real `201` with server-generated `subsId` |
| `POST subs-to-notify` subscription (no `ueId`) watching the full resolved individual-document path | Live curl, real `201` |
| `PATCH group-data/.../ee-subscriptions/{subsId}` (real RFC 6902 `replace`) | Live curl, real `204`; receiver logs a correct `DataChangeNotify` with no `"ueId"` key present |
| No regression | Full `conformance_tests` (excluding the two disclosed pre-existing flaky tests): 331/331 pass, zero regressions; `udr` built clean (zero warnings) before and after `clang-format-18` |

Continues ADR-0176/ADR-0177's own disclosed follow-up using `notify_subscribers_ue_less()`, no
new infrastructure: `group-data/{ueGroupId}/ee-subscriptions` individual document
(`PUT`+RFC 6902 `PATCH`+`DELETE`) and its 3 nested per-`subsId` sub-resources
(`amf-subscriptions`, `smf-subscriptions`, `hss-subscriptions`) -- the group-data-scoped
structural twins of the already-wired per-UE `ee-subscriptions` family (ADR-0175), keyed by
`ueGroupId`. This closes ADR-0176's own disclosed non-per-UE candidate list in full. Combined
total: **61 real per-UE call sites + 26 real non-per-UE call sites = 87 real write-route call
sites**, 41 distinct resource types now wired (31 per-UE + 10 non-per-UE). See ADR-0178 in
`docs/DECISIONS.md` for full disclosure -- the only remaining real gap is `Nudr_GroupIDmap`'s own
separate `onGroupIdMapChange` callback, a different API, currently unfireable regardless since
`nf-group-ids` itself has no write path.

## ADR-0179 -- real automated integration test for `onDataChange` webhook delivery

| Requirement | Test |
|---|---|
| Real subscriber callback endpoint | New in-process `sbi_core::http2::Server` (TLS 1.3 + mTLS), not a stub/mock/external script |
| `POST subs-to-notify` + `PUT`/`PATCH`/`DELETE` `smf-registrations` | `UdrIntegration.OnDataChangeWebhookDeliveredOnPutPatchDelete`, asserts a correctly-shaped `DataChangeNotify` after each of the 3 writes |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 332/332 pass |

Closes the real, previously-disclosed testing gap named in every ADR from 0171 through 0178:
`onDataChange` delivery had only ever been verified manually (curl + a standalone Python HTTPS
receiver script). New test file `tests/integration/test_udr_ondatachange_webhook.cpp` drives the
same flow inside `ctest`, using a real in-process `sbi_core::http2::Server` as the receiver -- the
exact same server implementation every NF in this project runs. `smf-registrations` was chosen
(not `amf-3gpp-access`) because it has real `PUT`+`PATCH`+`DELETE` all on one document, covering
all three real `ChangeItem` shapes (`change_replace`/`change_from_json_patch`/`change_remove`) in
one test. Three real bugs found and fixed while writing it: (1) a test-design bug -- `DELETE
amf-3gpp-access` was wrongly assumed to return `204`; direct read confirmed that resource
genuinely has no `DELETE` operation at all; (2) a real crash -- an `ASSERT_*` failure skipped
manual cleanup, leaving a joinable `std::thread` to call `std::terminate()`; fixed with two RAII
wrappers (`SpawnedProcess`, `IoContextThread`); (3) a real test-isolation bug -- a fixed `ue_id`
collided with a leftover row from an interrupted prior run (real, persistent PostgreSQL, by
design); fixed using this project's own existing `getpid()`-suffix precedent from
`test_n28_spending_limit.cpp`. See ADR-0179 in `docs/DECISIONS.md` for full disclosure --
this is one representative end-to-end proof of the shared delivery core, not a dedicated test per
each of the 41 wired resource types, and covers only the per-UE `notify_subscribers()` path, not
`notify_subscribers_ue_less()`.

## ADR-0180 -- `Nudr_GroupIDmap`'s `onGroupIdMapChange` callback fired from the one real mutation point this data has

| Requirement | Test |
|---|---|
| Direct read of `TS29504_Nudr_GroupIDmap.yaml` | Confirmed `/nf-group-ids` genuinely has only `GET` -- no write operation exists to hang a live trigger off of |
| Pre-seeded `udr_nf_group_id_subscriptions` row (`nfType=AMF`, `nfGroupId=amf-group-01`) + real HTTPS receiver, then start `udr` | Receiver logs a correct `GroupIdMapNotify` during `udr`'s own startup sequence, zero warnings |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 332/332 pass |

The last remaining gap disclosed at the end of ADR-0178: `onGroupIdMapChange` had never fired,
because `/nf-group-ids` genuinely has no write operation in the real spec to trigger it from
(confirmed by direct YAML read, matching this project's own pre-existing `NfGroupIdStore` code
comment). Rather than inventing a non-spec write endpoint, new `notify_group_id_map_change()`
fires the callback from the one real mutation this mapping data already undergoes:
`nf_group_ids.seed(...)` at `udr` process startup. Matches subscriptions (via a new
`NfGroupIdSubscriptionStore::list_all()`) by the real `SubscriptionData` schema's own
`nfType`+`nfGroupId` fields, POSTs a real `GroupIdMapNotify` to each match. Explicitly disclosed:
this fires before the server accepts connections, so in every real run it finds zero live
subscriptions -- the pre-seeded-row methodology above was the only way to exercise this real code
path end-to-end, since the normal "subscribe via the live API, then mutate" methodology this whole
series otherwise used doesn't apply here. See ADR-0180 in `docs/DECISIONS.md` for full disclosure,
including a real live-testing mistake found and corrected along the way (wrong receiver port).

## ADR-0181 -- `gpsis`/`ext-group-ids` filtering retrofit across 4 already-closed UDR `group-data` GET resources

| Requirement | Test |
|---|---|
| `PUT` two `5g-vn-groups` with distinct real `members`, `GET` bare collection unfiltered vs. `?gpsis=<value>` | Live curl: unfiltered returns both (+ seed data); filtered returns only the matching group; a non-matching value returns a real empty map |
| Pre-seed `allowedMtcProviders` (3 keys incl. `ALL`) directly via `psql`, `GET pp-profile-data?ext-group-ids=vn-grp-A` | Live curl, real `200`; response correctly contains exactly `{vn-grp-A, ALL}`, excluding the non-requested key |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 332/332 pass |

Retrofits the real, disclosed `gpsis`/`ext-group-ids`-not-honored backlog across
`Query5GVnGroup`/`Query5GmbsGroup` (bare collections, filter by real `members`/
`multicastGroupMemb` array field) and `Query5GVNGroupPPData`/`Query5GMbsGroupPPData` (keyless
singletons, filter `allowedMtcProviders`/`allowedMbsInfos` map keys, always keeping the real
spec-documented `"ALL"` wildcard key regardless of what was requested). Direct schema read found
this project's own original ADR-0167/ADR-0169 characterization of these filters as needing
"member-list inspection" overstated the real difficulty -- each targets one concrete field already
present in data these routes already return. `QueryUeSubscribedData`'s own `ext-group-ids` (and
sibling) filters were re-checked and correctly remain unhonored -- no store exists to filter its
32 flat per-UE fields by group membership at all, confirmed, not retrofitted. Also fixed: a stale
top-of-file comment block in `nfs/udr/src/main.cpp` that still described these two bare-collection
`gpsis` filters as blocked on missing array-query-param infra, a state ADR-0161's own
`split_form_array()` had already resolved. See ADR-0181 in `docs/DECISIONS.md` for full
disclosure.

## ADR-0182 -- `mbs-session-pol-data` implemented for its unambiguous `afAppId` branch, `mbsSessionId` branch remains deferred

| Requirement | Test |
|---|---|
| Apply schema change to real, running `docker-postgres-udr-1` | `CREATE TABLE IF NOT EXISTS`, confirmed via the standard `schema.postgres.sql` re-apply |
| `GET policy-data/mbs-session-pol-data/af-app-1` | Live curl, real `200` with the exact seeded body `{"5qis":[9]}` |
| `GET policy-data/mbs-session-pol-data/nonexistent` | Live curl, real `404` with a correctly-worded `ProblemDetails` |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 332/332 pass |

Real correction made while implementing, not a full "resolution" of the whole key space:
`polSessionId`'s own real schema (`MbsSessPolDataId`) is a `oneOf` of `{mbsSessionId}`/
`{afAppId}`, with `mbsSessionId` itself an `anyOf` of `{tmgi}`/`{ssm}`. ADR-0119's own original
deferral already explicitly considered and *declined* reusing `slice-control-data`'s own `snssai`
precedent (`sst + '-' + sd`, a deliberate, disclosed convention for a genuinely flat two-field
object) for this multi-level nested object, calling that "fabrication, not a disclosed convention
choice" -- that reasoning is re-checked here and still holds, not reversed. What genuinely changed:
the `oneOf`'s own `afAppId` branch is, on its own, just `type: string` -- already unambiguous, no
encoding decision needed. New `MbsSessionPolicyDataStore` (real GET-only, `seed()`/`get()`-only,
same shape as `SponsorConnectivityDataStore`) implements exactly that branch, seeded with one
representative key. The `mbsSessionId`/`tmgi`/`ssm` branch remains exactly as unaddressed as
ADR-0119 left it -- not touched, not claimed resolved. See ADR-0182 in `docs/DECISIONS.md` for
full disclosure.

## ADR-0183 -- NSSF: new Tier 1 NF, `Nnssf_NSSelection` + `Nnssf_NSSAIAvailability`

| Requirement | Test |
|---|---|
| `NSSelectionGet`, `requestedNssai` with a catalog and a non-catalog S-NSSAI | Live curl: real `200`, catalog member in `allowedNssaiList` (`accessType: 3GPP_ACCESS`), non-member in `rejectedNssaiInPlmn` |
| `NSSelectionGet`, no slice-info param | Live curl: real `200`, falls back to the whole seeded catalog |
| `NSSelectionGet`, missing `nf-id` | Live curl: real `400` |
| `NSSAIAvailabilityPut`/`Patch` with a real `SupportedNssaiAvailabilityData` | Live curl: real `200` with the echoed `AuthorizedNssaiAvailabilityInfo`; `Patch` against a nonexistent `nfId`: real `404` |
| `NSSAIAvailabilityDelete` | Live curl: real `204`; repeat: real `404` |
| `NSSAIAvailabilityPost` (subscribe) + real `nssaiAvailabilityNotification` delivery | Live curl subscribe (real `201` + `Location`), then a `Put` that triggers a real mTLS POST to a live HTTPS receiver, captured with the correct `subscriptionId` + `authorizedNssaiAvailabilityData` |
| `NSSAIAvailabilitySubModifyPatch` / `Unsubscribe` | Live curl: real `200` / real `204`, then real `404` on repeat |
| `NSSAIAvailabilityOptions` | Live curl: real `200` |
| Bad bearer token | Live curl: real `401` `ProblemDetails` |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 338/338 pass |

This project's ninth NF and first entirely new Tier 1 NF built since the original Phase 2 order
(NRF -> AMF -> SMF -> UDM -> UDR -> AUSF -> PCF) -- user-directed move off UDR's exhausted task
#106 backlog onto a whole unbuilt subsystem, chosen from `docs/CAPABILITY_GAP_ANALYSIS.md`'s own
"still not done" `nssf`/`nef`/`scp`/`bsf` list. All 8 real operations across both real Nnssf
services implemented: `NSSelectionGet`, `NSSAIAvailabilityPut`/`Patch`/`Delete`/`Post`/
`Unsubscribe`/`SubModifyPatch`/`Options`, plus the real outbound `nssaiAvailabilityNotification`
callback. Real, disclosed simplifications: the slice-selection decision is catalog-membership
filtering against a fixed, real-standardized-SST seed (no subscriber-entitlement/NRF-discovery/
NSAG-mapping logic); `accessType` always defaults to `3GPP_ACCESS` (the real query params never
convey it); availability "authorization" echoes submissions unchanged (no restriction/rejection
logic); the notification callback fires from `Put`/`Patch` only, not `Delete` (the real
notification schema has no field shaped for "an NF instance deregistered"). See ADR-0183 in
`docs/DECISIONS.md` for full disclosure, including why no Helm chart was added (matches a
pre-existing gap shared by 6 other NFs, not unique to this one).

## ADR-0184 -- continuous move-to-next-NF process decision + BSF, `Nbsf_Management`

| Requirement | Test |
|---|---|
| `CreatePCFBinding` + real duplicate-combination check | Live curl: real `201`+`Location`; a second create for the same supi+dnn+snssai gets a real `403` carrying the EXISTING binding's own `pcfSmFqdn` |
| `GetPCFBindings` filters (supi, JSON-encoded snssai) | Live curl: real `200` matches, real `204` no-match |
| `UpdateIndPCFBinding` (RFC 7396 merge-patch) | Live curl: real `200`, field actually changed; against a nonexistent binding: real `404` |
| `CreateIndividualSubcription` + real `myNotification` delivery | Live curl subscribe (real `201`+`Location`) against a live HTTPS receiver; a `CreatePCFBinding` for the subscribed supi triggered a real mTLS POST, captured, containing exactly the subscribed event and correct dnn/snssai (and correctly NOT the unsubscribed SNSSAI_DNN event that fired at the same time) |
| `ReplaceIndividualSubscription` / `DeleteIndividualSubscription` | Live curl: real `200` / real `204`, then real `404` on repeat/against a nonexistent id |
| `DeleteIndPCFBinding` | Live curl: real `204`, then real `404` |
| `CreatePCFforUEBinding` + duplicate-supi check, `GetPCFForUeBindings`, `UpdateIndPCFforUEBinding`, `DeleteIndPCFforUEBinding` | Live curl: real `201`/`403`/`200`(filtered+unfiltered)/`200`(merge-patch)/`204` |
| `CreatePCFMbsBinding` + duplicate-mbsSessionId check, `GetPCFMbsBinding`, `ModifyIndPCFMbsBinding`, `DeleteIndPCFMbsBinding` | Live curl: real `201`/`403`(with existing pcfFqdn)/`200`(nested JSON query param)/`400`(missing mandatory param)/`200`(merge-patch)/`204` |
| Bad bearer token | Live curl: real `401` `ProblemDetails` |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 343/343 pass |

This project's tenth NF. Records two things: (1) a user-directed process change -- move to the
next NF/subsystem continuously as each one completes, without a fresh per-NF prompt or per-NF
confirmation pause, while every other CLAUDE.md quality bar (no fabrication, live verification,
zero-warning builds, full doc trail, CI-health checks before pushing) stays exactly as strict; (2)
BSF (`Nbsf_Management`, TS29521 v1.5.0), the second NF built under that process, chosen over NEF
(too large for one slice, 14 files/~52 ops) and SCP (architecturally different -- a real inline
HTTP proxy, not another origin-server YAML) as the cleanest-scoped of the three remaining. All 15
real operations across `pcfBindings`/`subscriptions`/`pcf-ue-bindings`/`pcf-mbs-bindings`
implemented, plus the real `myNotification` callback. Real BSF-specific business logic (not just
CRUD): the spec's own duplicate-combination 403-with-existing-info rule for
`CreatePCFBinding`/`CreatePCFMbsBinding`, real RFC 7396 merge-patch (not RFC 6902) for all three
binding families' PATCH, and real `BsfEvent` notification coverage matching the spec's own
combination-uniqueness semantics. One real `-Wconversion` bug found and fixed before commit (a
generic JSON-query-param helper instantiated with `T = nlohmann::json` itself hit an ambiguous
optional-construction overload). See ADR-0184 in `docs/DECISIONS.md` for full disclosure.

## ADR-0185 -- NEF: first real service, `Nnef_PFDmanagement`

| Requirement | Test |
|---|---|
| `AllFetch` with real comma-separated `application-ids` | Live curl: real `200`, seeded app returned, unknown app silently omitted; missing the mandatory param: real `400` |
| `IndAppFetch` | Live curl: real `200` for a seeded appId, real `404` for an unknown one |
| `AppFetchPartialUpdate` "changed since" logic | Live curl: older client timestamp -> real `200` (changed); same-or-later timestamp -> real `204` (not changed); no timestamp -> real `200` (unconditional) |
| `CreateSubscr` / `ModifySubscr` / `Unsubscribe` | Live curl: real `201`+`Location` / real `200` (real `404` against a nonexistent id) / real `204` (real `404` on repeat) |
| Bad bearer token | Live curl: real `401` `ProblemDetails` |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 347/347 pass |

This project's eleventh NF, third built under ADR-0184's continuous move-to-next-NF process. NEF's
own real spec surface is 14 separate YAML files, ~52 operations total -- too large for one turn.
`Nnef_PFDmanagement` (v1.4.0, 6 real operations, the largest single well-defined NEF service)
chosen as this turn's slice: the real consumer-facing counterpart to this project's own
already-built UPF-side PFCP PFD Management (task #107/ADR-0086), though not wired to it this turn.
Real, disclosed structural finding (not a simplification): this YAML has no operation anywhere
that lets a caller WRITE PFD content into NEF -- the real AF-to-NEF PFD provisioning path is
genuinely out of 3GPP's own SBI framework scope. Two consequences: `PfdCatalogStore` is
seed()-only, and the real `PfdChangeNotification`/`NotificationPush` callback delivery
`CreateSubscr` declares has no real trigger this project can ever fire (PFD content never changes
after startup) -- deliberately not built as unreachable dead code, disclosed as a real structural
gap instead. 13 of NEF's 14 real YAML files remain entirely unbuilt. See ADR-0185 in
`docs/DECISIONS.md` for full disclosure.

## ADR-0186 -- SCP: real architectural-scope decision + `Nscp_EventExposure`

| Requirement | Test |
|---|---|
| `CreateSubscription` | Live curl: real `201` + `Location` + `ScpEventExposureSubsResp` |
| `ModifySubscription` (RFC 6902 patch) | Live curl: real `200`; against a nonexistent subscription: real `404` |
| `DeleteSubscription` | Live curl: real `204`; repeated: real `404` |
| Bad bearer token | Live curl: real `401` `ProblemDetails` |
| No regression | Full `conformance_tests`+`integration_tests` (excluding the two disclosed pre-existing flaky tests): 350/350 pass |

This project's twelfth NF, fourth built under ADR-0184's continuous move-to-next-NF process, and
the last of the original `nssf`/`nef`/`scp`/`bsf` "still not done" list. SCP's real defining role
(TS 29.500 §§6.10-6.11, inline HTTP/2 message-forwarding proxy for indirect communication) is
architecturally different from every other NF built so far -- a real, explicit scope decision was
presented to the user via `AskUserQuestion` (build `Nscp_EventExposure` only / design the real
proxy first / skip SCP), not silently assumed. User chose `Nscp_EventExposure` only. All 3 real
operations implemented and live-verified: `CreateSubscription`/`ModifySubscription`/
`DeleteSubscription`. Real, disclosed gap (same shape as NEF's ADR-0185 finding): the real
`onScpEventExposureNotification` callback never fires, since this SCP performs no real forwarding
and so has no genuine `ScpSignallingInfo` activity to report. The real SCP proxy/forwarding role
itself remains entirely undesigned and unbuilt. See ADR-0186 in `docs/DECISIONS.md` for full
disclosure.
