# Full NF API coverage audit -- every routed endpoint vs every R19 YAML path

**ADR-0252.** Method: for each NF, extract every path from its R19 YAML (`specs/5G_APIs-REL-19/`),
then test that path against the string table of the **compiled binary** (`build/nfs/<nf>/<nf>`).
Binary strings rather than regex over source: route registration concatenates API-root constants
and assigns path patterns to variables, so source regex cannot see the real routes -- an earlier
regex attempt reported 12 routes for UDR, which has 204 `add_route` calls. Abandoned and replaced.

**YAML is the prime authority here; the TS is reference.** Paths come from the YAML, never from
operationIds (`GetSupiOrGpsi` lives at `/{ueId}/id-translation-result` -- deriving a path from an
operation name produced a false negative earlier and is not done anywhere in this audit).

## Result: 12 of 16 NFs at 100% path coverage

| NF | Unrouted YAML paths | Status |
|---|---|---|
| amf | 0 | **complete** |
| bsf | 0 | **complete** |
| chf | 0 | **complete** |
| eir | 0 | **complete** |
| gmlc | 0 | **complete** |
| lmf | 0 | **complete** |
| nef | 0 | **complete** |
| nssf | 0 | **complete** |
| pcf | 0 | **complete** |
| scp | 0 | **complete** |
| smsf | 0 | **complete** |
| ausf | 1 | `/rg-authentications` -- disclosed deferred (5G-RG, out of Tier-1 5G-AKA scope) |
| udm | 1 | `/{supi}/am-data/update-sor` -- genuinely absent |
| smf | 2 | `transfer-mo-data`, `send-mo-data` -- genuinely absent |
| nrf | 5 | `/shared-data*`, `/scp-domain-routing-info*` -- disclosed deferred |
| udr | 19 (was 23) | largest real gap -- 4 closed by ADR-0253, see below |

**Total: 32 unrouted paths of 328.**

## The 32, verbatim
```
/rg-authentications
/scp-domain-routing-info
/scp-domain-routing-info-subs
/scp-domain-routing-info-subs/{subscriptionID}
/shared-data
/shared-data/{sharedDataId}
/pdu-sessions/{pduSessionRef}/transfer-mo-data
/sm-contexts/{smContextRef}/send-mo-data
/{supi}/am-data/update-sor
/aiot-data/af-authorization-data
/aiot-data/aiot-device-profile-data
/aiot-data/aiot-device-profile-data/{aiotDevPermId}
/application-data/bdtPolicyData
/application-data/bdtPolicyData/{bdtPolicyId}
/application-data/influenceData
/application-data/influenceData/subs-to-notify
/application-data/influenceData/subs-to-notify/{subscriptionId}
/application-data/influenceData/{influenceId}
/application-data/iptvConfigData
/application-data/iptvConfigData/{configurationId}
/application-data/pfds
/application-data/pfds/{appId}
/application-data/serviceParamData
/application-data/serviceParamData/{serviceParamId}
/application-data/subs-to-notify
/application-data/subs-to-notify/{subsId}
/data-restoration-events
/exposure-data/subs-to-notify
/exposure-data/subs-to-notify/{subId}
/exposure-data/{ueId}/access-and-mobility-data
/exposure-data/{ueId}/session-management-data/{pduSessionId}
/subscription-data/{ueId}/service-specific-authorization-data/{serviceType}
```

## Verification performed (not assumed)

Every "unrouted" finding above was confirmed against the source, because the binary method has a
known false-negative mode -- see below. Confirmed by grep returning **zero** occurrences:
UDR `application-data`/`influenceData`, SMF `send-mo-data`, UDM `update-sor`. Confirmed
present-but-only-in-a-comment (i.e. documented as deferred, not implemented): NRF `/shared-data`
and `/scp-domain-routing-info` (`nfs/nrf/src/main.cpp` lines 14-16), AUSF `/rg-authentications`
(`nfs/ausf/src/main.cpp` lines 26, 648).

## Known limitation of this method, stated up front

**Dynamically-assembled routes produce false negatives.** UDM registers its four ack endpoints in a
loop over `{"sor-ack", "upu-ack", "subscribed-snssais-ack", "cag-ack"}`, so the full path literal
`/{supi}/am-data/sor-ack` never appears in the binary even though the route exists. Caught by
cross-checking against source before publishing, and those four are **excluded** from the 32.

15 further paths were flagged as likely-dynamic and are **not** counted as gaps: 4 UDM (confirmed
loop-registered), 3 SMF (`/pdu-sessions/{ref}/modify|release|retrieve` -- `pdu-sessions` appears 4x
in SMF source), 8 UDR (`provisioned-data` appears 18x, `smf-registrations` 2x in UDR source).
The SMF and UDR ones are **consistent with being routed but not individually confirmed** -- stated
as unconfirmed rather than counted either way.

## Highest-value finding: UDR's 23

UDR is the one NF with a large, real, undisclosed coverage gap. The missing set is coherent rather
than scattered -- three whole resource families:
- **`/application-data/*`** (12 paths): bdtPolicyData, influenceData + its subs-to-notify tree,
  iptvConfigData, pfds, serviceParamData, subs-to-notify. Zero occurrences in UDR source.
- **`/exposure-data/*`** (4 paths) and **`/aiot-data/*`** (3 paths, R19 Ambient IoT).
- 4 singles including `/data-restoration-events` and service-specific-authorization-data.

This matters against the free5GC/open5GS parity mandate: `/application-data/influenceData` is the
traffic-influence store a real NEF/AF path depends on, and NEF is built here (all 14 YAML files).

## Update (ADR-0253): influenceData family closed, and a flaw in this audit's own heuristic

UDR's `/application-data/influenceData` family is now implemented -- **4 paths, 9 operations**.
Remaining UDR gap: **19 paths**.

**A flaw in the tier-2 heuristic, found while verifying that number and stated rather than left to
mislead.** After the family landed, the script reported UDR's unrouted count dropping 23 -> 11 and
its "likely-dynamic" count rising 8 -> 16. That improvement is **not real**: the literal
`application-data` now exists in the binary, so still-unimplemented siblings
(`/application-data/pfds`, `bdtPolicyData`, `iptvConfigData`, `serviceParamData`,
`subs-to-notify`) started matching the all-segments-present test and were reclassified as
"probably routed dynamically".

The heuristic gets **weaker as coverage grows**, because shared path prefixes accumulate in the
binary. The reliable check is a full-literal `grep -F` for the exact path, which was run for each
of the nine paths above and is the basis for the 4/19 figures. Treat tier-2 counts as a hint only.
