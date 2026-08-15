#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 MobileTerminatedCall (real [APPLICATION 10]) -- reuses ChargeableSubscriber/
// LocationInformation/ImeiOrEsn/BasicServiceUsed/CamelServiceUsed from tap3_mo_call.hpp (real
// spec fact: MT reuses these exact same types, per this session's own spec extraction). See
// tap3_common.hpp's own header for the full real sourcing/scope disclosure.

namespace tap3_core {

namespace MtCallTag {
constexpr std::uint32_t kMtBasicCallInformation = 153;
constexpr std::uint32_t kCallOriginator = 41;
constexpr std::uint32_t kCallingNumber = 405;
constexpr std::uint32_t kOriginatingNetwork = 164;
constexpr std::uint32_t kSMSOriginator = 425;
} // namespace MtCallTag

// CallOriginator ::= [APPLICATION 41] SEQUENCE { callingNumber CallingNumber OPT, clirIndicator
// ClirIndicator OPT (reuse MoCallTag::kClirIndicator), sMSOriginator SMSOriginator OPT }.
struct CallOriginator {
    std::optional<std::string> callingNumber; // real AddressStringDigits (BCDString)
    std::optional<std::int32_t> clirIndicator;
    std::optional<std::string> smsOriginator; // real AsciiString
};
Tlv encode_call_originator(const CallOriginator& v);
std::optional<CallOriginator> decode_call_originator(const Tlv& tlv);

// MtBasicCallInformation ::= [APPLICATION 153] SEQUENCE.
struct MtBasicCallInformation {
    std::optional<ChargeableSubscriber> chargeableSubscriber;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<CallOriginator> callOriginator;
    std::optional<std::string> originatingNetwork; // real NetworkId (AsciiString(1..6))
    std::optional<DateTime> callEventStartTimeStamp;
    std::optional<std::int32_t> totalCallEventDuration;
    std::optional<std::string> simToolkitIndicator;
    std::optional<std::int32_t> causeForTerm;
};
Tlv encode_mt_basic_call_information(const MtBasicCallInformation& v);
std::optional<MtBasicCallInformation> decode_mt_basic_call_information(const Tlv& tlv);

// MobileTerminatedCall ::= [APPLICATION 10] SEQUENCE. No supplServiceCode/thirdPartyInformation
// (those are MO-only per this session's own spec extraction).
struct MobileTerminatedCall {
    std::optional<MtBasicCallInformation> basicCallInformation;
    std::optional<LocationInformation> locationInformation;
    std::optional<ImeiOrEsn> equipmentIdentifier;
    std::vector<BasicServiceUsed> basicServiceUsedList;
    std::optional<CamelServiceUsed> camelServiceUsed;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_mobile_terminated_call(const MobileTerminatedCall& v);
std::optional<MobileTerminatedCall> decode_mobile_terminated_call(const Tlv& tlv);

} // namespace tap3_core
