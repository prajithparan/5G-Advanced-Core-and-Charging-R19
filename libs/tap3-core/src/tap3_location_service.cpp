#include "tap3_core/tap3_location_service.hpp"

#include <utility>

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

Tlv encode_type_id_pair(std::uint32_t outer_tag,
                        std::uint32_t type_tag,
                        const std::optional<std::int32_t>& type_val,
                        std::uint32_t id_tag,
                        const std::optional<std::string>& id_val) {
    std::vector<std::uint8_t> body;
    if (type_val.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, type_tag, *type_val));
    }
    if (id_val.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, id_tag, *id_val));
    }
    return make_seq_tag(outer_tag, std::move(body));
}

std::optional<std::pair<std::optional<std::int32_t>, std::optional<std::string>>>
decode_type_id_pair(const Tlv& tlv,
                    std::uint32_t outer_tag,
                    std::uint32_t type_tag,
                    std::uint32_t id_tag) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != outer_tag) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    std::optional<std::int32_t> type_val;
    std::optional<std::string> id_val;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, type_tag)) {
        type_val = decode_int_field(p[idx], TagClass::kApplication, type_tag);
        ++idx;
    }
    if (at_app_tag(p, idx, id_tag)) {
        id_val = decode_string_field(p[idx], TagClass::kApplication, id_tag);
        ++idx;
    }
    return std::make_pair(type_val, id_val);
}

template <typename T>
void decode_pair_list(const Tlv& list_tlv,
                      std::uint32_t item_tag,
                      std::uint32_t type_tag,
                      std::uint32_t id_tag,
                      std::vector<T>& out) {
    const auto items = tcap_core::decode_tlvs(list_tlv.value);
    if (!items.has_value()) {
        return;
    }
    for (const auto& item : *items) {
        if (auto pair = decode_type_id_pair(item, item_tag, type_tag, id_tag); pair.has_value()) {
            out.push_back(T{pair->first, pair->second});
        }
    }
}

} // namespace

// --- TrackingCustomerInformation ---

Tlv encode_tracking_customer_information(const TrackingCustomerInformation& v) {
    std::vector<std::uint8_t> body;
    if (!v.trackingCustomerIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.trackingCustomerIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kTrackingCustomerIdentification,
                                           LocSvcTag::kCustomerIdType,
                                           item.customerIdType,
                                           LocSvcTag::kCustomerIdentifier,
                                           item.customerIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kTrackingCustomerIdList, std::move(list_body)));
    }
    if (!v.trackingCustomerHomeIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.trackingCustomerHomeIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kTrackingCustomerHomeId,
                                           ContentTxTag::kHomeIdType,
                                           item.homeIdType,
                                           ContentTxTag::kHomeIdentifier,
                                           item.homeIdentifier));
        }
        encode_tlv(body,
                   make_seq_tag(LocSvcTag::kTrackingCustomerHomeIdList, std::move(list_body)));
    }
    if (!v.trackingCustomerLocList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.trackingCustomerLocList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kTrackingCustomerLocation,
                                           ContentTxTag::kLocationIdType,
                                           item.locationIdType,
                                           ContentTxTag::kLocationIdentifier,
                                           item.locationIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kTrackingCustomerLocList, std::move(list_body)));
    }
    if (v.trackingCustomerEquipment.has_value()) {
        encode_tlv(body,
                   encode_type_id_pair(LocSvcTag::kTrackingCustomerEquipment,
                                       ContentTxTag::kEquipmentIdType,
                                       v.trackingCustomerEquipment->equipmentIdType,
                                       ContentTxTag::kEquipmentId,
                                       v.trackingCustomerEquipment->equipmentId));
    }
    return make_seq_tag(LocSvcTag::kTrackingCustomerInformation, std::move(body));
}

std::optional<TrackingCustomerInformation> decode_tracking_customer_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != LocSvcTag::kTrackingCustomerInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    TrackingCustomerInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, LocSvcTag::kTrackingCustomerIdList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kTrackingCustomerIdentification,
                         LocSvcTag::kCustomerIdType,
                         LocSvcTag::kCustomerIdentifier,
                         out.trackingCustomerIdList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingCustomerHomeIdList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kTrackingCustomerHomeId,
                         ContentTxTag::kHomeIdType,
                         ContentTxTag::kHomeIdentifier,
                         out.trackingCustomerHomeIdList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingCustomerLocList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kTrackingCustomerLocation,
                         ContentTxTag::kLocationIdType,
                         ContentTxTag::kLocationIdentifier,
                         out.trackingCustomerLocList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingCustomerEquipment)) {
        if (auto pair = decode_type_id_pair(p[idx],
                                            LocSvcTag::kTrackingCustomerEquipment,
                                            ContentTxTag::kEquipmentIdType,
                                            ContentTxTag::kEquipmentId);
            pair.has_value()) {
            out.trackingCustomerEquipment = TrackingCustomerEquipment{pair->first, pair->second};
        }
        ++idx;
    }
    return out;
}

