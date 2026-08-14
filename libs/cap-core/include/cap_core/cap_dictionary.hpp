#pragma once

#include <cstdint>
#include <vector>

// Real CAMEL Application Part (CAP) constants -- 3GPP TS 29.078 V19.0.0 (CAP Phase 4), fetched
// directly from ETSI's own /deliver/ portal (specs/ts_129078v190000p.pdf, not committed --
// ETSI-copyrighted). P4.5/ADR-0059 Stage 6 (CAP) kickoff.
//
// Real, disclosed scope: only the circuit-switched call-control operations needed for the core
// InitialDP -> RequestReportBCSMEvent -> ApplyCharging -> EventReportBCSM -> ApplyChargingReport
// prepaid-charging flow (TS 29.078 clause 11) are covered here. SMS control (clause 12), GPRS
// control (clause 13), and the remaining gsmSSF/gsmSCF operations (Connect, CallGap,
// AssistRequestInstructions, etc.) are real, cited opcodes below but have no operation-argument
// codec yet in cap_operations.hpp.

namespace cap_core {

// Real operation local codes -- TS 29.078 clause 5.3 (CAP-operationcodes module).
namespace Opcode {
constexpr std::int32_t kInitialDp = 0;
constexpr std::int32_t kConnect = 20;
constexpr std::int32_t kReleaseCall = 22;
constexpr std::int32_t kRequestReportBcsmEvent = 23;
constexpr std::int32_t kEventReportBcsm = 24;
constexpr std::int32_t kCollectInformation = 27;
constexpr std::int32_t kContinue = 31;
constexpr std::int32_t kInitiateCallAttempt = 32;
constexpr std::int32_t kResetTimer = 33;
constexpr std::int32_t kFurnishChargingInformation = 34;
constexpr std::int32_t kApplyCharging = 35;
constexpr std::int32_t kApplyChargingReport = 36;
constexpr std::int32_t kCallGap = 41;
constexpr std::int32_t kCallInformationReport = 44;
constexpr std::int32_t kCallInformationRequest = 45;
constexpr std::int32_t kSendChargingInformation = 46;
constexpr std::int32_t kCancel = 53;
constexpr std::int32_t kActivityTest = 55;
} // namespace Opcode

// Real error local codes -- TS 29.078 clause 5.4 (CAP-errorcodes module).
namespace ErrorCode {
constexpr std::int32_t kCanceled = 0;
constexpr std::int32_t kCancelFailed = 1;
constexpr std::int32_t kEtcFailed = 3;
constexpr std::int32_t kImproperCallerResponse = 4;
constexpr std::int32_t kMissingCustomerRecord = 6;
constexpr std::int32_t kMissingParameter = 7;
constexpr std::int32_t kParameterOutOfRange = 8;
constexpr std::int32_t kRequestedInfoError = 10;
constexpr std::int32_t kSystemFailure = 11;
constexpr std::int32_t kTaskRefused = 12;
constexpr std::int32_t kUnavailableResource = 13;
constexpr std::int32_t kUnexpectedComponentSequence = 14;
constexpr std::int32_t kUnexpectedDataValue = 15;
constexpr std::int32_t kUnexpectedParameter = 16;
constexpr std::int32_t kUnknownLegId = 17;
constexpr std::int32_t kUnknownPdpId = 50;
constexpr std::int32_t kUnknownCsid = 51;
} // namespace ErrorCode

// Real Application Context OID -- TS 29.078 clause 5.6/17.3.2 (capssf-scfGenericAC): the gsmSSF's
// dialogue-initiating AC for a real InitialDP. Arc derivation, all real and cited:
//   id-CAPOE      = {itu-t(0) identified-organization(4) etsi(0) mobileDomain(0)
//                     umts-network(1) cap4OE(23)}
//   id-acE        = {id-CAPOE ac(3)}
//   id-ac-CAP-gsmSSF-scfGenericAC = {id-acE 4}
inline const std::vector<std::uint32_t> kGsmssfScfGenericAcOid = {0, 4, 0, 0, 1, 23, 3, 4};

} // namespace cap_core
