#include "tap3_core/tap3_content_transaction.hpp"

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

// Several real TAP3 types in this chain share an identical two-field shape (an INTEGER "type"
// code + an AsciiString "identifier"), each under its own real distinct outer/field tags
// (ChargedPartyIdentification, ChargedPartyHomeIdentification, ChargedPartyLocation,
// ChargedPartyEquipment, ContentProvider, InternetServiceProvider, ContentNetwork) -- shared here
// as one local helper rather than seven near-identical hand copies.
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

} // namespace

// --- ContentTransactionBasicInfo ---

namespace {
Tlv encode_content_transaction_basic_info(const ContentTransactionBasicInfo& v) {
    std::vector<std::uint8_t> body;
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.orderPlacedTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time(ContentTxTag::kOrderPlacedTimeStamp, *v.orderPlacedTimeStamp));
    }
    if (v.requestedDeliveryTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time(ContentTxTag::kRequestedDeliveryTimeStamp,
                                    *v.requestedDeliveryTimeStamp));
    }
    if (v.actualDeliveryTimeStamp.has_value()) {
        encode_tlv(
            body,
            encode_date_time(ContentTxTag::kActualDeliveryTimeStamp, *v.actualDeliveryTimeStamp));
    }
    if (v.totalTransactionDuration.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kTotalTransactionDuration,
                                    *v.totalTransactionDuration));
    }
    if (v.transactionStatus.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kTransactionStatus,
                                    *v.transactionStatus));
    }
    return make_seq_tag(ContentTxTag::kContentTransactionBasicInfo, std::move(body));
}

std::optional<ContentTransactionBasicInfo> decode_content_transaction_basic_info(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ContentTxTag::kContentTransactionBasicInfo) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ContentTransactionBasicInfo out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kOrderPlacedTimeStamp)) {
        out.orderPlacedTimeStamp = decode_date_time(p[idx], ContentTxTag::kOrderPlacedTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kRequestedDeliveryTimeStamp)) {
        out.requestedDeliveryTimeStamp =
            decode_date_time(p[idx], ContentTxTag::kRequestedDeliveryTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kActualDeliveryTimeStamp)) {
        out.actualDeliveryTimeStamp =
            decode_date_time(p[idx], ContentTxTag::kActualDeliveryTimeStamp);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTotalTransactionDuration)) {
        out.totalTransactionDuration = decode_int_field(
            p[idx], TagClass::kApplication, ContentTxTag::kTotalTransactionDuration);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTransactionStatus)) {
        out.transactionStatus =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kTransactionStatus);
        ++idx;
    }
    return out;
}

// --- ChargedPartyInformation chain ---

Tlv encode_charged_party_information(const ChargedPartyInformation& v) {
    std::vector<std::uint8_t> body;
    if (!v.chargedPartyIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.chargedPartyIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kChargedPartyIdentification,
                                           ContentTxTag::kChargedPartyIdType,
                                           item.chargedPartyIdType,
                                           ContentTxTag::kChargedPartyIdentifier,
                                           item.chargedPartyIdentifier));
        }
        encode_tlv(body, make_seq_tag(ContentTxTag::kChargedPartyIdList, std::move(list_body)));
    }
    if (!v.chargedPartyHomeIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.chargedPartyHomeIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kChargedPartyHomeIdentification,
                                           ContentTxTag::kHomeIdType,
                                           item.homeIdType,
                                           ContentTxTag::kHomeIdentifier,
                                           item.homeIdentifier));
        }
        encode_tlv(body, make_seq_tag(ContentTxTag::kChargedPartyHomeIdList, std::move(list_body)));
    }
    if (!v.chargedPartyLocationList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.chargedPartyLocationList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kChargedPartyLocation,
                                           ContentTxTag::kLocationIdType,
                                           item.locationIdType,
                                           ContentTxTag::kLocationIdentifier,
                                           item.locationIdentifier));
        }
        encode_tlv(body,
                   make_seq_tag(ContentTxTag::kChargedPartyLocationList, std::move(list_body)));
    }
    if (v.chargedPartyEquipment.has_value()) {
        encode_tlv(body,
                   encode_type_id_pair(ContentTxTag::kChargedPartyEquipment,
                                       ContentTxTag::kEquipmentIdType,
                                       v.chargedPartyEquipment->equipmentIdType,
                                       ContentTxTag::kEquipmentId,
                                       v.chargedPartyEquipment->equipmentId));
    }
    return make_seq_tag(ContentTxTag::kChargedPartyInformation, std::move(body));
}

