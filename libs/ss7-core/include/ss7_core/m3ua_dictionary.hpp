#pragma once

#include <cstdint>

// Real M3UA (RFC 4666) message class/type/parameter-tag constants -- P4.5/ADR-0059 Stage 5a.
// Every constant below is read directly from RFC 4666's own primary text (fetched from
// rfc-editor.org, §3.1.2 for message class/type, §3.3.1 for the Payload Data message's own
// parameter tags), never invented, and cross-checked against the real, vendored Osmocom
// `sigtran/protocol/m3ua.h` (simulators/reference/osmocom/, arms-length reference only -- see
// docs/DECISIONS.md's own Stage 5 ADR update for the real GPL-2+ license-check evidence).

namespace ss7_core::dictionary {

// Message Class values -- RFC 4666 §3.1.2.
namespace MessageClass {
constexpr std::uint8_t kMgmt = 0;
constexpr std::uint8_t kTransfer = 1;
constexpr std::uint8_t kSsnm = 2;  // SS7 Signalling Network Management
constexpr std::uint8_t kAspsm = 3; // ASP State Maintenance
constexpr std::uint8_t kAsptm = 4; // ASP Traffic Maintenance
constexpr std::uint8_t kRkm = 9;   // Routing Key Management
} // namespace MessageClass

// Message Type values, Transfer class (1) -- RFC 4666 §3.1.2.
namespace TransferMessageType {
constexpr std::uint8_t kData = 1; // Payload Data (DATA)
} // namespace TransferMessageType

// Parameter tags used by the DATA message -- RFC 4666 §3.3.1.
namespace ParamTag {
constexpr std::uint16_t kNetworkAppearance = 0x0200;
constexpr std::uint16_t kRoutingContext = 0x0006;
constexpr std::uint16_t kProtocolData = 0x0210;
constexpr std::uint16_t kCorrelationId = 0x0013;
} // namespace ParamTag

// MTP Service Indicator values (real Q.704/RFC 4666 §3.4.5 facts, cross-checked against the
// vendored Osmocom `sigtran/protocol/mtp.h`, which itself cites "Chapter 15.17.4 of Q.704 +
// RFC4666 3.4.5") -- only SCCP is consumed by this codebase's own real use (Stage 5a scope).
namespace ServiceIndicator {
constexpr std::uint8_t kSccp = 3;
} // namespace ServiceIndicator

} // namespace ss7_core::dictionary
