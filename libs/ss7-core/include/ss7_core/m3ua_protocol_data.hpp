#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// M3UA Protocol Data parameter (RFC 4666 §3.3.1) -- P4.5/ADR-0059 Stage 5a. This is the real
// wire content of the mandatory `Protocol Data` TLV parameter (ParamTag::kProtocolData=0x0210) of
// a DATA message: Originating/Destination Point Code, Service Indicator, Network Indicator,
// Message Priority, Signalling Link Selection, and the actual MTP-User (e.g. SCCP) payload --
// quoted directly from RFC 4666's own real ASCII diagram and field descriptions in §3.3.1.

namespace ss7_core {

struct M3uaProtocolData {
    std::uint32_t opc = 0;                        // Originating Point Code
    std::uint32_t dpc = 0;                        // Destination Point Code
    std::uint8_t si = 0;                          // Service Indicator
    std::uint8_t ni = 0;                          // Network Indicator
    std::uint8_t mp = 0;                          // Message Priority
    std::uint8_t sls = 0;                         // Signalling Link Selection
    std::vector<std::uint8_t> user_protocol_data; // e.g. the real SCCP message bytes
};

std::vector<std::uint8_t> encode_m3ua_protocol_data(const M3uaProtocolData& data);
std::optional<M3uaProtocolData> decode_m3ua_protocol_data(const std::vector<std::uint8_t>& bytes);

} // namespace ss7_core
