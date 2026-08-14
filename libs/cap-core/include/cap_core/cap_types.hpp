#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "tcap_core/ber.hpp"

// Shared CAP (TS 29.078) data-type building blocks used by more than one operation argument.
// P4.5/ADR-0059 Stage 6 (CAP) kickoff. Real ASN.1 facts cited per-declaration below.

namespace cap_core {

using tcap_core::Tlv;

// Real ASN.1 rule, cited directly from the CAP-errortypes module header (TS 29.078 clause 5.2):
// "Where a parameter of type CHOICE is tagged with a specific tag value, the tag is automatically
// replaced with an EXPLICIT tag of the same value." This project's own CAP-gsmSSF-gsmSCF-ops-args
// module is DEFINITIONS IMPLICIT TAGS, so every non-CHOICE field tag replaces the universal tag
// directly, but every field whose type is a CHOICE (SendingSideID, ReceivingSideID,
// AChBillingChargingCharacteristics, CallResult's own inner variants, TimeInformation, ...) must be
// wrapped EXPLICITLY instead. These two helpers implement that one rule generically so every
// operation-argument codec applies it identically rather than re-deriving it per field.
Tlv wrap_explicit(std::uint32_t tag_number, const Tlv& inner);
std::optional<Tlv> unwrap_explicit(const Tlv& outer, std::uint32_t expected_tag_number);

// LegType ::= OCTET STRING (SIZE(1)); leg1 LegType ::= '01'H, leg2 LegType ::= '02'H.
// TS 29.078 clause 5.1 ("LegType").
enum class LegType : std::uint8_t { kLeg1 = 0x01, kLeg2 = 0x02 };

// SendingSideID {PARAMETERS-BOUND:bound} ::= CHOICE { sendingSideID [0] LegType }
// ReceivingSideID ::= CHOICE { receivingSideID [1] LegType }
// TS 29.078 clause 5.1. Each CHOICE has exactly one real alternative, so both are modeled as a
// bare LegType value here; the caller applies wrap_explicit/unwrap_explicit at the embedding site.
Tlv encode_sending_side_id(LegType leg);
std::optional<LegType> decode_sending_side_id(const Tlv& tlv);

Tlv encode_receiving_side_id(LegType leg);
std::optional<LegType> decode_receiving_side_id(const Tlv& tlv);

// TimeInformation ::= CHOICE { timeIfNoTariffSwitch [0] TimeIfNoTariffSwitch, ... } where
// TimeIfNoTariffSwitch ::= INTEGER(0..86400), measured in 100 millisecond units (TS 29.078
// clause 5.1 "ElapsedTime"/"TimeInformation"/"TimeIfNoTariffSwitch").
// Real, disclosed scope: only the timeIfNoTariffSwitch variant is implemented. timeIfTariffSwitch
// (used only when a mid-call tariff switch occurred) is not -- not needed for a first, minimal
// InitialDP -> ApplyCharging -> ApplyChargingReport round trip.
Tlv encode_time_information_no_tariff_switch(std::int32_t hundred_ms_units);
std::optional<std::int32_t> decode_time_information_no_tariff_switch(const Tlv& tlv);

// Cause {PARAMETERS-BOUND:bound} ::= OCTET STRING (SIZE(bound.&minCauseLength ..
// bound.&maxCauseLength)) -- TS 29.078 clause 5.1 ("Cause"). Real, disclosed scope: this project
// does not decode the ETSI EN 300 356-1 (ISUP) Cause parameter internal structure (location/cause
// value/diagnostics octets) -- the primary ISUP text was not read as part of this increment, only
// referenced by TS 29.078 itself. Carried as opaque bytes, same disclosed-scope treatment already
// used for this codebase's SCCP Global Title and Diameter Host-IP-Address fields.
// cAPSpecificBoundSet (TS 29.078 clause 5.5) bounds this to 2..32 octets; not enforced here
// (encode/decode is length-agnostic, matching this project's existing OCTET STRING helpers
// elsewhere).
std::vector<std::uint8_t> encode_cause(const std::vector<std::uint8_t>& cause_octets);
std::vector<std::uint8_t> decode_cause(const Tlv& tlv);

} // namespace cap_core