// --- LCSSPInformation ---

Tlv encode_lcssp_information(const LCSSPInformation& v) {
    std::vector<std::uint8_t> body;
    if (!v.lcsspIdentificationList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.lcsspIdentificationList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kLCSSPIdentification,
                                           ContentTxTag::kContentProviderIdType,
                                           item.contentProviderIdType,
                                           ContentTxTag::kContentProviderIdentifier,
                                           item.contentProviderIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kLCSSPIdentificationList, std::move(list_body)));
    }
    if (!v.ispList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.ispList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kInternetServiceProvider,
                                           ContentTxTag::kIspIdType,
                                           item.ispIdType,
                                           ContentTxTag::kIspIdentifier,
                                           item.ispIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kISPList, std::move(list_body)));
    }
    if (!v.networkList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.networkList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kNetwork,
                                           ContentTxTag::kNetworkIdType,
                                           item.networkIdType,
                                           ContentTxTag::kNetworkIdentifier,
                                           item.networkIdentifier));
        }
        encode_tlv(body, make_seq_tag(ContentTxTag::kNetworkList, std::move(list_body)));
    }
    return make_seq_tag(LocSvcTag::kLCSSPInformation, std::move(body));
}

std::optional<LCSSPInformation> decode_lcssp_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != LocSvcTag::kLCSSPInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    LCSSPInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, LocSvcTag::kLCSSPIdentificationList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kLCSSPIdentification,
                         ContentTxTag::kContentProviderIdType,
                         ContentTxTag::kContentProviderIdentifier,
                         out.lcsspIdentificationList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kISPList)) {
        decode_pair_list(p[idx],
                         ContentTxTag::kInternetServiceProvider,
                         ContentTxTag::kIspIdType,
                         ContentTxTag::kIspIdentifier,
                         out.ispList);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kNetworkList)) {
        decode_pair_list(p[idx],
                         ContentTxTag::kNetwork,
                         ContentTxTag::kNetworkIdType,
                         ContentTxTag::kNetworkIdentifier,
                         out.networkList);
        ++idx;
    }
    return out;
}

// --- TrackedCustomerInformation ---

Tlv encode_tracked_customer_information(const TrackedCustomerInformation& v) {
    std::vector<std::uint8_t> body;
    if (!v.trackedCustomerIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.trackedCustomerIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kTrackedCustomerIdentification,
                                           LocSvcTag::kCustomerIdType,
                                           item.customerIdType,
                                           LocSvcTag::kCustomerIdentifier,
                                           item.customerIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kTrackedCustomerIdList, std::move(list_body)));
    }
    if (!v.trackedCustomerHomeIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.trackedCustomerHomeIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kTrackedCustomerHomeId,
                                           ContentTxTag::kHomeIdType,
                                           item.homeIdType,
                                           ContentTxTag::kHomeIdentifier,
                                           item.homeIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kTrackedCustomerHomeIdList, std::move(list_body)));
    }
    if (!v.trackedCustomerLocList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.trackedCustomerLocList) {
            encode_tlv(list_body,
                       encode_type_id_pair(LocSvcTag::kTrackedCustomerLocation,
                                           ContentTxTag::kLocationIdType,
                                           item.locationIdType,
                                           ContentTxTag::kLocationIdentifier,
                                           item.locationIdentifier));
        }
        encode_tlv(body, make_seq_tag(LocSvcTag::kTrackedCustomerLocList, std::move(list_body)));
    }
    if (v.trackedCustomerEquipment.has_value()) {
        encode_tlv(body,
                   encode_type_id_pair(LocSvcTag::kTrackedCustomerEquipment,
                                       ContentTxTag::kEquipmentIdType,
                                       v.trackedCustomerEquipment->equipmentIdType,
                                       ContentTxTag::kEquipmentId,
                                       v.trackedCustomerEquipment->equipmentId));
    }
    return make_seq_tag(LocSvcTag::kTrackedCustomerInformation, std::move(body));
}

