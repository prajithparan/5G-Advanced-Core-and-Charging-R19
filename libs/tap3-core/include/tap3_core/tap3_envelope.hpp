#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tap3_core/tap3_common.hpp"

// TAP3 envelope structures -- BatchControlInfo/AccountingInfo/NetworkInfo/AuditControlInfo, the
// real TransferBatch/Notification/DataInterchange wrapper every real TAP3 file needs regardless of
// which CallEventDetail variants it carries. See tap3_common.hpp's own header for the full real
// sourcing/scope disclosure (GSMA member-confidential source, real cited facts only, no verbatim
// reproduction). Real tag numbers all come from tap3_common.hpp's own `Tag` namespace.

namespace tap3_core {

// BatchControlInfo ::= [APPLICATION 4] SEQUENCE { sender Sender, recipient Recipient,
// fileSequenceNumber FileSequenceNumber, fileCreationTimeStamp FileCreationTimeStamp OPTIONAL,
// transferCutOffTimeStamp TransferCutOffTimeStamp, fileAvailableTimeStamp FileAvailableTimeStamp,
// specificationVersionNumber SpecificationVersionNumber, releaseVersionNumber
// ReleaseVersionNumber, fileTypeIndicator FileTypeIndicator OPTIONAL, rapFileSequenceNumber
// RapFileSequenceNumber OPTIONAL, operatorSpecInformation OperatorSpecInfoList OPTIONAL }. Real
// leaf types: Sender/Recipient ::= PlmnId ::= AsciiString(SIZE(5)); FileSequenceNumber ::=
// NumberString(SIZE(5)); *TimeStamp fields ::= DateTimeLong.
struct BatchControlInfo {
    std::optional<std::string> sender;
    std::optional<std::string> recipient;
    std::optional<std::string> fileSequenceNumber;
    std::optional<DateTimeLong> fileCreationTimeStamp;
    std::optional<DateTimeLong> transferCutOffTimeStamp;
    std::optional<DateTimeLong> fileAvailableTimeStamp;
    std::optional<std::int32_t> specificationVersionNumber;
    std::optional<std::int32_t> releaseVersionNumber;
    std::optional<std::string> fileTypeIndicator;
    std::optional<std::string> rapFileSequenceNumber;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_batch_control_info(const BatchControlInfo& v);
std::optional<BatchControlInfo> decode_batch_control_info(const Tlv& tlv);

// Taxation ::= [APPLICATION 216] SEQUENCE { taxCode TaxCode, taxType TaxType OPTIONAL, taxRate
// TaxRate OPTIONAL, chargeType ChargeType OPTIONAL, taxIndicator TaxIndicator OPTIONAL }.
struct Taxation {
    std::optional<std::int32_t> taxCode;
    std::optional<std::string> taxType;
    std::optional<std::string> taxRate;
    std::optional<std::string> chargeType;
    std::optional<std::string> taxIndicator;
};
Tlv encode_taxation(const Taxation& v);
std::optional<Taxation> decode_taxation(const Tlv& tlv);

// DiscountApplied ::= [APPLICATION 428] CHOICE { fixedDiscountValue FixedDiscountValue,
// discountRate DiscountRate }. Real tagged CHOICE (wrap_explicit).
struct DiscountApplied {
    bool isFixedValue =
        true; // true: fixedValue holds AbsoluteAmount; false: discountRate holds PercentageRate
    std::int32_t value = 0;
};
Tlv encode_discount_applied(const DiscountApplied& v);
std::optional<DiscountApplied> decode_discount_applied(const Tlv& tlv);

// Discounting ::= [APPLICATION 94] SEQUENCE { discountCode DiscountCode, discountApplied
// DiscountApplied }.
struct Discounting {
    std::optional<std::int32_t> discountCode;
    std::optional<DiscountApplied> discountApplied;
};
Tlv encode_discounting(const Discounting& v);
std::optional<Discounting> decode_discounting(const Tlv& tlv);

// CurrencyConversion ::= [APPLICATION 106] SEQUENCE { exchangeRateCode ExchangeRateCode,
// numberOfDecimalPlaces NumberOfDecimalPlaces, exchangeRate ExchangeRate }.
struct CurrencyConversion {
    std::optional<std::int32_t> exchangeRateCode;
    std::optional<std::int32_t> numberOfDecimalPlaces;
    std::optional<std::int32_t> exchangeRate;
};
Tlv encode_currency_conversion(const CurrencyConversion& v);
std::optional<CurrencyConversion> decode_currency_conversion(const Tlv& tlv);

// AccountingInfo ::= [APPLICATION 5] SEQUENCE { taxation TaxationList OPTIONAL, discounting
// DiscountingList OPTIONAL, localCurrency LocalCurrency, tapCurrency TapCurrency OPTIONAL,
// currencyConversionInfo CurrencyConversionList OPTIONAL, tapDecimalPlaces TapDecimalPlaces }.
struct AccountingInfo {
    std::vector<Taxation> taxation;
    std::vector<Discounting> discounting;
    std::optional<std::string> localCurrency;
    std::optional<std::string> tapCurrency;
    std::vector<CurrencyConversion> currencyConversionInfo;
    std::optional<std::int32_t> tapDecimalPlaces;
};
Tlv encode_accounting_info(const AccountingInfo& v);
std::optional<AccountingInfo> decode_accounting_info(const Tlv& tlv);

// RecEntityInformation ::= [APPLICATION 183] SEQUENCE { recEntityCode RecEntityCode,
// recEntityType RecEntityType OPTIONAL, recEntityId RecEntityId OPTIONAL }.
struct RecEntityInformation {
    std::optional<std::int32_t> recEntityCode;
    std::optional<std::int32_t> recEntityType;
    std::optional<std::string> recEntityId;
};
Tlv encode_rec_entity_information(const RecEntityInformation& v);
std::optional<RecEntityInformation> decode_rec_entity_information(const Tlv& tlv);

// NetworkInfo ::= [APPLICATION 6] SEQUENCE { utcTimeOffsetInfo UtcTimeOffsetInfoList OPTIONAL,
// recEntityInfo RecEntityInfoList OPTIONAL }. UtcTimeOffsetInfo itself ::= [APPLICATION 233]
// SEQUENCE { utcTimeOffsetCode UtcTimeOffsetCode, utcTimeOffset UtcTimeOffset }.
struct UtcTimeOffsetInfo {
    std::optional<std::int32_t> utcTimeOffsetCode;
    std::optional<std::string> utcTimeOffset;
};
struct NetworkInfo {
    std::vector<UtcTimeOffsetInfo> utcTimeOffsetInfo;
    std::vector<RecEntityInformation> recEntityInfo;
};
Tlv encode_network_info(const NetworkInfo& v);
std::optional<NetworkInfo> decode_network_info(const Tlv& tlv);

// AuditControlInfo ::= [APPLICATION 15] SEQUENCE { earliestCallTimeStamp EarliestCallTimeStamp
// OPTIONAL, latestCallTimeStamp LatestCallTimeStamp OPTIONAL, totalCharge TotalCharge,
// totalChargeRefund TotalChargeRefund OPTIONAL, totalTaxRefund TotalTaxRefund OPTIONAL,
// totalTaxValue TotalTaxValue, totalDiscountValue TotalDiscountValue,
// totalDiscountRefund TotalDiscountRefund OPTIONAL, callEventDetailsCount CallEventDetailsCount,
// operatorSpecInformation OperatorSpecInfoList OPTIONAL }. Real, disclosed scope narrowing:
// `totalAdvisedChargeValueList` (a further nested SEQUENCE OF, TAP-SPEC.pdf's own
// TotalAdvisedChargeValue chain) is real, cited, deferred -- not needed to prove the codec
// round-trips a real MobileOriginatedCall-only batch.
struct AuditControlInfo {
    std::optional<DateTimeLong> earliestCallTimeStamp;
    std::optional<DateTimeLong> latestCallTimeStamp;
    std::optional<std::int32_t> totalCharge;
    std::optional<std::int32_t> totalChargeRefund;
    std::optional<std::int32_t> totalTaxRefund;
    std::optional<std::int32_t> totalTaxValue;
    std::optional<std::int32_t> totalDiscountValue;
    std::optional<std::int32_t> totalDiscountRefund;
    std::optional<std::int32_t> callEventDetailsCount;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_audit_control_info(const AuditControlInfo& v);
std::optional<AuditControlInfo> decode_audit_control_info(const Tlv& tlv);

// CallEventDetail ::= CHOICE { mobileOriginatedCall MobileOriginatedCall, mobileTerminatedCall
// MobileTerminatedCall, supplServiceEvent SupplServiceEvent, serviceCentreUsage
// ServiceCentreUsage, gprsCall GprsCall, contentTransaction ContentTransaction, locationService
// LocationService, messagingEvent MessagingEvent, mobileSession MobileSession,
// aggregatedUsageRecord AggregatedUsageRecord }. Real, untagged CHOICE (see tap3_common.hpp's
// header) -- one opaque pre-encoded Tlv bucket per real alternative here; each alternative's own
// dedicated header (tap3_mo_call.hpp, tap3_mt_call.hpp, tap3_suppl_service.hpp, tap3_scu.hpp,
// tap3_gprs_call.hpp, tap3_content_transaction.hpp, tap3_location_service.hpp,
// tap3_messaging_event.hpp, tap3_mobile_session.hpp, tap3_aggregated_usage.hpp) owns the real
// struct/codec, this layer just dispatches by real top tag number (TAP-SPEC.pdf section 6.1,
// p.257) and frames the list. Any tag this codec doesn't recognize is skipped with the raw tag
// number preserved in `unrecognizedTagNumbers` (not silently dropped), covering both a genuinely
// unknown future tag and (real, disclosed) `MessageDescriptionInformation` entries, which are not
// a CallEventDetail alternative at all but would land here if ever mixed in by a malformed file.
struct CallEventDetailList {
    std::vector<Tlv> mobileOriginatedCall;
    std::vector<Tlv> mobileTerminatedCall;
    std::vector<Tlv> supplServiceEvent;
    std::vector<Tlv> serviceCentreUsage;
    std::vector<Tlv> gprsCall;
    std::vector<Tlv> contentTransaction;
    std::vector<Tlv> locationService;
    std::vector<Tlv> messagingEvent;
    std::vector<Tlv> mobileSession;
    std::vector<Tlv> aggregatedUsageRecord;
    std::vector<std::uint32_t> unrecognizedTagNumbers;
};
Tlv encode_call_event_detail_list(const CallEventDetailList& list);
std::optional<CallEventDetailList> decode_call_event_detail_list(const Tlv& tlv);

// TransferBatch ::= [APPLICATION 1] SEQUENCE { batchControlInfo, accountingInfo OPTIONAL,
// networkInfo, callEventDetails CallEventDetailList OPTIONAL, auditControlInfo }. Real, disclosed
// scope narrowing: `messageDescriptionInfo` (SMS message-description reference table) is real,
// cited, deferred -- not needed for a MobileOriginatedCall-only batch.
struct TransferBatch {
    std::optional<BatchControlInfo> batchControlInfo;
    std::optional<AccountingInfo> accountingInfo;
    std::optional<NetworkInfo> networkInfo;
    std::optional<CallEventDetailList> callEventDetails;
    std::optional<AuditControlInfo> auditControlInfo;
};
Tlv encode_transfer_batch(const TransferBatch& v);
std::optional<TransferBatch> decode_transfer_batch(const Tlv& tlv);

// Notification ::= [APPLICATION 2] SEQUENCE -- real, all-OPTIONAL per TAP-SPEC.pdf's own module
// text (used for real "no chargeable data this period" / administrative notices, not carrying a
// real TransferBatch's call data).
struct Notification {
    std::optional<std::string> sender;
    std::optional<std::string> recipient;
    std::optional<std::string> fileSequenceNumber;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<DateTimeLong> fileCreationTimeStamp;
    std::optional<DateTimeLong> fileAvailableTimeStamp;
    std::optional<DateTimeLong> transferCutOffTimeStamp;
    std::optional<std::int32_t> specificationVersionNumber;
    std::optional<std::int32_t> releaseVersionNumber;
    std::optional<std::string> fileTypeIndicator;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_notification(const Notification& v);
std::optional<Notification> decode_notification(const Tlv& tlv);

// DataInterchange ::= CHOICE { transferBatch TransferBatch, notification Notification, ... }.
// Real, untagged CHOICE -- top-level entry point for a real TAP3 file's own outermost structure.
struct DataInterchange {
    std::optional<TransferBatch> transferBatch;
    std::optional<Notification> notification;
};
std::vector<std::uint8_t> encode_data_interchange(const DataInterchange& v);
std::optional<DataInterchange> decode_data_interchange(const std::vector<std::uint8_t>& bytes);

} // namespace tap3_core
