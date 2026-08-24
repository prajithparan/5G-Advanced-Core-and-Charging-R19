// Round-trip conformance tests for SMSF's generated DTOs (docs/DECISIONS.md ADR-0188).
// Source: specs/5G_APIs-REL-19/TS29540_Nsmsf_SMService.yaml. Same precedent as
// test_eir_dtos.cpp/test_scp_dtos.cpp: construct -> to_json -> from_json -> compare, using the
// real generated types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29540_Nsmsf_SMService.hpp"
#include "TS29577_Nipsmgw_SMService.hpp"

#include <gtest/gtest.h>

TEST(SmsfDtos, UeSmsContextDataRoundTrips) {
    sbi_gen::UeSmsContextData original;
    original.supi = "imsi-001010000000001";
    original.amfId = "5ba9a927-1d31-4c8e-8a10-0000000000aa";
    original.accessType.value = sbi_gen::AccessType::V3GPP_ACCESS;

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::UeSmsContextData>();
    EXPECT_EQ(decoded.supi, "imsi-001010000000001");
    EXPECT_EQ(decoded.amfId, "5ba9a927-1d31-4c8e-8a10-0000000000aa");
    EXPECT_EQ(decoded.accessType.value, "3GPP_ACCESS");
}

TEST(SmsfDtos, SmsRecordDeliveryDataRoundTrips) {
    sbi_gen::SmsRecordDeliveryData original;
    original.smsRecordId = "rec-1";
    original.deliveryStatus.value = sbi_gen::SmsDeliveryStatus::SMS_DELIVERY_SMSF_ACCEPTED;

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::SmsRecordDeliveryData>();
    EXPECT_EQ(decoded.smsRecordId, "rec-1");
    EXPECT_EQ(decoded.deliveryStatus.value, "SMS_DELIVERY_SMSF_ACCEPTED");
}

TEST(SmsfDtos, IpSmGwSmsDataRoundTrips) {
    sbi_gen::SmsData original;
    original.smsPayload.contentId = "smsPayload";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::SmsData>();
    EXPECT_EQ(decoded.smsPayload.contentId, "smsPayload");
}
