#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tap3_core/tap3_common.hpp"

// TAP3 shared charging-detail chain -- ChargeDetail/TaxInformation/DiscountInformation/
// CallTypeGroup/ChargeInformation, real field structures pulled from nearly every real
// CallEventDetail variant (SupplServiceUsed.chargeInformation,
// ServiceCentreUsage.chargeInformation, GprsServiceUsed.chargeInformationList,
// ContentServiceUsed.chargeInformationList, LocationServiceUsage.chargeInformationList,
// SessionChargeInformation.{chargeDetailList, taxInformationList}, and MoCall/MtCall's own
// BasicServiceUsed.chargeInformationList + CamelServiceUsed.{taxInformation,discountInformation}).
// See tap3_common.hpp's own header for the full real sourcing/scope disclosure (GSMA
// member-confidential source, real cited facts only, no verbatim reproduction). Real tag numbers
// cited from TAP-SPEC.pdf section 6.1 throughout.

namespace tap3_core {

namespace ChargingTag {
constexpr std::uint32_t kChargedItem = 66;
constexpr std::uint32_t kCharge = 62;
constexpr std::uint32_t kChargeableUnits = 65;
constexpr std::uint32_t kChargedUnits = 68;
constexpr std::uint32_t kChargeDetailTimeStamp = 410;
constexpr std::uint32_t kChargeDetail = 63;
constexpr std::uint32_t kChargeDetailList = 64;

constexpr std::uint32_t kTaxValue = 397;
constexpr std::uint32_t kTaxableAmount = 398;
constexpr std::uint32_t kTaxInformation = 213;
constexpr std::uint32_t kTaxInformationList = 214;

constexpr std::uint32_t kDiscount = 412;
constexpr std::uint32_t kDiscountableAmount = 423;
constexpr std::uint32_t kDiscountInformation = 96;

constexpr std::uint32_t kCallTypeLevel1 = 259;
constexpr std::uint32_t kCallTypeLevel2 = 255;
constexpr std::uint32_t kCallTypeLevel3 = 256;
constexpr std::uint32_t kCallTypeGroup = 258;

constexpr std::uint32_t kChargeInformation = 69;
constexpr std::uint32_t kChargeInformationList = 70;
} // namespace ChargingTag

// ChargeDetail ::= [APPLICATION 63] SEQUENCE { chargeType ChargeType OPT (Tag::kChargeType, real
// alias to the shared leaf already used by Taxation), charge Charge OPT, chargeableUnits OPT,
// chargedUnits OPT, chargeDetailTimeStamp ChargeDetailTimeStamp(=DateTime) OPT }.
struct ChargeDetail {
    std::optional<std::string> chargeType;
    std::optional<std::int32_t> charge;
    std::optional<std::int32_t> chargeableUnits;
    std::optional<std::int32_t> chargedUnits;
    std::optional<DateTime> chargeDetailTimeStamp;
};
Tlv encode_charge_detail(const ChargeDetail& v);
std::optional<ChargeDetail> decode_charge_detail(const Tlv& tlv);

// TaxInformation ::= [APPLICATION 213] SEQUENCE { taxCode TaxCode OPT (Tag::kTaxCode, real alias
// to the shared leaf already used by Taxation), taxValue TaxValue(=AbsoluteAmount) OPT,
// taxableAmount TaxableAmount(=AbsoluteAmount) OPT }.
struct TaxInformation {
    std::optional<std::int32_t> taxCode;
    std::optional<std::int32_t> taxValue;
    std::optional<std::int32_t> taxableAmount;
};
Tlv encode_tax_information(const TaxInformation& v);
std::optional<TaxInformation> decode_tax_information(const Tlv& tlv);

// DiscountInformation ::= [APPLICATION 96] SEQUENCE { discountCode DiscountCode OPT
// (Tag::kDiscountCode, real alias to the shared leaf already used by Discounting), discount
// Discount(=AbsoluteAmount) OPT, discountableAmount DiscountableAmount(=AbsoluteAmount) OPT }.
struct DiscountInformation {
    std::optional<std::int32_t> discountCode;
    std::optional<std::int32_t> discount;
    std::optional<std::int32_t> discountableAmount;
};
Tlv encode_discount_information(const DiscountInformation& v);
std::optional<DiscountInformation> decode_discount_information(const Tlv& tlv);

// CallTypeGroup ::= [APPLICATION 258] SEQUENCE { callTypeLevel1/2/3 OPT }.
struct CallTypeGroup {
    std::optional<std::int32_t> callTypeLevel1;
    std::optional<std::int32_t> callTypeLevel2;
    std::optional<std::int32_t> callTypeLevel3;
};
Tlv encode_call_type_group(const CallTypeGroup& v);
std::optional<CallTypeGroup> decode_call_type_group(const Tlv& tlv);

// ChargeInformation ::= [APPLICATION 69] SEQUENCE { chargedItem ChargedItem OPT, exchangeRateCode
// ExchangeRateCode OPT (Tag::kExchangeRateCode, real alias to the shared leaf already used by
// CurrencyConversion), callTypeGroup OPT, chargeDetailList ChargeDetailList OPT, taxInformation
// TaxInformationList OPT, discountInformation DiscountInformation OPT }. Real, disclosed reading:
// the field named `taxInformation` carries the real LIST type `TaxInformationList` (spec's own
// field-vs-type naming, same pattern already established for AccountingInfo.taxation carrying
// TaxationList) -- modeled as a vector. `discountInformation` was NOT given a "...List" type name
// in this slice's own extraction, so modeled as a single optional instance; flagged, not resolved,
// if a genuine TAP3 sample later shows this needs to be a list too.
struct ChargeInformation {
    std::optional<std::string> chargedItem;
    std::optional<std::int32_t> exchangeRateCode;
    std::optional<CallTypeGroup> callTypeGroup;
    std::vector<ChargeDetail> chargeDetailList;
    std::vector<TaxInformation> taxInformation;
    std::optional<DiscountInformation> discountInformation;
};
Tlv encode_charge_information(const ChargeInformation& v);
std::optional<ChargeInformation> decode_charge_information(const Tlv& tlv);

// *List wrappers -- real [APPLICATION N] SEQUENCE OF X per TAP-SPEC.pdf section 6.1.
Tlv encode_charge_information_list(const std::vector<ChargeInformation>& items);
std::optional<std::vector<ChargeInformation>> decode_charge_information_list(const Tlv& tlv);

} // namespace tap3_core
