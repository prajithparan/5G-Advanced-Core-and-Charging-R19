#include "tap3_core/tap3_charging.hpp"

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

// --- ChargeDetail ---

Tlv encode_charge_detail(const ChargeDetail& v) {
    std::vector<std::uint8_t> body;
    if (v.chargeType.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, Tag::kChargeType, *v.chargeType));
    }
    if (v.charge.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, ChargingTag::kCharge, *v.charge));
    }
    if (v.chargeableUnits.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ChargingTag::kChargeableUnits, *v.chargeableUnits));
    }
    if (v.chargedUnits.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, ChargingTag::kChargedUnits, *v.chargedUnits));
    }
    if (v.chargeDetailTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time(ChargingTag::kChargeDetailTimeStamp, *v.chargeDetailTimeStamp));
    }
    return make_seq_tag(ChargingTag::kChargeDetail, std::move(body));
}

std::optional<ChargeDetail> decode_charge_detail(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ChargingTag::kChargeDetail) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ChargeDetail out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, Tag::kChargeType)) {
        out.chargeType = decode_string_field(p[idx], TagClass::kApplication, Tag::kChargeType);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kCharge)) {
        out.charge = decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kCharge);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeableUnits)) {
        out.chargeableUnits =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kChargeableUnits);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargedUnits)) {
        out.chargedUnits =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kChargedUnits);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeDetailTimeStamp)) {
        out.chargeDetailTimeStamp = decode_date_time(p[idx], ChargingTag::kChargeDetailTimeStamp);
        ++idx;
    }
    return out;
}

// --- TaxInformation ---

Tlv encode_tax_information(const TaxInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.taxCode.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, Tag::kTaxCode, *v.taxCode));
    }
    if (v.taxValue.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, ChargingTag::kTaxValue, *v.taxValue));
    }
    if (v.taxableAmount.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ChargingTag::kTaxableAmount, *v.taxableAmount));
    }
    return make_seq_tag(ChargingTag::kTaxInformation, std::move(body));
}

std::optional<TaxInformation> decode_tax_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ChargingTag::kTaxInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    TaxInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, Tag::kTaxCode)) {
        out.taxCode = decode_int_field(p[idx], TagClass::kApplication, Tag::kTaxCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kTaxValue)) {
        out.taxValue = decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kTaxValue);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kTaxableAmount)) {
        out.taxableAmount =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kTaxableAmount);
        ++idx;
    }
    return out;
}

// --- DiscountInformation ---

Tlv encode_discount_information(const DiscountInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.discountCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kDiscountCode, *v.discountCode));
    }
    if (v.discount.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, ChargingTag::kDiscount, *v.discount));
    }
    if (v.discountableAmount.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ChargingTag::kDiscountableAmount,
                                    *v.discountableAmount));
    }
    return make_seq_tag(ChargingTag::kDiscountInformation, std::move(body));
}

std::optional<DiscountInformation> decode_discount_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ChargingTag::kDiscountInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    DiscountInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, Tag::kDiscountCode)) {
        out.discountCode = decode_int_field(p[idx], TagClass::kApplication, Tag::kDiscountCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kDiscount)) {
        out.discount = decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kDiscount);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kDiscountableAmount)) {
        out.discountableAmount =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kDiscountableAmount);
        ++idx;
    }
    return out;
}

// --- CallTypeGroup ---

Tlv encode_call_type_group(const CallTypeGroup& v) {
    std::vector<std::uint8_t> body;
    if (v.callTypeLevel1.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ChargingTag::kCallTypeLevel1, *v.callTypeLevel1));
    }
    if (v.callTypeLevel2.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ChargingTag::kCallTypeLevel2, *v.callTypeLevel2));
    }
    if (v.callTypeLevel3.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ChargingTag::kCallTypeLevel3, *v.callTypeLevel3));
    }
    return make_seq_tag(ChargingTag::kCallTypeGroup, std::move(body));
}

std::optional<CallTypeGroup> decode_call_type_group(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ChargingTag::kCallTypeGroup) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    CallTypeGroup out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ChargingTag::kCallTypeLevel1)) {
        out.callTypeLevel1 =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kCallTypeLevel1);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kCallTypeLevel2)) {
        out.callTypeLevel2 =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kCallTypeLevel2);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kCallTypeLevel3)) {
        out.callTypeLevel3 =
            decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kCallTypeLevel3);
        ++idx;
    }
    return out;
}

// --- ChargeInformation / ChargeInformationList ---

Tlv encode_charge_information(const ChargeInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.chargedItem.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, ChargingTag::kChargedItem, *v.chargedItem));
    }
    if (v.exchangeRateCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, Tag::kExchangeRateCode, *v.exchangeRateCode));
    }
    if (v.callTypeGroup.has_value()) {
        encode_tlv(body, encode_call_type_group(*v.callTypeGroup));
    }
    if (!v.chargeDetailList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& cd : v.chargeDetailList) {
            encode_tlv(list_body, encode_charge_detail(cd));
        }
        encode_tlv(body, make_seq_tag(ChargingTag::kChargeDetailList, std::move(list_body)));
    }
    if (!v.taxInformation.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& ti : v.taxInformation) {
            encode_tlv(list_body, encode_tax_information(ti));
        }
        encode_tlv(body, make_seq_tag(ChargingTag::kTaxInformationList, std::move(list_body)));
    }
    if (v.discountInformation.has_value()) {
        encode_tlv(body, encode_discount_information(*v.discountInformation));
    }
    return make_seq_tag(ChargingTag::kChargeInformation, std::move(body));
}

std::optional<ChargeInformation> decode_charge_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ChargingTag::kChargeInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ChargeInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ChargingTag::kChargedItem)) {
        out.chargedItem =
            decode_string_field(p[idx], TagClass::kApplication, ChargingTag::kChargedItem);
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kExchangeRateCode)) {
        out.exchangeRateCode =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kExchangeRateCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kCallTypeGroup)) {
        out.callTypeGroup = decode_call_type_group(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeDetailList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto cd = decode_charge_detail(item); cd.has_value()) {
                    out.chargeDetailList.push_back(*cd);
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kTaxInformationList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto ti = decode_tax_information(item); ti.has_value()) {
                    out.taxInformation.push_back(*ti);
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kDiscountInformation)) {
        out.discountInformation = decode_discount_information(p[idx]);
        ++idx;
    }
    return out;
}

Tlv encode_charge_information_list(const std::vector<ChargeInformation>& items) {
    std::vector<std::uint8_t> body;
    for (const auto& item : items) {
        encode_tlv(body, encode_charge_information(item));
    }
    return make_seq_tag(ChargingTag::kChargeInformationList, std::move(body));
}

std::optional<std::vector<ChargeInformation>> decode_charge_information_list(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ChargingTag::kChargeInformationList) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    std::vector<ChargeInformation> out;
    for (const auto& item : *parts) {
        if (auto ci = decode_charge_information(item); ci.has_value()) {
            out.push_back(*ci);
        }
    }
    return out;
}

} // namespace tap3_core
