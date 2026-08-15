#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_common.hpp"

// TAP3 MobileOriginatedCall (real [APPLICATION 9]) -- one of the real CallEventDetail variants
// this codec implements (ADR-0067). See tap3_common.hpp's own header for the full real
// sourcing/scope disclosure.

namespace tap3_core {

// Real per-file (not per-service) tag constants for this chain, TAP-SPEC.pdf section 6.1.
namespace MoCallTag {
constexpr std::uint32_t kMoBasicCallInformation = 147;
constexpr std::uint32_t kChargeableSubscriber = 427; // CHOICE, real EXPLICIT wrap
constexpr std::uint32_t kSimChargeableSubscriber = 199;
constexpr std::uint32_t kMinChargeableSubscriber = 254;
constexpr std::uint32_t kImsi = 129;
constexpr std::uint32_t kMsisdn = 152;
constexpr std::uint32_t kMin = 146;
constexpr std::uint32_t kMdn = 253;
constexpr std::uint32_t kDestination = 89;
constexpr std::uint32_t kCalledNumber = 407;
constexpr std::uint32_t kDialledDigits = 279;
constexpr std::uint32_t kCalledPlace = 42;
constexpr std::uint32_t kCalledRegion = 46;
constexpr std::uint32_t kSMSDestinationNumber = 419;
constexpr std::uint32_t kDestinationNetwork = 90;
constexpr std::uint32_t kCallEventStartTimeStamp = 44;
constexpr std::uint32_t kTotalCallEventDuration = 223;
constexpr std::uint32_t kSimToolkitIndicator = 200;
constexpr std::uint32_t kCauseForTerm = 58;

constexpr std::uint32_t kLocationInformation = 138;
constexpr std::uint32_t kNetworkLocation = 156;
constexpr std::uint32_t kRecEntityCode = 184;
constexpr std::uint32_t kCallReference = 45;
constexpr std::uint32_t kLocationArea = 136;
constexpr std::uint32_t kCellId = 59;
constexpr std::uint32_t kHomeLocationInformation = 123;
constexpr std::uint32_t kHomeBid = 122;
constexpr std::uint32_t kHomeLocationDescription = 413;
constexpr std::uint32_t kGeographicalLocation = 113;
constexpr std::uint32_t kServingNetwork = 195;
constexpr std::uint32_t kServingBid = 198;
constexpr std::uint32_t kServingLocationDescription = 414;

constexpr std::uint32_t kImeiOrEsn = 429; // CHOICE, real EXPLICIT wrap
constexpr std::uint32_t kImei = 128;
constexpr std::uint32_t kEsn = 103;

constexpr std::uint32_t kBasicServiceUsedList = 38;
constexpr std::uint32_t kBasicServiceUsed = 39;
constexpr std::uint32_t kBasicService = 36;
constexpr std::uint32_t kBasicServiceCode = 426; // CHOICE, real EXPLICIT wrap
constexpr std::uint32_t kTeleServiceCode = 218;
constexpr std::uint32_t kBearerServiceCode = 40;
constexpr std::uint32_t kTransparencyIndicator = 228;
constexpr std::uint32_t kFnur = 111;
constexpr std::uint32_t kUserProtocolIndicator = 280;
constexpr std::uint32_t kGuaranteedBitRate = 420;
constexpr std::uint32_t kMaximumBitRate = 421;
constexpr std::uint32_t kChargingTimeStamp = 74;
constexpr std::uint32_t kHSCSDIndicator = 424;

constexpr std::uint32_t kSupplServiceCode = 209;

constexpr std::uint32_t kThirdPartyInformation = 219;
constexpr std::uint32_t kThirdPartyNumber = 403;
constexpr std::uint32_t kClirIndicator = 75;

constexpr std::uint32_t kCamelServiceUsed = 57;
constexpr std::uint32_t kCamelServiceLevel = 56;
constexpr std::uint32_t kCamelServiceKey = 55;
constexpr std::uint32_t kDefaultCallHandling = 87;
constexpr std::uint32_t kExchangeRateCode = 105;
constexpr std::uint32_t kCamelInvocationFee = 422;
constexpr std::uint32_t kThreeGcamelDestination = 431; // CHOICE, real EXPLICIT wrap
constexpr std::uint32_t kCamelDestinationNumber = 404;
constexpr std::uint32_t kGprsDestination = 116;
constexpr std::uint32_t kAccessPointNameNI = 261;
constexpr std::uint32_t kAccessPointNameOI = 262;
constexpr std::uint32_t kCseInformation = 79;

constexpr std::uint32_t kOperatorSpecInformation = 163;
constexpr std::uint32_t kOperatorSpecInfoList = 162;
} // namespace MoCallTag

// SimChargeableSubscriber ::= [APPLICATION 199] SEQUENCE { imsi Imsi, msisdn Msisdn OPTIONAL }.
// MinChargeableSubscriber ::= [APPLICATION 254] SEQUENCE { min Min OPTIONAL, mdn Mdn OPTIONAL }.
// ChargeableSubscriber ::= [APPLICATION 427] CHOICE { simChargeableSubscriber,
// minChargeableSubscriber } -- real tagged CHOICE.
struct ChargeableSubscriber {
    bool isSim = true;
    std::optional<std::string> imsi;   // SIM branch: BCDString(SIZE(3..8))
    std::optional<std::string> msisdn; // SIM branch: BCDString(SIZE(1..9))
    std::optional<std::string> min;    // MIN branch: NumberString(SIZE(2..15))
    std::optional<std::string> mdn;    // MIN branch: NumberString
};
Tlv encode_chargeable_subscriber(const ChargeableSubscriber& v);
std::optional<ChargeableSubscriber> decode_chargeable_subscriber(const Tlv& tlv);

// Destination ::= [APPLICATION 89] SEQUENCE, all real fields OPTIONAL.
struct Destination {
    std::optional<std::string> calledNumber;
    std::optional<std::string> dialledDigits;
    std::optional<std::string> calledPlace;
    std::optional<std::string> calledRegion;
    std::optional<std::string> smsDestinationNumber;
};
Tlv encode_destination(const Destination& v);
std::optional<Destination> decode_destination(const Tlv& tlv);

// MoBasicCallInformation ::= [APPLICATION 147] SEQUENCE.
struct MoBasicCallInformation {
    std::optional<ChargeableSubscriber> chargeableSubscriber;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<Destination> destination;
    std::optional<std::string> destinationNetwork;
    std::optional<DateTime> callEventStartTimeStamp;
    std::optional<std::int32_t> totalCallEventDuration;
    std::optional<std::string> simToolkitIndicator;
    std::optional<std::int32_t> causeForTerm;
};
Tlv encode_mo_basic_call_information(const MoBasicCallInformation& v);
std::optional<MoBasicCallInformation> decode_mo_basic_call_information(const Tlv& tlv);

// NetworkLocation ::= [APPLICATION 156] SEQUENCE.
struct NetworkLocation {
    std::optional<std::int32_t> recEntityCode;
    std::optional<std::vector<std::uint8_t>> callReference; // real OCTET STRING(SIZE(1..8))
    std::optional<std::int32_t> locationArea;
    std::optional<std::int32_t> cellId;
};
// HomeLocationInformation ::= [APPLICATION 123] SEQUENCE.
struct HomeLocationInformation {
    std::optional<std::string> homeBid;
    std::optional<std::string> homeLocationDescription;
};
// GeographicalLocation ::= [APPLICATION 113] SEQUENCE.
struct GeographicalLocation {
    std::optional<std::string> servingNetwork;
    std::optional<std::string> servingBid;
    std::optional<std::string> servingLocationDescription;
};
// LocationInformation ::= [APPLICATION 138] SEQUENCE.
struct LocationInformation {
    std::optional<NetworkLocation> networkLocation;
    std::optional<HomeLocationInformation> homeLocationInformation;
    std::optional<GeographicalLocation> geographicalLocation;
};
Tlv encode_location_information(const LocationInformation& v);
std::optional<LocationInformation> decode_location_information(const Tlv& tlv);

// ImeiOrEsn ::= [APPLICATION 429] CHOICE { imei Imei, esn Esn } -- real tagged CHOICE.
struct ImeiOrEsn {
    bool isImei = true;
    std::string value; // Imei: BCDString(SIZE(7..8)); Esn: NumberString
};
Tlv encode_imei_or_esn(const ImeiOrEsn& v);
std::optional<ImeiOrEsn> decode_imei_or_esn(const Tlv& tlv);

// BasicServiceCode ::= [APPLICATION 426] CHOICE { teleServiceCode, bearerServiceCode } -- real
// tagged CHOICE, both branches real HexString(SIZE(2)).
struct BasicServiceCode {
    bool isTeleService = true;
    std::string value;
};
Tlv encode_basic_service_code(const BasicServiceCode& v);
std::optional<BasicServiceCode> decode_basic_service_code(const Tlv& tlv);

// BasicService ::= [APPLICATION 36] SEQUENCE.
struct BasicService {
    std::optional<BasicServiceCode> serviceCode;
    std::optional<std::int32_t> transparencyIndicator;
    std::optional<std::int32_t> fnur;
    std::optional<std::int32_t> userProtocolIndicator;
    std::optional<std::vector<std::uint8_t>> guaranteedBitRate; // OCTET STRING(SIZE(1))
    std::optional<std::vector<std::uint8_t>> maximumBitRate;    // OCTET STRING(SIZE(1))
};
// BasicServiceUsed ::= [APPLICATION 39] SEQUENCE. Real, disclosed scope narrowing:
// `chargeInformationList` (real, cited, further-nested charging-detail list -- now that
// tap3_charging.hpp exists, the type is modeled, but this session's own spec extraction did not
// confirm its declared field ORDER within BasicServiceUsed, only that it exists) remains deferred
// here rather than guessing a position; CamelServiceUsed's `taxInformation`/`discountInformation`
// are the same real situation, same reason.
struct BasicServiceUsed {
    std::optional<BasicService> basicService;
    std::optional<DateTime> chargingTimeStamp;
    std::optional<std::string> hscsdIndicator;
};
Tlv encode_basic_service_used(const BasicServiceUsed& v);
std::optional<BasicServiceUsed> decode_basic_service_used(const Tlv& tlv);

// ThirdPartyInformation ::= [APPLICATION 219] SEQUENCE.
struct ThirdPartyInformation {
    std::optional<std::string> thirdPartyNumber;
    std::optional<std::int32_t> clirIndicator;
};
Tlv encode_third_party_information(const ThirdPartyInformation& v);
std::optional<ThirdPartyInformation> decode_third_party_information(const Tlv& tlv);

// GprsDestination ::= [APPLICATION 116] SEQUENCE.
struct GprsDestination {
    std::optional<std::string> accessPointNameNI;
    std::optional<std::string> accessPointNameOI;
};
// ThreeGcamelDestination ::= [APPLICATION 431] CHOICE { camelDestinationNumber, gprsDestination }
// -- real tagged CHOICE.
struct ThreeGcamelDestination {
    bool isCamelDestinationNumber = true;
    std::optional<std::string> camelDestinationNumber; // real AddressStringDigits (BCDString)
    std::optional<GprsDestination> gprsDestination;
};
// CamelServiceUsed ::= [APPLICATION 57] SEQUENCE. Real, disclosed scope narrowing:
// `taxInformation`/`discountInformation` (real, cited, further-nested charging-detail lists) are
// deferred -- see this file's own header comment.
struct CamelServiceUsed {
    std::optional<std::int32_t> camelServiceLevel;
    std::optional<std::int32_t> camelServiceKey;
    std::optional<std::int32_t> defaultCallHandling;
    std::optional<std::int32_t> exchangeRateCode;
    std::optional<std::int32_t> camelInvocationFee;
    std::optional<ThreeGcamelDestination> threeGcamelDestination;
    std::optional<std::vector<std::uint8_t>> cseInformation; // OCTET STRING(SIZE(1..40))
};
Tlv encode_camel_service_used(const CamelServiceUsed& v);
std::optional<CamelServiceUsed> decode_camel_service_used(const Tlv& tlv);

// MobileOriginatedCall ::= [APPLICATION 9] SEQUENCE -- the real top-level CallEventDetail variant
// this slice implements.
struct MobileOriginatedCall {
    std::optional<MoBasicCallInformation> basicCallInformation;
    std::optional<LocationInformation> locationInformation;
    std::optional<ImeiOrEsn> equipmentIdentifier;
    std::vector<BasicServiceUsed> basicServiceUsedList;
    std::optional<std::string> supplServiceCode; // real HexString(SIZE(2))
    std::optional<ThirdPartyInformation> thirdPartyInformation;
    std::optional<CamelServiceUsed> camelServiceUsed;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_mobile_originated_call(const MobileOriginatedCall& v);
std::optional<MobileOriginatedCall> decode_mobile_originated_call(const Tlv& tlv);

} // namespace tap3_core
