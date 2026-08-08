#pragma once

#include "pfcp_core/ie.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

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
std::vector<std::uint8_t> encode_f_teid_allocated_ipv4(std::uint32_t teid, std::array<std::uint8_t, 4> ipv4);
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

} // namespace pfcp_core