std::optional<TrackedCustomerInformation> decode_tracked_customer_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != LocSvcTag::kTrackedCustomerInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    TrackedCustomerInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, LocSvcTag::kTrackedCustomerIdList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kTrackedCustomerIdentification,
                         LocSvcTag::kCustomerIdType,
                         LocSvcTag::kCustomerIdentifier,
                         out.trackedCustomerIdList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackedCustomerHomeIdList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kTrackedCustomerHomeId,
                         ContentTxTag::kHomeIdType,
                         ContentTxTag::kHomeIdentifier,
                         out.trackedCustomerHomeIdList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackedCustomerLocList)) {
        decode_pair_list(p[idx],
                         LocSvcTag::kTrackedCustomerLocation,
                         ContentTxTag::kLocationIdType,
                         ContentTxTag::kLocationIdentifier,
                         out.trackedCustomerLocList);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackedCustomerEquipment)) {
        if (auto pair = decode_type_id_pair(p[idx],
                                            LocSvcTag::kTrackedCustomerEquipment,
                                            ContentTxTag::kEquipmentIdType,
                                            ContentTxTag::kEquipmentId);
            pair.has_value()) {
            out.trackedCustomerEquipment = TrackedCustomerEquipment{pair->first, pair->second};
        }
        ++idx;
    }
    return out;
}

// --- LocationServiceUsage ---

Tlv encode_lcs_qos_requested(const LCSQosRequested& v) {
    std::vector<std::uint8_t> body;
    if (v.lcsRequestTimestamp.has_value()) {
        encode_tlv(body, encode_date_time(LocSvcTag::kLCSRequestTimestamp, *v.lcsRequestTimestamp));
    }
    if (v.horizontalAccuracyRequested.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kHorizontalAccuracyRequested,
                                    *v.horizontalAccuracyRequested));
    }
    if (v.verticalAccuracyRequested.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kVerticalAccuracyRequested,
                                    *v.verticalAccuracyRequested));
    }
    if (v.responseTimeCategory.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kResponseTimeCategory,
                                    *v.responseTimeCategory));
    }
    if (v.trackingPeriod.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, LocSvcTag::kTrackingPeriod, *v.trackingPeriod));
    }
    if (v.trackingFrequency.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kTrackingFrequency,
                                    *v.trackingFrequency));
    }
    return make_seq_tag(LocSvcTag::kLCSQosRequested, std::move(body));
}

std::optional<LCSQosRequested> decode_lcs_qos_requested(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != LocSvcTag::kLCSQosRequested) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    LCSQosRequested out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, LocSvcTag::kLCSRequestTimestamp)) {
        out.lcsRequestTimestamp = decode_date_time(p[idx], LocSvcTag::kLCSRequestTimestamp);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kHorizontalAccuracyRequested)) {
        out.horizontalAccuracyRequested = decode_int_field(
            p[idx], TagClass::kApplication, LocSvcTag::kHorizontalAccuracyRequested);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kVerticalAccuracyRequested)) {
        out.verticalAccuracyRequested =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kVerticalAccuracyRequested);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kResponseTimeCategory)) {
        out.responseTimeCategory =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kResponseTimeCategory);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingPeriod)) {
        out.trackingPeriod =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kTrackingPeriod);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingFrequency)) {
        out.trackingFrequency =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kTrackingFrequency);
        ++idx;
    }
    return out;
}

Tlv encode_lcs_qos_delivered(const LCSQosDelivered& v) {
    std::vector<std::uint8_t> body;
    if (v.lcsTransactionStatus.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kLCSTransactionStatus,
                                    *v.lcsTransactionStatus));
    }
    if (v.horizontalAccuracyDelivered.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kHorizontalAccuracyDelivered,
                                    *v.horizontalAccuracyDelivered));
    }
    if (v.verticalAccuracyDelivered.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kVerticalAccuracyDelivered,
                                    *v.verticalAccuracyDelivered));
    }
    if (v.responseTime.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, LocSvcTag::kResponseTime, *v.responseTime));
    }
    if (v.positioningMethod.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kPositioningMethod,
                                    *v.positioningMethod));
    }
    if (v.trackingPeriod.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, LocSvcTag::kTrackingPeriod, *v.trackingPeriod));
    }
    if (v.trackingFrequency.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    LocSvcTag::kTrackingFrequency,
                                    *v.trackingFrequency));
    }
    if (v.ageOfLocation.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, LocSvcTag::kAgeOfLocation, *v.ageOfLocation));
    }
    return make_seq_tag(LocSvcTag::kLCSQosDelivered, std::move(body));
}

