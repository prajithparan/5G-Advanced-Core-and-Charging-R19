#include "tap3_core/tap3_gprs_call.hpp"

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

// --- GprsChargeableSubscriber ---

Tlv encode_gprs_chargeable_subscriber(const GprsChargeableSubscriber& v) {
    std::vector<std::uint8_t> body;
    if (v.chargeableSubscriber.has_value()) {
        encode_tlv(body, encode_chargeable_subscriber(*v.chargeableSubscriber));
    }
    if (v.pdpAddress.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, GprsCallTag::kPdpAddress, *v.pdpAddress));
    }
    if (v.networkAccessIdentifier.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       GprsCallTag::kNetworkAccessIdentifier,
                                       *v.networkAccessIdentifier));
    }
    return make_seq_tag(GprsCallTag::kGprsChargeableSubscriber, std::move(body));
}

std::optional<GprsChargeableSubscriber> decode_gprs_chargeable_subscriber(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != GprsCallTag::kGprsChargeableSubscriber) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    GprsChargeableSubscriber out;
    std::size_t idx = 0;
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == MoCallTag::kChargeableSubscriber) {
        out.chargeableSubscriber = decode_chargeable_subscriber(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kPdpAddress)) {
        out.pdpAddress =
            decode_string_field(p[idx], TagClass::kApplication, GprsCallTag::kPdpAddress);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kNetworkAccessIdentifier)) {
        out.networkAccessIdentifier = decode_string_field(
            p[idx], TagClass::kApplication, GprsCallTag::kNetworkAccessIdentifier);
        ++idx;
    }
    return out;
}

// --- GprsBasicCallInformation ---

Tlv encode_gprs_basic_call_information(const GprsBasicCallInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.gprsChargeableSubscriber.has_value()) {
        encode_tlv(body, encode_gprs_chargeable_subscriber(*v.gprsChargeableSubscriber));
    }
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.gprsDestination.has_value()) {
        std::vector<std::uint8_t> gd_body;
        if (v.gprsDestination->accessPointNameNI.has_value()) {
            encode_tlv(gd_body,
                       encode_string_field(TagClass::kApplication,
                                           MoCallTag::kAccessPointNameNI,
                                           *v.gprsDestination->accessPointNameNI));
        }
        if (v.gprsDestination->accessPointNameOI.has_value()) {
            encode_tlv(gd_body,
                       encode_string_field(TagClass::kApplication,
                                           MoCallTag::kAccessPointNameOI,
                                           *v.gprsDestination->accessPointNameOI));
        }
        encode_tlv(body, make_seq_tag(MoCallTag::kGprsDestination, std::move(gd_body)));
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
    if (v.causeForTerm.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kCauseForTerm, *v.causeForTerm));
    }
    if (v.partialTypeIndicator.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       GprsCallTag::kPartialTypeIndicator,
                                       *v.partialTypeIndicator));
    }
    if (v.pdpContextStartTimestamp.has_value()) {
        encode_tlv(
            body,
            encode_date_time(GprsCallTag::kPDPContextStartTimestamp, *v.pdpContextStartTimestamp));
    }
    if (v.networkInitPDPContext.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    GprsCallTag::kNetworkInitPDPContext,
                                    *v.networkInitPDPContext));
    }
    if (v.chargingId.has_value()) {
        encode_tlv(
            body,
            encode_int64_field(TagClass::kApplication, GprsCallTag::kChargingId, *v.chargingId));
    }
    return make_seq_tag(GprsCallTag::kGprsBasicCallInformation, std::move(body));
}

std::optional<GprsBasicCallInformation> decode_gprs_basic_call_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != GprsCallTag::kGprsBasicCallInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    GprsBasicCallInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, GprsCallTag::kGprsChargeableSubscriber)) {
        out.gprsChargeableSubscriber = decode_gprs_chargeable_subscriber(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kGprsDestination)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
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
            out.gprsDestination = gd;
        }
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
    if (at_app_tag(p, idx, MoCallTag::kCauseForTerm)) {
        out.causeForTerm =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kCauseForTerm);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kPartialTypeIndicator)) {
        out.partialTypeIndicator =
            decode_string_field(p[idx], TagClass::kApplication, GprsCallTag::kPartialTypeIndicator);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kPDPContextStartTimestamp)) {
        out.pdpContextStartTimestamp =
            decode_date_time(p[idx], GprsCallTag::kPDPContextStartTimestamp);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kNetworkInitPDPContext)) {
        out.networkInitPDPContext =
            decode_int_field(p[idx], TagClass::kApplication, GprsCallTag::kNetworkInitPDPContext);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kChargingId)) {
        out.chargingId =
            decode_int64_field(p[idx], TagClass::kApplication, GprsCallTag::kChargingId);
        ++idx;
    }
    return out;
}

// --- GprsLocationInformation ---

