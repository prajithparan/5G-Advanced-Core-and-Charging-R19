#include "tcap_core/dialogue_portion.hpp"

#include "tcap_core/message.hpp"

namespace tcap_core {

namespace {

constexpr std::uint32_t kAarqTag = 0; // jss7 DialogAPDU._TAG_REQUEST
constexpr std::uint32_t kProtocolVersionTag = 0;
constexpr std::uint32_t kApplicationContextNameTag = 1;
constexpr std::uint32_t kUserInformationTag = 30;
constexpr std::uint32_t kSingleAsn1TypeTag = 0; // real Q.773 Table 33: [CONTEXT 0] constructed

} // namespace

std::vector<std::uint8_t> encode_dialogue_portion_request(const DialogueRequest& req) {
    std::vector<std::uint8_t> aarq_body;

    if (req.protocol_version.has_value()) {
        Tlv pv;
        pv.tag_class = TagClass::kContext;
        pv.constructed = false;
        pv.tag_number = kProtocolVersionTag;
        pv.value = *req.protocol_version;
        encode_tlv(aarq_body, pv);
    }

    {
        Tlv oid_tlv;
        oid_tlv.tag_class = TagClass::kUniversal;
        oid_tlv.constructed = false;
        oid_tlv.tag_number = UniversalTag::kObjectIdentifier;
        oid_tlv.value = encode_oid(req.application_context_name);

        std::vector<std::uint8_t> acn_body;
        encode_tlv(acn_body, oid_tlv);

        Tlv acn;
        acn.tag_class = TagClass::kContext;
        acn.constructed = true;
        acn.tag_number = kApplicationContextNameTag;
        acn.value = std::move(acn_body);
        encode_tlv(aarq_body, acn);
    }

    if (req.user_information.has_value()) {
        Tlv ui;
        ui.tag_class = TagClass::kContext;
        ui.constructed = true;
        ui.tag_number = kUserInformationTag;
        ui.value = *req.user_information;
        encode_tlv(aarq_body, ui);
    }

    Tlv aarq;
    aarq.tag_class = TagClass::kApplication;
    aarq.constructed = true;
    aarq.tag_number = kAarqTag;
    aarq.value = std::move(aarq_body);

    std::vector<std::uint8_t> single_asn1_type_body;
    encode_tlv(single_asn1_type_body, aarq);
    Tlv single_asn1_type;
    single_asn1_type.tag_class = TagClass::kContext;
    single_asn1_type.constructed = true;
    single_asn1_type.tag_number = kSingleAsn1TypeTag;
    single_asn1_type.value = std::move(single_asn1_type_body);

    Tlv oid_tlv;
    oid_tlv.tag_class = TagClass::kUniversal;
    oid_tlv.constructed = false;
    oid_tlv.tag_number = UniversalTag::kObjectIdentifier;
    oid_tlv.value = encode_oid(DialogueAsId::kStructured);

    std::vector<std::uint8_t> external_body;
    encode_tlv(external_body, oid_tlv);
    encode_tlv(external_body, single_asn1_type);

    Tlv external_tlv;
    external_tlv.tag_class = TagClass::kUniversal;
    external_tlv.constructed = true;
    external_tlv.tag_number = UniversalTag::kExternal;
    external_tlv.value = std::move(external_body);

    std::vector<std::uint8_t> dp_body;
    encode_tlv(dp_body, external_tlv);

    Tlv dp;
    dp.tag_class = TagClass::kApplication;
    dp.constructed = true;
    dp.tag_number = MessageTag::kDialoguePortion;
    dp.value = std::move(dp_body);

    std::vector<std::uint8_t> out;
    encode_tlv(out, dp);
    return out;
}

std::optional<DialogueRequest>
decode_dialogue_portion_request(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    const auto dp = decode_tlv(bytes, offset);
    if (!dp.has_value() || dp->tag_class != TagClass::kApplication ||
        dp->tag_number != MessageTag::kDialoguePortion) {
        return std::nullopt;
    }

    const auto dp_parts = decode_tlvs(dp->value);
    if (!dp_parts.has_value() || dp_parts->empty()) {
        return std::nullopt;
    }
    const auto& external = (*dp_parts)[0];
    if (external.tag_class != TagClass::kUniversal ||
        external.tag_number != UniversalTag::kExternal) {
        return std::nullopt;
    }

    const auto external_parts = decode_tlvs(external.value);
    if (!external_parts.has_value() || external_parts->size() < 2) {
        return std::nullopt;
    }
    const auto& oid_tlv = (*external_parts)[0];
    if (oid_tlv.tag_class != TagClass::kUniversal ||
        oid_tlv.tag_number != UniversalTag::kObjectIdentifier) {
        return std::nullopt;
    }
    const auto oid = decode_oid(oid_tlv.value);
    if (!oid.has_value() || *oid != DialogueAsId::kStructured) {
        return std::nullopt; // unstructured or unrecognized -- real, disclosed scope narrowing
    }

    const auto& single_asn1_type = (*external_parts)[1];
    if (single_asn1_type.tag_class != TagClass::kContext ||
        single_asn1_type.tag_number != kSingleAsn1TypeTag) {
        return std::nullopt;
    }

    std::size_t aarq_offset = 0;
    const auto aarq = decode_tlv(single_asn1_type.value, aarq_offset);
    if (!aarq.has_value() || aarq->tag_class != TagClass::kApplication ||
        aarq->tag_number != kAarqTag) {
        return std::nullopt; // real AARE/ABRT -- not implemented this stage
    }

    const auto aarq_parts = decode_tlvs(aarq->value);
    if (!aarq_parts.has_value()) {
        return std::nullopt;
    }

    DialogueRequest req;
    std::size_t idx = 0;
    const auto& parts = *aarq_parts;
    if (idx < parts.size() && parts[idx].tag_class == TagClass::kContext &&
        parts[idx].tag_number == kProtocolVersionTag) {
        req.protocol_version = parts[idx].value;
        ++idx;
    }

    if (idx >= parts.size() || parts[idx].tag_class != TagClass::kContext ||
        parts[idx].tag_number != kApplicationContextNameTag) {
        return std::nullopt;
    }
    const auto acn_parts = decode_tlvs(parts[idx].value);
    if (!acn_parts.has_value() || acn_parts->empty()) {
        return std::nullopt;
    }
    const auto acn_oid = decode_oid((*acn_parts)[0].value);
    if (!acn_oid.has_value()) {
        return std::nullopt;
    }
    req.application_context_name = *acn_oid;
    ++idx;

    if (idx < parts.size() && parts[idx].tag_class == TagClass::kContext &&
        parts[idx].tag_number == kUserInformationTag) {
        req.user_information = parts[idx].value;
        ++idx;
    }

    return req;
}

} // namespace tcap_core
