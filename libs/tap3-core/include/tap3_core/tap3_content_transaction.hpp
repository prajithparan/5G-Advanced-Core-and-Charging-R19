#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_gprs_call.hpp"

// TAP3 ContentTransaction (real [APPLICATION 17]) -- premium/content billing CallEventDetail
// variant. Reuses ChargeInformation from tap3_charging.hpp and the real 8-byte-INTEGER
// DataVolumeIncoming/Outgoing tags already defined in GprsCallTag. See tap3_common.hpp's own
// header for the full real sourcing/scope disclosure.

namespace tap3_core {

namespace ContentTxTag {
constexpr std::uint32_t kContentTransactionBasicInfo = 304;
constexpr std::uint32_t kOrderPlacedTimeStamp = 300;
constexpr std::uint32_t kRequestedDeliveryTimeStamp = 301;
constexpr std::uint32_t kActualDeliveryTimeStamp = 302;
constexpr std::uint32_t kTotalTransactionDuration = 416;
constexpr std::uint32_t kTransactionStatus = 303;

constexpr std::uint32_t kChargedPartyInformation = 324;
constexpr std::uint32_t kChargedPartyIdList = 310;
constexpr std::uint32_t kChargedPartyIdentification = 309;
constexpr std::uint32_t kChargedPartyIdType = 305;
constexpr std::uint32_t kChargedPartyIdentifier = 287;
constexpr std::uint32_t kChargedPartyHomeIdList = 314;
constexpr std::uint32_t kChargedPartyHomeIdentification = 313;
constexpr std::uint32_t kHomeIdType = 311;
constexpr std::uint32_t kHomeIdentifier = 288;
constexpr std::uint32_t kChargedPartyLocationList = 321;
constexpr std::uint32_t kChargedPartyLocation = 320;
constexpr std::uint32_t kLocationIdType = 315;
constexpr std::uint32_t kLocationIdentifier = 289;
constexpr std::uint32_t kChargedPartyEquipment = 323;
constexpr std::uint32_t kEquipmentIdType = 322;
constexpr std::uint32_t kEquipmentId = 290;

constexpr std::uint32_t kServingPartiesInformation = 335;
constexpr std::uint32_t kContentProviderName = 334;
constexpr std::uint32_t kContentProviderIdList = 328;
constexpr std::uint32_t kContentProvider = 327;
constexpr std::uint32_t kContentProviderIdType = 291;
constexpr std::uint32_t kContentProviderIdentifier = 292;
constexpr std::uint32_t kInternetServiceProviderIdList = 330;
constexpr std::uint32_t kInternetServiceProvider = 329;
constexpr std::uint32_t kIspIdType = 293;
constexpr std::uint32_t kIspIdentifier = 294;
constexpr std::uint32_t kNetworkList = 333;
constexpr std::uint32_t kNetwork = 332;
constexpr std::uint32_t kNetworkIdType = 331;
constexpr std::uint32_t kNetworkIdentifier = 295;

constexpr std::uint32_t kContentServiceUsed = 352;
constexpr std::uint32_t kContentServiceUsedList = 285;
constexpr std::uint32_t kContentTransactionCode = 336;
constexpr std::uint32_t kContentTransactionType = 337;
constexpr std::uint32_t kObjectType = 281;
constexpr std::uint32_t kTransactionDescriptionSupp = 338;
constexpr std::uint32_t kTransactionShortDescription = 340;
constexpr std::uint32_t kTransactionDetailDescription = 339;
constexpr std::uint32_t kTransactionIdentifier = 341;
constexpr std::uint32_t kTransactionAuthCode = 342;
constexpr std::uint32_t kTotalDataVolume = 343; // real 8-byte-INTEGER exception (Table 44)
constexpr std::uint32_t kChargeRefundIndicator = 344;
constexpr std::uint32_t kContentChargingPoint = 345;

constexpr std::uint32_t kAdvisedChargeInformation = 351;
constexpr std::uint32_t kPaidIndicator = 346;
constexpr std::uint32_t kPaymentMethod = 347;
constexpr std::uint32_t kAdvisedChargeCurrency = 348;
constexpr std::uint32_t kAdvisedCharge = 349;
constexpr std::uint32_t kCommission = 350;
} // namespace ContentTxTag

struct ContentTransactionBasicInfo {
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<DateTime> orderPlacedTimeStamp;
    std::optional<DateTime> requestedDeliveryTimeStamp;
    std::optional<DateTime> actualDeliveryTimeStamp;
    std::optional<std::int32_t> totalTransactionDuration;
    std::optional<std::int32_t> transactionStatus;
};

struct ChargedPartyIdentification {
    std::optional<std::int32_t> chargedPartyIdType;
    std::optional<std::string> chargedPartyIdentifier;
};
struct ChargedPartyHomeIdentification {
    std::optional<std::int32_t> homeIdType;
    std::optional<std::string> homeIdentifier;
};
struct ChargedPartyLocation {
    std::optional<std::int32_t> locationIdType;
    std::optional<std::string> locationIdentifier;
};
struct ChargedPartyEquipment {
    std::optional<std::int32_t> equipmentIdType;
    std::optional<std::string> equipmentId;
};
struct ChargedPartyInformation {
    std::vector<ChargedPartyIdentification> chargedPartyIdList;
    std::vector<ChargedPartyHomeIdentification> chargedPartyHomeIdList;
    std::vector<ChargedPartyLocation> chargedPartyLocationList;
    std::optional<ChargedPartyEquipment> chargedPartyEquipment;
};

struct ContentProvider {
    std::optional<std::int32_t> contentProviderIdType;
    std::optional<std::string> contentProviderIdentifier;
};
struct InternetServiceProvider {
    std::optional<std::int32_t> ispIdType;
    std::optional<std::string> ispIdentifier;
};
struct ContentNetwork {
    std::optional<std::int32_t> networkIdType;
    std::optional<std::string> networkIdentifier;
};
struct ServingPartiesInformation {
    std::optional<std::string> contentProviderName;
    std::vector<ContentProvider> contentProviderIdList;
    std::vector<InternetServiceProvider> internetServiceProviderIdList;
    std::vector<ContentNetwork> networkList;
};

struct AdvisedChargeInformation {
    std::optional<std::int32_t> paidIndicator;
    std::optional<std::int32_t> paymentMethod;
    std::optional<std::string> advisedChargeCurrency;
    std::optional<std::int32_t> advisedCharge;
    std::optional<std::int32_t> commission;
};

// ContentServiceUsed ::= [APPLICATION 352] SEQUENCE.
struct ContentServiceUsed {
    std::optional<std::int32_t> contentTransactionCode;
    std::optional<std::int32_t> contentTransactionType;
    std::optional<std::int32_t> objectType;
    std::optional<std::int32_t> transactionDescriptionSupp;
    std::optional<std::string> transactionShortDescription;
    std::optional<std::string> transactionDetailDescription;
    std::optional<std::string> transactionIdentifier;
    std::optional<std::string> transactionAuthCode;
    std::optional<std::int64_t> dataVolumeIncoming;
    std::optional<std::int64_t> dataVolumeOutgoing;
    std::optional<std::int64_t> totalDataVolume;
    std::optional<std::int32_t> chargeRefundIndicator;
    std::optional<std::int32_t> contentChargingPoint;
    std::vector<ChargeInformation> chargeInformationList;
    std::optional<AdvisedChargeInformation> advisedChargeInformation;
};
Tlv encode_content_service_used(const ContentServiceUsed& v);
std::optional<ContentServiceUsed> decode_content_service_used(const Tlv& tlv);

// ContentTransaction ::= [APPLICATION 17] SEQUENCE.
struct ContentTransaction {
    std::optional<ContentTransactionBasicInfo> contentTransactionBasicInfo;
    std::optional<ChargedPartyInformation> chargedPartyInformation;
    std::optional<ServingPartiesInformation> servingPartiesInformation;
    std::vector<ContentServiceUsed> contentServiceUsed;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_content_transaction(const ContentTransaction& v);
std::optional<ContentTransaction> decode_content_transaction(const Tlv& tlv);

} // namespace tap3_core
