#pragma once

#include <optional>
#include <string>

#include "tap3_core/tap3_charging.hpp"
#include "tap3_core/tap3_common.hpp"
#include "tap3_core/tap3_content_transaction.hpp"
#include "tap3_core/tap3_mo_call.hpp"

// TAP3 LocationService (real [APPLICATION 297]) -- LCS/tracking CallEventDetail variant. Reuses
// InternetServiceProvider from tap3_content_transaction.hpp and ChargeInformation from
// tap3_charging.hpp. See tap3_common.hpp's own header for the full real sourcing/scope
// disclosure. Real spec fact: several Tracking*/Tracked* sub-entities are structurally identical
// (customerIdType+customerIdentifier / homeIdType+homeIdentifier / locationIdType+
// locationIdentifier / equipmentIdType+equipmentId) but are DISTINCT real types with distinct
// real tags -- modeled as distinct structs, not reused across Tracking/Tracked despite the shape
// match (same discipline already used for GsmChargeableSubscriber vs SimChargeableSubscriber).

namespace tap3_core {

namespace LocSvcTag {
constexpr std::uint32_t kLocationService = Tag::kLocationService;
constexpr std::uint32_t kCallReference = 45; // reuse MoCallTag::kCallReference

constexpr std::uint32_t kTrackingCustomerInformation = 298;
constexpr std::uint32_t kTrackingCustomerIdList = 299;
constexpr std::uint32_t kTrackingCustomerIdentification = 362;
constexpr std::uint32_t kCustomerIdType = 363;
constexpr std::uint32_t kCustomerIdentifier = 364;
constexpr std::uint32_t kTrackingCustomerHomeIdList = 365;
constexpr std::uint32_t kTrackingCustomerHomeId = 366;
constexpr std::uint32_t kTrackingCustomerLocList = 368;
constexpr std::uint32_t kTrackingCustomerLocation = 369;
constexpr std::uint32_t kTrackingCustomerEquipment = 371;

constexpr std::uint32_t kLCSSPInformation = 373;
constexpr std::uint32_t kLCSSPIdentificationList = 374;
constexpr std::uint32_t kLCSSPIdentification = 375;
constexpr std::uint32_t kISPList = 378;

constexpr std::uint32_t kTrackedCustomerInformation = 367;
constexpr std::uint32_t kTrackedCustomerIdList = 370;
constexpr std::uint32_t kTrackedCustomerIdentification = 372;
constexpr std::uint32_t kTrackedCustomerHomeIdList = 376;
constexpr std::uint32_t kTrackedCustomerHomeId = 377;
constexpr std::uint32_t kTrackedCustomerLocList = 379;
constexpr std::uint32_t kTrackedCustomerLocation = 380;
constexpr std::uint32_t kTrackedCustomerEquipment = 381;

constexpr std::uint32_t kLocationServiceUsage = 382;
constexpr std::uint32_t kLCSQosRequested = 383;
constexpr std::uint32_t kLCSRequestTimestamp = 384;
constexpr std::uint32_t kHorizontalAccuracyRequested = 385;
constexpr std::uint32_t kVerticalAccuracyRequested = 386;
constexpr std::uint32_t kResponseTimeCategory = 387;
constexpr std::uint32_t kTrackingPeriod = 388;
constexpr std::uint32_t kTrackingFrequency = 389;
constexpr std::uint32_t kLCSQosDelivered = 390;
constexpr std::uint32_t kLCSTransactionStatus = 391;
constexpr std::uint32_t kHorizontalAccuracyDelivered = 392;
constexpr std::uint32_t kVerticalAccuracyDelivered = 393;
constexpr std::uint32_t kResponseTime = 394;
constexpr std::uint32_t kPositioningMethod = 395;
constexpr std::uint32_t kAgeOfLocation = 396;
} // namespace LocSvcTag

struct TrackingCustomerIdentification {
    std::optional<std::int32_t> customerIdType;
    std::optional<std::string> customerIdentifier;
};
struct TrackingCustomerHomeId {
    std::optional<std::int32_t> homeIdType;
    std::optional<std::string> homeIdentifier;
};
struct TrackingCustomerLocation {
    std::optional<std::int32_t> locationIdType;
    std::optional<std::string> locationIdentifier;
};
struct TrackingCustomerEquipment {
    std::optional<std::int32_t> equipmentIdType;
    std::optional<std::string> equipmentId;
};
struct TrackingCustomerInformation {
    std::vector<TrackingCustomerIdentification> trackingCustomerIdList;
    std::vector<TrackingCustomerHomeId> trackingCustomerHomeIdList;
    std::vector<TrackingCustomerLocation> trackingCustomerLocList;
    std::optional<TrackingCustomerEquipment> trackingCustomerEquipment;
};

struct LCSSPIdentification {
    std::optional<std::int32_t> contentProviderIdType;
    std::optional<std::string> contentProviderIdentifier;
};
struct LCSSPInformation {
    std::vector<LCSSPIdentification> lcsspIdentificationList;
    std::vector<InternetServiceProvider> ispList;
    std::vector<ContentNetwork> networkList;
};

struct TrackedCustomerIdentification {
    std::optional<std::int32_t> customerIdType;
    std::optional<std::string> customerIdentifier;
};
struct TrackedCustomerHomeId {
    std::optional<std::int32_t> homeIdType;
    std::optional<std::string> homeIdentifier;
};
struct TrackedCustomerLocation {
    std::optional<std::int32_t> locationIdType;
    std::optional<std::string> locationIdentifier;
};
struct TrackedCustomerEquipment {
    std::optional<std::int32_t> equipmentIdType;
    std::optional<std::string> equipmentId;
};
struct TrackedCustomerInformation {
    std::vector<TrackedCustomerIdentification> trackedCustomerIdList;
    std::vector<TrackedCustomerHomeId> trackedCustomerHomeIdList;
    std::vector<TrackedCustomerLocation> trackedCustomerLocList;
    std::optional<TrackedCustomerEquipment> trackedCustomerEquipment;
};

struct LCSQosRequested {
    std::optional<DateTime> lcsRequestTimestamp;
    std::optional<std::int32_t> horizontalAccuracyRequested;
    std::optional<std::int32_t> verticalAccuracyRequested;
    std::optional<std::int32_t> responseTimeCategory;
    std::optional<std::int32_t> trackingPeriod;
    std::optional<std::int32_t> trackingFrequency;
};
struct LCSQosDelivered {
    std::optional<std::int32_t> lcsTransactionStatus;
    std::optional<std::int32_t> horizontalAccuracyDelivered;
    std::optional<std::int32_t> verticalAccuracyDelivered;
    std::optional<std::int32_t> responseTime;
    std::optional<std::int32_t> positioningMethod;
    std::optional<std::int32_t> trackingPeriod;
    std::optional<std::int32_t> trackingFrequency;
    std::optional<std::int32_t> ageOfLocation;
};
struct LocationServiceUsage {
    std::optional<LCSQosRequested> lcsQosRequested;
    std::optional<LCSQosDelivered> lcsQosDelivered;
    std::optional<DateTime> chargingTimeStamp;
    std::vector<ChargeInformation> chargeInformationList;
};

// LocationService ::= [APPLICATION 297] SEQUENCE.
struct LocationService {
    std::optional<std::string> rapFileSequenceNumber;
    std::optional<std::int32_t> recEntityCode;
    std::optional<std::vector<std::uint8_t>> callReference;
    std::optional<TrackingCustomerInformation> trackingCustomerInformation;
    std::optional<LCSSPInformation> lcsspInformation;
    std::optional<TrackedCustomerInformation> trackedCustomerInformation;
    std::optional<LocationServiceUsage> locationServiceUsage;
    std::vector<std::string> operatorSpecInformation;
};
Tlv encode_location_service(const LocationService& v);
std::optional<LocationService> decode_location_service(const Tlv& tlv);

} // namespace tap3_core