std::optional<ChargedPartyInformation> decode_charged_party_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ContentTxTag::kChargedPartyInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ChargedPartyInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ContentTxTag::kChargedPartyIdList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto pair = decode_type_id_pair(item,
                                                    ContentTxTag::kChargedPartyIdentification,
                                                    ContentTxTag::kChargedPartyIdType,
                                                    ContentTxTag::kChargedPartyIdentifier);
                    pair.has_value()) {
                    out.chargedPartyIdList.push_back(
                        ChargedPartyIdentification{pair->first, pair->second});
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kChargedPartyHomeIdList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto pair = decode_type_id_pair(item,
                                                    ContentTxTag::kChargedPartyHomeIdentification,
                                                    ContentTxTag::kHomeIdType,
                                                    ContentTxTag::kHomeIdentifier);
                    pair.has_value()) {
                    out.chargedPartyHomeIdList.push_back(
                        ChargedPartyHomeIdentification{pair->first, pair->second});
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kChargedPartyLocationList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto pair = decode_type_id_pair(item,
                                                    ContentTxTag::kChargedPartyLocation,
                                                    ContentTxTag::kLocationIdType,
                                                    ContentTxTag::kLocationIdentifier);
                    pair.has_value()) {
                    out.chargedPartyLocationList.push_back(
                        ChargedPartyLocation{pair->first, pair->second});
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kChargedPartyEquipment)) {
        if (auto pair = decode_type_id_pair(p[idx],
                                            ContentTxTag::kChargedPartyEquipment,
                                            ContentTxTag::kEquipmentIdType,
                                            ContentTxTag::kEquipmentId);
            pair.has_value()) {
            out.chargedPartyEquipment = ChargedPartyEquipment{pair->first, pair->second};
        }
        ++idx;
    }
    return out;
}

// --- ServingPartiesInformation chain ---

Tlv encode_serving_parties_information(const ServingPartiesInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.contentProviderName.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ContentTxTag::kContentProviderName,
                                       *v.contentProviderName));
    }
    if (!v.contentProviderIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.contentProviderIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kContentProvider,
                                           ContentTxTag::kContentProviderIdType,
                                           item.contentProviderIdType,
                                           ContentTxTag::kContentProviderIdentifier,
                                           item.contentProviderIdentifier));
        }
        encode_tlv(body, make_seq_tag(ContentTxTag::kContentProviderIdList, std::move(list_body)));
    }
    if (!v.internetServiceProviderIdList.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.internetServiceProviderIdList) {
            encode_tlv(list_body,
                       encode_type_id_pair(ContentTxTag::kInternetServiceProvider,
                                           ContentTxTag::kIspIdType,
                                           item.ispIdType,
                                           ContentTxTag::kIspIdentifier,
                                           item.ispIdentifier));
        }
        encode_tlv(
            body, make_seq_tag(ContentTxTag::kInternetServiceProviderIdList, std::move(list_body)));
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
    return make_seq_tag(ContentTxTag::kServingPartiesInformation, std::move(body));
}

