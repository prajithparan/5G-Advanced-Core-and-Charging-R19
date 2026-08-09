# Charging Mapping: 3GPP CDR/Charging Fields → TM Forum SID → TM Forum Open API

**Status: DRAFT — for review before any BSS/SID mapping code is written**, per CLAUDE.md's explicit
charging-domain deliverable requirement ("Deliverable before any mapping code:
`docs/CHARGING_MAPPING.md`... Ambiguous mappings are marked TODO and asked about, never silently
invented"). No SID/TMF mapping code exists anywhere in this repo yet — this document is that
prerequisite, not a retrofit.

## Sourcing methodology

Same arms-length reference-oracle discipline this project already uses for NGAP/PFCP/GTP-U
(ADR-0016/ADR-0031/ADR-0039): every TM Forum field name and API number below was checked against a
real source (TM Forum's own public GitHub OpenAPI/Swagger specs, or TM Forum's own web pages),
not recalled from memory and asserted. Sources are cited inline. Unlike the 3GPP OpenAPI YAML
(vendored under `specs/5G_APIs-REL-19/`), **no TM Forum Open API spec files are vendored in this
repo** — they were fetched live for this document. If a field can't be confirmed against a real
source, it is marked TODO here rather than guessed.

## Scope of this pass

This maps only the 3GPP fields **actually wired today** (Phase 4 Stage 0/1, ADR-0044): SMF's
`Nchf_ConvergedCharging_Create` call, which currently sends only
`nfConsumerIdentification`/`invocationTimeStamp`/`invocationSequenceNumber`/`subscriberIdentifier`
(see `nfs/smf/src/main.cpp`'s own disclosure — `pDUSessionChargingInformation` and every other
optional block are not populated yet). `ChargingDataRequest`/`ChargingDataResponse`'s ~20 other
optional `*ChargingInformation` blocks (SMS, NEF, registration, N2 connection, IMS, MMTel, edge
infrastructure, ProSe, MMS, MBS session, TSN, NSACF, NSSAA, ranging/SL, LCS) are **out of scope for
this pass** — nothing in this codebase generates them yet, so mapping them now would be mapping
fields that don't exist in any real request this system produces. They're listed at the bottom as
known future work, not silently dropped.

## Finding: four SID entities CLAUDE.md/PROMPT.md name don't map to an API in CLAUDE.md's own TMF
## API list — flagged, not silently resolved

CLAUDE.md's stated TMF API list is TMF620/622/632/635/637/666/676/678/727. Checking each in-scope
SID entity (Product, Service, Resource, Customer, Party, Agreement, ProductOffering, ProductPrice,
AppliedCustomerBillingRate, CustomerBill, BalanceTopUp) against TM Forum's own API directory found
**four** whose real, TM-Forum-designated home API is not in that list:

| SID entity | Real TM Forum home API | Not in CLAUDE.md's list because |
|---|---|---|
| `Service` (CFS/RFS) | TMF638 Service Inventory Management / TMF633 Service Catalog Management | Neither is in the stated list |
| `Resource` | TMF639 Resource Inventory Management | Not in the stated list |
| `Agreement` | TMF651 Agreement Management | Not in the stated list |
| `BalanceTopUp` | **TMF654 Prepay Balance Management** (confirmed: `BalanceTopup` resource, `partyAccount`/`paymentMethod`/`recurringPeriod`/`nrOfPeriods` fields — [TM Forum data model](https://datamodel.tmforum.org/en/latest/Customer/BalanceTopup/), [TMF654 spec](https://tmf-open-api-table-documents.s3.eu-west-1.amazonaws.com/Historic/TMF654_PrepayBalance/3.0.0/user_guides/TMF654_Prepay_Balance_Management_API_user_guides_17.0.1.pdf)) | Not in the stated list |

**TODO, asked here rather than silently resolved**: should `docs/DECISIONS.md`/CLAUDE.md's TMF API
list be extended to include TMF638 (or TMF633), TMF639, TMF651, and TMF654, or should
`Service`/`Resource`/`Agreement`/`BalanceTopUp` be treated as out of scope until that's decided?
This document does not silently pick one.

**Also corrected**: CLAUDE.md/an earlier ADR referred to "TMF727" as possibly
"ProductOfferingQualification" — checked directly: **TMF727 is Service Usage Management**
(confirmed via TM Forum's own directory listing and cross-confirmed by TMF635's own migration note,
below); Product Offering Qualification is a different API, **TMF679**, not in CLAUDE.md's list at
all. CLAUDE.md's original "727" citation is correct as a *number*, just needs its name corrected
in any future doc that names it — flagged here, not changed silently in CLAUDE.md itself (this repo
treats CLAUDE.md as project-owner-authored).

## Mapping table

| 3GPP field (TS 32.291, `ChargingDataRequest`/`Response`) | SID entity | TMF Open API / resource / field | Confidence |
|---|---|---|---|
| `subscriberIdentifier` (SUPI) | `Party` (specifically an `Individual`, playing a `Customer` role) | **TMF632** Party Management, `Individual` resource. No single field is "the SUPI" — TM Forum's `Individual.individualIdentification` (array of `IndividualIdentification`: `identificationType` + `identificationId`) is the real extensibility point for an external subscriber identifier ([TMF632 swagger](https://github.com/tmforum-apis/TMF632_PartyManagement/blob/main/TMF632-Party-v4.0.0.swagger.json), confirmed fields: `id`, `href`, `individualIdentification`, `partyCharacteristic`, ...). **TODO**: whether SUPI is stored as an `individualIdentification` entry (`identificationType="SUPI"`) or as a `partyCharacteristic` is a real design choice, not resolved here — either is schema-legal, no 3GPP-side field forces one over the other. |
| `nfConsumerIdentification.nodeFunctionality` (e.g. `"SMF"`) | Not a SID business entity — this is 3GPP network-function provenance, not a customer/product/billing concept | No TMF mapping. Recorded here explicitly as **out of scope for SID mapping** (a technical audit field for CHF's own PDU-session-charging record, not billing-domain data), not omitted by oversight. |
| `invocationTimeStamp` | Not a standalone SID entity | Realized as the `date`/`creationDate`-shaped field on whichever usage/billing-rate record eventually gets created from this charging event — see `ProductUsage.creationDate` and `AppliedCustomerBillingRate.date` below. Not an entity on its own. |
| `invocationSequenceNumber` | Not a SID entity | CHF-internal correlation only (see ADR-0044's disclosed "echoes the request" simplification) — no TM Forum field represents "the Nth invocation of a 3GPP charging trigger", this is 3GPP-side protocol bookkeeping, not billing-domain data. Not mapped. |
| `pDUSessionChargingInformation` (currently unset — see Scope above) | `Product` (the subscribed PDU-session-capable product instance) realizing `ProductUsage` | **TMF637** Product Inventory (`Product` resource — real TM Forum description confirmed: "a product offering procured by a customer... possible actions creating/updating/retrieving Product", [TMF637 repo](https://github.com/tmforum-apis/TMF637_ProductInventory)) for the subscribed product identity, and **TMF635** Usage Management, `ProductUsage` resource for the actual usage event once populated. Confirmed real `ProductUsage` fields: `id`, `href`, `status` (`ProductUsageStatusType`: initialized/rejected/validated/rated/corrected/superseded/notRated/failed), `usageType`, `usageCharacteristic` (array), `serviceUsage` (array of `ServiceUsageRef` — split out to **TMF727** Service Usage Management as of the v4→v5 uplift), `relatedParty`, `usageProductPrice`, `product` (`ProductRef`), `usageSpecification` ([TMF635 v5.0.0 OAS](https://github.com/tmforum-apis/TMF635_UsageManagement)). **TODO, not populated yet either side**: this field is unset in every real request this codebase currently sends (disclosed in `nfs/smf/src/main.cpp`), so there is no live 3GPP data to map its sub-fields (`chargingId`, `userLocationinfo`, `pduSessionInformation`, ...) against `ProductUsage.usageCharacteristic` yet — deferred to whichever future turn actually populates this field on the SMF side. |
| CHF's allocated `chargingId` (`ChargingId`, `Uint...` alias — TS 32.291) | Correlates a `Product`'s `ProductUsage` records to the `AppliedCustomerBillingRate`/`CustomerBill` that eventually bills for them | No single TMF field *is* `chargingId` — it is this system's own internal correlation key, realized on the BSS side as whatever key value links a stored `ProductUsage.id` to an `AppliedCustomerBillingRate.characteristic` entry (`AppliedCustomerBillingRate`'s confirmed real fields: `appliedTax`, `bill` (`CustomerBillRef`), `date`, `description`, `isBilled`, `name`, `periodCoverage`, `taxExcludedAmount`, `taxIncludedAmount`, `appliedBillingRateType`, `billingAccount`, `product` (`ProductRef`), `characteristic` (array) — [TMF678 v5.0.0 OAS](https://github.com/tmforum-apis/TMF678_CustomerBill/blob/main/TMF678-CustomerBill-v5.0.0.oas.yaml)). **TODO**: whether `chargingId` is stored in `characteristic` (name="chargingId") or as a custom extension field is a real design choice, not resolved here. |
| `ChargingDataResponse.multipleUnitInformation[].grantedUnit`/`.allocatedUnit` (currently never populated — see ADR-0044's disclosed "no rating engine yet") | `AppliedCustomerBillingRate` (once rated) | **TMF678**, `AppliedCustomerBillingRate.taxIncludedAmount`/`.taxExcludedAmount` (both `Money`) for a monetary grant, or `characteristic` for a raw unit-count grant (volume/time/service-specific-units) if the granted quota is never actually money-rated in this system's flow. **TODO, genuinely ambiguous, not resolved here**: TS 32.291's `GrantedUnit` is pre-rating (raw units: `totalVolume`/`uplinkVolume`/`downlinkVolume`/`time`/`serviceSpecificUnits`), while `AppliedCustomerBillingRate` is TM Forum's *post-rating* billing-rate concept — mapping a pre-rating quota grant onto a post-rating billing entity is not a clean 1:1 and needs a real rating-engine design decision (which doesn't exist in this codebase yet) before it can be answered correctly rather than guessed. |

## Deferred, not mapped in this pass (disclosed, not omitted by oversight)

- **Every other `*ChargingInformation` block** on `ChargingDataRequest` (SMS, NEF, registration,
  N2 connection, IMS, MMTel, edge infrastructure, ProSe, MMS, MBS session, TSN, NSACF, NSSAA,
  ranging/SL, LCS) — none are populated by any NF in this codebase yet (only PDU-session charging
  is wired, ADR-0044), so there is no real request shape to map against SID entities without
  inventing one. Each needs its own mapping pass once/if the corresponding NF actually starts
  sending that block.
- **`ChargingDataResponse.sessionFailover`/`.invocationResult`** — protocol-level
  retry/error-handling fields, same "not a SID business entity" reasoning as
  `invocationSequenceNumber` above; not expected to ever need a SID mapping.
- **`ProductOffering`/`ProductPrice` (TMF620), `CustomerOrder`/`Agreement` (TMF622/TMF651),
  `Customer`/`Party` role modeling beyond the single `individualIdentification` question above,
  `CustomerBill` generation itself (as opposed to the `AppliedCustomerBillingRate` line items that
  feed it), `Policy`, `Event`** — real TM Forum entities named in CLAUDE.md/PROMPT.md's scope, but
  nothing in this codebase produces or consumes product-catalog, ordering, or policy data yet (no
  NF has a real product catalog, no CHF rating engine exists to consult one) — mapping these now
  would be inventing a shape for data this system doesn't have, not documenting a real mapping.
  Deferred to whichever future turn actually builds product-catalog/ordering/rating logic.

## Next steps (not started, awaiting direction)

1. Resolve the four API-list gaps above (TMF638/633, TMF639, TMF651, TMF654) — add to CLAUDE.md's
   stated list, or explicitly scope those SID entities out.
2. Resolve the two TODOs marked inline above (`individualIdentification` vs `partyCharacteristic`
   for SUPI storage; how `chargingId` is represented in `AppliedCustomerBillingRate`).
3. Once resolved: implement the mapping as real code (a new `libs/`-level component, per CLAUDE.md's
   "the BSS layer could be swapped for a commercial stack" ODA-boundary requirement — not baked
   into CHF itself), covering only the fields this table actually maps (no speculative coverage of
   the deferred rows above).