Tlv encode_gprs_location_information(const GprsLocationInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.gprsNetworkLocation.has_value()) {
        std::vector<std::uint8_t> nl_body;
        const auto& nl = *v.gprsNetworkLocation;
        if (!nl.recEntity.empty()) {
            std::vector<std::uint8_t> list_body;
            for (const auto code : nl.recEntity) {
                encode_tlv(
                    list_body,
                    encode_int_field(TagClass::kApplication, MoCallTag::kRecEntityCode, code));
            }
            encode_tlv(nl_body,
                       make_seq_tag(GprsCallTag::kRecEntityCodeList, std::move(list_body)));
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
        encode_tlv(body, make_seq_tag(GprsCallTag::kGprsNetworkLocation, std::move(nl_body)));
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
    return make_seq_tag(GprsCallTag::kGprsLocationInformation, std::move(body));
}

std::optional<GprsLocationInformation> decode_gprs_location_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != GprsCallTag::kGprsLocationInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    GprsLocationInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, GprsCallTag::kGprsNetworkLocation)) {
        const auto sub = tcap_core::decode_tlvs(p[idx].value);
        if (sub.has_value()) {
            GprsNetworkLocation nl;
            std::size_t sidx = 0;
            const auto& sp = *sub;
            if (at_app_tag(sp, sidx, GprsCallTag::kRecEntityCodeList)) {
                const auto items = tcap_core::decode_tlvs(sp[sidx].value);
                if (items.has_value()) {
                    for (const auto& item : *items) {
                        if (auto v = decode_int_field(
                                item, TagClass::kApplication, MoCallTag::kRecEntityCode);
                            v.has_value()) {
                            nl.recEntity.push_back(*v);
                        }
                    }
                }
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
            out.gprsNetworkLocation = nl;
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

// --- GprsServiceUsed ---

Tlv encode_gprs_service_used(const GprsServiceUsed& v) {
    std::vector<std::uint8_t> body;
    if (v.imsSignallingContext.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    GprsCallTag::kIMSSignallingContext,
                                    *v.imsSignallingContext));
    }
    if (v.dataVolumeIncoming.has_value()) {
        encode_tlv(body,
                   encode_int64_field(TagClass::kApplication,
                                      GprsCallTag::kDataVolumeIncoming,
                                      *v.dataVolumeIncoming));
    }
    if (v.dataVolumeOutgoing.has_value()) {
        encode_tlv(body,
                   encode_int64_field(TagClass::kApplication,
                                      GprsCallTag::kDataVolumeOutgoing,
                                      *v.dataVolumeOutgoing));
    }
    if (!v.chargeInformationList.empty()) {
        encode_tlv(body, encode_charge_information_list(v.chargeInformationList));
    }
    return make_seq_tag(GprsCallTag::kGprsServiceUsed, std::move(body));
}

std::optional<GprsServiceUsed> decode_gprs_service_used(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != GprsCallTag::kGprsServiceUsed) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    GprsServiceUsed out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, GprsCallTag::kIMSSignallingContext)) {
        out.imsSignallingContext =
            decode_int_field(p[idx], TagClass::kApplication, GprsCallTag::kIMSSignallingContext);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kDataVolumeIncoming)) {
        out.dataVolumeIncoming =
            decode_int64_field(p[idx], TagClass::kApplication, GprsCallTag::kDataVolumeIncoming);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kDataVolumeOutgoing)) {
        out.dataVolumeOutgoing =
            decode_int64_field(p[idx], TagClass::kApplication, GprsCallTag::kDataVolumeOutgoing);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeInformationList)) {
        if (auto list = decode_charge_information_list(p[idx]); list.has_value()) {
            out.chargeInformationList = *list;
        }
        ++idx;
    }
    return out;
}

// --- GprsCall ---

Tlv encode_gprs_call(const GprsCall& v) {
    std::vector<std::uint8_t> body;
    if (v.gprsBasicCallInformation.has_value()) {
        encode_tlv(body, encode_gprs_basic_call_information(*v.gprsBasicCallInformation));
    }
    if (v.gprsLocationInformation.has_value()) {
        encode_tlv(body, encode_gprs_location_information(*v.gprsLocationInformation));
    }
    if (v.equipmentIdentifier.has_value()) {
        encode_tlv(body, encode_imei_or_esn(*v.equipmentIdentifier));
    }
    if (v.gprsServiceUsed.has_value()) {
        encode_tlv(body, encode_gprs_service_used(*v.gprsServiceUsed));
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
    return make_seq_tag(Tag::kGprsCall, std::move(body));
}

std::optional<GprsCall> decode_gprs_call(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kGprsCall) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    GprsCall out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, GprsCallTag::kGprsBasicCallInformation)) {
        out.gprsBasicCallInformation = decode_gprs_basic_call_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kGprsLocationInformation)) {
        out.gprsLocationInformation = decode_gprs_location_information(p[idx]);
        ++idx;
    }
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == MoCallTag::kImeiOrEsn) {
        out.equipmentIdentifier = decode_imei_or_esn(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, GprsCallTag::kGprsServiceUsed)) {
        out.gprsServiceUsed = decode_gprs_service_used(p[idx]);
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
