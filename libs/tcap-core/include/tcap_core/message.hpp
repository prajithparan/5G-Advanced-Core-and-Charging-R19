#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "tcap_core/ber.hpp"
#include "tcap_core/component.hpp"

// TCAP (ITU-T Q.773) message envelope -- P4.5/ADR-0059 Stage 5b kickoff. Real message tags and
// internal field structure confirmed directly from RestComm jss7's own real
// `TCBeginMessageImpl`/`TCContinueMessageImpl`/`TCEndMessageImpl`/`TCAbortMessageImpl`/
// `TCUniMessageImpl` encode/decode logic (simulators/reference/jss7/, arms-length reference only
// -- see this project's own docs/DECISIONS.md Stage 5b ADR update for the real AGPL-3.0
// license-check evidence).

namespace tcap_core {

namespace MessageTag {
constexpr std::uint32_t kUni = 1;
constexpr std::uint32_t kBegin = 2;
constexpr std::uint32_t kEnd = 4;
constexpr std::uint32_t kContinue = 5;
constexpr std::uint32_t kAbort = 7;
constexpr std::uint32_t kOriginatingTransactionId = 8;
constexpr std::uint32_t kDestinationTransactionId = 9;
constexpr std::uint32_t kPAbortCause = 10;
constexpr std::uint32_t kDialoguePortion = 11;
constexpr std::uint32_t kComponentPortion = 12;
} // namespace MessageTag

// `dialogue_portion`/`components` are pre-encoded opaque bytes (built by dialogue_portion.hpp's
// own encode functions and component.hpp's encode_invoke/encode_return_result/etc respectively) --
// this layer just frames them, it doesn't interpret their content.
struct TcBegin {
    std::vector<std::uint8_t> originating_transaction_id;
    std::optional<std::vector<std::uint8_t>> dialogue_portion;
    std::vector<Tlv> components;
};

struct TcContinue {
    std::vector<std::uint8_t> originating_transaction_id;
    std::vector<std::uint8_t> destination_transaction_id;
    std::optional<std::vector<std::uint8_t>> dialogue_portion;
    std::vector<Tlv> components;
};

struct TcEnd {
    std::vector<std::uint8_t> destination_transaction_id;
    std::optional<std::vector<std::uint8_t>> dialogue_portion;
    std::vector<Tlv> components;
};

// Real Q.773 CHOICE: an Abort carries EITHER a real P-Abort-Cause (protocol-level abort, this
// project's own transport/parsing layer detected a problem) OR a DialogPortion (a real
// U-Abort -- MAP/CAP's own ACSE ABRT-apdu, TCAP-user-initiated). `p_abort_cause`/
// `dialogue_portion` are mutually exclusive, matching the real spec's own CHOICE, not
// both-optional.
struct TcAbort {
    std::vector<std::uint8_t> destination_transaction_id;
    std::optional<std::int32_t> p_abort_cause;
    std::optional<std::vector<std::uint8_t>> dialogue_portion;
};

struct TcUni {
    std::optional<std::vector<std::uint8_t>> dialogue_portion;
    std::vector<Tlv> components;
};

std::vector<std::uint8_t> encode_tc_begin(const TcBegin& msg);
std::vector<std::uint8_t> encode_tc_continue(const TcContinue& msg);
std::vector<std::uint8_t> encode_tc_end(const TcEnd& msg);
std::vector<std::uint8_t> encode_tc_abort(const TcAbort& msg);
std::vector<std::uint8_t> encode_tc_uni(const TcUni& msg);

// Decodes the real top-level Application-class message tag from `bytes` (0x62/0x65/0x64/0x67/
// 0x61 -- Begin/Continue/End/Abort/Uni respectively) without yet decoding the message body, so
// callers can dispatch to the matching `decode_tc_*` function below.
std::optional<std::uint32_t> peek_tc_message_tag(const std::vector<std::uint8_t>& bytes);

std::optional<TcBegin> decode_tc_begin(const std::vector<std::uint8_t>& bytes);
std::optional<TcContinue> decode_tc_continue(const std::vector<std::uint8_t>& bytes);
std::optional<TcEnd> decode_tc_end(const std::vector<std::uint8_t>& bytes);
std::optional<TcAbort> decode_tc_abort(const std::vector<std::uint8_t>& bytes);
std::optional<TcUni> decode_tc_uni(const std::vector<std::uint8_t>& bytes);

} // namespace tcap_core
