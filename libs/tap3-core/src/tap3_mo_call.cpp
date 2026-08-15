#include "tap3_core/tap3_mo_call.hpp"

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

Tlv make_octet_string_field(std::uint32_t tag_number, const std::vector<std::uint8_t>& v) {
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = false;
    tlv.tag_number = tag_number;
    tlv.value = v;
    return tlv;
}

} // namespace

// --- ChargeableSubscriber ---

Tlv encode_chargeable_subscriber(const ChargeableSubscriber& v) {
    Tlv inner;
    if (v.isSim) {
        std::vector<std::uint8_t> body;
        if (v.imsi.has_value()) {
            encode_tlv(body,
                       encode_string_field(TagClass::kApplication, MoCallTag::kImsi, *v.imsi));
        }
        if (v.msisdn.has_value()) {
            encode_tlv(body,
                       encode_string_field(TagClass::kApplication, MoCallTag::kMsisdn, *v.msisdn));
        }
        inner = make_seq_tag(MoCallTag::kSimChargeableSubscriber, std::move(body));
    } else {
        std::vector<std::uint8_t> body;
        if (v.min.has_value()) {
            encode_tlv(body, encode_string_field(TagClass::kApplication, MoCallTag::kMin, *v.min));
        }
        if (v.mdn.has_value()) {
            encode_tlv(body, encode_string_field(TagClass::kApplication, MoCallTag::kMdn, *v.mdn));
        }
        inner = make_seq_tag(MoCallTag::kMinChargeableSubscriber, std::move(body));
    }
    return wrap_explicit(TagClass::kApplication, MoCallTag::kChargeableSubscriber, inner);
}

std::optional<ChargeableSubscriber> decode_chargeable_subscriber(const Tlv& tlv) {
    const auto inner =
        unwrap_explicit(tlv, TagClass::kApplication, MoCallTag::kChargeableSubscriber);
    if (!inner.has_value()) {
        return std::nullopt;
    }
    ChargeableSubscriber out;
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == MoCallTag::kSimChargeableSubscriber) {
        out.isSim = true;
        const auto parts = tcap_core::decode_tlvs(inner->value);
        if (!parts.has_value()) {
            return std::nullopt;
        }
        std::size_t idx = 0;
        const auto& p = *parts;
        if (at_app_tag(p, idx, MoCallTag::kImsi)) {
            out.imsi = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kImsi);
            ++idx;
        }
        if (at_app_tag(p, idx, MoCallTag::kMsisdn)) {
            out.msisdn = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMsisdn);
            ++idx;
        }
        return out;
    }
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == MoCallTag::kMinChargeableSubscriber) {
        out.isSim = false;
        const auto parts = tcap_core::decode_tlvs(inner->value);
        if (!parts.has_value()) {
            return std::nullopt;
        }
        std::size_t idx = 0;
        const auto& p = *parts;
        if (at_app_tag(p, idx, MoCallTag::kMin)) {
            out.min = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMin);
            ++idx;
        }
        if (at_app_tag(p, idx, MoCallTag::kMdn)) {
            out.mdn = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMdn);
            ++idx;
        }
        return out;
    }
    return std::nullopt;
}

// --- Destination ---

Tlv encode_destination(const Destination& v) {
    std::vector<std::uint8_t> body;
    if (v.calledNumber.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, MoCallTag::kCalledNumber, *v.calledNumber));
    }
    if (v.dialledDigits.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kDialledDigits, *v.dialledDigits));
    }
    if (v.calledPlace.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, MoCallTag::kCalledPlace, *v.calledPlace));
    }
    if (v.calledRegion.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, MoCallTag::kCalledRegion, *v.calledRegion));
    }
    if (v.smsDestinationNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MoCallTag::kSMSDestinationNumber,
                                       *v.smsDestinationNumber));
    }
    return make_seq_tag(MoCallTag::kDestination, std::move(body));
}

std::optional<Destination> decode_destination(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != MoCallTag::kDestination) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    Destination out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kCalledNumber)) {
        out.calledNumber =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kCalledNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kDialledDigits)) {
        out.dialledDigits =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kDialledDigits);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCalledPlace)) {
        out.calledPlace =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kCalledPlace);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCalledRegion)) {
        out.calledRegion =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kCalledRegion);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kSMSDestinationNumber)) {
        out.smsDestinationNumber =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kSMSDestinationNumber);
        ++idx;
    }
    return out;
}

// --- MoBasicCallInformation ---

