#include "ngap_core/ngap_codec.hpp"

#include <cstdlib>
#include <cstring>

extern "C" {
#include <OPEN_TYPE.h>
#include <asn_application.h>
#include <constr_CHOICE.h>
#include <constr_SEQUENCE.h>
#include <constr_SEQUENCE_OF.h>
#include <constr_SET_OF.h>
#include <per_decoder.h>
#include <per_encoder.h>
}

namespace ngap {

namespace {

// --- asn1c 0.9.29 open-type presence repair (ADR-0268) ---------------------------------------
//
// asn1c's generated open-type selector -- e.g. select_UnsuccessfulOutcome_value_type in
// build/generated/ngap_gen/UnsuccessfulOutcome.c -- ends with:
//
//     result.presence_index = row + 1;
//
// `row` is the row in the ALL-procedures Information Object Set table
// (asn_IOS_NGAP_ELEMENTARY_PROCEDURES_1), NOT the index within the `value` CHOICE. Those differ
// whenever a CHOICE omits procedures the table lists, because it only holds the ones that actually
// have that outcome. So a decoded HandoverPreparationFailure, whose correct presence is 5, gets
// 8 -- UnsuccessfulOutcome__value_PR_InitialContextSetupFailure.
//
// Measured across the whole generated set (see ADR-0268): InitiatingMessage 0 of 76 wrong,
// SuccessfulOutcome 0 of 29, UnsuccessfulOutcome 13 of 15. Every procedure has an initiating
// message, so that CHOICE's order matches the table's row for row; failures are sparse, so they
// do not. The repair below is written for the general case rather than the 13, since nothing in
// the generator promises the other two stay aligned.
//
// The decoded DATA lands in the right place (the selector's type_descriptor is correct; only the
// index is wrong), so decoding works and every existing caller reads the right bytes. What breaks
// is the free: ASN_STRUCT_FREE walks the union as whatever type the bogus index names. In this
// project that is never the same shape, because the ASN.1 is deliberately mixed -- 23 messages
// were hand-edited to ConcreteProtocolIE-Container, whose IE `value` is an inline OCTET_STRING,
// while 100 keep the real parameterized ProtocolIE-Container, whose IE `value` is a CHOICE. The
// OCTET_STRING's `buf` is therefore never freed (a real leak, 4 bytes per IE), and its pointer
// bytes get read as a CHOICE presence tag. It does not crash only because that bogus tag exceeds
// elements_count and CHOICE_free bails out -- benign by luck, not by design.
//
// This is not test-only: nfs/amf/src/ngap_task.cpp and nfs/amf/src/ngap_handover.cpp call
// decode_pdu on every inbound NGAP PDU. It went unseen because only the test binary is
// ASan-instrumented (NFs are spawned as separate processes) and nothing in that binary decoded an
// NGAP PDU until ADR-0264's NgapTestGnb.
//
// The repair: re-run the selector (its type_descriptor IS correct), find that descriptor among
// the CHOICE's own members, and write that 1-based index. Deliberately NOT via
// CHOICE_variant_set_presence -- that frees the currently-selected member first, which is exactly
// the wrong-type free being fixed. The index is written directly, mirroring asn1c's own
// _set_present_idx (static in constr_CHOICE.c, so reimplemented here rather than reached into).

void set_present_index(void* structure_ptr, const asn_CHOICE_specifics_t* specs, unsigned index) {
    void* present_ptr = static_cast<char*>(structure_ptr) + specs->pres_offset;
    switch (specs->pres_size) {
        case sizeof(unsigned int):
            *static_cast<unsigned int*>(present_ptr) = index;
            break;
        case sizeof(unsigned short):
            *static_cast<unsigned short*>(present_ptr) = static_cast<unsigned short>(index);
            break;
        case sizeof(unsigned char):
            *static_cast<unsigned char*>(present_ptr) = static_cast<unsigned char>(index);
            break;
        default:
            break; // unknown width: leave asn1c's value rather than corrupt the struct
    }
}

unsigned get_present_index(const void* structure_ptr, const asn_CHOICE_specifics_t* specs) {
    const void* present_ptr = static_cast<const char*>(structure_ptr) + specs->pres_offset;
    switch (specs->pres_size) {
        case sizeof(unsigned int):
            return *static_cast<const unsigned int*>(present_ptr);
        case sizeof(unsigned short):
            return *static_cast<const unsigned short*>(present_ptr);
        case sizeof(unsigned char):
            return *static_cast<const unsigned char*>(present_ptr);
        default:
            return 0;
    }
}

// Resolves a member's address, honouring ATF_POINTER. Returns nullptr for an absent OPTIONAL.
void* member_address(void* sptr, const asn_TYPE_member_t& elm) {
    if ((elm.flags & ATF_POINTER) != 0) {
        return *reinterpret_cast<void**>(static_cast<char*>(sptr) + elm.memb_offset);
    }
    return static_cast<char*>(sptr) + elm.memb_offset;
}

// asn1c's open-type member descriptor (asn_DEF_value_N) is not a CHOICE: its op is
// asn_OP_OPEN_TYPE. That type is CHOICE-SHAPED, though -- OPEN_TYPE.h defines OPEN_TYPE_free as
// CHOICE_free, and its `specifics` really is an asn_CHOICE_specifics_t -- so both ops are read
// and written the same way here. Nothing else is: `specifics` names a different struct for every
// other type, and writing at its pres_offset would corrupt an unrelated field.
bool is_choice_shaped(const asn_TYPE_descriptor_t* td) {
    return td != nullptr && (td->op == &asn_OP_CHOICE || td->op == &asn_OP_OPEN_TYPE);
}

// Walks a decoded structure repairing every open-type presence tag it finds. Recursive because
// the mis-set index occurs at two levels: the message-outcome SEQUENCE's own `value`, and -- for
// the 100 messages still using the real parameterized ProtocolIE-Container -- each
// ProtocolIE-Field's `value`. Only SEQUENCE, SET OF/SEQUENCE OF and the two CHOICE-shaped ops are
// descended, which is the whole shape of an NGAP PDU; anything else is left alone. `depth` bounds
// the walk so a structure that somehow refers to itself cannot spin.
void repair_open_type_presence(const asn_TYPE_descriptor_t* td, void* sptr, int depth) {
    constexpr int kMaxDepth = 16;
    if (td == nullptr || sptr == nullptr || depth > kMaxDepth) {
        return;
    }

    if (td->op == &asn_OP_SET_OF || td->op == &asn_OP_SEQUENCE_OF) {
        const auto* list = reinterpret_cast<const asn_anonymous_sequence_*>(sptr);
        for (int i = 0; i < list->count; ++i) {
            repair_open_type_presence(td->elements[0].type, list->array[i], depth + 1);
        }
        return;
    }

    if (is_choice_shaped(td)) {
        const auto* specs = static_cast<const asn_CHOICE_specifics_t*>(td->specifics);
        if (specs == nullptr) {
            return;
        }
        const unsigned present = get_present_index(sptr, specs);
        if (present == 0 || present > td->elements_count) {
            return;
        }
        const asn_TYPE_member_t& elm = td->elements[present - 1];
        repair_open_type_presence(elm.type, member_address(sptr, elm), depth + 1);
        return;
    }

    if (td->op != &asn_OP_SEQUENCE) {
        return; // a leaf (INTEGER, OCTET STRING, ...) -- nothing to repair below it
    }

    for (size_t edx = 0; edx < td->elements_count; ++edx) {
        const asn_TYPE_member_t& elm = td->elements[edx];
        void* memb_ptr = member_address(sptr, elm);
        if (memb_ptr == nullptr) {
            continue;
        }

        // is_choice_shaped is required, not decorative: the write goes through an
        // asn_CHOICE_specifics_t, and ATF_OPEN_TYPE alone is asn1c metadata -- the same metadata
        // this whole function exists to distrust.
        if ((elm.flags & ATF_OPEN_TYPE) != 0 && elm.type_selector != nullptr &&
            is_choice_shaped(elm.type) && elm.type->specifics != nullptr) {
            const asn_type_selector_result_t selected = elm.type_selector(td, sptr);
            if (selected.type_descriptor != nullptr) {
                unsigned correct = 0;
                for (size_t i = 0; i < elm.type->elements_count; ++i) {
                    if (elm.type->elements[i].type == selected.type_descriptor) {
                        correct = static_cast<unsigned>(i) + 1;
                        break;
                    }
                }
                // correct == 0 means this procedure/IE has no member in the CHOICE at all --
                // leave asn1c's value alone rather than guess.
                if (correct != 0) {
                    set_present_index(
                        memb_ptr,
                        static_cast<const asn_CHOICE_specifics_t*>(elm.type->specifics),
                        correct);
                }
            }
        }

        repair_open_type_presence(elm.type, memb_ptr, depth + 1);
    }
}

std::vector<std::uint8_t> per_encode(const asn_TYPE_descriptor_s* type_descriptor,
                                     const void* value) {
    void* buffer = nullptr;
    // Aligned PER (X.691), not Unaligned -- TS 38.413 NGAP mandates Aligned PER, and real gNB/UE
    // peers (confirmed against UERANSIM's own encode.hpp) only speak that. See ADR-0031.
    const ssize_t encoded = aper_encode_to_new_buffer(type_descriptor, nullptr, value, &buffer);
    if (encoded < 0 || buffer == nullptr) {
        return {};
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(encoded));
    std::memcpy(out.data(), buffer, out.size());
    std::free(buffer);
    return out;
}

} // namespace

ConcreteProtocolIE_Field_t make_ie(long id,
                                   Criticality_t criticality,
                                   const asn_TYPE_descriptor_s* type_descriptor,
                                   const void* value) {
    ConcreteProtocolIE_Field_t ie{};
    ie.id = id;
    ie.criticality = criticality;

    const auto encoded = per_encode(type_descriptor, value);
    ie.value.buf = static_cast<std::uint8_t*>(std::malloc(encoded.size()));
    ie.value.size = encoded.size();
    if (!encoded.empty()) {
        std::memcpy(ie.value.buf, encoded.data(), encoded.size());
    }
    return ie;
}

void add_ie(ConcreteProtocolIE_Container_t& container, ConcreteProtocolIE_Field_t ie) {
    auto* heap_ie = static_cast<ConcreteProtocolIE_Field_t*>(std::malloc(sizeof(ie)));
    *heap_ie = ie;
    ASN_SEQUENCE_ADD(&container.list, heap_ie);
}

const ConcreteProtocolIE_Field_t* find_ie(const ConcreteProtocolIE_Container_t& container,
                                          long id) {
    for (int i = 0; i < container.list.count; ++i) {
        const ConcreteProtocolIE_Field_t* ie = container.list.array[i];
        if (ie != nullptr && ie->id == id) {
            return ie;
        }
    }
    return nullptr;
}

void* decode_ie_value(const asn_TYPE_descriptor_s* type_descriptor,
                      const ConcreteProtocolIE_Field_t& ie) {
    void* out = nullptr;
    const asn_dec_rval_t rv =
        aper_decode_complete(nullptr, type_descriptor, &out, ie.value.buf, ie.value.size);
    if (rv.code != RC_OK) {
        if (out != nullptr) {
            // Deliberately NOT repaired on the failure path: the selector picks a type from a
            // sibling field (the IE id / procedureCode), which on a partial decode may hold
            // garbage. Writing an in-range-but-wrong index there would turn asn1c's current
            // benign bail-out (index > elements_count, CHOICE_free gives up) into a real
            // type-confused free. The leak this fixes is on the success path only.
            ASN_STRUCT_FREE(*type_descriptor, out);
        }
        return nullptr;
    }
    // Every decode in this file gets the repair, not just decode_pdu's: an IE value can itself be
    // a structure carrying an open type, and the caller frees whatever comes back from here.
    repair_open_type_presence(type_descriptor, out, 0);
    return out;
}

std::vector<std::uint8_t> encode_pdu(const NGAP_PDU_t& pdu) {
    return per_encode(&asn_DEF_NGAP_PDU, &pdu);
}

NGAP_PDU_t* decode_pdu(const std::vector<std::uint8_t>& bytes) {
    void* out = nullptr;
    const asn_dec_rval_t rv =
        aper_decode_complete(nullptr, &asn_DEF_NGAP_PDU, &out, bytes.data(), bytes.size());
    if (rv.code != RC_OK) {
        if (out != nullptr) {
            // Not repaired on the failure path -- see decode_ie_value above for why.
            ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, out);
        }
        return nullptr;
    }
    // See repair_open_type_presence: without this the caller's own ASN_STRUCT_FREE walks the
    // decoded message as the wrong type, leaking each IE's value buffer.
    repair_open_type_presence(&asn_DEF_NGAP_PDU, out, 0);
    return static_cast<NGAP_PDU_t*>(out);
}

std::vector<std::uint8_t> encode_value(const asn_TYPE_descriptor_s* type_descriptor,
                                       const void* value) {
    return per_encode(type_descriptor, value);
}

void* decode_value(const asn_TYPE_descriptor_s* type_descriptor,
                   const std::vector<std::uint8_t>& bytes) {
    void* out = nullptr;
    const asn_dec_rval_t rv =
        aper_decode_complete(nullptr, type_descriptor, &out, bytes.data(), bytes.size());
    if (rv.code != RC_OK) {
        if (out != nullptr) {
            // Deliberately NOT repaired on the failure path: the selector picks a type from a
            // sibling field (the IE id / procedureCode), which on a partial decode may hold
            // garbage. Writing an in-range-but-wrong index there would turn asn1c's current
            // benign bail-out (index > elements_count, CHOICE_free gives up) into a real
            // type-confused free. The leak this fixes is on the success path only.
            ASN_STRUCT_FREE(*type_descriptor, out);
        }
        return nullptr;
    }
    repair_open_type_presence(type_descriptor, out, 0);
    return out;
}

} // namespace ngap
