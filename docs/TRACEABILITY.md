# Spec Traceability

One row per implemented procedure: which TS clause it comes from, which source file implements it,
and which test proves it. Per CLAUDE.md's Definition of Done, every NF procedure gets an entry here
before it's considered complete.

| Procedure | TS clause | Source | Test | Status |
|---|---|---|---|---|
| hello-nf registers as consumer of Nnrf_NFManagement (RegisterNFInstance / UpdateNFInstance / DeregisterNFInstance) | TS 29.510 `PUT`/`PATCH`/`DELETE` `/nf-instances/{nfInstanceID}` (specs/5G_APIs-REL-19/TS29510_Nnrf_NFManagement.yaml) | `nfs/hello-nf/src/main.cpp`, `libs/sbi-core/src/http2_client.cpp`, `libs/sbi-core/src/oauth2_client.cpp` | `tests/integration/test_hello_nf_registration.cpp` | Phase 0 infra proof only -- see note below |

**Note on the row above:** this is not a Definition-of-Done entry for NRF. It proves the HTTP/2
transport, OAuth2 client-credentials flow, and header handling work end-to-end against a real (if
throwaway) server -- `nfs/stub-nrf` is explicitly not the real NRF (see its file header and
`docs/DECISIONS.md`). The real NRF, built from Phase 1's generated `Nnrf_NFManagement` DTOs and
meeting the full 8-point Definition of Done, is Phase 2 work and gets its own row(s) here as its
procedures land, one per turn per CLAUDE.md's working style.

No other procedures are implemented yet.
