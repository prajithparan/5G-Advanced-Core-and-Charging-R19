#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ss7_core/m3ua_header.hpp"

// M3UA ASP State Maintenance (ASPSM) and ASP Traffic Maintenance (ASPTM) messages -- RFC 4666
// §3.5/§3.7 -- P4.5/ADR-0059 Stage 5 continuation. These are the real capability-exchange/
// activation handshake a real M3UA Application Server Process (ASP, e.g. this project's own future
// MAP/CAP-speaking component) and Signalling Gateway Process (SGP) run before any real DATA
// message (m3ua_protocol_data.hpp) can flow -- the same real role Diameter's own CER/CEA plays
// before CCR/CCA (ADR-0059 Stage 2), just for a different transport layer.
//
// Real, disclosed scope: this is a pure message codec, same "wire-codec-first" pattern as the rest
// of `libs/ss7-core`/`libs/tcap-core` -- no live SCTP listener/ASP state machine exists yet (see
// `sctp_socket.hpp`'s own header for the real, separate reason: which NF, if any, should own a
// live M3UA/SCTP listener is an architectural decision this codec doesn't make on its own).

namespace ss7_core {

// ASP Up / ASP Up Ack / ASP Down / ASP Down Ack all share the real same optional-parameter shape
// (RFC 4666 §3.5.1-§3.5.4): an optional ASP Identifier and an optional INFO String.
struct AspStateMessage {
    std::optional<std::uint32_t> asp_identifier;
    std::optional<std::string> info_string; // opaque informational text, real spec allows any
};

// ASP Active / ASP Active Ack / ASP Inactive / ASP Inactive Ack (RFC 4666 §3.7.1/§3.7.2 for
// Active; Inactive mirrors the same real shape minus the mandatory Traffic Mode Type -- RFC 4666's
// own real text doesn't mark it mandatory there, only for Active/Active Ack).
struct AspTrafficMessage {
    std::optional<std::uint32_t> traffic_mode_type; // dictionary::TrafficModeType::* -- mandatory
                                                    // on Active/Active-Ack, absent on Inactive/
                                                    // Inactive-Ack
    std::optional<std::vector<std::uint32_t>> routing_context; // real: n x 32-bit values
    std::optional<std::string> info_string;
};

std::vector<std::uint8_t> encode_asp_state_message(std::uint8_t message_type,
                                                   const AspStateMessage& msg);
std::optional<AspStateMessage> decode_asp_state_message(std::uint8_t expected_message_type,
                                                        const std::vector<std::uint8_t>& bytes);

std::vector<std::uint8_t> encode_asp_traffic_message(std::uint8_t message_type,
                                                     const AspTrafficMessage& msg);
std::optional<AspTrafficMessage> decode_asp_traffic_message(std::uint8_t expected_message_type,
                                                            const std::vector<std::uint8_t>& bytes);

} // namespace ss7_core
