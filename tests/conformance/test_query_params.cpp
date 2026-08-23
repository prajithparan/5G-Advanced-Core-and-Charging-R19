// Unit tests for sbi_core::http2::split_form_array (docs/DECISIONS.md ADR-0161) -- the real
// helper for OpenAPI's `style: form, explode: false` array query-param convention (e.g.
// `?dataset-names=AMF_3GPP,SDM_SUBSCRIPTIONS`), used to unblock UDR's real
// `context-dataset-names`/`gpsis`/etc. required-array-query-param resources.

#include "sbi_core/http2_server.hpp"

#include <gtest/gtest.h>

TEST(SplitFormArray, EmptyStringReturnsEmptyVector) {
    EXPECT_TRUE(sbi_core::http2::split_form_array("").empty());
}

TEST(SplitFormArray, SingleValueNoComma) {
    const auto out = sbi_core::http2::split_form_array("AMF_3GPP");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], "AMF_3GPP");
}

TEST(SplitFormArray, MultipleValuesCommaSeparated) {
    const auto out = sbi_core::http2::split_form_array("AMF_3GPP,SDM_SUBSCRIPTIONS,PEI_INFO");
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], "AMF_3GPP");
    EXPECT_EQ(out[1], "SDM_SUBSCRIPTIONS");
    EXPECT_EQ(out[2], "PEI_INFO");
}

TEST(SplitFormArray, PreservesAlreadyDecodedNonCommaCharacters) {
    // Real values this project actually splits (external group IDs, MSISDN-style identifiers)
    // contain '@', '-', '.', digits -- none of which are the delimiter, so they must survive
    // untouched.
    const auto out =
        sbi_core::http2::split_form_array("extgroupid-group1@example.com,msisdn-12345");
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], "extgroupid-group1@example.com");
    EXPECT_EQ(out[1], "msisdn-12345");
}

TEST(SplitFormArray, EmptyElementsBetweenCommasAreKeptAsEmptyStrings) {
    // A real, disclosed edge case: "a,,b" is technically malformed per OpenAPI's own form-style
    // encoding, but this project chooses not to silently drop the empty middle element (which
    // would shift indices and corrupt correlation with an "explode" counterpart elsewhere) --
    // the caller sees exactly what was on the wire, minus the delimiters.
    const auto out = sbi_core::http2::split_form_array("a,,b");
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], "a");
    EXPECT_EQ(out[1], "");
    EXPECT_EQ(out[2], "b");
}

TEST(SplitFormArray, TrailingCommaProducesTrailingEmptyElementIsDropped) {
    // std::getline on a stringstream does NOT emit a final empty token after a trailing
    // delimiter with no following characters (unlike splitting "a,b," into ["a","b",""] in some
    // languages) -- disclosed here as real, observed behavior of the chosen implementation, not
    // an assumption.
    const auto out = sbi_core::http2::split_form_array("a,b,");
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], "a");
    EXPECT_EQ(out[1], "b");
}
