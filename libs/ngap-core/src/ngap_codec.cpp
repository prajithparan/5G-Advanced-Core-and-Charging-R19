#include "ngap_core/ngap_codec.hpp"

#include <cstdlib>
#include <cstring>

extern "C" {
#include <asn_application.h>
#include <per_decoder.h>
#include <per_encoder.h>
}

namespace ngap {

namespace {

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
            ASN_STRUCT_FREE(*type_descriptor, out);
        }
        return nullptr;
    }
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
            ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, out);
        }
        return nullptr;
    }
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
            ASN_STRUCT_FREE(*type_descriptor, out);
        }
        return nullptr;
    }
    return out;
}

} // namespace ngap
