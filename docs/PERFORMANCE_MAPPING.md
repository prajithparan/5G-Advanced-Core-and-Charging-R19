# Performance-measurement mapping: 3GPP TS 28.552 / TS 28.554 -> this project's NFs

**ADR-0238 step (1).** This is the reviewable mapping table that must exist before any
performance claim is framed against 3GPP KPI definitions, in the same spirit as
`docs/CHARGING_MAPPING.md` for the TM Forum side: an explicit, checkable list of
spec measurement -> project measurement point -> status. Ambiguous or absent items are marked,
never invented.

## Source material (vendored, exact versions)

| Spec | Version | File |
|---|---|---|
| TS 28.552 "5G performance measurements" | **v19.8.0 (2026-08), Release 19** | `specs/TS_28_552.pdf` |
| TS 28.554 "5G end to end KPIs" | **v19.7.0 (2026-04), Release 19** | `specs/TS_28_554.pdf` |

TS 28.552 defines **663 distinct measurement names** across 13 NF clause families (counted from
the spec's own `e)` measurement-name fields, not estimated).

## Applicability triage: which of TS 28.552's families apply to this project at all

| Clause | Family | Applies? | Why |
|---|---|---|---|
| 5.1 | gNB | **No** | This project has no real gNB. `simulators/ransim` is a UE/RAN simulator, not a measured gNB NF. The DRB/RRU/RRC/HO/PAG families all hang off this. |
| 5.2 | AMF | **Yes** | `nfs/amf` exists (RM.* / MM.* families) |
| 5.3 | SMF | **Yes** | `nfs/smf` exists (SM.* family) |
| 5.4 | UPF | **Yes, partly** | `nfs/upf` exists; GTP.* data-plane counters apply to the XDP datapath |
| 5.5 | PCF | **Yes** | `nfs/pcf` exists |
| 5.6 | UDM | **Yes** | `nfs/udm` exists |
| 5.7 | Common (VR usage) | **Largely NO -- see below** | |
| 5.8 | N3IWF | **No** | Not built, not in scope |
| 5.9 | NEF | **Yes** | `nfs/nef` exists |
| 5.10 | NRF | **Yes** | `nfs/nrf` exists (NFS.* family) |
| 5.11 | NSSF | **Yes** | `nfs/nssf` exists |
| 5.12 | SMSF | **Yes** | `nfs/smsf` exists |
| 5.13 | UDR | **Yes** | `nfs/udr` exists |
| 5.14/5.15 | ECS / EES | **No** | Edge enablement not built |
| 5.16 | LMF | **Yes** | `nfs/lmf` exists |
| 5.18 | NWDAF | **Yes (future)** | Tier-2, not yet built |

### Clause 5.7 "Common performance measurements for NFs" is mostly NOT usable here

Read directly rather than assumed from the title: 5.7.1 (`VR.VCpuUsageMean`,
`VR.VMemoryUsageMean`, `VR.VDiskUsageMean`) is sourced, in the spec's own words, from the
`VcpuUsageMeanVnf.vComputeId` measurements "(see ETSI GS IFA 027)... **from VNFM**". That is the
ETSI NFV-MANO layer **ADR-0238 already established this project does not have and was never
scoped to build**. Same reason NFV-TST/NFV-REL were rejected there; it applies again here, and is
recorded rather than quietly skipped.

Only **5.7.2** (`Connection data volumes of NF`: incoming/outgoing bytes and packets) is
implementable without a VNFM.

## What TS 28.552 does NOT cover -- and it includes this project's commercial core

**There is no CHF clause in TS 28.552.** Confirmed by direct search: no `Performance measurements
for CHF` heading exists. Neither do **AUSF, BSF, SCP, 5G-EIR, GMLC, or SEPP** -- all of which this
project has built.

This matters directly to ADR-0049's mandate: CHF is the commercial centre of this project, and
the framework selected in ADR-0238 does not measure it. Charging-domain performance lives in the
TS 32.xxx series instead. **This is a real gap in the selected framework, disclosed here rather
than papered over by mapping CHF onto some loosely-similar family.** It does not invalidate
ADR-0238 (28.552 remains right for the NFs it does cover), but "carrier-grade measurement of CHF"
is not achieved by 28.552 alone and no one should assume otherwise.

## Detailed mapping: NRF (clause 5.10, the `NFS.*` family) -- complete, 13 of 13

Chosen first because it is fully enumerated in the spec, small enough to map exhaustively, and is
the NF ADR-0244/0246 already benchmarked.

| TS 28.552 | Measurement name | Type | This project's metric | Status |
|---|---|---|---|---|
| 5.10.1.1 | `NFS.RegReq` | CC | `nrf_registrations_total` | **Partial** -- counts registrations, does not separate *requests* from *successes* |
| 5.10.1.2 | `NFS.RegSucc` | CC | (same counter) | **Partial** -- same counter serves both; success rate is therefore not computable |
| 5.10.1.3 | `NFS.RegFailEncodeErr` | CC | none | **Missing** |
| 5.10.1.4 | `NFS.RegFailNrfErr` | CC | none | **Missing** |
| 5.10.2.1 | `NFS.UpdateReq` | CC | none | **Missing** |
| 5.10.2.2 | `NFS.UpdateSucc` | CC | none | **Missing** |
| 5.10.2.3 | `NFS.UpdateFailEncodeErr` | CC | none | **Missing** |
| 5.10.2.4 | `NFS.UpdateFailNrfErr` | CC | none | **Missing** |
| 5.10.3.1 | `NFS.DiscReq` | CC | none | **Missing** |
| 5.10.3.2 | `NFS.DiscSucc` | CC | none | **Missing** |
| 5.10.3.3 | `NFS.DiscFailUnauth` | CC | none | **Missing** |
| 5.10.3.4 | `NFS.DiscFailInputErr` | CC | none | **Missing** |
| 5.10.3.5 | `NFS.DiscFailNrfErr` | CC | none | **Missing** |

NRF's other existing metrics (`nrf_deregistrations_total`, `nrf_heartbeats_total`,
`nrf_tokens_issued_total`, `nrf_registered_nf_count`) are real and useful but have **no TS 28.552
counterpart in clause 5.10** -- they are this project's own, not spec measurements, and must not
be relabelled as such.

