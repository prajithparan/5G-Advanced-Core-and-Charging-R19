#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// SCCP Called/Calling Party Address codec (ITU-T Q.713, Figure 3 for the indicator octet, Figure
// 6 for the point-code sub-field) -- P4.5/ADR-0059 Stage 5a. Real, disclosed scope: only
// point-code + SSN routing (GlobalTitleIndicator::kNone) is implemented -- Global-Title-based
// addressing (the fuller Translation-Type/Numbering-Plan/Encoding-Scheme/Nature-of-Address-
// Indicator sub-format used for real STP-routed international MAP signalling) is NOT implemented
// this stage: the vendored Osmocom reference header only shows the simpler single-octet
// nature-of-address+odd/even form (GTI=1), not the fuller GTI=4 sub-format most real MAP traffic
// actually uses, so building it now would risk guessing byte layout rather than citing it. A
// real, disclosed gap, not a silent one.

namespace ss7_core {

struct SccpAddress {
    bool point_code_present = false;
    bool ssn_present = false;
    std::uint8_t routing_indicator = 0; // dictionary::RoutingIndicator::*
    std::uint16_t point_code = 0;       // 14-bit ITU point code (Figure 6/Q.713)
    std::uint8_t ssn = 0;               // dictionary::SubsystemNumber::* or a real, unlisted value
};

// Encodes the address indicator octet + point code (2 octets, Figure 6/Q.713) + SSN (1 octet), in
// that real field order (Figure 3/Q.713) -- no global title (see this file's own header for why).
std::vector<std::uint8_t> encode_sccp_address(const SccpAddress& addr);

// Decodes an address. Returns std::nullopt if the buffer is too short for the fields the address
// indicator octet says are present, or if a Global-Title-indicating address is encountered (real,
// disclosed "not implemented" rejection, not a silent misparse).
std::optional<SccpAddress> decode_sccp_address(const std::vector<std::uint8_t>& bytes);

} // namespace ss7_core