Tlv encode_mo_basic_call_information(const MoBasicCallInformation& v) {
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
    if (v.destination.has_value()) {
        encode_tlv(body, encode_destination(*v.destination));
    }
    if (v.destinationNetwork.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MoCallTag::kDestinationNetwork,
                                       *v.destinationNetwork));
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
    return make_seq_tag(MoCallTag::kMoBasicCallInformation, std::move(body));
}

std::optional<MoBasicCallInformation> decode_mo_basic_call_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != MoCallTag::kMoBasicCallInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    MoBasicCallInformation out;
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
    if (at_app_tag(p, idx, MoCallTag::kDestination)) {
        out.destination = decode_destination(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kDestinationNetwork)) {
        out.destinationNetwork =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kDestinationNetwork);
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

// --- LocationInformation ---

Tlv encode_location_information(const LocationInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.networkLocation.has_value()) {
        std::vector<std::uint8_t> nl_body;
        const auto& nl = *v.networkLocation;
        if (nl.recEntityCode.has_value()) {
            encode_tlv(nl_body,
                       encode_int_field(
                           TagClass::kApplication, MoCallTag::kRecEntityCode, *nl.recEntityCode));
        }
        if (nl.callReference.has_value()) {
            encode_tlv(nl_body,
                       make_octet_string_field(MoCallTag::kCallReference, *nl.callReference));
        }
        if (nl.locationArea.has_value()) {
            encode_tlv(nl_body,
                       encode_int_field(
                           TagClass::kApplication, MoCallTag::kLocationArea, *nl.locationArea));
        }
        if (nl.cellId.has_value()) {
            encode_tlv(nl_body,
                       encode_int_field(TagClass::kApplication, MoCallTag::kCellId, *nl.cellId));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kNetworkLocation, std::move(nl_body)));
    }
    if (v.homeLocationInformation.has_value()) {
        std::vector<std::uint8_t> hl_body;
        const auto& hl = *v.homeLocationInformation;
        if (hl.homeBid.has_value()) {
            encode_tlv(
                hl_body,
                encode_string_field(TagClass::kApplication, MoCallTag::kHomeBid, *hl.homeBid));
        }
        if (hl.homeLocationDescription.has_value()) {
            encode_tlv(hl_body,
                       encode_string_field(TagClass::kApplication,
                                           MoCallTag::kHomeLocationDescription,
                                           *hl.homeLocationDescription));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kHomeLocationInformation, std::move(hl_body)));
    }
    if (v.geographicalLocation.has_value()) {
        std::vector<std::uint8_t> gl_body;
        const auto& gl = *v.geographicalLocation;
        if (gl.servingNetwork.has_value()) {
            encode_tlv(gl_body,
                       encode_string_field(
                           TagClass::kApplication, MoCallTag::kServingNetwork, *gl.servingNetwork));
        }
        if (gl.servingBid.has_value()) {
            encode_tlv(gl_body,
                       encode_string_field(
                           TagClass::kApplication, MoCallTag::kServingBid, *gl.servingBid));
        }
        if (gl.servingLocationDescription.has_value()) {
            encode_tlv(gl_body,
                       encode_string_field(TagClass::kApplication,
                                           MoCallTag::kServingLocationDescription,
                                           *gl.servingLocationDescription));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kGeographicalLocation, std::move(gl_body)));
    }
    return make_seq_tag(MoCallTag::kLocationInformation, std::move(body));
}

std::optional<LocationInformation> decode_location_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != MoCallTag::kLocationInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    LocationInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kNetworkLocation)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
        if (sub.has_value()) {
            NetworkLocation nl;
            std::size_t sidx = 0;
            const auto& sp = *sub;
            if (at_app_tag(sp, sidx, MoCallTag::kRecEntityCode)) {
                nl.recEntityCode =
                    decode_int_field(sp[sidx], TagClass::kApplication, MoCallTag::kRecEntityCode);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kCallReference)) {
                nl.callReference = sp[sidx].value;
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kLocationArea)) {
                nl.locationArea =
                    decode_int_field(sp[sidx], TagClass::kApplication, MoCallTag::kLocationArea);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kCellId)) {
                nl.cellId = decode_int_field(sp[sidx], TagClass::kApplication, MoCallTag::kCellId);
                ++sidx;
            }
            out.networkLocation = nl;
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kHomeLocationInformation)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
        if (sub.has_value()) {
            HomeLocationInformation hl;
            std::size_t sidx = 0;
            const auto& sp = *sub;
            if (at_app_tag(sp, sidx, MoCallTag::kHomeBid)) {
                hl.homeBid =
                    decode_string_field(sp[sidx], TagClass::kApplication, MoCallTag::kHomeBid);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kHomeLocationDescription)) {
                hl.homeLocationDescription = decode_string_field(
                    sp[sidx], TagClass::kApplication, MoCallTag::kHomeLocationDescription);
                ++sidx;
            }
            out.homeLocationInformation = hl;
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kGeographicalLocation)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
        if (sub.has_value()) {
            GeographicalLocation gl;
            std::size_t sidx = 0;
            const auto& sp = *sub;
            if (at_app_tag(sp, sidx, MoCallTag::kServingNetwork)) {
                gl.servingNetwork = decode_string_field(
                    sp[sidx], TagClass::kApplication, MoCallTag::kServingNetwork);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kServingBid)) {
                gl.servingBid =
                    decode_string_field(sp[sidx], TagClass::kApplication, MoCallTag::kServingBid);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kServingLocationDescription)) {
                gl.servingLocationDescription = decode_string_field(
                    sp[sidx], TagClass::kApplication, MoCallTag::kServingLocationDescription);
                ++sidx;
            }
            out.geographicalLocation = gl;
        }
        ++idx;
    }
    return out;
}

