#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "pfcp_core/ie.hpp"

// Encode/decode helpers for the Information Elements Phase 3 Stage 3 (real N4 Session
// Establishment, docs/DECISIONS.md ADR-0042) needs: F-SEID, PDR ID, Precedence, FAR ID, Apply
// Action, Source/Destination Interface, F-TEID. Byte layouts read directly from the real 3GPP
// TS 29.244 V14.3.0 spec PDF (specs/PFCP/29244-e30.pdf) -- see header.hpp's own comment for the
// version-gap disclosure this project already carries for every PFCP byte layout.

namespace pfcp_core {

// TS 29.244 §8.2.2/§8.2.24 -- Source Interface and Destination Interface share this exact value
// table (confirmed against both real spec figures independently, not assumed identical).
enum class InterfaceValue : std::uint8_t {
    Access = 0,
    Core = 1,
    SgiLan = 2,
    CpFunction = 3, // Source Interface only
};

std::vector<std::uint8_t> encode_source_interface(InterfaceValue v);
std::vector<std::uint8_t> encode_destination_interface(InterfaceValue v);
std::optional<InterfaceValue> decode_interface_value(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.36: PDR ID -- a 2-octet Rule ID (this build's own IDs, small integers, always
// dynamically-allocated-by-CP scope -- no predefined-in-UPF rules exist in this project).
std::vector<std::uint8_t> encode_pdr_id(std::uint16_t id);
std::optional<std::uint16_t> decode_pdr_id(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.11: Precedence -- Unsigned32, LOWER value = HIGHER precedence (spec's own
// wording, not the intuitive direction -- confirmed from the real spec text, not assumed).
std::vector<std::uint8_t> encode_precedence(std::uint32_t value);
std::optional<std::uint32_t> decode_precedence(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.74: FAR ID -- Unsigned32, bit 8 of the first (most significant) octet = 0 for
// dynamically-CP-allocated (this project's only case; naturally 0 for the small integers used
// here, never set explicitly).
std::vector<std::uint8_t> encode_far_id(std::uint32_t id);
std::optional<std::uint32_t> decode_far_id(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.26: Apply Action -- this project only ever sends/expects FORW (bit 2), the only
// action Stage 3's minimal uplink-forwarding PDR/FAR needs.
std::vector<std::uint8_t> encode_apply_action_forward();
bool decode_apply_action_has_forward(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.3: F-TEID. Two shapes this project uses:
//  - "CH request" (CP -> UP, inside a PDI): CH bit set, V4 bit set, no further octets -- asks the
//    UP function to allocate its own local F-TEID for this PDR (an IPv4 one, this project's only
//    address family throughout).
//  - "allocated" (UP -> CP, inside a Created PDR): V4 bit set, real TEID (4 octets) + real IPv4
//    address (4 octets), no CH/CHID bits.
std::vector<std::uint8_t> encode_f_teid_choose_ipv4();
std::vector<std::uint8_t> encode_f_teid_allocated_ipv4(std::uint32_t teid,
                                                       std::array<std::uint8_t, 4> ipv4);
struct FTeidAllocated {
    std::uint32_t teid = 0;
    std::array<std::uint8_t, 4> ipv4{};
};
std::optional<FTeidAllocated> decode_f_teid_allocated_ipv4(const std::vector<std::uint8_t>& value);
// True if the CH (CHOOSE) bit is set -- this project's decoder for the request-side form; UPF
// uses this to recognize "allocate one for me" without needing the full allocated-value decoder.
bool decode_f_teid_is_choose_request(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.37: F-SEID -- a Session Endpoint Identifier (8-octet SEID) plus the sending
// entity's own IPv4 address (this project's only address family). Used both as CP F-SEID (in the
// Establishment Request, telling UP which SEID to use when addressing this session back) and UP
// F-SEID (in the Establishment Response, the symmetric value for the other direction).
struct FSeid {
    std::uint64_t seid = 0;
    std::array<std::uint8_t, 4> ipv4{};
};
std::vector<std::uint8_t> encode_f_seid_ipv4(const FSeid& f_seid);
std::optional<FSeid> decode_f_seid_ipv4(const std::vector<std::uint8_t>& value);

// The following (TS 29.244 §7.5.2.4 Create URR, §7.5.8.3 Usage Report) support Phase 4's real
// online-charging quota-consumption/re-authorization flow (docs/DECISIONS.md ADR-0049's
// commercialization mandate turn) -- byte layouts confirmed directly against the real spec PDF,
// same discipline as every other IE in this file. Only what Annex C.2.1.1's real "online charging
// with intermediate and final quotas" call flow needs is modeled: volume-based measurement with a
// Total Volume threshold/quota, not time-based or event-based measurement, and not the many other
// optional Create URR/Usage Report fields (Time Threshold, Quota Holding Time, Monitoring Time,
// Linked URR ID, Application Detection Information, ...) this project has no real use for yet.

// TS 29.244 §8.2.54: URR ID -- Unsigned32. Bit 8 of the first octet = 0 for dynamically-CP-
// allocated (this project's only case, naturally 0 for the small integers used here, never set
// explicitly -- same convention as encode_far_id).
std::vector<std::uint8_t> encode_urr_id(std::uint32_t id);
std::optional<std::uint32_t> decode_urr_id(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.71: UR-SEQN (Usage Report Sequence Number) -- Unsigned32.
std::vector<std::uint8_t> encode_ur_seqn(std::uint32_t seqn);
std::optional<std::uint32_t> decode_ur_seqn(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.40: Measurement Method -- this project only ever requests VOLUM (bit 2, volume-
// based measurement), the only measurement basis its online-charging quota flow needs.
std::vector<std::uint8_t> encode_measurement_method_volume();

// TS 29.244 §8.2.19: Reporting Triggers -- 2-octet bitmask. This project only ever requests VOLTH
// (bit 2 of octet 5, report on reaching the Volume Threshold) and VOLQU (bit 1 of octet 6, report
// on Volume Quota exhaustion) -- the two triggers Annex C.2.1.1's real call flow uses.
std::vector<std::uint8_t> encode_reporting_triggers_volume();

// TS 29.244 §8.2.13/§8.2.50/§8.2.44: Volume Threshold, Volume Quota, and Volume Measurement all
// share this exact wire structure (confirmed independently against all three real spec figures,
// not assumed identical from the IE type numbers alone): a 1-octet TOVOL/ULVOL/DLVOL flag octet
// followed by whichever Unsigned64 volume fields the flags select, in TOVOL/ULVOL/DLVOL order.
// This project only ever uses the Total Volume (TOVOL) field -- one shared codec for all three IE
// types, since the byte layout is identical.
std::vector<std::uint8_t> encode_volume_total(std::uint64_t total_octets);
std::optional<std::uint64_t> decode_volume_total(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.21: Report Type -- this project only ever needs to recognize USAR (bit 2, Usage
// Report present).
bool decode_report_type_has_usage_report(const std::vector<std::uint8_t>& value);
// ADR-0050 Stage 2: UPF is the encoder of Report Type in the real UP->CP direction (a real,
// unsolicited Session Report Request) -- the mirror image of decode_report_type_has_usage_report
// above, which SMF (the CP side) uses to recognize one on receipt.
std::vector<std::uint8_t> encode_report_type_usage_report();

// TS 29.244 §8.2.41: Usage Report Trigger -- 2-octet bitmask, same octet-pair convention as
// Reporting Triggers but a different bit assignment per the real spec (confirmed independently,
// not assumed identical). This project only distinguishes the two triggers its flow can produce.
enum class UsageReportTriggerValue : std::uint8_t {
    VolumeThreshold,
    VolumeQuotaExhausted,
    Other,
};
UsageReportTriggerValue decode_usage_report_trigger(const std::vector<std::uint8_t>& value);
// ADR-0050 Stage 2: UPF-side encoders for the two real triggers its own datapath can detect (see
// gtpu_decap.bpf.c's urr_state/usage_report_event) -- no encoder for Other, since UPF itself never
// produces that value (it exists only as decode_usage_report_trigger's fallback for bit patterns
// this project doesn't otherwise model).
std::vector<std::uint8_t> encode_usage_report_trigger_volth();
std::vector<std::uint8_t> encode_usage_report_trigger_volqu();

} // namespace pfcp_core
