// Round-trip conformance tests for SCP's generated DTOs (docs/DECISIONS.md ADR for this NF).
// Source: specs/5G_APIs-REL-19/TS29570_Nscp_EventExposure.yaml. Same precedent as
// test_round_trip.cpp/test_nef_dtos.cpp: construct -> to_json -> from_json -> compare, using the
// real generated types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29570_Nscp_EventExposure.hpp"

#include <gtest/gtest.h>

TEST(ScpDtos, ScpEventExposureSubscriptionRoundTrips) {
    sbi_gen::ScpEventFilter filter;
    filter.eventType.value = sbi_gen::ScpEventType::SERVICE_SIGNALLING_CHARACTERISTICS;

    sbi_gen::ScpEventExposureSubscription original;
    original.eventList = {filter};
    original.eventNotifyUri = "https://consumer.example/notify";
    original.notifyCorrelationId = "corr-1";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::ScpEventExposureSubscription>();
    ASSERT_EQ(decoded.eventList.size(), 1u);
    EXPECT_EQ(decoded.eventList[0].eventType.value, "SERVICE_SIGNALLING_CHARACTERISTICS");
    EXPECT_EQ(decoded.eventNotifyUri, "https://consumer.example/notify");
    EXPECT_EQ(decoded.notifyCorrelationId, "corr-1");
}

TEST(ScpDtos, ScpEventExposureSubsRespRoundTrips) {
    sbi_gen::ScpEventExposureSubsResp original;
    original.expiryTime = "2026-01-01T00:00:00Z";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::ScpEventExposureSubsResp>();
    ASSERT_TRUE(decoded.expiryTime.has_value());
    EXPECT_EQ(*decoded.expiryTime, "2026-01-01T00:00:00Z");
}

TEST(ScpDtos, ScpEventReportRoundTrips) {
    sbi_gen::ScpSignallingInfo info;
    info.serviceInstanceId = "svc-1";
    info.rcvRequestCount = 42;

    sbi_gen::ScpEventReport original;
    original.eventType.value = sbi_gen::ScpEventType::SERVICE_SIGNALLING_CHARACTERISTICS;
    original.timeStamp = "2026-01-01T00:00:00Z";
    original.scpSignallingInfoList = std::vector<sbi_gen::ScpSignallingInfo>{info};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::ScpEventReport>();
    EXPECT_EQ(decoded.eventType.value, "SERVICE_SIGNALLING_CHARACTERISTICS");
    ASSERT_TRUE(decoded.scpSignallingInfoList.has_value());
    ASSERT_TRUE((*decoded.scpSignallingInfoList)[0].rcvRequestCount.has_value());
    EXPECT_EQ(*(*decoded.scpSignallingInfoList)[0].rcvRequestCount, 42);
}
