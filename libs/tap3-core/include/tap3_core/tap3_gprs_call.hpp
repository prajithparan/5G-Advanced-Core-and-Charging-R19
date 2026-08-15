#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 GprsCall (real [APPLICATION 14]) -- reuses ChargeableSubscriber/GprsDestination/ImeiOrEsn/
// CamelServiceUsed/HomeLocationInformation/GeographicalLocation from tap3_mo_call.hpp and
// ChargeInformation from tap3_charging.hpp. See tap3_common.hpp's own header for the full real
// sourcing/scope disclosure.

namespace tap3_core {

namespace GprsCallTag {
constexpr std::uint32_t kGprsBasicCallInformation = 114;
constexpr std::uint32_t kGprsChargeableSubscriber = 115;
constexpr std::uint32_t kPdpAddress = 167;
constexpr std::uint32_t kNetworkAccessIdentifier = 417;
constexpr std::uint32_t kPartialTypeIndicator = 166;
constexpr std::uint32_t kPDPContextStartTimestamp = 260;
constexpr std::uint32_t kNetworkInitPDPContext = 245;
constexpr std::uint32_t kChargingId = 72; // real 8-byte-INTEGER exception (Table 44)
constexpr std::uint32_t kGprsLocationInformation = 117;
constexpr std::uint32_t kGprsNetworkLocation = 118;
constexpr std::uint32_t kRecEntityCodeList = 185;
constexpr std::uint32_t kGprsServiceUsed = 121;
constexpr std::uint32_t kIMSSignallingContext = 418;
constexpr std::uint32_t kDataVolumeIncoming = 250; // real 8-byte-INTEGER exception (Table 44)
constexpr std::uint32_t kDataVolumeOutgoing = 251; // real 8-byte-INTEGER exception (Table 44)
} // namespace GprsCallTag

// GprsChargeableSubscriber ::= [APPLICATION 115] SEQUENCE.
struct GprsChargeableSubscriber {
    std::optional<ChargeableSubscriber> chargeableSubscriber;
    std::optional<std::string> pdpAddress; // real AsciiString (PacketDataProtocolAddress)
    std::optional<std::string> networkAccessIdentifier;
};
Tlv encode_gprs_chargeable_subscriber(const GprsChargeableSubscriber& v);
std::optional<GprsChargeableSubscriber> decode_gprs_chargeable_subscriber(const Tlv& tlv);

// GprsBasicCallInformation ::= [APPLICATION 114] SEQUENCE.
struct GprsBasicCallInformation {
    std::optional<GprsChargeableSubscriber> gprsChargeableSubscriber;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<GprsDestination> gprsDestination;
    std::optional<DateTime> callEventStartTimeStamp;
    std::optional<std::int32_t> totalCallEventDuration;
    std::optional<std::int32_t> causeForTerm;
    std::optional<std::string> partialTypeIndicator; // real AsciiString(SIZE(1))
    std::optional<DateTime> pdpContextStartTimestamp;
    std::optional<std::int32_t> networkInitPDPContext;
    std::optional<std::int64_t> chargingId;
};
Tlv encode_gprs_basic_call_information(const GprsBasicCallInformation& v);
std::optional<GprsBasicCallInformation> decode_gprs_basic_call_information(const Tlv& tlv);

// GprsNetworkLocation ::= [APPLICATION 118] SEQUENCE.
struct GprsNetworkLocation {
    std::vector<std::int32_t> recEntity; // real RecEntityCodeList (SEQUENCE OF RecEntityCode)
    std::optional<std::int32_t> locationArea;
    std::optional<std::int32_t> cellId;
};
// GprsLocationInformation ::= [APPLICATION 117] SEQUENCE.
struct GprsLocationInformation {
    std::optional<GprsNetworkLocation> gprsNetworkLocation;
    std::optional<HomeLocationInformation> homeLocationInformation;
    std::optional<GeographicalLocation> geographicalLocation;
};
Tlv encode_gprs_location_information(const GprsLocationInformation& v);
std::optional<GprsLocationInformation> decode_gprs_location_information(const Tlv& tlv);

// GprsServiceUsed ::= [APPLICATION 121] SEQUENCE.
struct GprsServiceUsed {
    std::optional<std::int32_t> imsSignallingContext;
    std::optional<std::int64_t> dataVolumeIncoming;
    std::optional<std::int64_t> dataVolumeOutgoing;
    std::vector<ChargeInformation> chargeInformationList;
};
Tlv encode_gprs_service_used(const GprsServiceUsed& v);
std::optional<GprsServiceUsed> decode_gprs_service_used(const Tlv& tlv);

// GprsCall ::= [APPLICATION 14] SEQUENCE.
struct GprsCall {
    std::optional<GprsBasicCallInformation> gprsBasicCallInformation;
    std::optional<GprsLocationInformation> gprsLocationInformation;
    std::optional<ImeiOrEsn> equipmentIdentifier;
    std::optional<GprsServiceUsed> gprsServiceUsed;
    std::optional<CamelServiceUsed> camelServiceUsed;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_gprs_call(const GprsCall& v);
std::optional<GprsCall> decode_gprs_call(const Tlv& tlv);

} // namespace tap3_core