**Honest headline for NRF: 0 of 13 measurements are fully conformant today; 2 are partially
served by one counter that conflates request with success.** The dominant gap is structural, not
volume: 28.552 consistently splits every operation into request / success / per-cause failure,
and this project's counters mostly count only "it happened".

## Per-NF status summary

| NF | 28.552 clause | Project metrics today | Assessment |
|---|---|---|---|
| NRF | 5.10 (13 meas.) | 5 | Mapped in full above: 0 conformant, 2 partial |
| AMF | 5.2 (RM.*/MM.*, ~166 meas.) | 19 | Large gap; RM.*/MM.* is the biggest family in the spec |
| SMF | 5.3 (SM.*, ~54 meas.) | 13 | Large gap |
| UPF | 5.4 (GTP.*, ~46 meas.) | 4 | Large gap; datapath counters not exposed |
| PCF | 5.5 | 34 | Needs per-measurement audit |
| UDM | 5.6 | 44 | Needs per-measurement audit |
| UDR | 5.13 | 150 | Highest local coverage; needs audit |
| NSSF/SMSF/NEF/LMF | 5.11/5.12/5.9/5.16 | 7 / — / — / — | Needs audit |
| **CHF** | **none** | 23 | **Not covered by 28.552 at all -- see above** |

The per-NF audits beyond NRF are deliberately **not** filled in with guesses. Each needs the same
line-by-line treatment NRF got, against the spec text, and that is real work rather than a
formatting exercise.

## TS 28.554 KPIs: what is reachable

Most 28.554 KPIs are RAN-weighted (Accessibility, Integrity, Mobility, Reliability over Uu / F1-U
/ N3) and depend on gNB counters this project does not produce. The **5GC-reachable** ones are:

- 6.2.3 Registration success rate of one single network slice
- 6.2.5 / 6.2.12 / 6.2.16 PDU session establishment success rate (slice / 5G VN group / MA PDU)
- 6.2.14 / 6.2.15 PDU Session Per Establishment Request Rate and Reject Rate
- 6.4.1 Mean number of PDU sessions of network and Network Slice Instance

Every one of these is a **ratio of the request/success/failure counters TS 28.552 defines** --
which is precisely the structural split this project's counters currently lack. That is the
single highest-leverage instrumentation change: split request / success / per-cause failure, and
the 5GC KPIs become computable.

6.4.2 (Virtualised Resource Utilization) and the Energy Efficiency KPIs (6.7) depend on the same
VNFM/VR source as 5.7.1 and are therefore **not reachable** in this deployment model.

## Do we need any further specs?

**For this mapping and for instrumenting the counters: no.** TS 28.552's core-NF clauses reference
TS 23.501/502/503 (stage-2, already this project's reference material) and the TS 29.5xx SBI
specs (already vendored as R19 YAML).

**Needed only if we go further than counting**, listed now so it is not a surprise later:

| Spec | Needed for | Priority |
|---|---|---|
| TS 28.622 (Generic NRM) + TS 28.541 (5G NRM) | The measured-object classes measurements attach to (`AMFFunction`, `NRFFunction`, ...). Required to model MOIs properly. | Only if exposing measurements per spec structure |
| TS 28.532 (Performance-data reporting MnS) | The actual reporting interface/file format a real OSS would consume | Only if building a PM reporting interface |
| TS 32.401 (PM concepts) | Formal PM vocabulary | Optional |
| **TS 32.4xx charging-performance series** | **Measuring CHF at all** -- 28.552 does not cover it | **High, given ADR-0049's commercial mandate** |

## What this document does NOT do

It does not claim conformance, and it does not compare against free5GC (ADR-0238 step (4), which
needs the counters split first). It is the mapping, with the gaps stated plainly.
