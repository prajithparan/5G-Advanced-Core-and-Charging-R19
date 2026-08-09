#pragma once

#include <chrono>
#include <string>

// A small, reusable formatter for OpenAPI's `format: date-time` (RFC 3339), the JSON wire
// representation TS29571_CommonData.yaml's `DateTime` schema uses (see
// specs/5G_APIs-REL-19/TS29571_CommonData.yaml, "string with format 'date-time' as defined in
// OpenAPI") -- distinct from sbi_headers.hpp's format_sender_timestamp, which formats the
// unrelated RFC 7231 IMF-fixdate shape TS 29.500's `3gpp-Sbi-Sender-Timestamp` HTTP header uses.
// First needed by nfs/chf (ChargingDataRequest/Response's invocationTimeStamp), kept here rather
// than private to one NF since any NF with a DateTime-typed JSON field needs the same formatting.

namespace sbi_core {

// e.g. "2026-08-09T14:23:01.123Z".
std::string format_rfc3339(std::chrono::system_clock::time_point tp);

} // namespace sbi_core
