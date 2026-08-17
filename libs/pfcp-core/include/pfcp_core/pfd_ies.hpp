#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pfcp_core/ie.hpp"

// Encode/decode helpers for gap-closure task #107 part 2 (docs/CAPABILITY_GAP_ANALYSIS.md,
// ADR-0086): the real Sx PFD Management Request/Response message pair (TS 29.244 §7.4.3). Byte
// layouts read directly from the real 3GPP TS 29.244 V14.3.0 spec PDF (specs/PFCP/29244-e30.pdf)
// -- see header.hpp's own comment for the version-gap disclosure this project already carries for
// every PFCP byte layout.
//
// Application ID (§8.2.6) is a bare OctetString -- decode_application_id below exists only for
// naming symmetry with every other per-IE decode function in this library, not because the wire
// format needs any real parsing beyond "the value bytes are the string".
//
// PFD Contents (§8.2.39) is the one real structured IE this message pair needs: a flag octet (FD/
// URL/DN/CP bits, spec order) followed by a spare octet, then each present sub-field as its own
// 2-byte-length-prefixed OctetString, in that same FD/URL/DN/CP order (confirmed from Figure
// 8.2.39-1's own octet layout, not assumed). "Application ID's PFDs" (type 58) and "PFD context"
// (type 59, called plain "PFD" in its own sub-table heading -- a real, disclosed naming
// inconsistency in the spec text itself) are both grouped IEs with no fixed-order fixed-count
// children of their own beyond what decode_ies/find_ie (or a manual loop over matching-type IEs,
// for the repeatable ones) already handle -- no dedicated struct/codec for either, same choice
// CreatePdr/CreateFar already made in session_ies.hpp.

namespace pfcp_core {

inline std::vector<std::uint8_t> encode_application_id(const std::string& application_id) {
    return std::vector<std::uint8_t>(application_id.begin(), application_id.end());
}

inline std::string decode_application_id(const std::vector<std::uint8_t>& value) {
    return std::string(value.begin(), value.end());
}

// Real, disclosed scope: this project's UPF has no Application Detection Filter (ADF) traffic-
// classification engine to actually consume these fields against live packets (a real, separate,
// much larger gap -- see docs/CAPABILITY_GAP_ANALYSIS.md's own UPF section). This struct exists so
// PFDs can be received, stored, and round-tripped correctly; it is real provisioning-plane state,
// not yet wired to any data-plane decision.
struct PfdContents {
    std::optional<std::vector<std::uint8_t>> flow_description; // FD bit, TS 29.251 §6.4.3.7
    std::optional<std::vector<std::uint8_t>> url;              // URL bit, TS 29.251 §6.4.3.8
    std::optional<std::vector<std::uint8_t>> domain_name;      // DN bit, TS 29.251 §6.4.3.9
    std::optional<std::vector<std::uint8_t>> custom_content;   // CP bit, no further spec structure
};

std::vector<std::uint8_t> encode_pfd_contents(const PfdContents& contents);
std::optional<PfdContents> decode_pfd_contents(const std::vector<std::uint8_t>& value);

} // namespace pfcp_core
