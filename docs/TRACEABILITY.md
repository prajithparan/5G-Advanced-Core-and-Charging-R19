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

## Codegen infrastructure (Phase 1)

Not a procedure row (this is infrastructure, not a business-logic procedure), noted separately:
`tools/sbi-codegen` generates C++ DTOs + JSON serializers for the full transitive `$ref` closure of
`TS29510_Nnrf_NFManagement.yaml`, `TS29518_Namf_Communication.yaml`, and `TS29571_CommonData.yaml`
(1076 types across 22 source YAML files, merged into 11 output file-groups -- see
`docs/DECISIONS.md` ADR-0010 for why). Every generated file carries a header comment citing its
source TS number(s), YAML filename(s), and commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`.
Proven by `tests/conformance/test_round_trip.cpp` (C++ round-trip: construct -> to_json -> from_json
-> equality, including an anyOf-open-enum value outside the known list, and `AmfId`'s pattern
validation) plus `tests/conformance/validate_structural_conformance.py` (the emitted JSON's field
names checked against the real OpenAPI schema's `required`/`properties`, not a hand-copied
expectation). This is Phase 1 infrastructure proof, not a Phase 2 NF implementation -- no NF
procedure yet consumes these generated types.

## Transport security (pre-Phase-2)

Not a procedure row (infrastructure). `libs/sbi-core`'s HTTP/2 server and client now require TLS
1.3 + mTLS (`docs/DECISIONS.md` ADR-0011, superseding ADR-0005's h2c-only Phase 0 state) --
`scripts/gen-lab-pki.sh` generates a lab CA and per-NF certs, and the hello-nf/stub-nrf integration
test proves the full flow works over a real mTLS handshake, not h2c. Proven additionally by manual
`openssl s_client` checks recorded in ADR-0011 (mTLS actually rejects clients without a cert; ALPN
negotiates `h2`). This closes a non-negotiable-rules gap that existed since Phase 0; still ahead of
any Phase 2 NF procedure.
