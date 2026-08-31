#pragma once

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

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

// The inverse: parses the same RFC 3339 `date-time` shape back into a time point. Accepts the
// full grammar TS29571_CommonData.yaml's `DateTime` permits, not just what this project's own
// formatter emits -- a real peer NF is entitled to send any of it:
//   - `Z`/`z`, or a real numeric offset (`+05:30`, `-08:00`), which is applied to yield UTC
//   - an optional fractional second of any length (truncated to milliseconds, not rejected)
//   - `T` or `t` as the date/time separator
// Returns `std::nullopt` on anything malformed (including a real calendar-invalid date such as
// 2026-02-31, rejected via `year_month_day::ok()`), so callers decide the fallback rather than
// silently receiving a wrong instant. Deliberately hand-parsed with `std::chrono`'s calendar
// types, mirroring format_rfc3339 above, rather than `strptime`: no locale/TZ dependence and no
// `std::tm`-shaped 2-digit-year ambiguity.
std::optional<std::chrono::system_clock::time_point> parse_rfc3339(std::string_view text);

// Convenience wrapper for callers that store a `std::time_t` (nfs/chf's own CdrRecord does).
std::optional<std::time_t> parse_rfc3339_to_time_t(std::string_view text);

} // namespace sbi_core
