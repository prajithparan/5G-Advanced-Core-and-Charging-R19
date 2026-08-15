#include "tap3_core/tap3_envelope.hpp"

namespace tap3_core {

namespace {

// Same real ordered-optional-SEQUENCE decode pattern this project's own map_operations.cpp/
// cap_operations.cpp already established: a forward cursor over decode_tlvs()'s output, checking
// each real expected field IN THE SEQUENCE'S OWN DEFINED ORDER (ASN.1 requires OPTIONAL SEQUENCE
// components to appear in declared order when present, so a present field can never appear before
// an absent one that's declared earlier) -- consuming and advancing on a tag match, leaving the
// cursor alone (field absent) otherwise.
bool at_tag(const std::vector<Tlv>& parts, std::size_t idx, std::uint32_t tag_number) {
    return idx < parts.size() && parts[idx].tag_class == TagClass::kApplication &&
           parts[idx].tag_number == tag_number;
}

} // namespace

// --- BatchControlInfo ---

Tlv encode_batch_control_info(const BatchControlInfo& v) {
    std::vector<std::uint8_t> body;
    if (v.sender.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, Tag::kSender, *v.sender));
    }
    if (v.recipient.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, Tag::kRecipient, *v.recipient));
    }
    if (v.fileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, Tag::kFileSequenceNumber, *v.fileSequenceNumber));
    }
    if (v.fileCreationTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time_long(Tag::kFileCreationTimeStamp, *v.fileCreationTimeStamp));
    }
    if (v.transferCutOffTimeStamp.has_value()) {
        encode_tlv(
            body, encode_date_time_long(Tag::kTransferCutOffTimeStamp, *v.transferCutOffTimeStamp));
    }
    if (v.fileAvailableTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time_long(Tag::kFileAvailableTimeStamp, *v.fileAvailableTimeStamp));
    }
    if (v.specificationVersionNumber.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    Tag::kSpecificationVersionNumber,
                                    *v.specificationVersionNumber));
    }
    if (v.releaseVersionNumber.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    Tag::kReleaseVersionNumber,
                                    *v.releaseVersionNumber));
    }
    if (v.fileTypeIndicator.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, Tag::kFileTypeIndicator, *v.fileTypeIndicator));
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
            encode_tlv(
                list_body,
                encode_string_field(TagClass::kApplication, Tag::kOperatorSpecInformation, s));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kOperatorSpecInfoList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }

    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kBatchControlInfo;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<BatchControlInfo> decode_batch_control_info(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kBatchControlInfo) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    BatchControlInfo out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kSender)) {
        out.sender = decode_string_field(p[idx], TagClass::kApplication, Tag::kSender);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRecipient)) {
        out.recipient = decode_string_field(p[idx], TagClass::kApplication, Tag::kRecipient);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileSequenceNumber)) {
        out.fileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kFileSequenceNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileCreationTimeStamp)) {
        out.fileCreationTimeStamp = decode_date_time_long(p[idx], Tag::kFileCreationTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTransferCutOffTimeStamp)) {
        out.transferCutOffTimeStamp = decode_date_time_long(p[idx], Tag::kTransferCutOffTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileAvailableTimeStamp)) {
        out.fileAvailableTimeStamp = decode_date_time_long(p[idx], Tag::kFileAvailableTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kSpecificationVersionNumber)) {
        out.specificationVersionNumber =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kSpecificationVersionNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kReleaseVersionNumber)) {
        out.releaseVersionNumber =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kReleaseVersionNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileTypeIndicator)) {
        out.fileTypeIndicator =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kFileTypeIndicator);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kOperatorSpecInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto s = decode_string_field(
                        item, TagClass::kApplication, Tag::kOperatorSpecInformation);
                    s.has_value()) {
                    out.operatorSpecInformation.push_back(*s);
                }
            }
        }
        ++idx;
    }
    return out;
}

// --- Taxation / Discounting / CurrencyConversion (AccountingInfo children) ---

