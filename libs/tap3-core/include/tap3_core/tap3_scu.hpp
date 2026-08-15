#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 ServiceCentreUsage (real [APPLICATION 12]) -- real SMS-via-service-centre CallEventDetail
// variant. Reuses ChargeInformation from tap3_charging.hpp and several leaf tag constants already
// defined in MoCallTag (kServingNetwork, kClirIndicator, kDestinationNetwork, kImsi, kMsisdn) and
// MtCallTag (kOriginatingNetwork). See tap3_common.hpp's own header for the full real
// sourcing/scope disclosure.

namespace tap3_core {

namespace ScuTag {
constexpr std::uint32_t kServiceCentreUsage = Tag::kServiceCentreUsage;
constexpr std::uint32_t kScuBasicInformation = 191;
constexpr std::uint32_t kChargedPartyStatus = 67;
constexpr std::uint32_t kNonChargedNumber = 402;
constexpr std::uint32_t kScuChargeableSubscriber = 430; // CHOICE, real EXPLICIT wrap
constexpr std::uint32_t kGsmChargeableSubscriber = 286;
constexpr std::uint32_t kScuChargeType = 192;
constexpr std::uint32_t kMessageStatus = 144;
constexpr std::uint32_t kPriorityCode = 170;
constexpr std::uint32_t kDistanceChargeBandCode = 98;
constexpr std::uint32_t kMessageType = 145;
constexpr std::uint32_t kMessageDescriptionCode = 141;
constexpr std::uint32_t kScuTimeStamps = 193;
constexpr std::uint32_t kDepositTimeStamp = 88;
constexpr std::uint32_t kCompletionTimeStamp = 76;
constexpr std::uint32_t kChargingPoint = 73;
constexpr std::uint32_t kOriginatingNetwork =
    164; // same real tag as MtCallTag::kOriginatingNetwork
} // namespace ScuTag

// GsmChargeableSubscriber ::= [APPLICATION 286] SEQUENCE -- real, distinct type/tag from MoCall's
// own SimChargeableSubscriber (199) despite the identical field shape (imsi/msisdn).
struct GsmChargeableSubscriber {
    std::optional<std::string> imsi;
    std::optional<std::string> msisdn;
};

// ScuChargeableSubscriber ::= [APPLICATION 430] CHOICE { gsmChargeableSubscriber,
// minChargeableSubscriber (reuse MoCall's MinChargeableSubscriber shape) } -- real tagged CHOICE.
struct ScuChargeableSubscriber {
    bool isGsm = true;
    std::optional<GsmChargeableSubscriber> gsm;
    std::optional<ChargeableSubscriber> min; // only .min/.mdn populated when !isGsm
};
Tlv encode_scu_chargeable_subscriber(const ScuChargeableSubscriber& v);
std::optional<ScuChargeableSubscriber> decode_scu_chargeable_subscriber(const Tlv& tlv);

// ScuBasicInformation ::= [APPLICATION 191] SEQUENCE.
struct ScuBasicInformation {
    std::optional<ScuChargeableSubscriber> chargeableSubscriber;
    std::optional<std::int32_t> chargedPartyStatus;
    std::optional<std::string> nonChargedNumber;
    std::optional<std::int32_t> clirIndicator;
    std::optional<std::string> originatingNetwork;
    std::optional<std::string> destinationNetwork;
};
Tlv encode_scu_basic_information(const ScuBasicInformation& v);
std::optional<ScuBasicInformation> decode_scu_basic_information(const Tlv& tlv);

// ScuChargeType ::= [APPLICATION 192] SEQUENCE.
struct ScuChargeType {
    std::optional<std::int32_t> messageStatus;
    std::optional<std::int32_t> priorityCode;
    std::optional<std::string> distanceChargeBandCode; // real AsciiString(SIZE(1))
    std::optional<std::int32_t> messageType;
    std::optional<std::int32_t> messageDescriptionCode;
};
Tlv encode_scu_charge_type(const ScuChargeType& v);
std::optional<ScuChargeType> decode_scu_charge_type(const Tlv& tlv);

// ScuTimeStamps ::= [APPLICATION 193] SEQUENCE.
struct ScuTimeStamps {
    std::optional<DateTime> depositTimeStamp;
    std::optional<DateTime> completionTimeStamp;
    std::optional<std::string> chargingPoint; // real AsciiString(SIZE(1))
};
Tlv encode_scu_time_stamps(const ScuTimeStamps& v);
std::optional<ScuTimeStamps> decode_scu_time_stamps(const Tlv& tlv);

// ServiceCentreUsage ::= [APPLICATION 12] SEQUENCE.
struct ServiceCentreUsage {
    std::optional<ScuBasicInformation> basicInformation;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<std::string> servingNetwork;
    std::optional<std::int32_t> recEntityCode;
    std::optional<ChargeInformation> chargeInformation;
    std::optional<ScuChargeType> scuChargeType;
    std::optional<ScuTimeStamps> scuTimeStamps;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_service_centre_usage(const ServiceCentreUsage& v);
std::optional<ServiceCentreUsage> decode_service_centre_usage(const Tlv& tlv);

} // namespace tap3_core
