#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cdr.hpp"

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #108, ADR-0089): real TS 32.298 CHF-CDR
// encoding (Charging Data Record parameter description, clause 5.1.5/5.2.5.2), read directly from
// the real, vendored ETSI TS 132 298 V18.8.0 (Release 18) spec PDF (specs/TS_32_298.pdf) -- not
// adapted from any reference implementation's own structs, per ADR-0001's greenfield discipline.
// Disclosed version gap: v18.8.0 (Release 18) is the release actually supplied and read, not this
// project's own REL-19 baseline -- same disclosed-gap shape as PFCP's own V14.3.0 (ADR-0039); the
// CHF-CDR ASN.1 structure has not been re-verified against a REL-19 text.
//
// Real BER (X.690) encoding, reusing libs/tcap-core's own generic TLV primitives
// (tcap_core::Tlv/encode_tlv/encode_integer) rather than building a second BER codec -- the exact
// "reuse, not rebuild" precedent already established for TAP3 (docs/DECISIONS.md ADR-0067's own
// Decision 2: tcap_core::ber.hpp's primitives are generic X.690 BER, nothing TCAP-specific).
// `CHFRecord`'s own ASN.1 module declares `DEFINITIONS IMPLICIT TAGS`, so every field below is
// encoded with tcap_core::TagClass::kContext replacing (not wrapping) that field's own universal
// tag, exactly as X.690 §31 (implicit tagging) requires.
//
// Real, disclosed scope: only the generic `ChargingRecord` fields this project's own CHF has real
// data for are encoded -- `recordType`/`recordingNetworkFunctionID`/`subscriberIdentifier`/
// `nFunctionConsumerInformation`/`listOfMultipleUnitUsage`/`recordOpeningTime`/`duration`/
// `causeForRecClosing`/`localRecordSequenceNumber`/`chargingSessionIdentifier`/
// `invocationTimestamp`. The real ASN.1 `ChargingRecord` SET has 46 total fields ([0]..[45]);
// every other one is real, cited, and deliberately NOT populated here -- most need a separate,
// unvendored spec (`pDUSessionChargingInformation`/`pDUContainerInformation` need TS 32.255,
// `iMSChargingInformation` needs TS 32.260, etc.) or simply don't apply to this project's own
// current CHF scope (no real MMTel/SMS/ProSe/edge/MBS/NSACF consumer exists). See cdr_asn1.cpp's
// own per-field comments for the exact real tag number and spec clause each populated field cites.
//
// This is an ADDITIONAL real, spec-conformant encoded form, not a replacement for the existing
// `cdr` table's own structured columns (ADR-0058, Doris-backed since ADR-0192) -- those stay real
// and queryable for this project's own gap-detection/analytics use (CdrWriter::detect_gaps); this
// encoder's own output is stored alongside them in a new `asn1_cdr` column, matching how a real
// billing-mediation system would actually consume CHF-CDRs (as an exported encoded blob, not a
// live SQL query).

namespace chf {

// Encodes one `CHFRecord ::= CHOICE { chargingFunctionRecord [200] ChargingRecord }` (real tag
// 200, TS 32.298 §5.2.5.2) for `record`. `recording_network_function_id` is this CHF instance's
// own UUID (real field [1], `NetworkFunctionName`, IA5String(1..36) -- "Shall be a... UUID version
// 4", per the spec's own field comment). Returns an empty vector (real, disclosed, not an error)
// if `record.nf_consumer_node_functionality` has no real TS 32.298 `NetworkFunctionality` value to
// map to -- see cdr_asn1.cpp's own `map_network_functionality` for the exact real, cited value set
// this covers.
std::vector<std::uint8_t> encode_chf_cdr(const CdrRecord& record,
                                         const std::string& recording_network_function_id);

} // namespace chf
