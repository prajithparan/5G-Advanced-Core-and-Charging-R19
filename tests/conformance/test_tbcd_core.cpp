#include "tbcd_core/tbcd.hpp"

#include <gtest/gtest.h>

// P4.5/ADR-0061. Real TS 23.003 clause 2.2 TBCD-STRING packing.

namespace {

TEST(TbcdCore, EncodesEvenLengthDigitString) {
    const auto bytes = tbcd_core::encode_tbcd("9997");
    // '9','9' -> low=9 high=9 -> 0x99 ; '9','7' -> low=9 high=7 -> 0x79
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0x99);
    EXPECT_EQ(bytes[1], 0x79);
}

TEST(TbcdCore, EncodesOddLengthDigitStringWithFillerNibble) {
    const auto bytes = tbcd_core::encode_tbcd("999");
    // '9','9' -> 0x99 ; '9', filler(0xF) -> high=0xF low=9 -> 0xF9
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0x99);
    EXPECT_EQ(bytes[1], 0xF9);
}

TEST(TbcdCore, DecodeRoundTripsEvenLengthDigits) {
    const std::string digits = "999700000000001";
    const auto bytes = tbcd_core::encode_tbcd(digits);
    EXPECT_EQ(tbcd_core::decode_tbcd(bytes), digits);
}

TEST(TbcdCore, DecodeRoundTripsOddLengthDigits) {
    const std::string digits = "12345";
    const auto bytes = tbcd_core::encode_tbcd(digits);
    EXPECT_EQ(tbcd_core::decode_tbcd(bytes), digits);
}

TEST(TbcdCore, DecodeStopsAtFillerNibble) {
    const std::vector<std::uint8_t> bytes = {0x21, 0xF3};
    // octet0: low=1 high=2 -> "12" ; octet1: low=3 high=0xF(filler) -> "3", stop
    EXPECT_EQ(tbcd_core::decode_tbcd(bytes), "123");
}

} // namespace
