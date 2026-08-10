// Unit tests for libs/bss-sid -- see docs/CHARGING_MAPPING.md for the resolved SUPI ->
// TMF632 Individual mapping this tests, and bss_sid/party.hpp's own file header for why these
// fields (confirmed against TM Forum's real TMF632 v4.0.0 swagger) are the ones modeled.

#include "bss_sid/party.hpp"

#include <gtest/gtest.h>

TEST(BssSidParty, MapsSupiToIndividualIdentification) {
    const auto individual = bss_sid::map_supi_to_individual("imsi-999700000000001");

    ASSERT_EQ(individual.individualIdentification.size(), 1u);
    EXPECT_EQ(individual.individualIdentification[0].identificationType, "SUPI");
    EXPECT_EQ(individual.individualIdentification[0].identificationId, "imsi-999700000000001");
}

TEST(BssSidParty, MapSupiToIndividualLeavesIdUnset) {
    // See party.hpp's own comment: no real Party-management store exists yet to allocate a real
    // BSS-side party id, so map_supi_to_individual deliberately never fabricates one.
    const auto individual = bss_sid::map_supi_to_individual("imsi-999700000000001");
    EXPECT_FALSE(individual.id.has_value());
}

TEST(BssSidParty, IndividualJsonRoundTrips) {
    bss_sid::Individual original{};
    original.id = "party-1";
    original.individualIdentification.push_back(
        bss_sid::IndividualIdentification{.identificationType = "SUPI", .identificationId = "imsi-1"});

    const nlohmann::json j = original;
    const auto decoded = j.get<bss_sid::Individual>();

    ASSERT_TRUE(decoded.id.has_value());
    EXPECT_EQ(*decoded.id, "party-1");
    ASSERT_EQ(decoded.individualIdentification.size(), 1u);
    EXPECT_EQ(decoded.individualIdentification[0].identificationType, "SUPI");
    EXPECT_EQ(decoded.individualIdentification[0].identificationId, "imsi-1");
}

TEST(BssSidParty, IndividualJsonOmitsUnsetId) {
    // Real TMF632 wire shape: a JSON body this project might one day submit to a real TMF632
    // implementation should not send a null/empty "id" for a resource that hasn't been assigned
    // one -- omit the key entirely rather than emit id: null.
    const bss_sid::Individual individual = bss_sid::map_supi_to_individual("imsi-1");
    const nlohmann::json j = individual;
    EXPECT_FALSE(j.contains("id"));
}

TEST(BssSidParty, IndividualIdentificationJsonRoundTrips) {
    bss_sid::IndividualIdentification original{.identificationType = "SUPI",
                                               .identificationId = "imsi-999700000000001"};
    const nlohmann::json j = original;
    const auto decoded = j.get<bss_sid::IndividualIdentification>();
    EXPECT_EQ(decoded.identificationType, original.identificationType);
    EXPECT_EQ(decoded.identificationId, original.identificationId);
}