Tlv encode_taxation(const Taxation& v) {
    std::vector<std::uint8_t> body;
    if (v.taxCode.has_value()) {
        encode_tlv(body, encode_int_field(TagClass::kApplication, Tag::kTaxCode, *v.taxCode));
    }
    if (v.taxType.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, Tag::kTaxType, *v.taxType));
    }
    if (v.taxRate.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, Tag::kTaxRate, *v.taxRate));
    }
    if (v.chargeType.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, Tag::kChargeType, *v.chargeType));
    }
    if (v.taxIndicator.has_value()) {
        encode_tlv(
            body, encode_string_field(TagClass::kApplication, Tag::kTaxIndicator, *v.taxIndicator));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kTaxation;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<Taxation> decode_taxation(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kTaxation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    Taxation out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kTaxCode)) {
        out.taxCode = decode_int_field(p[idx], TagClass::kApplication, Tag::kTaxCode);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTaxType)) {
        out.taxType = decode_string_field(p[idx], TagClass::kApplication, Tag::kTaxType);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTaxRate)) {
        out.taxRate = decode_string_field(p[idx], TagClass::kApplication, Tag::kTaxRate);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kChargeType)) {
        out.chargeType = decode_string_field(p[idx], TagClass::kApplication, Tag::kChargeType);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTaxIndicator)) {
        out.taxIndicator = decode_string_field(p[idx], TagClass::kApplication, Tag::kTaxIndicator);
        ++idx;
    }
    return out;
}

Tlv encode_discount_applied(const DiscountApplied& v) {
    Tlv inner = v.isFixedValue
                    ? encode_int_field(TagClass::kApplication, Tag::kFixedDiscountValue, v.value)
                    : encode_int_field(TagClass::kApplication, Tag::kDiscountRate, v.value);
    return wrap_explicit(TagClass::kApplication, Tag::kDiscountApplied, inner);
}

std::optional<DiscountApplied> decode_discount_applied(const Tlv& tlv) {
    const auto inner = unwrap_explicit(tlv, TagClass::kApplication, Tag::kDiscountApplied);
    if (!inner.has_value()) {
        return std::nullopt;
    }
    DiscountApplied out;
    if (inner->tag_class == TagClass::kApplication &&
        inner->tag_number == Tag::kFixedDiscountValue) {
        out.isFixedValue = true;
        const auto v = tcap_core::decode_integer(inner->value);
        if (!v.has_value()) {
            return std::nullopt;
        }
        out.value = *v;
        return out;
    }
    if (inner->tag_class == TagClass::kApplication && inner->tag_number == Tag::kDiscountRate) {
        out.isFixedValue = false;
        const auto v = tcap_core::decode_integer(inner->value);
        if (!v.has_value()) {
            return std::nullopt;
        }
        out.value = *v;
        return out;
    }
    return std::nullopt;
}

Tlv encode_discounting(const Discounting& v) {
    std::vector<std::uint8_t> body;
    if (v.discountCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kDiscountCode, *v.discountCode));
    }
    if (v.discountApplied.has_value()) {
        encode_tlv(body, encode_discount_applied(*v.discountApplied));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kDiscounting;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<Discounting> decode_discounting(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kDiscounting) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    Discounting out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kDiscountCode)) {
        out.discountCode = decode_int_field(p[idx], TagClass::kApplication, Tag::kDiscountCode);
        ++idx;
    }
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == Tag::kDiscountApplied) {
        out.discountApplied = decode_discount_applied(p[idx]);
        ++idx;
    }
    return out;
}

Tlv encode_currency_conversion(const CurrencyConversion& v) {
    std::vector<std::uint8_t> body;
    if (v.exchangeRateCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, Tag::kExchangeRateCode, *v.exchangeRateCode));
    }
    if (v.numberOfDecimalPlaces.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    Tag::kNumberOfDecimalPlaces,
                                    *v.numberOfDecimalPlaces));
    }
    if (v.exchangeRate.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kExchangeRate, *v.exchangeRate));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kCurrencyConversion;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<CurrencyConversion> decode_currency_conversion(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kCurrencyConversion) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    CurrencyConversion out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kExchangeRateCode)) {
        out.exchangeRateCode =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kExchangeRateCode);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kNumberOfDecimalPlaces)) {
        out.numberOfDecimalPlaces =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kNumberOfDecimalPlaces);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kExchangeRate)) {
        out.exchangeRate = decode_int_field(p[idx], TagClass::kApplication, Tag::kExchangeRate);
        ++idx;
    }
    return out;
}

// --- AccountingInfo ---

