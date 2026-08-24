// Round-trip conformance tests for NSSF's generated DTOs (docs/DECISIONS.md ADR for this NF).
// Source: specs/5G_APIs-REL-19/TS29531_Nnssf_NSSelection.yaml +
// TS29531_Nnssf_NSSAIAvailability.yaml. Same precedent as test_round_trip.cpp/
// test_query_params.cpp: construct -> to_json -> from_json -> compare, using the real generated
// types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29122_CommonData_grp.hpp"

#include <gtest/gtest.h>

TEST(NssfDtos, SnssaiRoundTrips) {
    sbi_gen::Snssai original;
    original.sst = 1;
    original.sd = "a00001";

    nlohmann::json j = original;
    EXPECT_EQ(j.at("sst").get<int>(), 1);
    EXPECT_EQ(j.at("sd").get<std::string>(), "a00001");

    auto decoded = j.get<sbi_gen::Snssai>();
    EXPECT_EQ(decoded.sst, original.sst);
    EXPECT_EQ(decoded.sd, original.sd);
}

TEST(NssfDtos, AuthorizedNetworkSliceInfoRoundTrips) {
    sbi_gen::Snssai embb;
    embb.sst = 1;

    sbi_gen::AllowedSnssai allowed_snssai;
    allowed_snssai.allowedSnssai = embb;

    sbi_gen::AllowedNssai allowed_nssai;
    allowed_nssai.allowedSnssaiList = {allowed_snssai};
    allowed_nssai.accessType.value = sbi_gen::AccessType::V3GPP_ACCESS;

    sbi_gen::AuthorizedNetworkSliceInfo original;
    original.allowedNssaiList = std::vector<sbi_gen::AllowedNssai>{allowed_nssai};

    nlohmann::json j = original;
    ASSERT_TRUE(j.contains("allowedNssaiList"));
    EXPECT_EQ(j.at("allowedNssaiList")[0].at("accessType").get<std::string>(), "3GPP_ACCESS");
    EXPECT_EQ(j.at("allowedNssaiList")[0]
                  .at("allowedSnssaiList")[0]
                  .at("allowedSnssai")
                  .at("sst")
                  .get<int>(),
              1);

    auto decoded = j.get<sbi_gen::AuthorizedNetworkSliceInfo>();
    ASSERT_TRUE(decoded.allowedNssaiList.has_value());
    ASSERT_EQ(decoded.allowedNssaiList->size(), 1u);
    EXPECT_EQ((*decoded.allowedNssaiList)[0].accessType.value, "3GPP_ACCESS");
}

TEST(NssfDtos, SliceInfoForRegistrationRoundTrips) {
    sbi_gen::Snssai requested;
    requested.sst = 1;

    sbi_gen::SliceInfoForRegistration original;
    original.requestedNssai = std::vector<sbi_gen::Snssai>{requested};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::SliceInfoForRegistration>();
    ASSERT_TRUE(decoded.requestedNssai.has_value());
    ASSERT_EQ(decoded.requestedNssai->size(), 1u);
    EXPECT_EQ((*decoded.requestedNssai)[0].sst, 1);
}

TEST(NssfDtos, NssaiAvailabilityInfoRoundTrips) {
    sbi_gen::Tai_CommonData tai;
    tai.plmnId.mcc = "310";
    tai.plmnId.mnc = "410";
    tai.tac = "000001";

    sbi_gen::ExtSnssai supported;
    supported.sst = 1;

    sbi_gen::SupportedNssaiAvailabilityData data;
    data.tai = tai;
    data.supportedSnssaiList = {supported};

    sbi_gen::NssaiAvailabilityInfo original;
    original.supportedNssaiAvailabilityData = {data};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::NssaiAvailabilityInfo>();
    ASSERT_EQ(decoded.supportedNssaiAvailabilityData.size(), 1u);
    EXPECT_EQ(decoded.supportedNssaiAvailabilityData[0].tai.plmnId.mcc, "310");
    EXPECT_EQ(decoded.supportedNssaiAvailabilityData[0].tai.tac, "000001");
    EXPECT_EQ(decoded.supportedNssaiAvailabilityData[0].supportedSnssaiList[0].sst, 1);
}

TEST(NssfDtos, NssfEventSubscriptionCreateDataRoundTrips) {
    sbi_gen::Tai_CommonData tai;
    tai.plmnId.mcc = "310";
    tai.plmnId.mnc = "410";
    tai.tac = "000001";

    sbi_gen::NssfEventSubscriptionCreateData original;
    original.nfNssaiAvailabilityUri = "https://amf.example/callback";
    original.event.value = "SNSSAI_STATUS_CHANGE_REPORT";
    original.taiList = std::vector<sbi_gen::Tai_CommonData>{tai};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::NssfEventSubscriptionCreateData>();
    EXPECT_EQ(decoded.nfNssaiAvailabilityUri, "https://amf.example/callback");
    EXPECT_EQ(decoded.event.value, "SNSSAI_STATUS_CHANGE_REPORT");
    ASSERT_TRUE(decoded.taiList.has_value());
    EXPECT_EQ((*decoded.taiList)[0].tac, "000001");
}

TEST(NssfDtos, NssfEventNotificationRoundTrips) {
    sbi_gen::NssfEventNotification original;
    original.subscriptionId = "nssai-avail-sub-1";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::NssfEventNotification>();
    EXPECT_EQ(decoded.subscriptionId, "nssai-avail-sub-1");
}
