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

// Message Type values, ASPSM class (3) -- RFC 4666 §3.1.2, cross-checked against the vendored
// Osmocom `m3ua.h` (`M3UA_ASPSM_UP`=1 etc. -- both sources agree exactly).
namespace AspsmMessageType {
constexpr std::uint8_t kAspUp = 1;
constexpr std::uint8_t kAspDown = 2;
constexpr std::uint8_t kHeartbeat = 3;
constexpr std::uint8_t kAspUpAck = 4;
constexpr std::uint8_t kAspDownAck = 5;
constexpr std::uint8_t kHeartbeatAck = 6;
} // namespace AspsmMessageType

// Message Type values, ASPTM class (4) -- RFC 4666 §3.1.2, cross-checked against the vendored
// Osmocom `m3ua.h` (`M3UA_ASPTM_ACTIVE`=1 etc. -- both sources agree exactly).
namespace AsptmMessageType {
constexpr std::uint8_t kAspActive = 1;
constexpr std::uint8_t kAspInactive = 2;
constexpr std::uint8_t kAspActiveAck = 3;
constexpr std::uint8_t kAspInactiveAck = 4;
} // namespace AsptmMessageType

// Parameter tags -- RFC 4666 §3.3.1 (DATA message) and §3.5/§3.7 (ASPSM/ASPTM messages), each
// cross-checked against the vendored Osmocom `m3ua.h` (`M3UA_IEI_*` -- both sources agree exactly).
namespace ParamTag {
constexpr std::uint16_t kInfoString = 0x0004;
constexpr std::uint16_t kNetworkAppearance = 0x0200;
constexpr std::uint16_t kRoutingContext = 0x0006;
constexpr std::uint16_t kTrafficModeType = 0x000B;
constexpr std::uint16_t kProtocolData = 0x0210;
constexpr std::uint16_t kCorrelationId = 0x0013;
constexpr std::uint16_t kAspIdentifier = 0x0011;
} // namespace ParamTag

// Traffic Mode Type real enumerated values -- RFC 4666 §3.7.1, cross-checked against the vendored
// Osmocom `m3ua.h` (`M3UA_TMOD_OVERRIDE`=1/`M3UA_TMOD_LOADSHARE`=2/`M3UA_TMOD_BCAST`=3 -- both
// sources agree exactly).
namespace TrafficModeType {
constexpr std::uint32_t kOverride = 1;
constexpr std::uint32_t kLoadshare = 2;
constexpr std::uint32_t kBroadcast = 3;
} // namespace TrafficModeType

// Real IANA-assigned SCTP registered port for M3UA -- RFC 4666 §1.4.8. Real IANA-assigned SCTP
// Payload Protocol Identifier for M3UA -- IANA's own SCTP Payload Protocol Identifiers registry
// (iana.org/assignments/sctp-parameters), citing RFC 4666.
constexpr std::uint16_t kSctpPort = 2905;
constexpr std::uint32_t kSctpPpid = 3;

// MTP Service Indicator values (real Q.704/RFC 4666 §3.4.5 facts, cross-checked against the
// vendored Osmocom `sigtran/protocol/mtp.h`, which itself cites "Chapter 15.17.4 of Q.704 +
// RFC4666 3.4.5") -- only SCCP is consumed by this codebase's own real use (Stage 5a scope).
namespace ServiceIndicator {
constexpr std::uint8_t kSccp = 3;
} // namespace ServiceIndicator

} // namespace ss7_core::dictionary