Tlv encode_accounting_info(const AccountingInfo& v) {
    std::vector<std::uint8_t> body;
    if (!v.taxation.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& t : v.taxation) {
            encode_tlv(list_body, encode_taxation(t));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kTaxationList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }
    if (!v.discounting.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& d : v.discounting) {
            encode_tlv(list_body, encode_discounting(d));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kDiscountingList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }
    if (v.localCurrency.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, Tag::kLocalCurrency, *v.localCurrency));
    }
    if (v.tapCurrency.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, Tag::kTapCurrency, *v.tapCurrency));
    }
    if (!v.currencyConversionInfo.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& c : v.currencyConversionInfo) {
            encode_tlv(list_body, encode_currency_conversion(c));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kCurrencyConversionList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }
    if (v.tapDecimalPlaces.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, Tag::kTapDecimalPlaces, *v.tapDecimalPlaces));
    }

    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kAccountingInfo;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<AccountingInfo> decode_accounting_info(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kAccountingInfo) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    AccountingInfo out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kTaxationList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto t = decode_taxation(item); t.has_value()) {
                    out.taxation.push_back(*t);
                }
            }
        }
        ++idx;
    }
    if (at_tag(p, idx, Tag::kDiscountingList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto d = decode_discounting(item); d.has_value()) {
                    out.discounting.push_back(*d);
                }
            }
        }
        ++idx;
    }
    if (at_tag(p, idx, Tag::kLocalCurrency)) {
        out.localCurrency =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kLocalCurrency);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTapCurrency)) {
        out.tapCurrency = decode_string_field(p[idx], TagClass::kApplication, Tag::kTapCurrency);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kCurrencyConversionList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto c = decode_currency_conversion(item); c.has_value()) {
                    out.currencyConversionInfo.push_back(*c);
                }
            }
        }
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTapDecimalPlaces)) {
        out.tapDecimalPlaces =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kTapDecimalPlaces);
        ++idx;
    }
    return out;
}

// --- NetworkInfo ---

Tlv encode_rec_entity_information(const RecEntityInformation& v) {
    std::vector<std::uint8_t> body;
    if (v.recEntityCode.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kRecEntityCode, *v.recEntityCode));
    }
    if (v.recEntityType.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kRecEntityType, *v.recEntityType));
    }
    if (v.recEntityId.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, Tag::kRecEntityId, *v.recEntityId));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kRecEntityInformation;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<RecEntityInformation> decode_rec_entity_information(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kRecEntityInformation) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    RecEntityInformation out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kRecEntityCode)) {
        out.recEntityCode = decode_int_field(p[idx], TagClass::kApplication, Tag::kRecEntityCode);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRecEntityType)) {
        out.recEntityType = decode_int_field(p[idx], TagClass::kApplication, Tag::kRecEntityType);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRecEntityId)) {
        out.recEntityId = decode_string_field(p[idx], TagClass::kApplication, Tag::kRecEntityId);
        ++idx;
    }
    return out;
}

Tlv encode_network_info(const NetworkInfo& v) {
    std::vector<std::uint8_t> body;
    if (!v.utcTimeOffsetInfo.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& u : v.utcTimeOffsetInfo) {
            std::vector<std::uint8_t> item_body;
            if (u.utcTimeOffsetCode.has_value()) {
                encode_tlv(item_body,
                           encode_int_field(TagClass::kApplication,
                                            Tag::kUtcTimeOffsetCode,
                                            *u.utcTimeOffsetCode));
            }
            if (u.utcTimeOffset.has_value()) {
                encode_tlv(item_body,
                           encode_string_field(
                               TagClass::kApplication, Tag::kUtcTimeOffset, *u.utcTimeOffset));
            }
            Tlv item_tlv;
            item_tlv.tag_class = TagClass::kApplication;
            item_tlv.constructed = true;
            item_tlv.tag_number = Tag::kUtcTimeOffsetInfo;
            item_tlv.value = std::move(item_body);
            encode_tlv(list_body, item_tlv);
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kUtcTimeOffsetInfoList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }
    if (!v.recEntityInfo.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& r : v.recEntityInfo) {
            encode_tlv(list_body, encode_rec_entity_information(r));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kRecEntityInfoList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kNetworkInfo;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<NetworkInfo> decode_network_info(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kNetworkInfo) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    NetworkInfo out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kUtcTimeOffsetInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (item.tag_class != TagClass::kApplication ||
                    item.tag_number != Tag::kUtcTimeOffsetInfo) {
                    continue;
                }
                const auto sub = tcap_core::decode_tlvs(item.value);
                if (!sub.has_value()) {
                    continue;
                }
                UtcTimeOffsetInfo info;
                std::size_t sidx = 0;
                const auto& sp = *sub;
                if (at_tag(sp, sidx, Tag::kUtcTimeOffsetCode)) {
                    info.utcTimeOffsetCode =
                        decode_int_field(sp[sidx], TagClass::kApplication, Tag::kUtcTimeOffsetCode);
                    ++sidx;
                }
                if (at_tag(sp, sidx, Tag::kUtcTimeOffset)) {
                    info.utcTimeOffset =
                        decode_string_field(sp[sidx], TagClass::kApplication, Tag::kUtcTimeOffset);
                    ++sidx;
                }
                out.utcTimeOffsetInfo.push_back(info);
            }
        }
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRecEntityInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto r = decode_rec_entity_information(item); r.has_value()) {
                    out.recEntityInfo.push_back(*r);
                }
            }
        }
        ++idx;
    }
    return out;
}

