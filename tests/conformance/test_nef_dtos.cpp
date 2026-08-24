// Round-trip conformance tests for NEF's generated DTOs (docs/DECISIONS.md ADR for this NF).
// Source: specs/5G_APIs-REL-19/TS29551_Nnef_PFDmanagement.yaml. Same precedent as
// test_round_trip.cpp/test_bsf_dtos.cpp: construct -> to_json -> from_json -> compare, using the
// real generated types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29551_Nnef_PFDmanagement.hpp"

#include <gtest/gtest.h>

TEST(NefDtos, PfdDataForAppRoundTrips) {
    sbi_gen::PfdContent content;
    content.pfdId = "pfd1";
    content.urls = std::vector<std::string>{"^https://video.example.com/.*$"};

    sbi_gen::PfdDataForApp original;
    original.applicationId = "app-video-streaming";
    original.pfds = std::vector<sbi_gen::PfdContent>{content};
    original.pfdTimestamp = "2026-01-01T00:00:00Z";

    nlohmann::json j = original;
    EXPECT_EQ(j.at("applicationId").get<std::string>(), "app-video-streaming");

    auto decoded = j.get<sbi_gen::PfdDataForApp>();
    EXPECT_EQ(decoded.applicationId, "app-video-streaming");
    ASSERT_TRUE(decoded.pfds.has_value());
    ASSERT_EQ(decoded.pfds->size(), 1u);
    ASSERT_TRUE((*decoded.pfds)[0].pfdId.has_value());
    EXPECT_EQ(*(*decoded.pfds)[0].pfdId, "pfd1");
}

TEST(NefDtos, PfdSubscriptionRoundTrips) {
    sbi_gen::PfdSubscription original;
    original.applicationIds = std::vector<sbi_gen::ApplicationId>{"app-video-streaming"};
    original.notifyUri = "https://amf.example/notify";
    original.supportedFeatures = "";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::PfdSubscription>();
    ASSERT_TRUE(decoded.applicationIds.has_value());
    EXPECT_EQ((*decoded.applicationIds)[0], "app-video-streaming");
    EXPECT_EQ(decoded.notifyUri, "https://amf.example/notify");
}

TEST(NefDtos, ApplicationForPfdRequestRoundTrips) {
    sbi_gen::ApplicationForPfdRequest original;
    original.applicationId = "app-video-streaming";
    original.pfdTimestamp = "2026-01-01T00:00:00Z";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::ApplicationForPfdRequest>();
    EXPECT_EQ(decoded.applicationId, "app-video-streaming");
    ASSERT_TRUE(decoded.pfdTimestamp.has_value());
    EXPECT_EQ(*decoded.pfdTimestamp, "2026-01-01T00:00:00Z");
}

TEST(NefDtos, PfdChangeNotificationRoundTrips) {
    sbi_gen::PfdChangeNotification original;
    original.applicationId = "app-video-streaming";
    original.removalFlag = true;

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::PfdChangeNotification>();
    EXPECT_EQ(decoded.applicationId, "app-video-streaming");
    ASSERT_TRUE(decoded.removalFlag.has_value());
    EXPECT_TRUE(*decoded.removalFlag);
}
