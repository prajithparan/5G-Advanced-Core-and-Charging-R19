#include "tap3_core/tap3_suppl_service.hpp"

namespace tap3_core {

namespace {

bool at_app_tag(const std::vector<Tlv>& parts, std::size_t idx, std::uint32_t tag_number) {
    return idx < parts.size() && parts[idx].tag_class == TagClass::kApplication &&
           parts[idx].tag_number == tag_number;
}

Tlv make_seq_tag(std::uint32_t tag_number, std::vector<std::uint8_t> body) {
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = tag_number;
    tlv.value = std::move(body);
    return tlv;
}

} // namespace

// --- SupplServiceUsed ---

Tlv encode_suppl_service_used(const SupplServiceUsed& v) {
    std::vector<std::uint8_t> body;
    if (v.supplServiceCode.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kSupplServiceCode, *v.supplServiceCode));
    }
    if (v.supplServiceActionCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    SupplServiceTag::kSupplServiceActionCode,
                                    *v.supplServiceActionCode));
    }
    if (v.ssParameters.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, SupplServiceTag::kSsParameters, *v.ssParameters));
    }
    if (v.chargingTimeStamp.has_value()) {
        encode_tlv(body, encode_date_time(MoCallTag::kChargingTimeStamp, *v.chargingTimeStamp));
    }
    if (v.chargeInformation.has_value()) {
        encode_tlv(body, encode_charge_information(*v.chargeInformation));
    }
    if (!v.basicServiceCodeList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& bsc : v.basicServiceCodeList) {
            encode_tlv(list_body, encode_basic_service_code(bsc));
        }
        encode_tlv(body,
                   make_seq_tag(SupplServiceTag::kBasicServiceCodeList, std::move(list_body)));
    }
    return make_seq_tag(SupplServiceTag::kSupplServiceUsed, std::move(body));
}

std::optional<SupplServiceUsed> decode_suppl_service_used(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != SupplServiceTag::kSupplServiceUsed) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    SupplServiceUsed out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kSupplServiceCode)) {
        out.supplServiceCode =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kSupplServiceCode);
        ++idx;
    }
    if (at_app_tag(p, idx, SupplServiceTag::kSupplServiceActionCode)) {
        out.supplServiceActionCode = decode_int_field(
            p[idx], TagClass::kApplication, SupplServiceTag::kSupplServiceActionCode);
        ++idx;
    }
    if (at_app_tag(p, idx, SupplServiceTag::kSsParameters)) {
        out.ssParameters =
            decode_string_field(p[idx], TagClass::kApplication, SupplServiceTag::kSsParameters);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kChargingTimeStamp)) {
        out.chargingTimeStamp = decode_date_time(p[idx], MoCallTag::kChargingTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeInformation)) {
        out.chargeInformation = decode_charge_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, SupplServiceTag::kBasicServiceCodeList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto bsc = decode_basic_service_code(item); bsc.has_value()) {
                    out.basicServiceCodeList.push_back(*bsc);
                }
            }
        }
        ++idx;
    }
    return out;
}

// --- SupplServiceEvent ---

Tlv encode_suppl_service_event(const SupplServiceEvent& v) {
    std::vector<std::uint8_t> body;
    if (v.chargeableSubscriber.has_value()) {
        encode_tlv(body, encode_chargeable_subscriber(*v.chargeableSubscriber));
    }
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.locationInformation.has_value()) {
        encode_tlv(body, encode_location_information(*v.locationInformation));
    }
    if (v.equipmentIdentifier.has_value()) {
        encode_tlv(body, encode_imei_or_esn(*v.equipmentIdentifier));
    }
    if (v.supplServiceUsed.has_value()) {
        encode_tlv(body, encode_suppl_service_used(*v.supplServiceUsed));
    }
    if (!v.operatorSpecInformation.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& s : v.operatorSpecInformation) {
            encode_tlv(list_body,
                       encode_string_field(
                           TagClass::kApplication, MoCallTag::kOperatorSpecInformation, s));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kOperatorSpecInfoList, std::move(list_body)));
    }
    return make_seq_tag(Tag::kSupplServiceEvent, std::move(body));
}

std::optional<SupplServiceEvent> decode_suppl_service_event(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kSupplServiceEvent) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    SupplServiceEvent out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kChargeableSubscriber)) {
        out.chargeableSubscriber = decode_chargeable_subscriber(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kLocationInformation)) {
        out.locationInformation = decode_location_information(p[idx]);
        ++idx;
    }
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == MoCallTag::kImeiOrEsn) {
        out.equipmentIdentifier = decode_imei_or_esn(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, SupplServiceTag::kSupplServiceUsed)) {
        out.supplServiceUsed = decode_suppl_service_used(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kOperatorSpecInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto s = decode_string_field(
                        item, TagClass::kApplication, MoCallTag::kOperatorSpecInformation);
                    s.has_value()) {
                    out.operatorSpecInformation.push_back(*s);
                }
            }
        }
        ++idx;
    }
    return out;
}

} // namespace tap3_core
