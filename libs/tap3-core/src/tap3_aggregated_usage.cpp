#include "tap3_core/tap3_aggregated_usage.hpp"

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

// --- AURTaxInformation ---

Tlv encode_aur_tax_information(const AURTaxInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.taxCode.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, Tag::kTaxCode, *v.taxCode));
    }
    if (v.aurTaxValue.has_value()) {
        encode_tlv(
            body, encode_int64_field(TagClass::kApplication, AurTag::kAURTaxValue, *v.aurTaxValue));
    }
    if (v.aurTaxableAmount.has_value()) {
        encode_tlv(body,
                   encode_int64_field(
                       TagClass::kApplication, AurTag::kAURTaxableAmount, *v.aurTaxableAmount));
    }
    return make_seq_tag(AurTag::kAURTaxInformation, std::move(body));
}

std::optional<AURTaxInformation> decode_aur_tax_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != AurTag::kAURTaxInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    AURTaxInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, Tag::kTaxCode)) {
        out.taxCode = decode_int_field(p[idx], TagClass::kApplication, Tag::kTaxCode);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAURTaxValue)) {
        out.aurTaxValue = decode_int64_field(p[idx], TagClass::kApplication, AurTag::kAURTaxValue);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAURTaxableAmount)) {
        out.aurTaxableAmount =
            decode_int64_field(p[idx], TagClass::kApplication, AurTag::kAURTaxableAmount);
        ++idx;
    }
    return out;
}

// --- AggregatedUsageRecord ---

Tlv encode_aggregated_usage_record(const AggregatedUsageRecord& v) {
    std::vector<std::uint8_t> body;
    if (v.aggregatedUsageDateStart.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       AurTag::kAggregatedUsageDateStart,
                                       *v.aggregatedUsageDateStart));
    }
    if (v.aggregatedUsageDateEnd.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       AurTag::kAggregatedUsageDateEnd,
                                       *v.aggregatedUsageDateEnd));
    }
    if (v.servingNetwork.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kServingNetwork, *v.servingNetwork));
    }
    if (v.aggregationType.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, AurTag::kAggregationType, *v.aggregationType));
    }
    if (v.aggregationIdentifier.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       AurTag::kAggregationIdentifier,
                                       *v.aggregationIdentifier));
    }
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
    if (v.aggregatedChrgUnitType.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    AurTag::kAggregatedChrgUnitType,
                                    *v.aggregatedChrgUnitType));
    }
    if (v.aggregatedChrgUnits.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    AurTag::kAggregatedChrgUnits,
                                    *v.aggregatedChrgUnits));
    }
    if (v.aggregatedUsageCharge.has_value()) {
        encode_tlv(body,
                   encode_int64_field(TagClass::kApplication,
                                      AurTag::kAggregatedUsageCharge,
                                      *v.aggregatedUsageCharge));
    }
    if (v.exchangeRateCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, Tag::kExchangeRateCode, *v.exchangeRateCode));
    }
    if (!v.aurTaxInformationList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.aurTaxInformationList) {
            encode_tlv(list_body, encode_aur_tax_information(item));
        }
        encode_tlv(body, make_seq_tag(AurTag::kAURTaxInformationList, std::move(list_body)));
    }
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
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
    return make_seq_tag(Tag::kAggregatedUsageRecord, std::move(body));
}

std::optional<AggregatedUsageRecord> decode_aggregated_usage_record(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kAggregatedUsageRecord) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    AggregatedUsageRecord out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, AurTag::kAggregatedUsageDateStart)) {
        out.aggregatedUsageDateStart =
            decode_string_field(p[idx], TagClass::kApplication, AurTag::kAggregatedUsageDateStart);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAggregatedUsageDateEnd)) {
        out.aggregatedUsageDateEnd =
            decode_string_field(p[idx], TagClass::kApplication, AurTag::kAggregatedUsageDateEnd);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kServingNetwork)) {
        out.servingNetwork =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kServingNetwork);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAggregationType)) {
        out.aggregationType =
            decode_int_field(p[idx], TagClass::kApplication, AurTag::kAggregationType);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAggregationIdentifier)) {
        out.aggregationIdentifier =
            decode_string_field(p[idx], TagClass::kApplication, AurTag::kAggregationIdentifier);
        ++idx;
    }
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
    if (at_app_tag(p, idx, AurTag::kAggregatedChrgUnitType)) {
        out.aggregatedChrgUnitType =
            decode_int_field(p[idx], TagClass::kApplication, AurTag::kAggregatedChrgUnitType);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAggregatedChrgUnits)) {
        out.aggregatedChrgUnits =
            decode_int_field(p[idx], TagClass::kApplication, AurTag::kAggregatedChrgUnits);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAggregatedUsageCharge)) {
        out.aggregatedUsageCharge =
            decode_int64_field(p[idx], TagClass::kApplication, AurTag::kAggregatedUsageCharge);
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kExchangeRateCode)) {
        out.exchangeRateCode =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kExchangeRateCode);
        ++idx;
    }
    if (at_app_tag(p, idx, AurTag::kAURTaxInformationList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto ti = decode_aur_tax_information(item); ti.has_value()) {
                    out.aurTaxInformationList.push_back(*ti);
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
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
