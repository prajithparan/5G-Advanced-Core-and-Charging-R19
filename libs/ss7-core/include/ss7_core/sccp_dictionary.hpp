#pragma once

#include <cstdint>

// Real SCCP (ITU-T Q.713) message-type/parameter/cause constants -- P4.5/ADR-0059 Stage 5a.
// Primary ITU-T Q.713 text itself is gated behind ITU's own login portal (unlike the freely
// published IETF RFCs and 3GPP ETSI PDFs this project's other Diameter/Sy work could read
// directly) -- every constant below is instead extracted from the real, vendored Osmocom
// `sccp/sccp_types.h` (simulators/reference/osmocom/, GPL-2+, arms-length reference ONLY, not
// linked -- see docs/DECISIONS.md's own Stage 5 ADR update for the full real license-check
// evidence). Osmocom's own header cites the exact real ITU-T table/figure/section number for each
// fact (a mature, real-world SS7 implementation's own citations, not this project's guess) -- each
// constant below repeats that same citation. This is a different, more indirect evidence tier than
// this project's other protocol work (which could quote primary spec text directly), disclosed
// honestly rather than presented as equally strong.

namespace ss7_core::dictionary {

// SCCP message types -- Table 1/Q.713.
namespace MessageType {
constexpr std::uint8_t kCr = 1;
constexpr std::uint8_t kCc = 2;
constexpr std::uint8_t kCref = 3;
constexpr std::uint8_t kRlsd = 4;
constexpr std::uint8_t kRlc = 5;
constexpr std::uint8_t kDt1 = 6;
constexpr std::uint8_t kDt2 = 7;
constexpr std::uint8_t kAk = 8;
constexpr std::uint8_t kUdt = 9;
constexpr std::uint8_t kUdts = 10;
constexpr std::uint8_t kEd = 11;
constexpr std::uint8_t kEa = 12;
constexpr std::uint8_t kRsr = 13;
constexpr std::uint8_t kRsc = 14;
constexpr std::uint8_t kErr = 15;
constexpr std::uint8_t kIt = 16;
constexpr std::uint8_t kXudt = 17;
constexpr std::uint8_t kXudts = 18;
constexpr std::uint8_t kLudt = 19;
constexpr std::uint8_t kLudts = 20;
} // namespace MessageType

// SCCP parameter name codes -- Table 2/Q.713.
namespace ParameterNameCode {
constexpr std::uint8_t kEndOfOptional = 0;
constexpr std::uint8_t kDestinationLocalReference = 1;
constexpr std::uint8_t kSourceLocalReference = 2;
constexpr std::uint8_t kCalledPartyAddress = 3;
constexpr std::uint8_t kCallingPartyAddress = 4;
constexpr std::uint8_t kProtocolClass = 5;
constexpr std::uint8_t kSegmenting = 6;
constexpr std::uint8_t kReceiveSeqNumber = 7;
constexpr std::uint8_t kSequencing = 8;
constexpr std::uint8_t kCredit = 9;
constexpr std::uint8_t kReleaseCause = 10;
constexpr std::uint8_t kReturnCause = 11;
constexpr std::uint8_t kResetCause = 12;
constexpr std::uint8_t kErrorCause = 13;
constexpr std::uint8_t kRefusalCause = 14;
constexpr std::uint8_t kData = 15;
constexpr std::uint8_t kSegmentation = 16;
constexpr std::uint8_t kHopCounter = 17;
constexpr std::uint8_t kImportance = 18;
constexpr std::uint8_t kLongData = 19;
} // namespace ParameterNameCode

// Called/Calling Party Address indicator octet -- Figure 3/Q.713. Only
// GlobalTitleIndicator::kNone (no global title, point-code+SSN routing) is consumed by this
// codebase's own real SccpAddress codec (Stage 5a scope) -- the fuller Global-Title-based
// addressing (GTI 1-4, needed for real STP-routed international MAP signalling) is a real,
// disclosed gap, not implemented this stage (see sccp_address.hpp's own header).
namespace GlobalTitleIndicator {
constexpr std::uint8_t kNone = 0;
} // namespace GlobalTitleIndicator

namespace RoutingIndicator {
constexpr std::uint8_t kRouteOnGt = 0;
constexpr std::uint8_t kRouteOnSsn = 1;
} // namespace RoutingIndicator

// Subsystem numbers -- real GSM/3GPP-relevant subset only (the values this codebase's own future
// MAP work will actually need), not the full Q.713 table. Sourced from Osmocom's own header, which
// itself cites "GSM 03.03 8.2" for the BSSAP/BSSOM values.
namespace SubsystemNumber {
constexpr std::uint8_t kNotKnownOrUsed = 0;
constexpr std::uint8_t kManagement = 1;
constexpr std::uint8_t kHlr = 6;
constexpr std::uint8_t kVlr = 7;
constexpr std::uint8_t kMsc = 8;
} // namespace SubsystemNumber

// Protocol class -- ITU-T Q.714 §3.6 (Osmocom's own header cites Q.714 here, not Q.713 -- a real
// cross-reference between the two related ITU-T recommendations, not a mistake).
namespace ProtocolClass {
constexpr std::uint8_t kClass0 = 0;
constexpr std::uint8_t kClass1 = 1;
constexpr std::uint8_t kClass2 = 2;
constexpr std::uint8_t kClass3 = 3;
} // namespace ProtocolClass

// Return cause values (used by UDTS -- the "your UDT could not be delivered" service message).
namespace ReturnCause {
constexpr std::uint8_t kNoTranslationForAddressNature = 0;
constexpr std::uint8_t kNoTranslation = 1;
constexpr std::uint8_t kSubsystemCongestion = 2;
constexpr std::uint8_t kSubsystemFailure = 3;
constexpr std::uint8_t kUnequippedUser = 4;
constexpr std::uint8_t kMtpFailure = 5;
constexpr std::uint8_t kNetworkCongestion = 6;
constexpr std::uint8_t kUnqualified = 7;
} // namespace ReturnCause

} // namespace ss7_core::dictionary
