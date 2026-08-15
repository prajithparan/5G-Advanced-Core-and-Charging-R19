#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 SupplServiceEvent (real [APPLICATION 11]) -- reuses ChargeableSubscriber/
// LocationInformation/ImeiOrEsn/BasicServiceCode from tap3_mo_call.hpp and ChargeInformation from
// tap3_charging.hpp. See tap3_common.hpp's own header for the full real sourcing/scope disclosure.

namespace tap3_core {

namespace SupplServiceTag {
constexpr std::uint32_t kSupplServiceUsed = 206;
constexpr std::uint32_t kSupplServiceActionCode = 208;
constexpr std::uint32_t kSsParameters = 204;
constexpr std::uint32_t kBasicServiceCodeList = 37;
} // namespace SupplServiceTag

// SupplServiceUsed ::= [APPLICATION 206] SEQUENCE.
struct SupplServiceUsed {
    std::optional<std::string> supplServiceCode; // real HexString(SIZE(2)), reuse MoCallTag tag
    std::optional<std::int32_t> supplServiceActionCode;
    std::optional<std::string> ssParameters; // real AsciiString(SIZE(1..40))
    std::optional<DateTime> chargingTimeStamp;
    std::optional<ChargeInformation> chargeInformation;
    std::vector<BasicServiceCode> basicServiceCodeList;
};
Tlv encode_suppl_service_used(const SupplServiceUsed& v);
std::optional<SupplServiceUsed> decode_suppl_service_used(const Tlv& tlv);

// SupplServiceEvent ::= [APPLICATION 11] SEQUENCE.
struct SupplServiceEvent {
    std::optional<ChargeableSubscriber> chargeableSubscriber;
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<LocationInformation> locationInformation;
    std::optional<ImeiOrEsn> equipmentIdentifier;
    std::optional<SupplServiceUsed> supplServiceUsed;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_suppl_service_event(const SupplServiceEvent& v);
std::optional<SupplServiceEvent> decode_suppl_service_event(const Tlv& tlv);

} // namespace tap3_core
