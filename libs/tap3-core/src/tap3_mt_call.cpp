#include "tap3_core/tap3_mt_call.hpp"

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

// --- CallOriginator ---

Tlv encode_call_originator(const CallOriginator& v) {
    std::vector<std::uint8_t> body;
    if (v.callingNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MtCallTag::kCallingNumber, *v.callingNumber));
    }
    if (v.clirIndicator.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kClirIndicator, *v.clirIndicator));
    }
    if (v.smsOriginator.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MtCallTag::kSMSOriginator, *v.smsOriginator));
    }
    return make_seq_tag(MtCallTag::kCallOriginator, std::move(body));
}

std::optional<CallOriginator> decode_call_originator(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != MtCallTag::kCallOriginator) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    CallOriginator out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MtCallTag::kCallingNumber)) {
        out.callingNumber =
            decode_string_field(p[idx], TagClass::kApplication, MtCallTag::kCallingNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kClirIndicator)) {
        out.clirIndicator =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kClirIndicator);
        ++idx;
    }
    if (at_app_tag(p, idx, MtCallTag::kSMSOriginator)) {
        out.smsOriginator =
            decode_string_field(p[idx], TagClass::kApplication, MtCallTag::kSMSOriginator);
        ++idx;
    }
    return out;
}

// --- MtBasicCallInformation ---

Tlv encode_mt_basic_call_information(const MtBasicCallInformation& v) {
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
    if (v.callOriginator.has_value()) {
        encode_tlv(body, encode_call_originator(*v.callOriginator));
    }
    if (v.originatingNetwork.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MtCallTag::kOriginatingNetwork,
                                       *v.originatingNetwork));
    }
    if (v.callEventStartTimeStamp.has_value()) {
        encode_tlv(
            body,
            encode_date_time(MoCallTag::kCallEventStartTimeStamp, *v.callEventStartTimeStamp));
    }
    if (v.totalCallEventDuration.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MoCallTag::kTotalCallEventDuration,
                                    *v.totalCallEventDuration));
    }
    if (v.simToolkitIndicator.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MoCallTag::kSimToolkitIndicator,
                                       *v.simToolkitIndicator));
    }
    if (v.causeForTerm.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kCauseForTerm, *v.causeForTerm));
    }
    return make_seq_tag(MtCallTag::kMtBasicCallInformation, std::move(body));
}

std::optional<MtBasicCallInformation> decode_mt_basic_call_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != MtCallTag::kMtBasicCallInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    MtBasicCallInformation out;
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
    if (at_app_tag(p, idx, MtCallTag::kCallOriginator)) {
        out.callOriginator = decode_call_originator(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, MtCallTag::kOriginatingNetwork)) {
        out.originatingNetwork =
            decode_string_field(p[idx], TagClass::kApplication, MtCallTag::kOriginatingNetwork);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCallEventStartTimeStamp)) {
        out.callEventStartTimeStamp = decode_date_time(p[idx], MoCallTag::kCallEventStartTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kTotalCallEventDuration)) {
        out.totalCallEventDuration =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kTotalCallEventDuration);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kSimToolkitIndicator)) {
        out.simToolkitIndicator =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kSimToolkitIndicator);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCauseForTerm)) {
        out.causeForTerm =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCauseForTerm);
        ++idx;
    }
    return out;
}

// --- MobileTerminatedCall ---

Tlv encode_mobile_terminated_call(const MobileTerminatedCall& v) {
    std::vector<std::uint8_t> body;
    if (v.basicCallInformation.has_value()) {
        encode_tlv(body, encode_mt_basic_call_information(*v.basicCallInformation));
    }
    if (v.locationInformation.has_value()) {
        encode_tlv(body, encode_location_information(*v.locationInformation));
    }
    if (v.equipmentIdentifier.has_value()) {
        encode_tlv(body, encode_imei_or_esn(*v.equipmentIdentifier));
    }
    if (!v.basicServiceUsedList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& bsu : v.basicServiceUsedList) {
            encode_tlv(list_body, encode_basic_service_used(bsu));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kBasicServiceUsedList, std::move(list_body)));
    }
    if (v.camelServiceUsed.has_value()) {
        encode_tlv(body, encode_camel_service_used(*v.camelServiceUsed));
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
    return make_seq_tag(Tag::kMobileTerminatedCall, std::move(body));
}

std::optional<MobileTerminatedCall> decode_mobile_terminated_call(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kMobileTerminatedCall) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    MobileTerminatedCall out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MtCallTag::kMtBasicCallInformation)) {
        out.basicCallInformation = decode_mt_basic_call_information(p[idx]);
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
    if (at_app_tag(p, idx, MoCallTag::kBasicServiceUsedList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto bsu = decode_basic_service_used(item); bsu.has_value()) {
                    out.basicServiceUsedList.push_back(*bsu);
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCamelServiceUsed)) {
        out.camelServiceUsed = decode_camel_service_used(p[idx]);
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
