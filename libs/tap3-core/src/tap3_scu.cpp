#include "tap3_core/tap3_scu.hpp"

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

// --- ScuChargeableSubscriber ---

Tlv encode_scu_chargeable_subscriber(const ScuChargeableSubscriber& v) {
    Tlv inner;
    if (v.isGsm) {
        std::vector<std::uint8_t> body;
        if (v.gsm.has_value()) {
            if (v.gsm->imsi.has_value()) {
                encode_tlv(
                    body,
                    encode_string_field(TagClass::kApplication, MoCallTag::kImsi, *v.gsm->imsi));
            }
            if (v.gsm->msisdn.has_value()) {
                encode_tlv(body,
                           encode_string_field(
                               TagClass::kApplication, MoCallTag::kMsisdn, *v.gsm->msisdn));
            }
        }
        inner = make_seq_tag(ScuTag::kGsmChargeableSubscriber, std::move(body));
    } else {
        std::vector<std::uint8_t> body;
        if (v.min.has_value()) {
            if (v.min->min.has_value()) {
                encode_tlv(
                    body,
                    encode_string_field(TagClass::kApplication, MoCallTag::kMin, *v.min->min));
            }
            if (v.min->mdn.has_value()) {
                encode_tlv(
                    body,
                    encode_string_field(TagClass::kApplication, MoCallTag::kMdn, *v.min->mdn));
            }
        }
        inner = make_seq_tag(MoCallTag::kMinChargeableSubscriber, std::move(body));
    }
    return wrap_explicit(TagClass::kApplication, ScuTag::kScuChargeableSubscriber, inner);
}

std::optional<ScuChargeableSubscriber> decode_scu_chargeable_subscriber(const Tlv& tlv) {
    const auto inner =
        unwrap_explicit(tlv, TagClass::kApplication, ScuTag::kScuChargeableSubscriber);
    if (!inner.has_value()) {
        return std::nullopt;
    }
    ScuChargeableSubscriber out;
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == ScuTag::kGsmChargeableSubscriber) {
        out.isGsm = true;
        const auto parts = tcap_core::decode_tlvs(inner->value);
        if (!parts.has_value()) {
            return std::nullopt;
        }
        GsmChargeableSubscriber gsm;
        std::size_t idx = 0;
        const auto& p = *parts;
        if (at_app_tag(p, idx, MoCallTag::kImsi)) {
            gsm.imsi = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kImsi);
            ++idx;
        }
        if (at_app_tag(p, idx, MoCallTag::kMsisdn)) {
            gsm.msisdn = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMsisdn);
            ++idx;
        }
        out.gsm = gsm;
        return out;
    }
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == MoCallTag::kMinChargeableSubscriber) {
        out.isGsm = false;
        const auto parts = tcap_core::decode_tlvs(inner->value);
        if (!parts.has_value()) {
            return std::nullopt;
        }
        ChargeableSubscriber min_sub;
        min_sub.isSim = false;
        std::size_t idx = 0;
        const auto& p = *parts;
        if (at_app_tag(p, idx, MoCallTag::kMin)) {
            min_sub.min = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMin);
            ++idx;
        }
        if (at_app_tag(p, idx, MoCallTag::kMdn)) {
            min_sub.mdn = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMdn);
            ++idx;
        }
        out.min = min_sub;
        return out;
    }
    return std::nullopt;
}

// --- ScuBasicInformation ---

Tlv encode_scu_basic_information(const ScuBasicInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.chargeableSubscriber.has_value()) {
        encode_tlv(body, encode_scu_chargeable_subscriber(*v.chargeableSubscriber));
    }
    if (v.chargedPartyStatus.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ScuTag::kChargedPartyStatus, *v.chargedPartyStatus));
    }
    if (v.nonChargedNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, ScuTag::kNonChargedNumber, *v.nonChargedNumber));
    }
    if (v.clirIndicator.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kClirIndicator, *v.clirIndicator));
    }
    if (v.originatingNetwork.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, ScuTag::kOriginatingNetwork, *v.originatingNetwork));
    }
    if (v.destinationNetwork.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MoCallTag::kDestinationNetwork,
                                       *v.destinationNetwork));
    }
    return make_seq_tag(ScuTag::kScuBasicInformation, std::move(body));
}

std::optional<ScuBasicInformation> decode_scu_basic_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ScuTag::kScuBasicInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ScuBasicInformation out;
    std::size_t idx = 0;
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == ScuTag::kScuChargeableSubscriber) {
        out.chargeableSubscriber = decode_scu_chargeable_subscriber(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kChargedPartyStatus)) {
        out.chargedPartyStatus =
            decode_int_field(p[idx], TagClass::kApplication, ScuTag::kChargedPartyStatus);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kNonChargedNumber)) {
        out.nonChargedNumber =
            decode_string_field(p[idx], TagClass::kApplication, ScuTag::kNonChargedNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kClirIndicator)) {
        out.clirIndicator =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kClirIndicator);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kOriginatingNetwork)) {
        out.originatingNetwork =
            decode_string_field(p[idx], TagClass::kApplication, ScuTag::kOriginatingNetwork);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kDestinationNetwork)) {
        out.destinationNetwork =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kDestinationNetwork);
        ++idx;
    }
    return out;
}

