#pragma once

#include <cstdint>

// Real Diameter command codes and base/DCC AVP codes -- P4.5/ADR-0059 Stage 1. Every constant
// below is read directly from freeDiameter's own real, vendored dictionary source
// (simulators/reference/freeDiameter/, commit e48fd4f8afc48f5e839558a90ef5a67165e94fad, BSD
// licensed -- see ADR-0059 for the license check), never invented. Each constant cites its exact
// source file.

namespace diameter_core::dictionary {

// Command codes -- simulators/reference/freeDiameter/libfdcore/dict_base_proto.c (RFC 6733 base
// protocol) and .../extensions/dict_dcca/dict_dcca.c (RFC 4006 Credit-Control Application).
namespace Command {
constexpr std::uint32_t kCapabilitiesExchange = 257; // dict_base_proto.c:2604
constexpr std::uint32_t kDeviceWatchdog = 280;       // dict_base_proto.c:2791
constexpr std::uint32_t kDisconnectPeer = 282;       // dict_base_proto.c:2714
constexpr std::uint32_t kCreditControl = 272;        // dict_dcca.c:1353 (RFC 4006, real Gy command)
constexpr std::uint32_t kAccounting = 271; // dict_base_proto.c:3236 (RFC 6733 base ACR/ACA, real Rf
                                           // command -- TS 32.299 Rf runs the Diameter Base
                                           // Accounting application over this same real command)
} // namespace Command

// Base protocol AVP codes -- dict_base_proto.c. All base AVPs carry AVP_FLAG_VENDOR |
// AVP_FLAG_MANDATORY as their *fixed flags mask* in the real dictionary (meaning the V bit must
// always be explicitly set-or-cleared, not that V is always set) -- Vendor-Id is 0/absent for
// every one of these (they are IETF base AVPs, not vendor-specific).
namespace Avp {
constexpr std::uint32_t kSessionId = 263;        // dict_base_proto.c:1774
constexpr std::uint32_t kOriginHost = 264;       // dict_base_proto.c:548
constexpr std::uint32_t kVendorId = 266;         // dict_base_proto.c:370
constexpr std::uint32_t kFirmwareRevision = 267; // dict_base_proto.c:396
constexpr std::uint32_t kResultCode = 268;       // dict_base_proto.c:1109
constexpr std::uint32_t kProductName = 269;      // dict_base_proto.c:469
constexpr std::uint32_t kDisconnectCause = 273;  // dict_base_proto.c:513
constexpr std::uint32_t kOriginStateId = 278;    // dict_base_proto.c:2050
constexpr std::uint32_t kErrorMessage = 281;     // dict_base_proto.c:1493
constexpr std::uint32_t kDestinationRealm = 283; // dict_base_proto.c:632
constexpr std::uint32_t kDestinationHost = 293;  // dict_base_proto.c:600
constexpr std::uint32_t kOriginRealm = 296;      // dict_base_proto.c:571
constexpr std::uint32_t kHostIpAddress =
    257; // dict_base_proto.c:420 (same numeric value as CER/CEA's command code -- different
         // namespace, real, not a typo)
constexpr std::uint32_t kAuthApplicationId = 258;           // dict_base_proto.c:750
constexpr std::uint32_t kAcctApplicationId = 259;           // dict_base_proto.c:774
constexpr std::uint32_t kVendorSpecificApplicationId = 260; // dict_base_proto.c:882
constexpr std::uint32_t kSupportedVendorId = 265;           // dict_base_proto.c:447
constexpr std::uint32_t kInbandSecurityId = 299;            // dict_base_proto.c:817
constexpr std::uint32_t kAccountingRecordType = 480;        // dict_base_proto.c:2312 (Enumerated)
constexpr std::uint32_t kAccountingRecordNumber = 485;      // dict_base_proto.c:2388 (Unsigned32)
} // namespace Avp

// Real Diameter Base Accounting application (RFC 6733 §9, dict_base_proto.c:107: "Diameter Base
// Accounting") -- distinct real Application-Id from RFC 4006 DCC's own Dcc::kApplicationId=4
// below. TS 32.299's Rf reference point runs this same real base-protocol accounting application,
// not a 3GPP-specific one.
namespace BaseAccounting {
constexpr std::uint32_t kApplicationId = 3; // dict_base_proto.c:107

// Accounting-Record-Type real enumerated values -- dict_base_proto.c:2304-2308.
namespace AccountingRecordType {
constexpr std::int32_t kEvent = 1;
constexpr std::int32_t kStart = 2;
constexpr std::int32_t kInterim = 3;
constexpr std::int32_t kStop = 4;
} // namespace AccountingRecordType
} // namespace BaseAccounting

// Real Result-Code enumerated values -- include/libfdproto.h (the #define macros
// dict_base_proto.c's own real DIAMETER_SUCCESS/DIAMETER_MISSING_AVP enum registrations use).
namespace ResultCode {
constexpr std::int32_t kDiameterSuccess = 2001;          // libfdproto.h:1854
constexpr std::int32_t kDiameterUnknownSessionId = 5002; // libfdproto.h:1873
constexpr std::int32_t kDiameterMissingAvp = 5005;       // libfdproto.h:1876
constexpr std::int32_t kDiameterUnableToComply = 5012;   // libfdproto.h:1883
// RFC 4006 §9.9 extended result codes, registered by dict_dcca.c against the base Result-Code
// enumerated type -- extensions/dict_dcca/dict_dcca.c:85-104 (not in libfdproto.h, which only
// carries the base-protocol values; DCC's own extension registers these at dictionary load time).
constexpr std::int32_t kDiameterEndUserServiceDenied = 4010; // dict_dcca.c:85
constexpr std::int32_t kDiameterCreditLimitReached = 4012;   // dict_dcca.c:95
constexpr std::int32_t kDiameterUserUnknown = 5030;          // dict_dcca.c:100
constexpr std::int32_t kDiameterRatingFailed = 5031;         // dict_dcca.c:105
} // namespace ResultCode

// RFC 4006 Diameter Credit-Control (DCC) AVP codes -- dict_dcca.c. This is the base DCC
// application (Auth-Application-Id 4); 3GPP's own Ro/Rf/Gy AVPs (TS 32.299) are a further
// extension on top of these, vendored separately at
// simulators/reference/freeDiameter/extensions/dict_dcca_3gpp/dict_dcca_3gpp.c and not yet
// consumed by this Stage 1 codec (Stage 3's own deliverable, per ADR-0059).
namespace Dcc {
// RFC 4006 §1 (quoted verbatim in dict_dcca.c:1257-1258): "The Auth-Application-Id MUST be set to
// the value 4, indicating the Diameter credit-control application."
constexpr std::uint32_t kApplicationId = 4;

constexpr std::uint32_t kCcRequestNumber = 415;               // dict_dcca.c:182
constexpr std::uint32_t kCcRequestType = 416;                 // dict_dcca.c:207
constexpr std::uint32_t kCcServiceSpecificUnits = 417;        // dict_dcca.c:230
constexpr std::uint32_t kCcTotalOctets = 421;                 // dict_dcca.c:307
constexpr std::uint32_t kRatingGroup = 432;                   // dict_dcca.c:582
constexpr std::uint32_t kFinalUnitIndication = 430;           // dict_dcca.c:967
constexpr std::uint32_t kGrantedServiceUnit = 431;            // dict_dcca.c:1109 (Grouped)
constexpr std::uint32_t kSubscriptionId = 443;                // dict_dcca.c:972 (Grouped)
constexpr std::uint32_t kRequestedServiceUnit = 437;          // dict_dcca.c:1160 (Grouped)
constexpr std::uint32_t kMultipleServicesCreditControl = 456; // dict_dcca.c:1216 (Grouped)
constexpr std::uint32_t kServiceContextId = 461;              // dict_dcca.c:694
constexpr std::uint32_t kUsedServiceUnit = 446;               // dict_dcca.c:1187 (Grouped)
constexpr std::uint32_t kValidityTime = 448;                  // dict_dcca.c:906
constexpr std::uint32_t kSubscriptionIdData = 444;            // dict_dcca.c:754 (UTF8String)
constexpr std::uint32_t kSubscriptionIdType = 450;            // dict_dcca.c:774 (Enumerated)

// CC-Request-Type real enumerated values (RFC 4006 §8.7).
namespace CcRequestType {
constexpr std::int32_t kInitial = 1;
constexpr std::int32_t kUpdate = 2;
constexpr std::int32_t kTermination = 3;
constexpr std::int32_t kEvent = 4;
} // namespace CcRequestType

// Subscription-Id-Type real enumerated values -- dict_dcca.c:772-775. Only END_USER_IMSI is
// consumed by this project's own CCR decoder (chf's real SUPI convention is IMSI-based, see
// nfs/udm/src/main.cpp's seeded subscribers) -- the other three are real, cited values, not
// invented, even though unused today.
namespace SubscriptionIdType {
constexpr std::int32_t kEndUserE164 = 0;
constexpr std::int32_t kEndUserImsi = 1;
constexpr std::int32_t kEndUserSipUri = 2;
constexpr std::int32_t kEndUserNai = 3;
} // namespace SubscriptionIdType
} // namespace Dcc

} // namespace diameter_core::dictionary
