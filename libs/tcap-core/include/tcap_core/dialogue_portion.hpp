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
// Real, disclosed scope: only the structured Dialogue Portion wrapping a real AARQ (dialogue
// request/establishment -- Table 33/Q.773, ITU-T X.227 ACSE) is implemented. AARE (dialogue
// response) and ABRT-wrapped-in-DialoguePortion (real U-Abort, TCAP-user-level) are NOT
// implemented this stage -- AARE's own real `Result`/`ResultSourceDiagnostic` sub-fields need
// further real evidence this pass didn't fully gather, disclosed rather than guessed. A real
// protocol-level abort (P-Abort-Cause, no DialoguePortion at all) is already fully supported by
// `message.hpp`'s own `TcAbort::p_abort_cause`.

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
// `DialogueAsId::kStructured`, or a dialogue PDU that isn't a real AARQ (real, disclosed scope --
// see this file's own header for why AARE/ABRT aren't implemented yet).
std::optional<DialogueRequest>
decode_dialogue_portion_request(const std::vector<std::uint8_t>& bytes);

} // namespace tcap_core