// --- ScuChargeType ---

Tlv encode_scu_charge_type(const ScuChargeType& v) {
    std::vector<std::uint8_t> body;
    if (v.messageStatus.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, ScuTag::kMessageStatus, *v.messageStatus));
    }
    if (v.priorityCode.has_value()) {
        encode_tlv(
            body, encode_int_field(TagClass::kApplication, ScuTag::kPriorityCode, *v.priorityCode));
    }
    if (v.distanceChargeBandCode.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ScuTag::kDistanceChargeBandCode,
                                       *v.distanceChargeBandCode));
    }
    if (v.messageType.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, ScuTag::kMessageType, *v.messageType));
    }
    if (v.messageDescriptionCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ScuTag::kMessageDescriptionCode,
                                    *v.messageDescriptionCode));
    }
    return make_seq_tag(ScuTag::kScuChargeType, std::move(body));
}

std::optional<ScuChargeType> decode_scu_charge_type(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ScuTag::kScuChargeType) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ScuChargeType out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ScuTag::kMessageStatus)) {
        out.messageStatus =
            decode_int_field(p[idx], TagClass::kApplication, ScuTag::kMessageStatus);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kPriorityCode)) {
        out.priorityCode = decode_int_field(p[idx], TagClass::kApplication, ScuTag::kPriorityCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kDistanceChargeBandCode)) {
        out.distanceChargeBandCode =
            decode_string_field(p[idx], TagClass::kApplication, ScuTag::kDistanceChargeBandCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kMessageType)) {
        out.messageType = decode_int_field(p[idx], TagClass::kApplication, ScuTag::kMessageType);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kMessageDescriptionCode)) {
        out.messageDescriptionCode =
            decode_int_field(p[idx], TagClass::kApplication, ScuTag::kMessageDescriptionCode);
        ++idx;
    }
    return out;
}

// --- ScuTimeStamps ---

Tlv encode_scu_time_stamps(const ScuTimeStamps& v) {
    std::vector<std::uint8_t> body;
    if (v.depositTimeStamp.has_value()) {
        encode_tlv(body, encode_date_time(ScuTag::kDepositTimeStamp, *v.depositTimeStamp));
    }
    if (v.completionTimeStamp.has_value()) {
        encode_tlv(body, encode_date_time(ScuTag::kCompletionTimeStamp, *v.completionTimeStamp));
    }
    if (v.chargingPoint.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, ScuTag::kChargingPoint, *v.chargingPoint));
    }
    return make_seq_tag(ScuTag::kScuTimeStamps, std::move(body));
}

std::optional<ScuTimeStamps> decode_scu_time_stamps(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ScuTag::kScuTimeStamps) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ScuTimeStamps out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ScuTag::kDepositTimeStamp)) {
        out.depositTimeStamp = decode_date_time(p[idx], ScuTag::kDepositTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kCompletionTimeStamp)) {
        out.completionTimeStamp = decode_date_time(p[idx], ScuTag::kCompletionTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kChargingPoint)) {
        out.chargingPoint =
            decode_string_field(p[idx], TagClass::kApplication, ScuTag::kChargingPoint);
        ++idx;
    }
    return out;
}

// --- ServiceCentreUsage ---

Tlv encode_service_centre_usage(const ServiceCentreUsage& v) {
    std::vector<std::uint8_t> body;
    if (v.basicInformation.has_value()) {
        encode_tlv(body, encode_scu_basic_information(*v.basicInformation));
    }
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.servingNetwork.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kServingNetwork, *v.servingNetwork));
    }
    if (v.recEntityCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kRecEntityCode, *v.recEntityCode));
    }
    if (v.chargeInformation.has_value()) {
        encode_tlv(body, encode_charge_information(*v.chargeInformation));
    }
    if (v.scuChargeType.has_value()) {
        encode_tlv(body, encode_scu_charge_type(*v.scuChargeType));
    }
    if (v.scuTimeStamps.has_value()) {
        encode_tlv(body, encode_scu_time_stamps(*v.scuTimeStamps));
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
    return make_seq_tag(ScuTag::kServiceCentreUsage, std::move(body));
}

std::optional<ServiceCentreUsage> decode_service_centre_usage(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != ScuTag::kServiceCentreUsage) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ServiceCentreUsage out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ScuTag::kScuBasicInformation)) {
        out.basicInformation = decode_scu_basic_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kServingNetwork)) {
        out.servingNetwork =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kServingNetwork);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kRecEntityCode)) {
        out.recEntityCode =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kRecEntityCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeInformation)) {
        out.chargeInformation = decode_charge_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kScuChargeType)) {
        out.scuChargeType = decode_scu_charge_type(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ScuTag::kScuTimeStamps)) {
        out.scuTimeStamps = decode_scu_time_stamps(p[idx]);
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
