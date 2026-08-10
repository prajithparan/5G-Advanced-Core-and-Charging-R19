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
};

struct Ie {
    std::uint16_t type = 0;
    std::vector<std::uint8_t> value;
};

// Appends one IE (type + 2-byte length + value) to `out`.
void encode_ie(std::vector<std::uint8_t>& out, std::uint16_t type,
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
