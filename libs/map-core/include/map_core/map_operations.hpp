#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "tcap_core/ber.hpp"

// MAP (TS 29.002) insertSubscriberData operation argument -- the real, minimal subset needed to
// provision O-CSI/D-CSI CAMEL Subscription Info from HLR to VLR/MSC, which is what causes a real
// switch to later invoke CAP's InitialDP (libs/cap-core). P4.5/ADR-0059 Stage 7 kickoff. See
// map_dictionary.hpp's own header for the full real, disclosed scope/gap list.
//
// Real, disclosed scope narrowing (every field modeled has a real, cited tag; every field NOT
// modeled is a real, disclosed gap):
//   InsertSubscriberDataArg: imsi[0], msisdn[1], vlrCamelSubscriptionInfo[13] only. The real
//     structure has ~50 more real OPTIONAL fields (COMPONENTS OF SubscriberData plus ~40
//     extension fields up to tag [54]) -- not modeled.
//   VlrCamelSubscriptionInfo: o-CSI[0], d-CSI[9] only (of its real 11 real OPTIONAL fields --
//     ss-CSI, tif-CSI, m-CSI, mo-sms-CSI, vt-CSI, t-BCSM-CAMEL-TDP-CriteriaList, mt-sms-CSI,
//     mt-smsCAMELTDP-CriteriaList are not modeled).
//   O-CSI: o-BcsmCamelTDPDataList (mandatory) and camelCapabilityHandling (optional) only;
//     notificationToCSE/csiActive/extensionContainer not modeled.
//   O-BcsmCamelTDPData: all 4 real mandatory fields (trigger detection point, service key,
//     gsmSCF-Address, defaultCallHandling); extensionContainer (optional) not modeled.
//   D-CSI: dp-AnalysedInfoCriteriaList (optional) and camelCapabilityHandling (optional) only;
//     notificationToCSE/csiActive/extensionContainer not modeled.
//   DP-AnalysedInfoCriterion: all 4 real mandatory fields; extensionContainer not modeled.
//   InsertSubscriberDataRes: modeled as a real, valid EMPTY SEQUENCE (the real operation
//     definition marks its RESULT "-- optional" and every one of InsertSubscriberDataRes's real
//     fields is itself OPTIONAL, so an empty response is genuinely valid, not a simplification).
//     supportedCamelPhases and the other real RES fields are not modeled (would need a BIT STRING
//     BER primitive this codebase does not have yet).
//
// Real ASN.1 encoding note: TS 29.002's own MAP-MS-DataTypes module is DEFINITIONS IMPLICIT TAGS
// (no CHOICE-tagged fields appear anywhere in this increment's scope, unlike CAP -- every tagged
// field here is a plain IMPLICIT replacement of its type's natural tag, no EXPLICIT wrap needed).
// A second real, disclosed encoding fact: several real fields in this increment are UNTAGGED
// (retain their type's own universal tag) and, within DP-AnalysedInfoCriterion specifically, two
// untagged sibling fields (dialledNumber, gsmSCF-Address) share the identical universal OCTET
// STRING tag -- real BER disambiguates this by definition order, not by tag, so this codec
// decodes O-BcsmCamelTDPData and DP-AnalysedInfoCriterion POSITIONALLY (fixed field order),
// unlike CAP's tag-lookup-based decoding, which would be ambiguous here.

namespace map_core {

// Real O-BcsmTriggerDetectionPoint ENUMERATED values actually needed -- TS 29.002 clause 17.7.1
// (page 411). The real enum also has termAttemptAuthorized/tBusy/... for T-CSI's own trigger
// type (T-BcsmTriggerDetectionPoint); only the O-CSI-relevant values are named here.
namespace OBcsmTriggerDetectionPoint {
constexpr std::int32_t kCollectedInfo = 2;
constexpr std::int32_t kRouteSelectFailure = 4;
} // namespace OBcsmTriggerDetectionPoint

// Real DefaultCallHandling ENUMERATED values -- TS 29.002 clause 17.7.1 (page 412).
namespace DefaultCallHandling {
constexpr std::int32_t kContinueCall = 0;
constexpr std::int32_t kReleaseCall = 1;
} // namespace DefaultCallHandling

struct OBcsmCamelTdpData {
    std::int32_t trigger_detection_point = 0;  // untagged ENUMERATED (universal tag 10)
    std::int32_t service_key = 0;              // untagged INTEGER (universal tag 2)
    std::vector<std::uint8_t> gsm_scf_address; // [0], opaque ISDN-AddressString
    std::int32_t default_call_handling = 0;    // [1], ENUMERATED
};

struct OCsi {
    std::vector<OBcsmCamelTdpData> tdp_data_list;          // untagged SEQUENCE OF (mandatory)
    std::optional<std::int32_t> camel_capability_handling; // [0], OPTIONAL
};

struct DpAnalysedInfoCriterion {
    std::vector<std::uint8_t> dialled_number;  // untagged ISDN-AddressString (opaque)
    std::int32_t service_key = 0;              // untagged INTEGER
    std::vector<std::uint8_t> gsm_scf_address; // untagged ISDN-AddressString (opaque)
    std::int32_t default_call_handling = 0;    // untagged ENUMERATED
};

struct DCsi {
    std::vector<DpAnalysedInfoCriterion> dp_analysed_info_criteria_list; // [0], OPTIONAL
    std::optional<std::int32_t> camel_capability_handling;               // [1], OPTIONAL
};

struct VlrCamelSubscriptionInfo {
    std::optional<OCsi> o_csi; // [0]
    std::optional<DCsi> d_csi; // [9]
};

struct InsertSubscriberDataArg {
    std::optional<std::vector<std::uint8_t>> imsi; // [0], TBCD-STRING (TS 23.003 clause 2.2) --
                                                   // see libs/tbcd-core, added alongside ADR-0061's
                                                   // CHF/CAP wiring
    std::optional<std::vector<std::uint8_t>> msisdn; // [1], opaque AddressString
    std::optional<VlrCamelSubscriptionInfo> vlr_camel_subscription_info; // [13]
};

std::vector<std::uint8_t> encode_insert_subscriber_data_arg(const InsertSubscriberDataArg& arg);
std::optional<InsertSubscriberDataArg>
decode_insert_subscriber_data_arg(const std::vector<std::uint8_t>& parameter);

// Real, valid empty result -- see this file's own header comment.
std::vector<std::uint8_t> encode_insert_subscriber_data_res();
bool decode_insert_subscriber_data_res(const std::vector<std::uint8_t>& parameter);

} // namespace map_core