// --- AuditControlInfo ---

Tlv encode_audit_control_info(const AuditControlInfo& v) {
    std::vector<std::uint8_t> body;
    if (v.earliestCallTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time_long(Tag::kEarliestCallTimeStamp, *v.earliestCallTimeStamp));
    }
    if (v.latestCallTimeStamp.has_value()) {
        encode_tlv(body, encode_date_time_long(Tag::kLatestCallTimeStamp, *v.latestCallTimeStamp));
    }
    if (v.totalCharge.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kTotalCharge, *v.totalCharge));
    }
    if (v.totalChargeRefund.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, Tag::kTotalChargeRefund, *v.totalChargeRefund));
    }
    if (v.totalTaxRefund.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, Tag::kTotalTaxRefund, *v.totalTaxRefund));
    }
    if (v.totalTaxValue.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication, Tag::kTotalTaxValue, *v.totalTaxValue));
    }
    if (v.totalDiscountValue.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, Tag::kTotalDiscountValue, *v.totalDiscountValue));
    }
    if (v.totalDiscountRefund.has_value()) {
        encode_tlv(body,
                   encode_int_field(
                       TagClass::kApplication, Tag::kTotalDiscountRefund, *v.totalDiscountRefund));
    }
    if (v.callEventDetailsCount.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    Tag::kCallEventDetailsCount,
                                    *v.callEventDetailsCount));
    }
    if (!v.operatorSpecInformation.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& s : v.operatorSpecInformation) {
            encode_tlv(
                list_body,
                encode_string_field(TagClass::kApplication, Tag::kOperatorSpecInformation, s));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kOperatorSpecInfoList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }

    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kAuditControlInfo;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<AuditControlInfo> decode_audit_control_info(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kAuditControlInfo) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    AuditControlInfo out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kEarliestCallTimeStamp)) {
        out.earliestCallTimeStamp = decode_date_time_long(p[idx], Tag::kEarliestCallTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kLatestCallTimeStamp)) {
        out.latestCallTimeStamp = decode_date_time_long(p[idx], Tag::kLatestCallTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTotalCharge)) {
        out.totalCharge = decode_int_field(p[idx], TagClass::kApplication, Tag::kTotalCharge);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTotalChargeRefund)) {
        out.totalChargeRefund =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kTotalChargeRefund);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTotalTaxRefund)) {
        out.totalTaxRefund = decode_int_field(p[idx], TagClass::kApplication, Tag::kTotalTaxRefund);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTotalTaxValue)) {
        out.totalTaxValue = decode_int_field(p[idx], TagClass::kApplication, Tag::kTotalTaxValue);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTotalDiscountValue)) {
        out.totalDiscountValue =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kTotalDiscountValue);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTotalDiscountRefund)) {
        out.totalDiscountRefund =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kTotalDiscountRefund);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kCallEventDetailsCount)) {
        out.callEventDetailsCount =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kCallEventDetailsCount);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kOperatorSpecInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto s = decode_string_field(
                        item, TagClass::kApplication, Tag::kOperatorSpecInformation);
                    s.has_value()) {
                    out.operatorSpecInformation.push_back(*s);
                }
            }
        }
        ++idx;
    }
    return out;
}

// --- CallEventDetailList / TransferBatch / Notification / DataInterchange ---

