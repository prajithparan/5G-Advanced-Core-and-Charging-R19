#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Generic PFCP Information Element TLV codec -- TS 29.244 §8.1.1 "Information Element Format":
// every 3GPP-defined IE is Type(2 octets) + Length(2 octets) + Value(n octets), with no exceptions
// for the message-level IEs this project implements (vendor-specific IEs, which use an extra
// Enterprise ID field, are out of scope -- this project never sends/expects one). Byte layout read
// directly from the real 3GPP TS 29.244 V14.3.0 spec PDF -- see header.hpp's own comment for the
// version-gap disclosure, and docs/DECISIONS.md ADR-0039.

namespace pfcp_core {

// TS 29.244 Table 8.1.2-1 -- only the values this project's implemented messages actually use.
enum class IeType : std::uint16_t {
    CreatePdr = 1,
    Pdi = 2,
    CreateFar = 3,
    ForwardingParameters = 4,
    CreateUrr = 6,
    CreatedPdr = 8,
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #107 part 2, ADR-0086): real IE types
    // confirmed against TS 29.244 Table 7.4.3.1-1/7.4.3.1-2/7.4.3.1-3 and §8.2.6/§8.2.39 (same
    // vendored spec text, specs/PFCP/29244-e30.pdf, every other IeType value here is confirmed
    // against). "Application ID's PFDs" and "PFD" (called "PFD context" in the master IE table,
    // Table 8.1.2-1 -- a real, disclosed naming inconsistency in the spec itself, not a typo here)
    // are grouped IEs, decoded like CreatePdr/CreateUrr above (pfcp_core::decode_ies on the raw
    // nested value bytes) -- no dedicated codec function needed for either.
    ApplicationId = 24,
    ApplicationIdsPfds = 58,
    PfdContext = 59,
    PfdContents = 61,
    // ADR-0050 Stage 5: real IE type confirmed against TS 29.244 Table 7.5.4.4-1 -- the grouped IE
    // a Sx Session Modification Request uses to push a real, updated Volume Threshold/Volume
    // Quota for an already-created URR (same child IEs as CreateUrr's own UrrId/VolumeThreshold/
    // VolumeQuota, reused as-is -- only fields that "need to be modified" are present per the real
    // spec table, so MeasurementMethod/ReportingTriggers are omitted when unchanged).
    UpdateUrr = 13,
    ReportType = 39,
    Cause = 19,
    SourceInterface = 20,
    FTeid = 21,
    VolumeThreshold = 31,
    Precedence = 29,
    ReportingTriggers = 37,
    ApplyAction = 44,
    UpFunctionFeatures = 43,
    DestinationInterface = 42,
    UsageReport = 80,
    UrrId = 81,
    FSeid = 57,
    NodeId = 60,
    MeasurementMethod = 62,
    UsageReportTrigger = 63,
    VolumeMeasurement = 66,
    VolumeQuota = 73,
    CpFunctionFeatures = 89,
    RecoveryTimeStamp = 96,
    UrSeqn = 104,
    FarId = 108,
    PdrId = 56,
    // ADR-0071, gap-closure Tier 1d: real QER/BAR support. Real IE type numbers confirmed
    // directly against TS 29.244 Table 8.1.2-1 (the same master table every IE type above was
    // already confirmed against).
    CreateQer = 7,   // Table 7.5.2.5-1
    UpdateQer = 14,  // Table 7.5.4.5-1
    RemoveQer = 18,  // Table 7.5.4.9-1
    GateStatus = 25, // §8.2.7
    Mbr = 26,        // §8.2.8
    CreateBar = 85,  // Table 7.5.2.6-1
    // Real, confirmed asymmetry: "Update BAR" has TWO distinct real type numbers depending on
    // message direction -- 86 when it's a CP->UP component of Sx Session Modification Request
    // (Table 7.5.4.11-1, what this project uses), 12 when it's a UP->CP component of Sx Session
    // Report Response (Table 7.5.9.2-1, a different, richer real IE this project doesn't need).
    UpdateBar = 86,
    RemoveBar = 87, // Table 7.5.4.12-1
    BarId = 88,     // §8.2.57 -- real 1-octet IE, NOT the 4-octet Unsigned32 shape FarId/UrrId/
                    // QerId share; see encode_bar_id/decode_bar_id's own comment.
    QerId = 109,    // §8.2.75 -- same real "bit 8 of octet 5 = 0 for CP-allocated" convention as
                    // FarId/UrrId (confirmed identical spec text, not assumed).
    // Real, confirmed asymmetry (same class as UpdateBar's own 86-vs-12 split above, re-verified
    // directly against TS 29.244 V14.3.0 p.95 before writing this rather than trusted from a
    // secondary source): the "Usage Report" grouped IE has TWO distinct real type numbers
    // depending on which message carries it -- 80 (Table 7.5.8.3-1, Sx Session Report Request,
    // already in use above) when it's an unsolicited threshold/quota-crossing report, 79
    // (Table 7.5.7.2-1, Sx Session Deletion Response) when it's the final cumulative usage
    // reported at session teardown. Different child-IE set too (no Application Detection
    // Information/UE IP address/etc. -- Session Deletion's variant is the narrower one, matching
    // exactly the fields this project's UrrState already tracks).
    UsageReportSessionDeletion = 79,
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #107, ADR-0087): real IE types for Sx Node
    // Report Request (TS 29.244 §7.4.5.1/§8.2.69/§8.2.70), confirmed against the same vendored
    // spec text every other IeType value here is confirmed against. UserPlanePathFailureReport is
    // a grouped IE (repeated RemoteGtpuPeer children) with no dedicated codec, same choice
    // CreatePdr/ApplicationIdsPfds already made.
    NodeReportType = 101,
    UserPlanePathFailureReport = 102,
    RemoteGtpuPeer = 103,
    // Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #101, ADR-0092): real IE types confirmed
    // against TS 29.244 §7.5.4.3 (Table 7.5.4.3-1/7.5.4.3-2) and §8.2.56, same vendored spec text
    // every other IeType value here is confirmed against. UpdateFar/UpdateForwardingParameters are
    // grouped IEs (decoded like CreatePdr/CreateFar above, no dedicated codec needed for the
    // container itself); OuterHeaderCreation has its own codec (session_ies.hpp).
    UpdateFar = 10,                  // Table 7.5.4.3-1
    UpdateForwardingParameters = 11, // Table 7.5.4.3-2
    OuterHeaderCreation = 84,        // §8.2.56
};

struct Ie {
    std::uint16_t type = 0;
    std::vector<std::uint8_t> value;
};

// Appends one IE (type + 2-byte length + value) to `out`.
void encode_ie(std::vector<std::uint8_t>& out,
               std::uint16_t type,
               const std::vector<std::uint8_t>& value);

// Decodes every top-level IE in `bytes` (a message's IE region, i.e. the bytes after the PFCP
// header -- see header.hpp's decode_header). Does not recurse into Grouped IEs (TS 29.244 §7.2.3.3
// -- none of this project's currently implemented messages use one). Returns std::nullopt if any
// IE's declared length would run past the end of `bytes` (malformed message); a value of 0 IEs
// (empty `bytes`) is not an error.
std::optional<std::vector<Ie>> decode_ies(const std::vector<std::uint8_t>& bytes);

// Finds the first IE of the given type, or nullptr if absent. `ies` must outlive the returned
// pointer.
const Ie* find_ie(const std::vector<Ie>& ies, std::uint16_t type);

} // namespace pfcp_core
