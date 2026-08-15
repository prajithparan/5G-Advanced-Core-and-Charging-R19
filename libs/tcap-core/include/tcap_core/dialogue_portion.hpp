#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "tcap_core/ber.hpp"

// TCAP Dialogue Portion (ITU-T Q.773, real table citations below) -- P4.5/ADR-0059 Stage 5b
// kickoff. Real structure and OID values confirmed directly from RestComm jss7's own real
// `DialogPortionImpl`/`DialogRequestAPDUImpl` (simulators/reference/jss7/, arms-length reference
// only -- see docs/DECISIONS.md's own Stage 5b ADR update for the real AGPL-3.0 license-check
// evidence). jss7's own Javadoc on `DialogPortionImpl` cites the exact real Q.773 table numbers
// quoted in the comments below.
//
// Real, disclosed scope: the structured Dialogue Portion wraps either a real AARQ (dialogue
// request/establishment) or a real AARE (dialogue response) -- Table 33/34/Q.773, ITU-T X.227
// ACSE. ABRT-wrapped-in-DialoguePortion (real U-Abort, TCAP-user-level) is NOT implemented -- a
// real, disclosed, deferred gap, not attempted without further evidence. A real protocol-level
// abort (P-Abort-Cause, no DialoguePortion at all) is already fully supported by `message.hpp`'s
// own `TcAbort::p_abort_cause`.
//
// AARE's own real `Result`/`ResultSourceDiagnostic` structure (2026-08-15, extending Stage 5b's
// own disclosed gap) is confirmed directly from RestComm jss7's own real `DialogResponseAPDUImpl`/
// `ResultImpl`/`ResultSourceDiagnosticImpl` (simulators/reference/jss7/, fetched fresh at the same
// pinned commit, arms-length reference only -- see this project's own docs/DECISIONS.md for the
// real AGPL-3.0 license-check evidence, unchanged from Stage 5b's own finding). Real wire shape,
// confirmed from jss7's own real encode()/decode() logic, not just its interface constants: both
// `Result` ([CONTEXT 2] constructed) and each `ResultSourceDiagnostic` sub-choice ([CONTEXT 1] or
// [CONTEXT 2] constructed) wrap a nested real UNIVERSAL INTEGER TLV directly -- functionally
// EXPLICIT tagging on the wire, matched here byte-for-byte regardless of how the underlying X.227
// ASN.1 module text itself expresses it (not in hand this pass, only jss7's own real runtime
// behavior is).