// --- ImeiOrEsn ---

Tlv encode_imei_or_esn(const ImeiOrEsn& v) {
    Tlv inner = v.isImei ? encode_string_field(TagClass::kApplication, MoCallTag::kImei, v.value)
                         : encode_string_field(TagClass::kApplication, MoCallTag::kEsn, v.value);
    return wrap_explicit(TagClass::kApplication, MoCallTag::kImeiOrEsn, inner);
}

std::optional<ImeiOrEsn> decode_imei_or_esn(const Tlv& tlv) {
    const auto inner = unwrap_explicit(tlv, TagClass::kApplication, MoCallTag::kImeiOrEsn);
    if (!inner.has_value()) {
        return std::nullopt;
    }
    ImeiOrEsn out;
    if (inner->tag_class == TagClass::kApplication && inner->tag_number == MoCallTag::kImei) {
        out.isImei = true;
        out.value = std::string(inner->value.begin(), inner->value.end());
        return out;
    }
    if (inner->tag_class == TagClass::kApplication && inner->tag_number == MoCallTag::kEsn) {
        out.isImei = false;
        out.value = std::string(inner->value.begin(), inner->value.end());
        return out;
    }
    return std::nullopt;
}

// --- BasicServiceCode / BasicService / BasicServiceUsed ---

Tlv encode_basic_service_code(const BasicServiceCode& v) {
    Tlv inner =
        v.isTeleService
            ? encode_string_field(TagClass::kApplication, MoCallTag::kTeleServiceCode, v.value)
            : encode_string_field(TagClass::kApplication, MoCallTag::kBearerServiceCode, v.value);
    return wrap_explicit(TagClass::kApplication, MoCallTag::kBasicServiceCode, inner);
}

std::optional<BasicServiceCode> decode_basic_service_code(const Tlv& tlv) {
    const auto inner = unwrap_explicit(tlv, TagClass::kApplication, MoCallTag::kBasicServiceCode);
    if (!inner.has_value()) {
        return std::nullopt;
    }
    BasicServiceCode out;
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == MoCallTag::kTeleServiceCode) {
        out.isTeleService = true;
        out.value = std::string(inner->value.begin(), inner->value.end());
        return out;
    }
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == MoCallTag::kBearerServiceCode) {
        out.isTeleService = false;
        out.value = std::string(inner->value.begin(), inner->value.end());
        return out;
    }
    return std::nullopt;
}

Tlv encode_basic_service_used(const BasicServiceUsed& v) {
    std::vector<std::uint8_t> body;
    if (v.basicService.has_value()) {
        std::vector<std::uint8_t> bs_body;
        const auto& bs = *v.basicService;
        if (bs.serviceCode.has_value()) {
            encode_tlv(bs_body, encode_basic_service_code(*bs.serviceCode));
        }
        if (bs.transparencyIndicator.has_value()) {
            encode_tlv(bs_body,
                       encode_int_field(TagClass::kApplication,
                                        MoCallTag::kTransparencyIndicator,
                                        *bs.transparencyIndicator));
        }
        if (bs.fnur.has_value()) {
            encode_tlv(bs_body,
                       encode_int_field(TagClass::kApplication, MoCallTag::kFnur, *bs.fnur));
        }
        if (bs.userProtocolIndicator.has_value()) {
            encode_tlv(bs_body,
                       encode_int_field(TagClass::kApplication,
                                        MoCallTag::kUserProtocolIndicator,
                                        *bs.userProtocolIndicator));
        }
        if (bs.guaranteedBitRate.has_value()) {
            encode_tlv(
                bs_body,
                make_octet_string_field(MoCallTag::kGuaranteedBitRate, *bs.guaranteedBitRate));
        }
        if (bs.maximumBitRate.has_value()) {
            encode_tlv(bs_body,
                       make_octet_string_field(MoCallTag::kMaximumBitRate, *bs.maximumBitRate));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kBasicService, std::move(bs_body)));
    }
    if (v.chargingTimeStamp.has_value()) {
        encode_tlv(body, encode_date_time(MoCallTag::kChargingTimeStamp, *v.chargingTimeStamp));
    }
    if (v.hscsdIndicator.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kHSCSDIndicator, *v.hscsdIndicator));
    }
    return make_seq_tag(MoCallTag::kBasicServiceUsed, std::move(body));
}

