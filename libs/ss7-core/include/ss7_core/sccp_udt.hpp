#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "ss7_core/sccp_address.hpp"

// SCCP UDT (Unitdata, connectionless class 0/1) message codec -- ITU-T Q.713 Chapter 4 -- P4.5/
// ADR-0059 Stage 5a. UDT is the real, standard transport real GSM MAP/CAP dialogues ride over in
// the large majority of real deployments (connectionless SCCP, not the connection-oriented
// CR/CC/DT class 2/3 messages, which this stage does NOT implement -- a real, disclosed scope
// narrowing to the transport this codebase's own future MAP/CAP work will actually need).
//
// Real, disclosed evidence-tier caveat: the field ORDER (type, protocol class, then three
// single-byte pointers, then the three length-prefixed variable fields) is confirmed directly from
// the vendored Osmocom `sccp_data_unitdata` struct (arms-length reference, real ITU-T Q.713
// Chapter 4 citation in Osmocom's own header). The exact POINTER ARITHMETIC (each pointer octet's
// value is the number of octets from the pointer octet's OWN position to the first octet -- the
// length octet -- of the field it points to) is standard, established SS7/Q.713 protocol
// convention, NOT itself cross-checked against primary ITU-T text (gated, see sccp_dictionary.hpp's
// own header) -- same disclosure class as this project's own Diameter Host-IP-Address byte layout
// (ADR-0059 Stage 2), a reconstruction from real, established protocol knowledge, not a literal
// spec-PDF citation, but also not invented from nothing (the self-relative-pointer shape is
// necessary for the format's own independent-parsing property to work at all).

namespace ss7_core {

struct SccpUdt {
    std::uint8_t protocol_class = 0; // dictionary::ProtocolClass::* (bits 1-4) + options (bits 5-8)
    SccpAddress called_party;
    SccpAddress calling_party;
    std::vector<std::uint8_t> data;
};

std::vector<std::uint8_t> encode_sccp_udt(const SccpUdt& udt);
std::optional<SccpUdt> decode_sccp_udt(const std::vector<std::uint8_t>& bytes);

} // namespace ss7_core
