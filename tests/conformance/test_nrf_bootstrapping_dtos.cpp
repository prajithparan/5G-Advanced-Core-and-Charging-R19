// Round-trip conformance test for NRF's Nnrf_Bootstrapping generated DTO (docs/DECISIONS.md
// ADR-0194, gap-closure per ADR-0193). Source:
// specs/5G_APIs-REL-19/TS29510_Nnrf_Bootstrapping.yaml. Same precedent as
// test_lmf_dtos.cpp/test_gmlc_dtos.cpp: construct -> to_json -> from_json -> compare, using the
// real generated types, not synthetic ones.

#include <nlohmann/json.hpp>

#include "TS29510_Nnrf_Bootstrapping.hpp"

#include <gtest/gtest.h>

TEST(NrfBootstrappingDtos, StatusRoundTrips) {
    sbi_gen::Status original{sbi_gen::Status::OPERATIVE};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::Status>();
    EXPECT_EQ(decoded.value, "OPERATIVE");
    EXPECT_EQ(decoded, original);
}

TEST(NrfBootstrappingDtos, BootstrappingInfoRoundTrips) {
    sbi_gen::BootstrappingInfo original;
    original.status = sbi_gen::Status{sbi_gen::Status::OPERATIVE};
    original.nrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";
    original.oauth2Required = nlohmann::json{{"nnrf-nfm", false}, {"nnrf-disc", false}};
    original._links = nlohmann::json{{"self", nlohmann::json{{"href", "/bootstrapping"}}}};

    nlohmann::json j = original;
    auto decoded = j.get<sbi_gen::BootstrappingInfo>();
    ASSERT_TRUE(decoded.status.has_value());
    EXPECT_EQ(decoded.status->value, "OPERATIVE");
    ASSERT_TRUE(decoded.nrfInstanceId.has_value());
    EXPECT_EQ(*decoded.nrfInstanceId, "5ba9a927-1d31-4c8e-8a10-000000000001");
    ASSERT_TRUE(decoded.oauth2Required.has_value());
    EXPECT_EQ((*decoded.oauth2Required)["nnrf-nfm"], false);
    EXPECT_EQ(decoded._links["self"]["href"], "/bootstrapping");
}