std::optional<BasicServiceUsed> decode_basic_service_used(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != MoCallTag::kBasicServiceUsed) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    BasicServiceUsed out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kBasicService)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
        if (sub.has_value()) {
            BasicService bs;
            std::size_t sidx = 0;
            const auto& sp = *sub;
            if (sidx < sp.size() && sp[sidx].tag_class == TagClass::kApplication &&
                sp[sidx].tag_number == MoCallTag::kBasicServiceCode) {
                bs.serviceCode = decode_basic_service_code(sp[sidx]);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kTransparencyIndicator)) {
                bs.transparencyIndicator = decode_int_field(
                    sp[sidx], TagClass::kApplication, MoCallTag::kTransparencyIndicator);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kFnur)) {
                bs.fnur = decode_int_field(sp[sidx], TagClass::kApplication, MoCallTag::kFnur);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kUserProtocolIndicator)) {
                bs.userProtocolIndicator = decode_int_field(
                    sp[sidx], TagClass::kApplication, MoCallTag::kUserProtocolIndicator);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kGuaranteedBitRate)) {
                bs.guaranteedBitRate = sp[sidx].value;
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MoCallTag::kMaximumBitRate)) {
                bs.maximumBitRate = sp[sidx].value;
                ++sidx;
            }
            out.basicService = bs;
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kChargingTimeStamp)) {
        out.chargingTimeStamp = decode_date_time(p[idx], MoCallTag::kChargingTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kHSCSDIndicator)) {
        out.hscsdIndicator =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kHSCSDIndicator);
        ++idx;
    }
    return out;
}

// --- ThirdPartyInformation ---

Tlv encode_third_party_information(const ThirdPartyInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.thirdPartyNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kThirdPartyNumber, *v.thirdPartyNumber));
    }
    if (v.clirIndicator.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kClirIndicator, *v.clirIndicator));
    }
    return make_seq_tag(MoCallTag::kThirdPartyInformation, std::move(body));
}

std::optional<ThirdPartyInformation> decode_third_party_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != MoCallTag::kThirdPartyInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ThirdPartyInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kThirdPartyNumber)) {
        out.thirdPartyNumber =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kThirdPartyNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kClirIndicator)) {
        out.clirIndicator =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kClirIndicator);
        ++idx;
    }
    return out;
}

// --- CamelServiceUsed ---

Tlv encode_camel_service_used(const CamelServiceUsed& v) {
    std::vector<std::uint8_t> body;
    if (v.camelServiceLevel.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MoCallTag::kCamelServiceLevel,
                                    *v.camelServiceLevel));
    }
    if (v.camelServiceKey.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, MoCallTag::kCamelServiceKey, *v.camelServiceKey));
    }
    if (v.defaultCallHandling.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MoCallTag::kDefaultCallHandling,
                                    *v.defaultCallHandling));
    }
    if (v.exchangeRateCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, MoCallTag::kExchangeRateCode, *v.exchangeRateCode));
    }
    if (v.camelInvocationFee.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MoCallTag::kCamelInvocationFee,
                                    *v.camelInvocationFee));
    }
    if (v.threeGcamelDestination.has_value()) {
        const auto& d = *v.threeGcamelDestination;
        Tlv inner =
            d.isCamelDestinationNumber
                ? encode_string_field(TagClass::kApplication,
                                      MoCallTag::kCamelDestinationNumber,
                                      d.camelDestinationNumber.value_or(""))
                : [&] {
                      std::vector<std::uint8_t> gd_body;
                      if (d.gprsDestination.has_value()) {
                          if (d.gprsDestination->accessPointNameNI.has_value()) {
                              encode_tlv(
                                  gd_body,
                                  encode_string_field(TagClass::kApplication,
                                                      MoCallTag::kAccessPointNameNI,
                                                      *d.gprsDestination->accessPointNameNI));
                          }
                          if (d.gprsDestination->accessPointNameOI.has_value()) {
                              encode_tlv(
                                  gd_body,
                                  encode_string_field(TagClass::kApplication,
                                                      MoCallTag::kAccessPointNameOI,
                                                      *d.gprsDestination->accessPointNameOI));
                          }
                      }
                      return make_seq_tag(MoCallTag::kGprsDestination, std::move(gd_body));
                  }();
        encode_tlv(
            body, wrap_explicit(TagClass::kApplication, MoCallTag::kThreeGcamelDestination, inner));
    }
    if (v.cseInformation.has_value()) {
        encode_tlv(body, make_octet_string_field(MoCallTag::kCseInformation, *v.cseInformation));
    }
    return make_seq_tag(MoCallTag::kCamelServiceUsed, std::move(body));
}

