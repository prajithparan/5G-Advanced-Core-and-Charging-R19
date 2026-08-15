#include "tap3_core/tap3_mobile_session.hpp"

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

Tlv encode_session_charge_information(const SessionChargeInformation& v) {
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
    if (!v.taxInformationList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& ti : v.taxInformationList) {
            encode_tlv(list_body, encode_tax_information(ti));
        }
        encode_tlv(body, make_seq_tag(ChargingTag::kTaxInformationList, std::move(list_body)));
    }
    return make_seq_tag(MobileSessionTag::kSessionChargeInformation, std::move(body));
}

std::optional<SessionChargeInformation> decode_session_charge_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != MobileSessionTag::kSessionChargeInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    SessionChargeInformation out;
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
                    out.taxInformationList.push_back(*ti);
                }
            }
        }
        ++idx;
    }
    return out;
}

} // namespace

Tlv encode_mobile_session(const MobileSession& v) {
    std::vector<std::uint8_t> body;
    if (v.mobileSessionService.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MobileSessionTag::kMobileSessionService,
                                    *v.mobileSessionService));
    }
    if (v.chargedParty.has_value()) {
        encode_tlv(body, encode_charged_party(*v.chargedParty));
    }
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.simToolkitIndicator.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MoCallTag::kSimToolkitIndicator,
                                       *v.simToolkitIndicator));
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
    if (v.locationArea.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kLocationArea, *v.locationArea));
    }
    if (v.cellId.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, MoCallTag::kCellId, *v.cellId));
    }
    if (v.eventReference.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MsgEventTag::kEventReference, *v.eventReference));
    }
    if (!v.recEntityCodeList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto code : v.recEntityCodeList) {
            encode_tlv(list_body,
                       encode_int_field(TagClass::kApplication, MoCallTag::kRecEntityCode, code));
        }
        encode_tlv(body, make_seq_tag(MsgEventTag::kRecEntityCodeList, std::move(list_body)));
    }
    if (v.serviceStartTimestamp.has_value()) {
        encode_tlv(body,
                   encode_date_time(MsgEventTag::kServiceStartTimestamp, *v.serviceStartTimestamp));
    }
    if (v.causeForTerm.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kCauseForTerm, *v.causeForTerm));
    }
    if (v.totalCallEventDuration.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MoCallTag::kTotalCallEventDuration,
                                    *v.totalCallEventDuration));
    }
    if (v.nonChargedParty.has_value()) {
        encode_tlv(body, encode_non_charged_party(*v.nonChargedParty));
    }
    if (v.requestedDestination.has_value()) {
        std::vector<std::uint8_t> rd_body;
        if (v.requestedDestination->requestedNumber.has_value()) {
            encode_tlv(rd_body,
                       encode_string_field(TagClass::kApplication,
                                           MobileSessionTag::kRequestedNumber,
                                           *v.requestedDestination->requestedNumber));
        }
        if (v.requestedDestination->requestedPublicUserId.has_value()) {
            encode_tlv(rd_body,
                       encode_string_field(TagClass::kApplication,
                                           MobileSessionTag::kRequestedPublicUserId,
                                           *v.requestedDestination->requestedPublicUserId));
        }
        encode_tlv(body, make_seq_tag(MobileSessionTag::kRequestedDestination, std::move(rd_body)));
    }
    if (!v.sessionChargeInfoList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& sci : v.sessionChargeInfoList) {
            encode_tlv(list_body, encode_session_charge_information(sci));
        }
        encode_tlv(body,
                   make_seq_tag(MobileSessionTag::kSessionChargeInfoList, std::move(list_body)));
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
    return make_seq_tag(Tag::kMobileSession, std::move(body));
}

std::optional<MobileSession> decode_mobile_session(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kMobileSession) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    MobileSession out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MobileSessionTag::kMobileSessionService)) {
        out.mobileSessionService = decode_int_field(
            p[idx], TagClass::kApplication, MobileSessionTag::kMobileSessionService);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kChargedParty)) {
        out.chargedParty = decode_charged_party(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kSimToolkitIndicator)) {
        out.simToolkitIndicator =
            decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kSimToolkitIndicator);
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
    if (at_app_tag(p, idx, MoCallTag::kLocationArea)) {
        out.locationArea =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kLocationArea);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCellId)) {
        out.cellId = decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCellId);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kEventReference)) {
        out.eventReference =
            decode_string_field(p[idx], TagClass::kApplication, MsgEventTag::kEventReference);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kRecEntityCodeList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto v =
                        decode_int_field(item, TagClass::kApplication, MoCallTag::kRecEntityCode);
                    v.has_value()) {
                    out.recEntityCodeList.push_back(*v);
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kServiceStartTimestamp)) {
        out.serviceStartTimestamp = decode_date_time(p[idx], MsgEventTag::kServiceStartTimestamp);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kCauseForTerm)) {
        out.causeForTerm =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCauseForTerm);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kTotalCallEventDuration)) {
        out.totalCallEventDuration =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kTotalCallEventDuration);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kNonChargedParty)) {
        out.nonChargedParty = decode_non_charged_party(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, MobileSessionTag::kRequestedDestination)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
        if (sub.has_value()) {
            RequestedDestination rd;
            std::size_t sidx = 0;
            const auto& sp = *sub;
            if (at_app_tag(sp, sidx, MobileSessionTag::kRequestedNumber)) {
                rd.requestedNumber = decode_string_field(
                    sp[sidx], TagClass::kApplication, MobileSessionTag::kRequestedNumber);
                ++sidx;
            }
            if (at_app_tag(sp, sidx, MobileSessionTag::kRequestedPublicUserId)) {
                rd.requestedPublicUserId = decode_string_field(
                    sp[sidx], TagClass::kApplication, MobileSessionTag::kRequestedPublicUserId);
                ++sidx;
            }
            out.requestedDestination = rd;
        }
        ++idx;
    }
    if (at_app_tag(p, idx, MobileSessionTag::kSessionChargeInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto sci = decode_session_charge_information(item); sci.has_value()) {
                    out.sessionChargeInfoList.push_back(*sci);
                }
            }
        }
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
