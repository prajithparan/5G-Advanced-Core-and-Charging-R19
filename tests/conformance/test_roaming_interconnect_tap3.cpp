// Real TAP3 wiring for roaming_interconnect::RoamingCdrFile (ADR-0067) -- covers
// make_tap3_roaming_cdr_file/decode_tap3_roaming_cdr_file (pure functions, no PostgreSQL
// connection needed) separately from tests/integration/test_roaming_interconnect_postgres.cpp,
// which covers the real DB-backed CRUD paths.

#include "../../bss/roaming-interconnect/src/store.hpp"

#include <gtest/gtest.h>

TEST(RoamingInterconnectTap3, MakeAndDecodeRoundTripsThroughRawPayload) {
    tap3_core::DataInterchange data;
    tap3_core::TransferBatch batch;
    tap3_core::BatchControlInfo bci;
    bci.sender = "OPERA";
    bci.recipient = "OPERB";
    bci.fileSequenceNumber = "00007";
    batch.batchControlInfo = bci;
    data.transferBatch = batch;

    const auto file = roaming_interconnect::make_tap3_roaming_cdr_file("agreement-1", data);
    EXPECT_EQ(file.format, "TAP3");
    EXPECT_EQ(*file.agreementId, "agreement-1");
    ASSERT_FALSE(file.rawPayload.empty());

    const auto decoded = roaming_interconnect::decode_tap3_roaming_cdr_file(file);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->transferBatch.has_value());
    ASSERT_TRUE(decoded->transferBatch->batchControlInfo.has_value());
    EXPECT_EQ(*decoded->transferBatch->batchControlInfo->sender, "OPERA");
    EXPECT_EQ(*decoded->transferBatch->batchControlInfo->fileSequenceNumber, "00007");
}

TEST(RoamingInterconnectTap3, DecodeRejectsNonTap3Format) {
    roaming_interconnect::RoamingCdrFile file;
    file.format = "STUB";
    EXPECT_FALSE(roaming_interconnect::decode_tap3_roaming_cdr_file(file).has_value());
}
