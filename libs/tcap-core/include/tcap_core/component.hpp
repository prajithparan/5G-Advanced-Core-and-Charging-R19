#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tcap_core/ber.hpp"

// TCAP component portion (ITU-T Q.773) -- P4.5/ADR-0059 Stage 5b kickoff. Every real MAP/CAP
// operation invocation/result/error rides inside one of these five component types. Real tag
// constants (context-specific, constructed) cited directly from the vendored RestComm jss7
// interfaces (simulators/reference/jss7/, arms-length reference only -- see this project's own
// docs/DECISIONS.md Stage 5b ADR update for the real AGPL-3.0 license-check evidence).
//
// Real, disclosed scope: the operation code (local or global form) and the invocation/result
// parameter are carried as OPAQUE bytes here -- TCAP itself doesn't decode them (Q.773's own real
// `Parameter ::= ANY DEFINED BY operationCode` semantics), and this project's own MAP/CAP-specific
// argument/result ASN.1 types are a further, later increment, not yet built.

namespace tcap_core {

// Operation-Code / Error-Code CHOICE -- Q.773 (jss7's own `OperationCode`/`ErrorCode` interfaces:
// local form = a plain INTEGER [UNIVERSAL 2], global form = an OBJECT IDENTIFIER [UNIVERSAL 6]).
struct OperationCode {
    std::optional<std::int32_t> local;
    std::optional<std::vector<std::uint32_t>> global;
};
using ErrorCode = OperationCode;

// Invoke component -- jss7 `Invoke._TAG`=0x01 (context, constructed).
struct Invoke {
    std::int32_t invoke_id = 0;
    std::optional<std::int32_t> linked_id; // jss7 `Invoke._TAG_LID`=0x00 (context, primitive)
    OperationCode operation_code;
    std::vector<std::uint8_t> parameter; // opaque -- see this file's own header
};

// ReturnResult (not last -- more results to follow in a further ReturnResult/ReturnResultLast) --
// jss7 `ReturnResult._TAG`=0x07 (context, constructed). Real Q.773 structure confirmed from
// `ReturnResultImpl.decode`: InvokeID (UNIVERSAL INTEGER), then an OPTIONAL UNIVERSAL SEQUENCE
// wrapping {OperationCode, Parameter} together (not two independent optional fields).
struct ReturnResult {
    std::int32_t invoke_id = 0;
    struct Result {
        OperationCode operation_code;
        std::vector<std::uint8_t> parameter;
    };
    std::optional<Result> result;
};

// ReturnResultLast -- jss7 `ReturnResultLast._TAG`=0x02 (context, constructed). Same real
// structure as ReturnResult, a distinct wire tag per Q.773 (not a flag on ReturnResult).
using ReturnResultLast = ReturnResult;

// ReturnError -- jss7 `ReturnError._TAG`=0x03 (context, constructed).
struct ReturnError {
    std::int32_t invoke_id = 0;
    ErrorCode error_code;
    std::vector<std::uint8_t> parameter;
};

// Reject -- jss7 `Reject._TAG`=0x04 (context, constructed). `invoke_id_present` distinguishes the
// real Q.773 CHOICE between a known InvokeID and the NULL "general problem, no InvokeID" case.
struct Reject {
    bool invoke_id_present = false;
    std::int32_t invoke_id = 0;
    // Real Q.773 Problem CHOICE (generalProblem/invokeProblem/returnResultProblem/
    // returnErrorProblem) -- only the raw (choice-tag, problem-value) pair is modeled, not yet the
    // full named enum for each of the four sub-choices (real, disclosed scope narrowing: no real
    // MAP/CAP work in this codebase yet actually needs to distinguish them).
    std::uint8_t problem_choice_tag = 0;
    std::int32_t problem_value = 0;
};

// Component tags -- Q.773, cited from jss7's own real interface constants.
namespace ComponentTag {
constexpr std::uint32_t kInvoke = 1;
constexpr std::uint32_t kReturnResultLast = 2;
constexpr std::uint32_t kReturnError = 3;
constexpr std::uint32_t kReject = 4;
constexpr std::uint32_t kReturnResult = 7;
} // namespace ComponentTag

// A decoded component -- exactly one of these is populated, per which real tag was seen.
struct Component {
    std::optional<Invoke> invoke;
    std::optional<ReturnResult> return_result;
    std::optional<ReturnResultLast> return_result_last;
    std::optional<ReturnError> return_error;
    std::optional<Reject> reject;
};

Tlv encode_invoke(const Invoke& invoke);
Tlv encode_return_result(const ReturnResult& rr, bool is_last);
Tlv encode_return_error(const ReturnError& re);
Tlv encode_reject(const Reject& rej);

// Dispatches on `tlv.tag_number` (must be tag_class==kContext, constructed) to decode into the
// matching real component type. Returns std::nullopt for an unrecognized tag or malformed content.
std::optional<Component> decode_component(const Tlv& tlv);

} // namespace tcap_core