namespace tcap_core {

// Real Table 37/Q.773 & Table 36/Q.773 dialogue-as-ID OIDs.
namespace DialogueAsId {
inline const std::vector<std::uint32_t> kStructured = {0, 0, 17, 773, 1, 1, 1};
inline const std::vector<std::uint32_t> kUnstructured = {0, 0, 17, 773, 1, 2, 1};
} // namespace DialogueAsId

// AARQ (dialogue request) -- real fields confirmed from `DialogRequestAPDUImpl`: optional
// ProtocolVersion [CONTEXT 0] (a real BIT STRING per X.227, modeled here as opaque raw content --
// this codec doesn't assign semantics to individual bits), mandatory ApplicationContextName
// [CONTEXT 1] (an OBJECT IDENTIFIER -- the real, spec-defined value that identifies WHICH MAP/CAP
// service this dialogue is for, e.g. a real TS 29.002 AC name -- not modeled by this codec, the
// caller supplies the real OID for whichever operation it's building), optional UserInformation
// [CONTEXT 30] (opaque -- real MAP-open-info/similar EXTERNAL-wrapped content, not decoded).
struct DialogueRequest {
    std::optional<std::vector<std::uint8_t>> protocol_version;
    std::vector<std::uint32_t> application_context_name;
    std::optional<std::vector<std::uint8_t>> user_information;
};

// Encodes a real, structured (Table 33/Q.773) DialoguePortion -- [APPLICATION 11] wrapping a real
// EXTERNAL (UNIVERSAL 8) containing {dialogue-as-id OID, [CONTEXT 0] Single-ASN.1-type wrapping the
// AARQ}. The returned bytes are ready to pass directly as `TcBegin::dialogue_portion` etc.
std::vector<std::uint8_t> encode_dialogue_portion_request(const DialogueRequest& req);

// Decodes a DialoguePortion TLV (as produced by `message.hpp`'s own decode functions, which
// re-encode the real [APPLICATION 11] TLV verbatim into the bytes this function consumes).
// Returns std::nullopt for an unstructured dialogue portion or any dialogue-as-id OID other than
// `DialogueAsId::kStructured`, or a dialogue PDU that isn't a real AARQ (a real AARE/ABRT there is
// a real, disclosed mismatch, not a malformed message -- use `decode_dialogue_portion_response`
// for a TC-Continue/TC-End's own real AARE instead).
std::optional<DialogueRequest>
decode_dialogue_portion_request(const std::vector<std::uint8_t>& bytes);

// Real ResultType ENUMERATED values -- confirmed from jss7's own real `ResultType` enum.
namespace ResultType {
constexpr std::int32_t kAccepted = 0;
constexpr std::int32_t kRejectedPermanent = 1;
} // namespace ResultType

// Real DialogServiceUserType ENUMERATED values -- confirmed from jss7's own real
// `DialogServiceUserType` enum.
namespace DialogServiceUserType {
constexpr std::int32_t kNull = 0;
constexpr std::int32_t kNoReasonGiven = 1;
constexpr std::int32_t kAcnNotSupported = 2;
} // namespace DialogServiceUserType

// Real DialogServiceProviderType ENUMERATED values -- confirmed from jss7's own real
// `DialogServiceProviderType` enum.
namespace DialogServiceProviderType {
constexpr std::int32_t kNull = 0;
constexpr std::int32_t kNoReasonGiven = 1;
constexpr std::int32_t kNoCommonDialogPortion = 2;
} // namespace DialogServiceProviderType

// ResultSourceDiagnostic ([CONTEXT 3] constructed) -- a real CHOICE between dialog-service-user
// ([CONTEXT 1], `DialogServiceUserType::*`) and dialog-service-provider ([CONTEXT 2],
// `DialogServiceProviderType::*`), confirmed from jss7's own real `ResultSourceDiagnosticImpl`.
struct ResultSourceDiagnostic {
    bool is_user_type =
        true; // true: `value` is DialogServiceUserType::*; false: DialogServiceProviderType::*
    std::int32_t value = DialogServiceUserType::kNull;
};

// AARE (dialogue response) -- real fields confirmed from `DialogResponseAPDUImpl`: optional
// ProtocolVersion [CONTEXT 0] (same real opaque-BIT-STRING treatment as AARQ's), mandatory
// ApplicationContextName [CONTEXT 1] (same OID encoding as AARQ's -- echoes back the AC the AARQ
// proposed, or a real alternative one, per real X.227 semantics not modeled further here),
// mandatory Result [CONTEXT 2] (`ResultType::*`), mandatory ResultSourceDiagnostic [CONTEXT 3],
// optional UserInformation [CONTEXT 30].
struct DialogueResponse {
    std::optional<std::vector<std::uint8_t>> protocol_version;
    std::vector<std::uint32_t> application_context_name;
    std::int32_t result = ResultType::kAccepted;
    ResultSourceDiagnostic diagnostic;
    std::optional<std::vector<std::uint8_t>> user_information;
};

// Encodes a real, structured DialoguePortion wrapping a real AARE ([APPLICATION 1], Table 34/
// Q.773). Same real [APPLICATION 11]-wrapping-EXTERNAL shape as
// `encode_dialogue_portion_request`'s own AARQ case -- see that function's own comment.
std::vector<std::uint8_t> encode_dialogue_portion_response(const DialogueResponse& res);

// Decodes a DialoguePortion TLV expected to carry a real AARE. Returns std::nullopt for an
// unstructured dialogue portion, an unrecognized dialogue-as-id OID, or a dialogue PDU that isn't
// a real AARE (use `decode_dialogue_portion_request` for a TC-Begin's own real AARQ instead).
std::optional<DialogueResponse>
decode_dialogue_portion_response(const std::vector<std::uint8_t>& bytes);

} // namespace tcap_core
