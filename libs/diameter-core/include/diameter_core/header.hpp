#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Diameter (RFC 6733) message header codec -- P4.5/ADR-0059 Stage 1. Diameter has no OpenAPI YAML
// (it's a binary TCP/SCTP protocol, not SBI/HTTP2), so like pfcp_core's header codec, this is
// hand-rolled rather than tools/sbi-codegen-generated -- same class of exception, same reason (no
// machine-readable schema exists for this protocol family).
//
// Field composition/sizes (Version 1 octet, Message Length 3 octets, Command Flags 1 octet,
// Command-Code 3 octets, Application-ID 4 octets, Hop-by-Hop Identifier 4 octets, End-to-End
// Identifier 4 octets -- 20 octets total) are cross-checked against freeDiameter's own real,
// vendored `struct msg_hdr` (simulators/reference/freeDiameter/include/libfdproto.h:2210-2218,
// commit e48fd4f8afc48f5e839558a90ef5a67165e94fad) for field names/order/widths. Disclosed,
// same-class caveat as ADR-0027's KAUSF/KAMF FC-value treatment: RFC 6733's own text itself is not
// vendored in this repo, so the exact wire bit-packing below is established, standard Diameter
// protocol knowledge cross-checked against real vendored evidence of field composition, not a
// literal spec-PDF citation the way pfcp_core's header (read from a vendored TS 29.244 PDF) is.
//
// Command flags (CMD_FLAG_REQUEST=0x80, CMD_FLAG_PROXIABLE=0x40, CMD_FLAG_ERROR=0x20,
// CMD_FLAG_RETRANSMIT=0x10) and command codes (CER/CEA=257, DWR/DWA=280, DPR/DPA=282, CCR/CCA=272)
// are real values read directly from the vendored dictionary source, not invented -- see
// dictionary.hpp for the full citation of each constant.

namespace diameter_core {

constexpr std::uint8_t kDiameterVersion = 1;
constexpr std::uint16_t kDiameterTcpPort = 3868; // IANA-assigned, RFC 6733 §2.1

namespace CommandFlag {
constexpr std::uint8_t kRequest = 0x80;
constexpr std::uint8_t kProxiable = 0x40;
constexpr std::uint8_t kError = 0x20;
constexpr std::uint8_t kRetransmit = 0x10;
} // namespace CommandFlag

struct Header {
    std::uint8_t flags = 0;         // CommandFlag::* bits
    std::uint32_t command_code = 0; // 24-bit on the wire, stored in the low 24 bits
    std::uint32_t application_id = 0;
    std::uint32_t hop_by_hop_id = 0;
    std::uint32_t end_to_end_id = 0;
};

// Encodes the 20-byte fixed header with Message Length filled in (header's own 20 bytes plus
// avps_length, matching RFC 6733's "Message Length... includes the header fields and the padded
// AVPs" rule).
std::vector<std::uint8_t> encode_header(const Header& header, std::uint32_t avps_length);

// Decodes a header from the start of `bytes`. On success, advances `offset` past the 20-byte
// header and sets `avps_length` to the number of AVP-region bytes that follow (the wire Message
// Length field minus the 20-byte header itself). Returns std::nullopt on a too-short buffer, an
// unrecognized version, or a Message Length field smaller than the fixed 20-byte header.
std::optional<Header> decode_header(const std::vector<std::uint8_t>& bytes,
                                    std::size_t& offset,
                                    std::uint32_t& avps_length);

} // namespace diameter_core