std::optional<LCSQosDelivered> decode_lcs_qos_delivered(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != LocSvcTag::kLCSQosDelivered) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    LCSQosDelivered out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, LocSvcTag::kLCSTransactionStatus)) {
        out.lcsTransactionStatus =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kLCSTransactionStatus);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kHorizontalAccuracyDelivered)) {
        out.horizontalAccuracyDelivered = decode_int_field(
            p[idx], TagClass::kApplication, LocSvcTag::kHorizontalAccuracyDelivered);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kVerticalAccuracyDelivered)) {
        out.verticalAccuracyDelivered =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kVerticalAccuracyDelivered);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kResponseTime)) {
        out.responseTime =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kResponseTime);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kPositioningMethod)) {
        out.positioningMethod =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kPositioningMethod);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingPeriod)) {
        out.trackingPeriod =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kTrackingPeriod);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingFrequency)) {
        out.trackingFrequency =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kTrackingFrequency);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kAgeOfLocation)) {
        out.ageOfLocation =
            decode_int_field(p[idx], TagClass::kApplication, LocSvcTag::kAgeOfLocation);
        ++idx;
    }
    return out;
}

Tlv encode_location_service_usage(const LocationServiceUsage& v) {
    std::vector<std::uint8_t> body;
    if (v.lcsQosRequested.has_value()) {
        encode_tlv(body, encode_lcs_qos_requested(*v.lcsQosRequested));
    }
    if (v.lcsQosDelivered.has_value()) {
        encode_tlv(body, encode_lcs_qos_delivered(*v.lcsQosDelivered));
    }
    if (v.chargingTimeStamp.has_value()) {
        encode_tlv(body, encode_date_time(MoCallTag::kChargingTimeStamp, *v.chargingTimeStamp));
    }
    if (!v.chargeInformationList.empty()) {
        encode_tlv(body, encode_charge_information_list(v.chargeInformationList));
    }
    return make_seq_tag(LocSvcTag::kLocationServiceUsage, std::move(body));
}

std::optional<LocationServiceUsage> decode_location_service_usage(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != LocSvcTag::kLocationServiceUsage) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    LocationServiceUsage out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, LocSvcTag::kLCSQosRequested)) {
        out.lcsQosRequested = decode_lcs_qos_requested(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kLCSQosDelivered)) {
        out.lcsQosDelivered = decode_lcs_qos_delivered(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kChargingTimeStamp)) {
        out.chargingTimeStamp = decode_date_time(p[idx], MoCallTag::kChargingTimeStamp);
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

// --- LocationService ---

Tlv encode_location_service(const LocationService& v) {
    std::vector<std::uint8_t> body;
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.recEntityCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, MoCallTag::kRecEntityCode, *v.recEntityCode));
    }
    if (v.callReference.has_value()) {
        Tlv cr;
        cr.tag_class = TagClass::kApplication;
        cr.constructed = false;
        cr.tag_number = LocSvcTag::kCallReference;
        cr.value = *v.callReference;
        encode_tlv(body, cr);
    }
    if (v.trackingCustomerInformation.has_value()) {
        encode_tlv(body, encode_tracking_customer_information(*v.trackingCustomerInformation));
    }
    if (v.lcsspInformation.has_value()) {
        encode_tlv(body, encode_lcssp_information(*v.lcsspInformation));
    }
    if (v.trackedCustomerInformation.has_value()) {
        encode_tlv(body, encode_tracked_customer_information(*v.trackedCustomerInformation));
    }
    if (v.locationServiceUsage.has_value()) {
        encode_tlv(body, encode_location_service_usage(*v.locationServiceUsage));
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
    return make_seq_tag(LocSvcTag::kLocationService, std::move(body));
}

std::optional<LocationService> decode_location_service(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != LocSvcTag::kLocationService) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    LocationService out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, MoCallTag::kRecEntityCode)) {
        out.recEntityCode =
            decode_int_field(p[idx], TagClass::kApplication, MoCallTag::kRecEntityCode);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kCallReference)) {
        out.callReference = p[idx].value;
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackingCustomerInformation)) {
        out.trackingCustomerInformation = decode_tracking_customer_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kLCSSPInformation)) {
        out.lcsspInformation = decode_lcssp_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kTrackedCustomerInformation)) {
        out.trackedCustomerInformation = decode_tracked_customer_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, LocSvcTag::kLocationServiceUsage)) {
        out.locationServiceUsage = decode_location_service_usage(p[idx]);
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
