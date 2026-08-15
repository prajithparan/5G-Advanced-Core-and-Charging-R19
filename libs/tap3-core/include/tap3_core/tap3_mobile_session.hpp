#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_messaging_event.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 MobileSession (real [APPLICATION 434]) -- reuses ChargedParty/NonChargedParty from
// tap3_messaging_event.hpp (real spec fact: MobileSession shares these exact same types with
// MessagingEvent) and CallTypeGroup/ChargeDetail/TaxInformation from tap3_charging.hpp. See
// tap3_common.hpp's own header for the full real sourcing/scope disclosure.

namespace tap3_core {

namespace MobileSessionTag {
constexpr std::uint32_t kMobileSessionService = 440;
constexpr std::uint32_t kRequestedDestination = 450;
constexpr std::uint32_t kRequestedNumber = 451;
constexpr std::uint32_t kRequestedPublicUserId = 452;
constexpr std::uint32_t kSessionChargeInformation = 449;
constexpr std::uint32_t kSessionChargeInfoList = 448;
} // namespace MobileSessionTag

// RequestedDestination ::= [APPLICATION 450] SEQUENCE.
struct RequestedDestination {
    std::optional<std::string> requestedNumber; // real AddressStringDigits
    std::optional<std::string> requestedPublicUserId;
};

// SessionChargeInformation ::= [APPLICATION 449] SEQUENCE.
struct SessionChargeInformation {
    std::optional<std::string> chargedItem;
    std::optional<std::int32_t> exchangeRateCode;
    std::optional<CallTypeGroup> callTypeGroup;
    std::vector<ChargeDetail> chargeDetailList;
    std::vector<TaxInformation> taxInformationList;
};

// MobileSession ::= [APPLICATION 434] SEQUENCE.
struct MobileSession {
    std::optional<std::int32_t> mobileSessionService;
    std::optional<ChargedParty> chargedParty;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<std::string> simToolkitIndicator;
    std::optional<GeographicalLocation> geographicalLocation;
    std::optional<std::int32_t> locationArea;
    std::optional<std::int32_t> cellId;
    std::optional<std::string> eventReference;
    std::vector<std::int32_t> recEntityCodeList;
    std::optional<DateTime> serviceStartTimestamp;
    std::optional<std::int32_t> causeForTerm;
    std::optional<std::int32_t> totalCallEventDuration;
    std::optional<NonChargedParty> nonChargedParty;
    std::optional<RequestedDestination> requestedDestination;
    std::vector<SessionChargeInformation> sessionChargeInfoList;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_mobile_session(const MobileSession& v);
std::optional<MobileSession> decode_mobile_session(const Tlv& tlv);

} // namespace tap3_core
