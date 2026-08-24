// Round-trip conformance tests for 5G-EIR's generated DTOs (docs/DECISIONS.md ADR-0187).
// Source: specs/5G_APIs-REL-19/TS29511_N5g-eir_EquipmentIdentityCheck.yaml. Same precedent as
// test_scp_dtos.cpp/test_nef_dtos.cpp: construct -> to_json -> from_json -> compare, using the
// real generated types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29511_N5g-eir_EquipmentIdentityCheck.hpp"

#include <gtest/gtest.h>

TEST(EirDtos, EirResponseDataWhitelistedRoundTrips) {
    sbi_gen::EirResponseData original;
    original.status.value = sbi_gen::EquipmentStatus::WHITELISTED;

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::EirResponseData>();
    EXPECT_EQ(decoded.status.value, sbi_gen::EquipmentStatus::WHITELISTED);
}

TEST(EirDtos, EirResponseDataBlacklistedRoundTrips) {
    sbi_gen::EirResponseData original;
    original.status.value = sbi_gen::EquipmentStatus::BLACKLISTED;

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::EirResponseData>();
    EXPECT_EQ(decoded.status.value, sbi_gen::EquipmentStatus::BLACKLISTED);
    EXPECT_TRUE(decoded.status == original.status);
}
