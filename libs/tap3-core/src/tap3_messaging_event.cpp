#include "tap3_core/tap3_messaging_event.hpp"

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

// --- ChargedParty ---

Tlv encode_charged_party(const ChargedParty& v) {
    std::vector<std::uint8_t> body;
    if (v.imsi.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, MoCallTag::kImsi, *v.imsi));
    }
    if (v.msisdn.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, MoCallTag::kMsisdn, *v.msisdn));
    }
    if (v.publicUserId.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, MsgEventTag::kPublicUserId, *v.publicUserId));
    }
    if (v.homeBid.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, MoCallTag::kHomeBid, *v.homeBid));
    }
    if (v.homeLocationDescription.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MoCallTag::kHomeLocationDescription,
                                       *v.homeLocationDescription));
    }
    if (v.imei.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, MoCallTag::kImei, *v.imei));
    }
    return make_seq_tag(MsgEventTag::kChargedParty, std::move(body));
}

std::optional<ChargedParty> decode_charged_party(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != MsgEventTag::kChargedParty) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ChargedParty out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MoCallTag::kImsi)) {
        out.imsi = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kImsi);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kMsisdn)) {
        out.msisdn = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kMsisdn);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kPublicUserId)) {
        out.publicUserId =
            decode_string_field(p[idx], TagClass::kApplication, MsgEventTag::kPublicUserId);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kHomeBid)) {
        out.homeBid = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kHomeBid);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kHomeLocationDescription)) {
        out.homeLocationDescription = decode_string_field(
            p[idx], TagClass::kApplication, MoCallTag::kHomeLocationDescription);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kImei)) {
        out.imei = decode_string_field(p[idx], TagClass::kApplication, MoCallTag::kImei);
        ++idx;
    }
    return out;
}

// --- NonChargedParty ---

Tlv encode_non_charged_party(const NonChargedParty& v) {
    std::vector<std::uint8_t> body;
    if (v.nonChargedPartyNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MsgEventTag::kNonChargedPartyNumber,
                                       *v.nonChargedPartyNumber));
    }
    if (v.nonChargedPublicUserId.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       MsgEventTag::kNonChargedPublicUserId,
                                       *v.nonChargedPublicUserId));
    }
    return make_seq_tag(MsgEventTag::kNonChargedParty, std::move(body));
}

std::optional<NonChargedParty> decode_non_charged_party(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != MsgEventTag::kNonChargedParty) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    NonChargedParty out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MsgEventTag::kNonChargedPartyNumber)) {
        out.nonChargedPartyNumber = decode_string_field(
            p[idx], TagClass::kApplication, MsgEventTag::kNonChargedPartyNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kNonChargedPublicUserId)) {
        out.nonChargedPublicUserId = decode_string_field(
            p[idx], TagClass::kApplication, MsgEventTag::kNonChargedPublicUserId);
        ++idx;
    }
    return out;
}

// --- MessagingEvent ---

Tlv encode_messaging_event(const MessagingEvent& v) {
    std::vector<std::uint8_t> body;
    if (v.messagingEventService.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    MsgEventTag::kMessagingEventService,
                                    *v.messagingEventService));
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
    if (!v.networkElementList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& ne : v.networkElementList) {
            std::vector<std::uint8_t> ne_body;
            if (ne.elementType.has_value()) {
                encode_tlv(ne_body,
                           encode_int_field(
                               TagClass::kApplication, MsgEventTag::kElementType, *ne.elementType));
            }
            if (ne.elementId.has_value()) {
                encode_tlv(ne_body,
                           encode_string_field(
                               TagClass::kApplication, MsgEventTag::kElementId, *ne.elementId));
            }
            encode_tlv(list_body, make_seq_tag(MsgEventTag::kNetworkElement, std::move(ne_body)));
        }
        encode_tlv(body, make_seq_tag(MsgEventTag::kNetworkElementList, std::move(list_body)));
    }
    if (v.locationArea.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kLocationArea, *v.locationArea));
    }
    if (v.cellId.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, MoCallTag::kCellId, *v.cellId));
    }
    if (v.serviceStartTimestamp.has_value()) {
        encode_tlv(body,
                   encode_date_time(MsgEventTag::kServiceStartTimestamp, *v.serviceStartTimestamp));
    }
    if (v.nonChargedParty.has_value()) {
        encode_tlv(body, encode_non_charged_party(*v.nonChargedParty));
    }
    if (v.exchangeRateCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, Tag::kExchangeRateCode, *v.exchangeRateCode));
    }
    if (v.callTypeGroup.has_value()) {
        encode_tlv(body, encode_call_type_group(*v.callTypeGroup));
    }
    if (v.charge.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, ChargingTag::kCharge, *v.charge));
    }
    if (!v.taxInformationList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& ti : v.taxInformationList) {
            encode_tlv(list_body, encode_tax_information(ti));
        }
        encode_tlv(body, make_seq_tag(ChargingTag::kTaxInformationList, std::move(list_body)));
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
    return make_seq_tag(Tag::kMessagingEvent, std::move(body));
}

std::optional<MessagingEvent> decode_messaging_event(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kMessagingEvent) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    MessagingEvent out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, MsgEventTag::kMessagingEventService)) {
        out.messagingEventService =
            decode_int_field(p[idx], TagClass::kApplication, MsgEventTag::kMessagingEventService);
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
    if (at_app_tag(p, idx, MsgEventTag::kNetworkElementList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (item.tag_class != TagClass::kApplication ||
                    item.tag_number != MsgEventTag::kNetworkElement) {
                    continue;
                }
                const auto sub = tcap_core::decode_tlvs(item.value);
                if (!sub.has_value()) {
                    continue;
                }
                NetworkElement ne;
                std::size_t sidx = 0;
                const auto& sp = *sub;
                if (at_app_tag(sp, sidx, MsgEventTag::kElementType)) {
                    ne.elementType = decode_int_field(
                        sp[sidx], TagClass::kApplication, MsgEventTag::kElementType);
                    ++sidx;
                }
                if (at_app_tag(sp, sidx, MsgEventTag::kElementId)) {
                    ne.elementId = decode_string_field(
                        sp[sidx], TagClass::kApplication, MsgEventTag::kElementId);
                    ++sidx;
                }
                out.networkElementList.push_back(ne);
            }
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
    if (at_app_tag(p, idx, MsgEventTag::kServiceStartTimestamp)) {
        out.serviceStartTimestamp = decode_date_time(p[idx], MsgEventTag::kServiceStartTimestamp);
        ++idx;
    }
    if (at_app_tag(p, idx, MsgEventTag::kNonChargedParty)) {
        out.nonChargedParty = decode_non_charged_party(p[idx]);
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
    if (at_app_tag(p, idx, ChargingTag::kCharge)) {
        out.charge = decode_int_field(p[idx], TagClass::kApplication, ChargingTag::kCharge);
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
