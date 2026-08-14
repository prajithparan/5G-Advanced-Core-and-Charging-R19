#pragma once

#include <cstdint>

// Real MAP (Mobile Application Part) constants -- 3GPP TS 29.002 V19.0.0, fetched directly from
// ETSI's own /deliver/ portal (specs/ts_129002v190000p.pdf, not committed -- ETSI-copyrighted).
// P4.5/ADR-0059 Stage 7 (MAP) kickoff.
//
// Real, disclosed scope: MAP is a genuinely huge protocol (clause 17, ~170 pages, 25 ASN.1
// modules, ~90+ operations covering mobility management, call handling, SS, SMS, group-call,
// LCS). This first increment covers exactly one real operation --
// insertSubscriberData (TS 29.002 clause 17.6.1, page 358) -- chosen because it is the real
// mechanism by which an HLR provisions CAMEL Subscription Info (O-CSI/D-CSI) to a VLR/MSC, which
// is what causes a real switch to later invoke CAP's InitialDP (already built, libs/cap-core) --
// closing the real architectural loop between the two protocols. All other MAP operations are
// out of scope for this increment; their real opcodes (cited below where already found during
// research) are NOT wired to any argument codec yet.
//
// Real, disclosed gap: the numeric Application Context Object Identifier for this operation
// (subscriberDataMngtContext-v3, TS 29.002 clause 17.3.2.17: "ID {map-ac subscriberDataMngt(16)
// version3(3)}") cannot be fully resolved from TS 29.002 alone -- clause 17.1.6 states plainly
// that `MobileDomainDefinitions` (which would define the `gsm-NetworkId`/`ac-Id` root arcs that
// `map-ac` is built from: "map-ac OBJECT IDENTIFIER ::= {gsm-NetworkId ac-Id}") is an external
// module "defined in the technical specification Mobile Services Domain" -- NOT reproduced in
// TS 29.002 itself. Not fabricated here; only the real, cited symbolic name/version is recorded.
// This does not block the operation-argument codec below, which needs no Application Context.

namespace map_core {

// Real operation local codes -- TS 29.002 clause 17.6.1 (MAP-MobileServiceOperations), each cited
// with its exact CODE local:N as printed on the page. Only insertSubscriberData has an argument
// codec in map_operations.hpp; the rest are recorded as real, cited facts for future stages.
namespace Opcode {
constexpr std::int32_t kUpdateLocation = 2;
constexpr std::int32_t kCancelLocation = 3;
constexpr std::int32_t kInsertSubscriberData = 7;
constexpr std::int32_t kDeleteSubscriberData = 8;
constexpr std::int32_t kCheckImei = 43;
constexpr std::int32_t kSendIdentification = 55;
constexpr std::int32_t kSendAuthenticationInfo = 56;
constexpr std::int32_t kPurgeMs = 67;
} // namespace Opcode

// Real error names used by insertSubscriberData (TS 29.002 clause 17.6.1, page 358: "ERRORS {
// dataMissing | unexpectedDataValue | unidentifiedSubscriber }"). Real, disclosed gap: the
// numeric local error codes for these (module 12, MAP-Errors) were not located during this
// increment's research pass -- clause 17.6.9 (where MAP-Errors' operations-clause slot would sit,
// by the module-number ordering TS 29.002 clause 17.1 itself documents) is marked "Void" in this
// V19.0.0 text, so the numeric definitions live somewhere else not yet found. Not fabricated: no
// numeric constants are declared for these until located.

} // namespace map_core
