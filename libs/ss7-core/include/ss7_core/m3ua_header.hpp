#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// M3UA (MTP3-User Adaptation Layer, RFC 4666) common message header codec -- P4.5/ADR-0059 Stage
// 5a. Real, primary IETF RFC text (freely published, no login/paywall -- unlike ITU-T Q.704/Q.713,
// see this file's own header comment in dictionary.hpp for the full real evidence-tier
// disclosure). M3UA is used here as this project's own real choice of "MTP3 equivalent" transport:
// this lab has no real E1/T1 SS7 links (same reasoning NGAP's own SCTP choice and Diameter's own
// TCP choice already used -- no real point-to-point telecom hardware exists in this environment),
// and M3UA (SCTP-based MTP3-User Adaptation) is the real, standard way SS7 signalling (including
// SCCP/TCAP/MAP) is carried over IP in real modern deployments.
//
// Field composition (Version 1 octet, Reserved 1 octet, Message Class 1 octet, Message Type 1
// octet, Message Length 4 octets -- 8 octets total) is RFC 4666 §3.1's own real ABNF, quoted
// directly from the primary RFC text (fetched via rfc-editor.org, not recalled from memory or a
// secondary source).

namespace ss7_core {

constexpr std::uint8_t kM3uaVersion = 1; // RFC 4666 §3.1: "currently value 1"

struct M3uaHeader {
    std::uint8_t message_class = 0;
    std::uint8_t message_type = 0;
};

// Encodes the 8-byte fixed header with Message Length filled in (header's own 8 bytes plus
// payload_length, matching RFC 4666 §3.1's own "Message Length... includes the Common Header"
// and "MUST include parameter padding" rules).
std::vector<std::uint8_t> encode_m3ua_header(const M3uaHeader& header,
                                             std::uint32_t payload_length);

// Decodes a header from the start of `bytes`. On success, advances `offset` past the 8-byte header
// and sets `payload_length` to the number of payload bytes that follow (the wire Message Length
// field minus the 8-byte header itself). Returns std::nullopt on a too-short buffer, an
// unrecognized version, or a Message Length field smaller than the fixed 8-byte header.
std::optional<M3uaHeader> decode_m3ua_header(const std::vector<std::uint8_t>& bytes,
                                             std::size_t& offset,
                                             std::uint32_t& payload_length);

} // namespace ss7_core