Tlv encode_call_event_detail_list(const CallEventDetailList& list) {
    std::vector<std::uint8_t> body;
    for (const auto& t : list.mobileOriginatedCall) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.mobileTerminatedCall) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.supplServiceEvent) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.serviceCentreUsage) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.gprsCall) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.contentTransaction) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.locationService) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.messagingEvent) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.mobileSession) {
        encode_tlv(body, t);
    }
    for (const auto& t : list.aggregatedUsageRecord) {
        encode_tlv(body, t);
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kCallEventDetailList;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<CallEventDetailList> decode_call_event_detail_list(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kCallEventDetailList) {
        return std::nullopt;
    }
    const auto items = tcap_core::decode_tlvs(tlv.value);
    if (!items.has_value()) {
        return std::nullopt;
    }
    CallEventDetailList out;
    for (const auto& item : *items) {
        if (item.tag_class != TagClass::kApplication) {
            out.unrecognizedTagNumbers.push_back(item.tag_number);
            continue;
        }
        switch (item.tag_number) {
            case Tag::kMobileOriginatedCall:
                out.mobileOriginatedCall.push_back(item);
                break;
            case Tag::kMobileTerminatedCall:
                out.mobileTerminatedCall.push_back(item);
                break;
            case Tag::kSupplServiceEvent:
                out.supplServiceEvent.push_back(item);
                break;
            case Tag::kServiceCentreUsage:
                out.serviceCentreUsage.push_back(item);
                break;
            case Tag::kGprsCall:
                out.gprsCall.push_back(item);
                break;
            case Tag::kContentTransaction:
                out.contentTransaction.push_back(item);
                break;
            case Tag::kLocationService:
                out.locationService.push_back(item);
                break;
            case Tag::kMessagingEvent:
                out.messagingEvent.push_back(item);
                break;
            case Tag::kMobileSession:
                out.mobileSession.push_back(item);
                break;
            case Tag::kAggregatedUsageRecord:
                out.aggregatedUsageRecord.push_back(item);
                break;
            default:
                out.unrecognizedTagNumbers.push_back(item.tag_number);
                break;
        }
    }
    return out;
}

Tlv encode_transfer_batch(const TransferBatch& v) {
    std::vector<std::uint8_t> body;
    if (v.batchControlInfo.has_value()) {
        encode_tlv(body, encode_batch_control_info(*v.batchControlInfo));
    }
    if (v.accountingInfo.has_value()) {
        encode_tlv(body, encode_accounting_info(*v.accountingInfo));
    }
    if (v.networkInfo.has_value()) {
        encode_tlv(body, encode_network_info(*v.networkInfo));
    }
    if (v.callEventDetails.has_value()) {
        encode_tlv(body, encode_call_event_detail_list(*v.callEventDetails));
    }
    if (v.auditControlInfo.has_value()) {
        encode_tlv(body, encode_audit_control_info(*v.auditControlInfo));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kTransferBatch;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<TransferBatch> decode_transfer_batch(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kTransferBatch) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    TransferBatch out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kBatchControlInfo)) {
        out.batchControlInfo = decode_batch_control_info(p[idx]);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kAccountingInfo)) {
        out.accountingInfo = decode_accounting_info(p[idx]);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kNetworkInfo)) {
        out.networkInfo = decode_network_info(p[idx]);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kCallEventDetailList)) {
        out.callEventDetails = decode_call_event_detail_list(p[idx]);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kAuditControlInfo)) {
        out.auditControlInfo = decode_audit_control_info(p[idx]);
        ++idx;
    }
    return out;
}

