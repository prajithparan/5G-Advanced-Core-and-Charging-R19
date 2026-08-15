#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 AggregatedUsageRecord (real [APPLICATION 453]) -- reuses CallTypeGroup's own
// callTypeLevel1/2/3 leaf tags and Tag::kTaxCode/kExchangeRateCode. See tap3_common.hpp's own
// header for the full real sourcing/scope disclosure.

namespace tap3_core {

namespace AurTag {
constexpr std::uint32_t kAggregatedUsageDateStart = 455;
constexpr std::uint32_t kAggregatedUsageDateEnd = 456;
constexpr std::uint32_t kAggregationType = 457;
constexpr std::uint32_t kAggregationIdentifier = 458;
constexpr std::uint32_t kAggregatedChrgUnitType = 459;
constexpr std::uint32_t kAggregatedChrgUnits = 460;
constexpr std::uint32_t kAggregatedUsageCharge = 461; // real 8-byte-INTEGER exception (Table 44)
constexpr std::uint32_t kAURTaxInformationList = 464;
constexpr std::uint32_t kAURTaxInformation = 454;
constexpr std::uint32_t kAURTaxValue = 462;      // real 8-byte-INTEGER exception (Table 44)
constexpr std::uint32_t kAURTaxableAmount = 463; // real 8-byte-INTEGER exception (Table 44)
} // namespace AurTag

// AURTaxInformation ::= [APPLICATION 454] SEQUENCE.
struct AURTaxInformation {
    std::optional<std::int32_t> taxCode;
    std::optional<std::int64_t> aurTaxValue;
    std::optional<std::int64_t> aurTaxableAmount;
};
Tlv encode_aur_tax_information(const AURTaxInformation& v);
std::optional<AURTaxInformation> decode_aur_tax_information(const Tlv& tlv);

// AggregatedUsageRecord ::= [APPLICATION 453] SEQUENCE. Real, disclosed reading: this session's
// own spec extraction could not confirm whether `operatorSpecInformation`'s real list-type name
// here is the same `OperatorSpecInfoList` (tag 162) used everywhere else in this module, or a
// distinct `OperatorSpecInformationList` -- modeled reusing MoCallTag::kOperatorSpecInfoList/
// kOperatorSpecInformation (the same real tags used by every other variant in this codec) since
// that is the only real tag pair actually confirmed in this session's own reading; flagged, not
// resolved, if a genuine TAP3 sample later shows AggregatedUsageRecord uses a different tag here.
struct AggregatedUsageRecord {
    std::optional<std::string> aggregatedUsageDateStart; // real LocalDate (NumberString(SIZE(8)))
    std::optional<std::string> aggregatedUsageDateEnd;
    std::optional<std::string> servingNetwork;
    std::optional<std::int32_t> aggregationType;
    std::optional<std::string> aggregationIdentifier;
    std::optional<std::int32_t> callTypeLevel1;
    std::optional<std::int32_t> callTypeLevel2;
    std::optional<std::int32_t> callTypeLevel3;
    std::optional<std::int32_t> aggregatedChrgUnitType;
    std::optional<std::int32_t> aggregatedChrgUnits;
    std::optional<std::int64_t> aggregatedUsageCharge;
    std::optional<std::int32_t> exchangeRateCode;
    std::vector<AURTaxInformation> aurTaxInformationList;
    std::optional<std::string> rapFileSequenceNumber;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_aggregated_usage_record(const AggregatedUsageRecord& v);
std::optional<AggregatedUsageRecord> decode_aggregated_usage_record(const Tlv& tlv);

} // namespace tap3_core
