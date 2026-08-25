// Round-trip conformance test for AUSF's Nausf_UPUProtection generated DTOs (docs/DECISIONS.md
// ADR-0195, gap-closure per ADR-0193). Source:
// specs/5G_APIs-REL-19/TS29509_Nausf_UPUProtection.yaml. Same precedent as
// test_nrf_bootstrapping_dtos.cpp: construct -> to_json -> from_json -> compare, using the real
// generated types, not synthetic ones. UpuInfo_Nausf_UPUProtection/UpuSecurityInfo land in the
// shared TS26510_CommonData_grp.hpp (real SCC grouping) -- both UpuInfo and UpuData are
// disambiguated (suffixed _Nausf_UPUProtection) from other real, distinct schemas of the same name
// declared in other already-wired pilot files.

#include <nlohmann/json.hpp>

#include "TS26510_CommonData_grp.hpp"

#include <gtest/gtest.h>

TEST(AusfUpuDtos, UpuInfoRoundTrips) {
    sbi_gen::UpuInfo_Nausf_UPUProtection original;
    sbi_gen::UpuData_Nausf_UPUProtection data{};
    data.drei = true;
    original.upuDataList = {data};
    original.upuAckInd = true;

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::UpuInfo_Nausf_UPUProtection>();
    ASSERT_EQ(decoded.upuDataList.size(), 1U);
    ASSERT_TRUE(decoded.upuDataList[0].drei.has_value());
    EXPECT_TRUE(*decoded.upuDataList[0].drei);
    EXPECT_TRUE(decoded.upuAckInd);
}

TEST(AusfUpuDtos, UpuSecurityInfoRoundTrips) {
    sbi_gen::UpuSecurityInfo original;
    original.upuMacIausf = "0123456789abcdef0123456789abcdef";
    original.counterUpu = "0001";
    original.upuXmacIue = "fedcba9876543210fedcba9876543210";

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::UpuSecurityInfo>();
    EXPECT_EQ(decoded.upuMacIausf, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(decoded.counterUpu, "0001");
    ASSERT_TRUE(decoded.upuXmacIue.has_value());
    EXPECT_EQ(*decoded.upuXmacIue, "fedcba9876543210fedcba9876543210");
}