std::optional<ServingPartiesInformation> decode_serving_parties_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ContentTxTag::kServingPartiesInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ServingPartiesInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ContentTxTag::kContentProviderName)) {
        out.contentProviderName =
            decode_string_field(p[idx], TagClass::kApplication, ContentTxTag::kContentProviderName);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kContentProviderIdList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto pair = decode_type_id_pair(item,
                                                    ContentTxTag::kContentProvider,
                                                    ContentTxTag::kContentProviderIdType,
                                                    ContentTxTag::kContentProviderIdentifier);
                    pair.has_value()) {
                    out.contentProviderIdList.push_back(ContentProvider{pair->first, pair->second});
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kInternetServiceProviderIdList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto pair = decode_type_id_pair(item,
                                                    ContentTxTag::kInternetServiceProvider,
                                                    ContentTxTag::kIspIdType,
                                                    ContentTxTag::kIspIdentifier);
                    pair.has_value()) {
                    out.internetServiceProviderIdList.push_back(
                        InternetServiceProvider{pair->first, pair->second});
                }
            }
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kNetworkList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto pair = decode_type_id_pair(item,
                                                    ContentTxTag::kNetwork,
                                                    ContentTxTag::kNetworkIdType,
                                                    ContentTxTag::kNetworkIdentifier);
                    pair.has_value()) {
                    out.networkList.push_back(ContentNetwork{pair->first, pair->second});
                }
            }
        }
        ++idx;
    }
    return out;
}

Tlv encode_advised_charge_information(const AdvisedChargeInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.paidIndicator.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ContentTxTag::kPaidIndicator, *v.paidIndicator));
    }
    if (v.paymentMethod.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ContentTxTag::kPaymentMethod, *v.paymentMethod));
    }
    if (v.advisedChargeCurrency.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ContentTxTag::kAdvisedChargeCurrency,
                                       *v.advisedChargeCurrency));
    }
    if (v.advisedCharge.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, ContentTxTag::kAdvisedCharge, *v.advisedCharge));
    }
    if (v.commission.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, ContentTxTag::kCommission, *v.commission));
    }
    return make_seq_tag(ContentTxTag::kAdvisedChargeInformation, std::move(body));
}

std::optional<AdvisedChargeInformation> decode_advised_charge_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ContentTxTag::kAdvisedChargeInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    AdvisedChargeInformation out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ContentTxTag::kPaidIndicator)) {
        out.paidIndicator =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kPaidIndicator);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kPaymentMethod)) {
        out.paymentMethod =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kPaymentMethod);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kAdvisedChargeCurrency)) {
        out.advisedChargeCurrency = decode_string_field(
            p[idx], TagClass::kApplication, ContentTxTag::kAdvisedChargeCurrency);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kAdvisedCharge)) {
        out.advisedCharge =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kAdvisedCharge);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kCommission)) {
        out.commission =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kCommission);
        ++idx;
    }
    return out;
}

} // namespace

// --- ContentServiceUsed ---

Tlv encode_content_service_used(const ContentServiceUsed& v) {
    std::vector<std::uint8_t> body;
    if (v.contentTransactionCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kContentTransactionCode,
                                    *v.contentTransactionCode));
    }
    if (v.contentTransactionType.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kContentTransactionType,
                                    *v.contentTransactionType));
    }
    if (v.objectType.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, ContentTxTag::kObjectType, *v.objectType));
    }
    if (v.transactionDescriptionSupp.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kTransactionDescriptionSupp,
                                    *v.transactionDescriptionSupp));
    }
    if (v.transactionShortDescription.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ContentTxTag::kTransactionShortDescription,
                                       *v.transactionShortDescription));
    }
    if (v.transactionDetailDescription.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ContentTxTag::kTransactionDetailDescription,
                                       *v.transactionDetailDescription));
    }
    if (v.transactionIdentifier.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ContentTxTag::kTransactionIdentifier,
                                       *v.transactionIdentifier));
    }
    if (v.transactionAuthCode.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       ContentTxTag::kTransactionAuthCode,
                                       *v.transactionAuthCode));
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
    if (v.totalDataVolume.has_value()) {
        encode_tlv(body,
                   encode_int64_field(
                       TagClass::kApplication, ContentTxTag::kTotalDataVolume, *v.totalDataVolume));
    }
    if (v.chargeRefundIndicator.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kChargeRefundIndicator,
                                    *v.chargeRefundIndicator));
    }
    if (v.contentChargingPoint.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    ContentTxTag::kContentChargingPoint,
                                    *v.contentChargingPoint));
    }
    if (!v.chargeInformationList.empty()) {
        encode_tlv(body, encode_charge_information_list(v.chargeInformationList));
    }
    if (v.advisedChargeInformation.has_value()) {
        encode_tlv(body, encode_advised_charge_information(*v.advisedChargeInformation));
    }
    return make_seq_tag(ContentTxTag::kContentServiceUsed, std::move(body));
}

