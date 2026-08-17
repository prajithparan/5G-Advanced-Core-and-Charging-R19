#pragma once

#include <cstdint>
#include <optional>
#include <vector>

extern "C" {
#include <ConcreteProtocolIE-Container.h>
#include <ConcreteProtocolIE-Field.h>
#include <Criticality.h>
#include <NGAP-PDU.h>
}

// Encode/decode helpers for NGAP's ProtocolIE-Container pattern, working around a confirmed
// asn1c 0.9.29 limitation (see docs/DECISIONS.md ADR-0031): specs/NGAP/ngap-17.9.asn was patched
// to give the six message types this build handles a CONCRETE (non-parameterized)
// ConcreteProtocolIE-Container in place of the real spec's parameterized
// `ProtocolIE-Container {{XxxIEs}}`. Per X.691 clause 10.9, an ASN.1 open type's PER encoding is
// byte-for-byte an octet-string-wrapped blob -- these helpers PER-encode each IE's own value via
// its real, fully-resolved asn1c type descriptor into that OCTET STRING wrapper, so the wire
// bytes match what a correctly IOC-resolving compiler would have produced.

namespace ngap {

// Encodes `value` via `type_descriptor`'s own PER codec and wraps it as one IE tuple.
// `type_descriptor`/`value` must outlive nothing -- the encoded bytes are copied in.
ConcreteProtocolIE_Field_t make_ie(long id,
                                   Criticality_t criticality,
                                   const struct asn_TYPE_descriptor_s* type_descriptor,
                                   const void* value);

// Appends `ie` to `container` (transfers ownership of ie's heap-allocated OCTET_STRING buffer).
void add_ie(ConcreteProtocolIE_Container_t& container, ConcreteProtocolIE_Field_t ie);

// Finds the first IE with the given id, or nullptr if absent.
const ConcreteProtocolIE_Field_t* find_ie(const ConcreteProtocolIE_Container_t& container, long id);

// PER-decodes an IE's value via `type_descriptor`. Returns nullptr on decode failure. Caller owns
// the returned pointer (free via ASN_STRUCT_FREE(*type_descriptor, ptr)).
void* decode_ie_value(const struct asn_TYPE_descriptor_s* type_descriptor,
                      const ConcreteProtocolIE_Field_t& ie);

// Encodes a full top-level NGAP_PDU (InitiatingMessage/SuccessfulOutcome/UnsuccessfulOutcome
// wrapping an inner message) to PER bytes.
std::vector<std::uint8_t> encode_pdu(const NGAP_PDU_t& pdu);

// Decodes a full top-level NGAP_PDU from PER bytes. Returns nullptr on decode failure. Caller
// owns the returned pointer (free via ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, ptr)).
NGAP_PDU_t* decode_pdu(const std::vector<std::uint8_t>& bytes);

// Generic single-type Aligned PER encode/decode (gap-closure, docs/CAPABILITY_GAP_ANALYSIS.md
// task #100, ADR-0090) -- the same underlying codec make_ie/decode_ie_value already use
// internally, exposed directly for types that need to be embedded as a raw
// "OCTET STRING (CONTAINING SomeType)" transparent container rather than wrapped in an IE tuple
// (e.g. PathSwitchRequestTransfer/PathSwitchRequestAcknowledgeTransfer inside
// PDUSessionResourceToBeSwitchedDLItem/PDUSessionResourceSwitchedItem).
std::vector<std::uint8_t> encode_value(const struct asn_TYPE_descriptor_s* type_descriptor,
                                       const void* value);
void* decode_value(const struct asn_TYPE_descriptor_s* type_descriptor,
                   const std::vector<std::uint8_t>& bytes);

} // namespace ngap
