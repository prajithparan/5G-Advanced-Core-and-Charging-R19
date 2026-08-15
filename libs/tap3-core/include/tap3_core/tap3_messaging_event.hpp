#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 MessagingEvent (real [APPLICATION 433]) -- reuses GeographicalLocation from
// tap3_mo_call.hpp and CallTypeGroup/ChargingTag::kCharge/TaxInformation from tap3_charging.hpp.
// ChargedParty/NonChargedParty defined here are also reused by tap3_mobile_session.hpp (real
// spec fact: both variants share these exact same types). See tap3_common.hpp's own header for
// the full real sourcing/scope disclosure.

namespace tap3_core {

namespace MsgEventTag {
constexpr std::uint32_t kMessagingEventService = 439;
constexpr std::uint32_t kChargedParty = 436;
constexpr std::uint32_t kPublicUserId = 446;
constexpr std::uint32_t kEventReference = 435;
constexpr std::uint32_t kNetworkElementList = 442;
constexpr std::uint32_t kNetworkElement = 441;
constexpr std::uint32_t kElementType = 438;
constexpr std::uint32_t kElementId = 437;
constexpr std::uint32_t kServiceStartTimestamp = 447;
constexpr std::uint32_t kNonChargedParty = 443;
constexpr std::uint32_t kNonChargedPartyNumber = 444;
constexpr std::uint32_t kNonChargedPublicUserId = 445;
constexpr std::uint32_t kRecEntityCodeList =
    185; // same real tag as GprsCallTag::kRecEntityCodeList
} // namespace MsgEventTag

// ChargedParty ::= [APPLICATION 436] SEQUENCE.
struct ChargedParty {
    std::optional<std::string> imsi;
    std::optional<std::string> msisdn;
    std::optional<std::string> publicUserId;
    std::optional<std::string> homeBid;
    std::optional<std::string> homeLocationDescription;
    std::optional<std::string> imei;
};
Tlv encode_charged_party(const ChargedParty& v);
std::optional<ChargedParty> decode_charged_party(const Tlv& tlv);

// NetworkElement ::= [APPLICATION 441] SEQUENCE.
struct NetworkElement {
    std::optional<std::int32_t> elementType;
    std::optional<std::string> elementId;
};

// NonChargedParty ::= [APPLICATION 443] SEQUENCE.
struct NonChargedParty {
    std::optional<std::string> nonChargedPartyNumber; // real AddressStringDigits (BCDString)
    std::optional<std::string> nonChargedPublicUserId;
};
Tlv encode_non_charged_party(const NonChargedParty& v);
std::optional<NonChargedParty> decode_non_charged_party(const Tlv& tlv);

// MessagingEvent ::= [APPLICATION 433] SEQUENCE.
struct MessagingEvent {
    std::optional<std::int32_t> messagingEventService;
    std::optional<ChargedParty> chargedParty;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<std::string> simToolkitIndicator;
    std::optional<GeographicalLocation> geographicalLocation;
    std::optional<std::string> eventReference;
    std::vector<std::int32_t> recEntityCodeList;
    std::vector<NetworkElement> networkElementList;
    std::optional<std::int32_t> locationArea;
    std::optional<std::int32_t> cellId;
    std::optional<DateTime> serviceStartTimestamp;
    std::optional<NonChargedParty> nonChargedParty;
    std::optional<std::int32_t> exchangeRateCode;
    std::optional<CallTypeGroup> callTypeGroup;
    std::optional<std::int32_t> charge;
    std::vector<TaxInformation> taxInformationList;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_messaging_event(const MessagingEvent& v);
std::optional<MessagingEvent> decode_messaging_event(const Tlv& tlv);

} // namespace tap3_core
