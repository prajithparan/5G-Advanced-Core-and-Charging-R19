// Round-trip conformance tests for BSF's generated DTOs (docs/DECISIONS.md ADR for this NF).
// Source: specs/5G_APIs-REL-19/TS29521_Nbsf_Management.yaml. Same precedent as test_round_trip.cpp
// / test_nssf_dtos.cpp: construct -> to_json -> from_json -> compare, using the real generated
// types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29521_Nbsf_Management.hpp"

#include <gtest/gtest.h>

TEST(BsfDtos, PcfBindingRoundTrips) {
    sbi_gen::PcfBinding original;
    original.supi = "imsi-310410123456789";
    original.dnn = "internet";
    original.snssai.sst = 1;

    nlohmann::json j = original;
    EXPECT_EQ(j.at("dnn").get<std::string>(), "internet");

    auto decoded = j.get<sbi_gen::PcfBinding>();
    ASSERT_TRUE(decoded.supi.has_value());
    EXPECT_EQ(*decoded.supi, "imsi-310410123456789");
    EXPECT_EQ(decoded.dnn, "internet");
    EXPECT_EQ(decoded.snssai.sst, 1);
}

TEST(BsfDtos, ExtProblemDetailsRoundTrips) {
    sbi_gen::ExtProblemDetails_Nbsf_Management original;
    original.status = 403;
    original.title = "Forbidden";
    original.pcfSmFqdn = "pcf1.example.com";

    nlohmann::json j = original;
    EXPECT_EQ(j.at("status").get<int>(), 403);
    EXPECT_EQ(j.at("pcfSmFqdn").get<std::string>(), "pcf1.example.com");

    auto decoded = j.get<sbi_gen::ExtProblemDetails_Nbsf_Management>();
    ASSERT_TRUE(decoded.status.has_value());
    EXPECT_EQ(*decoded.status, 403);
    ASSERT_TRUE(decoded.pcfSmFqdn.has_value());
    EXPECT_EQ(*decoded.pcfSmFqdn, "pcf1.example.com");
}

TEST(BsfDtos, BsfSubscriptionRoundTrips) {
    sbi_gen::BsfEvent event;
    event.value = sbi_gen::BsfEvent::PCF_UE_BINDING_REGISTRATION;

    sbi_gen::BsfSubscription original;
    original.events = {event};
    original.notifUri = "https://amf.example/notify";
    original.notifCorreId = "corr-1";
    original.supi = "imsi-310410123456789";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::BsfSubscription>();
    ASSERT_EQ(decoded.events.size(), 1u);
    EXPECT_EQ(decoded.events[0].value, "PCF_UE_BINDING_REGISTRATION");
    EXPECT_EQ(decoded.notifUri, "https://amf.example/notify");
    EXPECT_EQ(decoded.supi, "imsi-310410123456789");
}

TEST(BsfDtos, BsfNotificationRoundTrips) {
    sbi_gen::BsfEvent event;
    event.value = sbi_gen::BsfEvent::SNSSAI_DNN_BINDING_DEREGISTRATION;

    sbi_gen::PcfForPduSessionInfo info;
    info.dnn = "internet";
    info.snssai.sst = 1;

    sbi_gen::BsfEventNotification event_notif;
    event_notif.event = event;
    event_notif.pcfForPduSessInfos = std::vector<sbi_gen::PcfForPduSessionInfo>{info};

    sbi_gen::BsfNotification original;
    original.notifCorreId = "corr-1";
    original.eventNotifs = {event_notif};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::BsfNotification>();
    ASSERT_EQ(decoded.eventNotifs.size(), 1u);
    EXPECT_EQ(decoded.eventNotifs[0].event.value, "SNSSAI_DNN_BINDING_DEREGISTRATION");
    ASSERT_TRUE(decoded.eventNotifs[0].pcfForPduSessInfos.has_value());
    EXPECT_EQ((*decoded.eventNotifs[0].pcfForPduSessInfos)[0].dnn, "internet");
}

TEST(BsfDtos, PcfMbsBindingRoundTrips) {
    sbi_gen::Tmgi tmgi;
    tmgi.mbsServiceId = "000001";
    tmgi.plmnId.mcc = "310";
    tmgi.plmnId.mnc = "410";

    sbi_gen::PcfMbsBinding original;
    original.mbsSessionId.tmgi = tmgi;
    original.pcfFqdn = "pcf1.example.com";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::PcfMbsBinding>();
    ASSERT_TRUE(decoded.pcfFqdn.has_value());
    EXPECT_EQ(*decoded.pcfFqdn, "pcf1.example.com");
}