Tlv encode_notification(const Notification& v) {
    std::vector<std::uint8_t> body;
    if (v.sender.has_value()) {
        encode_tlv(body, encode_string_field(TagClass::kApplication, Tag::kSender, *v.sender));
    }
    if (v.recipient.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication, Tag::kRecipient, *v.recipient));
    }
    if (v.fileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, Tag::kFileSequenceNumber, *v.fileSequenceNumber));
    }
    if (v.rapFileSequenceNumber.has_value()) {
        encode_tlv(body,
                   encode_string_field(TagClass::kApplication,
                                       Tag::kRapFileSequenceNumber,
                                       *v.rapFileSequenceNumber));
    }
    if (v.fileCreationTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time_long(Tag::kFileCreationTimeStamp, *v.fileCreationTimeStamp));
    }
    if (v.fileAvailableTimeStamp.has_value()) {
        encode_tlv(body,
                   encode_date_time_long(Tag::kFileAvailableTimeStamp, *v.fileAvailableTimeStamp));
    }
    if (v.transferCutOffTimeStamp.has_value()) {
        encode_tlv(
            body, encode_date_time_long(Tag::kTransferCutOffTimeStamp, *v.transferCutOffTimeStamp));
    }
    if (v.specificationVersionNumber.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    Tag::kSpecificationVersionNumber,
                                    *v.specificationVersionNumber));
    }
    if (v.releaseVersionNumber.has_value()) {
        encode_tlv(body,
                   encode_int_field(TagClass::kApplication,
                                    Tag::kReleaseVersionNumber,
                                    *v.releaseVersionNumber));
    }
    if (v.fileTypeIndicator.has_value()) {
        encode_tlv(body,
                   encode_string_field(
                       TagClass::kApplication, Tag::kFileTypeIndicator, *v.fileTypeIndicator));
    }
    if (!v.operatorSpecInformation.empty()) {
        std::vector<std::uint8_t> list_body;
        for (const auto& s : v.operatorSpecInformation) {
            encode_tlv(
                list_body,
                encode_string_field(TagClass::kApplication, Tag::kOperatorSpecInformation, s));
        }
        Tlv list_tlv;
        list_tlv.tag_class = TagClass::kApplication;
        list_tlv.constructed = true;
        list_tlv.tag_number = Tag::kOperatorSpecInfoList;
        list_tlv.value = std::move(list_body);
        encode_tlv(body, list_tlv);
    }

    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = Tag::kNotification;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<Notification> decode_notification(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != Tag::kNotification) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    Notification out;
    std::size_t idx = 0;
    if (at_tag(p, idx, Tag::kSender)) {
        out.sender = decode_string_field(p[idx], TagClass::kApplication, Tag::kSender);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRecipient)) {
        out.recipient = decode_string_field(p[idx], TagClass::kApplication, Tag::kRecipient);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileSequenceNumber)) {
        out.fileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kFileSequenceNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kRapFileSequenceNumber)) {
        out.rapFileSequenceNumber =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kRapFileSequenceNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileCreationTimeStamp)) {
        out.fileCreationTimeStamp = decode_date_time_long(p[idx], Tag::kFileCreationTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileAvailableTimeStamp)) {
        out.fileAvailableTimeStamp = decode_date_time_long(p[idx], Tag::kFileAvailableTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kTransferCutOffTimeStamp)) {
        out.transferCutOffTimeStamp = decode_date_time_long(p[idx], Tag::kTransferCutOffTimeStamp);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kSpecificationVersionNumber)) {
        out.specificationVersionNumber =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kSpecificationVersionNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kReleaseVersionNumber)) {
        out.releaseVersionNumber =
            decode_int_field(p[idx], TagClass::kApplication, Tag::kReleaseVersionNumber);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kFileTypeIndicator)) {
        out.fileTypeIndicator =
            decode_string_field(p[idx], TagClass::kApplication, Tag::kFileTypeIndicator);
        ++idx;
    }
    if (at_tag(p, idx, Tag::kOperatorSpecInfoList)) {
        const auto items = tcap_core::decode_tlvs(p[idx].value);
        if (items.has_value()) {
            for (const auto& item : *items) {
                if (auto s = decode_string_field(
                        item, TagClass::kApplication, Tag::kOperatorSpecInformation);
                    s.has_value()) {
                    out.operatorSpecInformation.push_back(*s);
                }
            }
        }
        ++idx;
    }
    return out;
}

std::vector<std::uint8_t> encode_data_interchange(const DataInterchange& v) {
    std::vector<std::uint8_t> out;
    if (v.transferBatch.has_value()) {
        encode_tlv(out, encode_transfer_batch(*v.transferBatch));
    } else if (v.notification.has_value()) {
        encode_tlv(out, encode_notification(*v.notification));
    }
    return out;
}

std::optional<DataInterchange> decode_data_interchange(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    const auto tlv = tcap_core::decode_tlv(bytes, offset);
    if (!tlv.has_value()) {
        return std::nullopt;
    }
    DataInterchange out;
    if (tlv->tag_class == TagClass::kApplication && tlv->tag_number == Tag::kTransferBatch) {
        out.transferBatch = decode_transfer_batch(*tlv);
        return out;
    }
    if (tlv->tag_class == TagClass::kApplication && tlv->tag_number == Tag::kNotification) {
        out.notification = decode_notification(*tlv);
        return out;
    }
    return std::nullopt; // real, disclosed: neither real alternative's tag matched
}

} // namespace tap3_core
