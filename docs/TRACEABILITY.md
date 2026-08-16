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

**Disclosed gaps**: CHF has never been part of this project's automated `ctest` suite (needs Redis/
ClickHouse, neither provisioned in `.github/workflows/ci.yml` -- a real, pre-existing gap, not
newly introduced). Automated PCC/session-rule re-decisioning from a pushed policy-counter status
not implemented (3GPP leaves the status-to-action mapping operator-defined). `subscriptionTermination`
(the other real TS29594 callback) not implemented. AM-policy-side/UE-policy-side spending limits
(`AmPolicyData`/`UePolicySet`, distinct real TS29519 resources) out of scope -- only the SM-policy
variant PCF's already-built surface needs was built.
