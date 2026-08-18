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
// ADR-0071: TERMR (Termination Report, octet 6 bit 4) -- real spec text explicitly names this the
// trigger for "a usage report being reported (in a Sx Session Deletion Response) for a URR due to
// the termination of the Sx session" (the exact case this project's Session Deletion handling
// uses it for), confirmed directly against the spec figure, not assumed from the bit's name alone.
std::vector<std::uint8_t> encode_usage_report_trigger_termr();

// The following (TS 29.244 Create/Update/Remove QER, Create/Update/Remove BAR) support ADR-0071's
// gap-closure Tier 1d: real per-session QoS enforcement (QER) and real BAR protocol-level
// bookkeeping. Byte layouts confirmed directly against the real spec PDF (specs/PFCP/29244-e30.pdf
// V14.3.0), same discipline as every other IE in this file -- see each function's own citation.

// TS 29.244 §8.2.75: QER ID -- Unsigned32, same real "bit 8 of octet 5 = 0 for dynamically-CP-
// allocated" convention as FAR ID/URR ID (confirmed identical spec text, not assumed identical
// from the IE type numbers alone) -- reuses that exact codec shape.
std::vector<std::uint8_t> encode_qer_id(std::uint32_t id);
std::optional<std::uint32_t> decode_qer_id(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.57: BAR ID -- real, confirmed DIFFERENT shape from QER/FAR/URR ID: a single
// octet (range 0-255), no CP-allocated-bit convention exists for it in the real spec text at all
// (checked, not assumed identical to the 4-octet IDs above).
std::vector<std::uint8_t> encode_bar_id(std::uint8_t id);
std::optional<std::uint8_t> decode_bar_id(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.7: Gate Status -- 1 octet, UL Gate in bits 4-3, DL Gate in bits 2-1 (real bit
// positions, confirmed from the real spec figure, not assumed). OPEN=0, CLOSED=1 both real Table
// 8.2.7-1/8.2.7-2 values; the real spec's own reserved values 2-3 are not encoded by this project
// and decode as CLOSED per the spec's own "shall be interpreted as the value '1'" fallback rule.
struct GateStatus {
    bool ul_closed = false;
    bool dl_closed = false;
};
std::vector<std::uint8_t> encode_gate_status(const GateStatus& v);
std::optional<GateStatus> decode_gate_status(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.8: MBR (Maximum Bitrate) -- real fixed 10 octets: 5-octet UL MBR + 5-octet DL
// MBR, each a binary integer in real units of kbps (1000 bps -- confirmed from the real spec
// text, not assumed to be plain bps). 5 octets comfortably fits in std::uint64_t (max real value
// 2^40-1 kbps).
struct Mbr {
    std::uint64_t ul_kbps = 0;
    std::uint64_t dl_kbps = 0;
};
std::vector<std::uint8_t> encode_mbr(const Mbr& v);
std::optional<Mbr> decode_mbr(const std::vector<std::uint8_t>& value);

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #101, ADR-0092): real TS 29.244 §8.2.56 Outer
// Header Creation -- the IE a downlink FAR's own Forwarding Parameters uses to instruct UPF to
// GTP-U-encapsulate outgoing packets toward a real peer (a gNB, in this project's own N3 use).
// Real byte layout: 2-octet Outer Header Creation Description (bitmask; this project only ever
// sets bit 5/1, "GTP-U/UDP/IPv4"), then TEID (4 octets, present because the GTP-U bit is set),
// then IPv4 Address (4 octets, present because the IPv4 bit is implied by GTP-U/UDP/IPv4) -- no
// IPv6/Port Number octets, this project's only real address family and encapsulation choice.
std::vector<std::uint8_t> encode_outer_header_creation_gtpu_ipv4(std::uint32_t teid,
                                                                 std::array<std::uint8_t, 4> ipv4);
struct OuterHeaderCreationGtpuIpv4 {
    std::uint32_t teid = 0;
    std::array<std::uint8_t, 4> ipv4{};
};
std::optional<OuterHeaderCreationGtpuIpv4>
decode_outer_header_creation_gtpu_ipv4(const std::vector<std::uint8_t>& value);

} // namespace pfcp_core
