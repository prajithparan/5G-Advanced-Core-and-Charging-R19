// Tests for sbi_core::parse_rfc3339 / parse_rfc3339_to_time_t -- the inverse of the existing
// format_rfc3339, added so nfs/chf can record the real TS 32.291 `invocationTimeStamp` a consumer
// actually sent instead of CHF's own write time (see charging_engine.cpp).
//
// The DateTime shape under test is TS29571_CommonData.yaml's own: RFC 3339 date-time. These cases
// exercise the grammar a real peer NF is entitled to send, not just what this project emits.

#include "sbi_core/datetime.hpp"

#include <chrono>

#include <gtest/gtest.h>

namespace {

std::time_t parsed(const std::string& text) {
    const auto t = sbi_core::parse_rfc3339_to_time_t(text);
    EXPECT_TRUE(t.has_value()) << "failed to parse: " << text;
    return t.value_or(0);
}

} // namespace

TEST(Rfc3339DateTime, ParsesBasicUtcTimestamp) {
    // 2026-08-31T14:00:01Z == 1788184801 (independently computed, not read back from our own
    // formatter -- a self-consistency check would not catch a shared offset bug).
    EXPECT_EQ(parsed("2026-08-31T14:00:01Z"), 1788184801);
}

TEST(Rfc3339DateTime, ParsesEpochItself) {
    EXPECT_EQ(parsed("1970-01-01T00:00:00Z"), 0);
}

TEST(Rfc3339DateTime, FractionalSecondsAreTruncatedNotRejected) {
    EXPECT_EQ(parsed("2026-08-31T14:00:01.123Z"), 1788184801);
    // More precision than milliseconds is real and must not be an error.
    EXPECT_EQ(parsed("2026-08-31T14:00:01.123456789Z"), 1788184801);
}

TEST(Rfc3339DateTime, NumericOffsetIsAppliedToReachUtc) {
    // 14:00:01+05:30 is 08:30:01Z -- 5h30m earlier in absolute time.
    EXPECT_EQ(parsed("2026-08-31T14:00:01+05:30"), 1788184801 - (5 * 3600 + 30 * 60));
    EXPECT_EQ(parsed("2026-08-31T14:00:01-08:00"), 1788184801 + (8 * 3600));
    // An explicit +00:00 must equal the same instant as 'Z'.
    EXPECT_EQ(parsed("2026-08-31T14:00:01+00:00"), parsed("2026-08-31T14:00:01Z"));
}

TEST(Rfc3339DateTime, LowercaseSeparatorsAccepted) {
    EXPECT_EQ(parsed("2026-08-31t14:00:01z"), 1788184801);
}

TEST(Rfc3339DateTime, RoundTripsWithFormatRfc3339) {
    const auto tp = sbi_core::parse_rfc3339("2026-08-31T14:00:01.500Z");
    ASSERT_TRUE(tp.has_value());
    EXPECT_EQ(sbi_core::format_rfc3339(*tp), "2026-08-31T14:00:01.500Z");
}

TEST(Rfc3339DateTime, LeapSecondClampsRatherThanFailing) {
    // system_clock has no leap-second representation; 60 must not be silently reinterpreted as
    // the next minute either.
    EXPECT_EQ(parsed("2016-12-31T23:59:60Z"), parsed("2016-12-31T23:59:59Z"));
}

TEST(Rfc3339DateTime, RejectsMalformedInput) {
    const char* bad[] = {
        "",
        "2026-08-31",                // date only: no time, no offset
        "2026-08-31T14:00:01",       // missing mandatory offset
        "2026-08-31 14:00:01Z",      // space separator is not RFC 3339's 'T'
        "2026-13-01T00:00:00Z",      // month 13
        "2026-02-31T00:00:00Z",      // real calendar-invalid date
        "2026-08-31T24:00:01Z",      // hour 24
        "2026-08-31T14:60:01Z",      // minute 60
        "2026-08-31T14:00:01.Z",     // '.' with no digits
        "2026-08-31T14:00:01+0530",  // offset missing its ':'
        "2026-08-31T14:00:01+24:00", // offset hour out of range
        "2026-08-31T14:00:01Zjunk",  // trailing junk
        "20260831T140001Z",          // basic-format ISO 8601, not RFC 3339
        "not-a-timestamp",
    };
    for (const char* text : bad) {
        EXPECT_FALSE(sbi_core::parse_rfc3339(text).has_value())
            << "should have been rejected: " << text;
    }
}
