#pragma once

#include "pfcp_core/ie.hpp"

#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <vector>

// Encode/decode helpers for the small set of PFCP Information Elements this project's Stage 1
// (Heartbeat, Association Setup) needs -- TS 29.244 §8.2.1 (Cause), §8.2.25/§8.2.58 (UP/CP
// Function Features), §8.2.38 (Node ID), §8.2.65 (Recovery Time Stamp). Byte layouts read directly
// from the real 3GPP TS 29.244 V14.3.0 spec PDF -- see header.hpp's own comment for the
// version-gap disclosure.

namespace pfcp_core {

// TS 29.244 Table 8.2.1-1 -- only the values this project's implemented messages actually use.
enum class Cause : std::uint8_t {
    RequestAccepted = 1,
    RequestRejected = 64,
    MandatoryIeMissing = 66,
    NoEstablishedAssociation = 72,
};

std::vector<std::uint8_t> encode_cause(Cause cause);
std::optional<Cause> decode_cause(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.65: the first 4 octets of an RFC 5905 NTP 64-bit timestamp -- seconds since
// 1900-01-01T00:00:00Z, unsigned 32-bit, big-endian. `unix_time` is a normal Unix epoch (seconds
// since 1970-01-01); the 2208988800s offset between the two epochs is standard NTP/RFC 5905
// knowledge, not a 3GPP-specific fact.
std::vector<std::uint8_t> encode_recovery_time_stamp(std::time_t unix_time);
std::optional<std::time_t> decode_recovery_time_stamp(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.38: this project only ever sends/expects the IPv4 form (Node ID Type = 0), since
// every NF in this project already only speaks IPv4 (see e.g. libs/sbi-core's own IPv4-only
// scope) -- disclosed narrowing, not the full IPv4/IPv6/FQDN union the IE type supports.
std::vector<std::uint8_t> encode_node_id_ipv4(std::array<std::uint8_t, 4> ipv4);
std::optional<std::array<std::uint8_t, 4>> decode_node_id_ipv4(const std::vector<std::uint8_t>& value);

// TS 29.244 §8.2.25/§8.2.58: a Supported-Features bitmask. This project declares no optional
// features on either side (UP or CP) -- disclosed minimal-viable scope, not because any feature
// was evaluated and rejected. UP Function Features is 2 octets; CP Function Features is 1 octet
// (confirmed against the different fixed-octet figures in the real spec, 8.2.25-1 vs 8.2.58-1).
std::vector<std::uint8_t> encode_up_function_features_none();
std::vector<std::uint8_t> encode_cp_function_features_none();

} // namespace pfcp_core
