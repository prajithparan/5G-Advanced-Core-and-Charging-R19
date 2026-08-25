// Round-trip conformance tests for LMF's generated DTOs (docs/DECISIONS.md ADR-0190).
// Source: specs/5G_APIs-REL-19/TS29572_Nlmf_Location.yaml. Same precedent as
// test_gmlc_dtos.cpp/test_smsf_dtos.cpp: construct -> to_json -> from_json -> compare, using the
// real generated types, not synthetic ones. These types land in the shared,
// strongly-connected-component-grouped TS26510_CommonData_grp.hpp -- see nfs/lmf/src/main.cpp's
// own include comment for why.

#include <nlohmann/json.hpp>

#include "TS26510_CommonData_grp.hpp"

#include <gtest/gtest.h>

TEST(LmfDtos, UpSubscriptionRoundTrips) {
    sbi_gen::UpSubscription original;
    original.upNotifyCallBackUri = "https://consumer.example/up-notify";
    original.notifCorrelationId = "corr-1";
    original.supi = "imsi-001010000000001";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::UpSubscription>();
    EXPECT_EQ(decoded.upNotifyCallBackUri, "https://consumer.example/up-notify");
    EXPECT_EQ(decoded.notifCorrelationId, "corr-1");
    EXPECT_EQ(decoded.supi, "imsi-001010000000001");
}

TEST(LmfDtos, LocContextDataRoundTrips) {
    sbi_gen::LocContextData original;
    original.amfId = "5ba9a927-1d31-4c8e-8a10-0000000000dd";
    original.ldrType.value = "PERIODIC";
    original.hgmlcCallBackURI = "https://hgmlc.example/cb";
    original.ldrReference = "ab12cd34";
    original.eventReportMessage.eventClass.value = "DUMMY";
    original.eventReportMessage.eventContent.contentId = "evt-1";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::LocContextData>();
    EXPECT_EQ(decoded.amfId, "5ba9a927-1d31-4c8e-8a10-0000000000dd");
    EXPECT_EQ(decoded.ldrType.value, "PERIODIC");
    EXPECT_EQ(decoded.ldrReference, "ab12cd34");
    EXPECT_EQ(decoded.eventReportMessage.eventClass.value, "DUMMY");
}

TEST(LmfDtos, UpConfigRoundTrips) {
    sbi_gen::UpConfig original;
    original.upNotifyCallBackUri = "https://consumer.example/up-notify";
    original.notifCorrelationId = "corr-2";
    original.supi = "imsi-001010000000002";
    original.lcsUpConnectionInd = sbi_gen::LcsUpConnectionInd{};
    original.lcsUpConnectionInd->value = "TERMINATION";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::UpConfig>();
    ASSERT_TRUE(decoded.lcsUpConnectionInd.has_value());
    EXPECT_EQ(decoded.lcsUpConnectionInd->value, "TERMINATION");
    ASSERT_TRUE(decoded.supi.has_value());
    EXPECT_EQ(*decoded.supi, "imsi-001010000000002");
}