std::optional<CamelServiceUsed> decode_camel_service_used(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != MoCallTag::kCamelServiceUsed) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    CamelServiceUsed out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kCamelServiceLevel)) {
        out.camelServiceLevel =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCamelServiceLevel);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCamelServiceKey)) {
        out.camelServiceKey =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCamelServiceKey);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kDefaultCallHandling)) {
        out.defaultCallHandling =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kDefaultCallHandling);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kExchangeRateCode)) {
        out.exchangeRateCode =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kExchangeRateCode);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCamelInvocationFee)) {
        out.camelInvocationFee =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCamelInvocationFee);
        ++idx;
    }
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == MoCallTag::kThreeGcamelDestination) {
        const auto inner =
            unwrap_explicit(p[idx], TagClass::kApplication, MoCallTag::kThreeGcamelDestination);
        if (inner.has_value()) {
            ThreeGcamelDestination d;
            if (inner->tag_class == TagClass::kApplication &&
                inner->tag_number == MoCallTag::kCamelDestinationNumber) {
                d.isCamelDestinationNumber = true;
                d.camelDestinationNumber = std::string(inner->value.begin(), inner->value.end());
            } else if (inner->tag_class == TagClass::kApplication &&
                       inner->tag_number == MoCallTag::kGprsDestination) {
                d.isCamelDestinationNumber = false;
                const auto sub = tcap_core::decode_tlvs(inner->value);
                if (sub.has_value()) {
                    GprsDestination gd;
                    std::size_t sidx = 0;
                    const auto& sp = *sub;
                    if (at_app_tag(sp, sidx, MoCallTag::kAccessPointNameNI)) {
                        gd.accessPointNameNI = decode_string_field(
                            sp[sidx], TagClass::kApplication, MoCallTag::kAccessPointNameNI);
                        ++sidx;
                    }
                    if (at_app_tag(sp, sidx, MoCallTag::kAccessPointNameOI)) {
                        gd.accessPointNameOI = decode_string_field(
                            sp[sidx], TagClass::kApplication, MoCallTag::kAccessPointNameOI);
                        ++sidx;
                    }
                    d.gprsDestination = gd;
                }
            }
            out.threeGcamelDestination = d;
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCseInformation)) {
        out.cseInformation = p[idx].value;
        ++idx;
    }
    return out;
}

// --- MobileOriginatedCall ---

Tlv encode_mobile_originated_call(const MobileOriginatedCall& v) {
    std::vector<std::uint8_t> body;
    if (v.basicCallInformation.has_value()) {
        encode_tlv(body, encode_mo_basic_call_information(*v.basicCallInformation));
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
    if (v.supplServiceCode.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MoCallTag::kSupplServiceCode, *v.supplServiceCode));
    }
    if (v.thirdPartyInformation.has_value()) {
        encode_tlv(body, encode_third_party_information(*v.thirdPartyInformation));
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
    return make_seq_tag(Tag::kMobileOriginatedCall, std::move(body));
}

std::optional<MobileOriginatedCall> decode_mobile_originated_call(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kMobileOriginatedCall) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    MobileOriginatedCall out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kMoBasicCallInformation)) {
        out.basicCallInformation = decode_mo_basic_call_information(p[idx]);
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
    if (at_app_tag(p, idx, MoCallTag::kSupplServiceCode)) {
        out.supplServiceCode =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kSupplServiceCode);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kThirdPartyInformation)) {
        out.thirdPartyInformation = decode_third_party_information(p[idx]);
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