std::optional<ContentServiceUsed> decode_content_service_used(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication ||
        tlv.tag_number != ContentTxTag::kContentServiceUsed) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ContentServiceUsed out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ContentTxTag::kContentTransactionCode)) {
        out.contentTransactionCode =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kContentTransactionCode);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kContentTransactionType)) {
        out.contentTransactionType =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kContentTransactionType);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kObjectType)) {
        out.objectType =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kObjectType);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTransactionDescriptionSupp)) {
        out.transactionDescriptionSupp = decode_int_field(
            p[idx], TagClass::kApplication, ContentTxTag::kTransactionDescriptionSupp);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTransactionShortDescription)) {
        out.transactionShortDescription = decode_string_field(
            p[idx], TagClass::kApplication, ContentTxTag::kTransactionShortDescription);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTransactionDetailDescription)) {
        out.transactionDetailDescription = decode_string_field(
            p[idx], TagClass::kApplication, ContentTxTag::kTransactionDetailDescription);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTransactionIdentifier)) {
        out.transactionIdentifier = decode_string_field(
            p[idx], TagClass::kApplication, ContentTxTag::kTransactionIdentifier);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kTransactionAuthCode)) {
        out.transactionAuthCode =
            decode_string_field(p[idx], TagClass::kApplication, ContentTxTag::kTransactionAuthCode);
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
    if (at_app_tag(p, idx, ContentTxTag::kTotalDataVolume)) {
        out.totalDataVolume =
            decode_int64_field(p[idx], TagClass::kApplication, ContentTxTag::kTotalDataVolume);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kChargeRefundIndicator)) {
        out.chargeRefundIndicator =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kChargeRefundIndicator);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kContentChargingPoint)) {
        out.contentChargingPoint =
            decode_int_field(p[idx], TagClass::kApplication, ContentTxTag::kContentChargingPoint);
        ++idx;
    }
    if (at_app_tag(p, idx, ChargingTag::kChargeInformationList)) {
        if (auto list = decode_charge_information_list(p[idx]); list.has_value()) {
            out.chargeInformationList = *list;
        }
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kAdvisedChargeInformation)) {
        out.advisedChargeInformation = decode_advised_charge_information(p[idx]);
        ++idx;
    }
    return out;
}

// --- ContentTransaction ---

Tlv encode_content_transaction(const ContentTransaction& v) {
    std::vector<std::uint8_t> body;
    if (v.contentTransactionBasicInfo.has_value()) {
        encode_tlv(body, encode_content_transaction_basic_info(*v.contentTransactionBasicInfo));
    }
    if (v.chargedPartyInformation.has_value()) {
        encode_tlv(body, encode_charged_party_information(*v.chargedPartyInformation));
    }
    if (v.servingPartiesInformation.has_value()) {
        encode_tlv(body, encode_serving_parties_information(*v.servingPartiesInformation));
    }
    if (!v.contentServiceUsed.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& item : v.contentServiceUsed) {
            encode_tlv(list_body, encode_content_service_used(item));
        }
        encode_tlv(body, make_seq_tag(ContentTxTag::kContentServiceUsedList, std::move(list_body)));
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
    return make_seq_tag(Tag::kContentTransaction, std::move(body));
}

std::optional<ContentTransaction> decode_content_transaction(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kContentTransaction) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    ContentTransaction out;
    std::size_t idx = 0;
    if (at_app_tag(p, idx, ContentTxTag::kContentTransactionBasicInfo)) {
        out.contentTransactionBasicInfo = decode_content_transaction_basic_info(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kChargedPartyInformation)) {
        out.chargedPartyInformation = decode_charged_party_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kServingPartiesInformation)) {
        out.servingPartiesInformation = decode_serving_parties_information(p[idx]);
        ++idx;
    }
    if (at_app_tag(p, idx, ContentTxTag::kContentServiceUsedList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto csu = decode_content_service_used(item); csu.has_value()) {
                    out.contentServiceUsed.push_back(*csu);
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
