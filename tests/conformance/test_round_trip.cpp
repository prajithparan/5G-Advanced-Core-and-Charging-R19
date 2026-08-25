// Round-trip schema-conformance test for tools/sbi-codegen output (Phase 1,
// DoD item 6: "Conformance test against the generated OpenAPI schema
// (request AND response)"). Exercises real generated types -- not synthetic
// ones -- from the merged TS26510_CommonData_grp header (see
// docs/DECISIONS.md ADR-0010 for why 22 source YAML files ended up merged
// into that one header: a genuine circular dependency in 3GPP's own schema
// graph, not a codegen artifact).
//
// Two things are checked:
//   1. C++ round-trip: construct -> to_json -> from_json -> must equal the
//      original. Run here via GoogleTest.
//   2. Structural schema conformance: the JSON this test emits is checked by
//      tests/conformance/validate_structural_conformance.py against the
//      *actual* OpenAPI schema's required/properties field names (not a
//      hand-copied expectation) -- see that script for what it does and does
//      not check (field name/required-ness only, not full JSON Schema
//      type/pattern validation; disclosed scope limit, not silently assumed
//      exhaustive).

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "TS26510_CommonData_grp.hpp"

#include <gtest/gtest.h>

namespace {

std::filesystem::path SamplesDir() {
    const char* dir = std::getenv("SBI_CONFORMANCE_SAMPLES_DIR");
    return dir != nullptr ? std::filesystem::path(dir) : std::filesystem::path(".");
}

void WriteSample(const std::string& filename, const nlohmann::json& j) {
    std::filesystem::create_directories(SamplesDir());
    std::ofstream out(SamplesDir() / filename);
    out << j.dump(2);
}

} // namespace

TEST(RoundTrip, NFTypeKnownValueRoundTrips) {
    sbi_gen::NFType original;
    original.value = sbi_gen::NFType::AMF;

    nlohmann::json j;
    sbi_gen::to_json(j, original);
    EXPECT_EQ(j.get<std::string>(), "AMF");

    sbi_gen::NFType decoded;
    sbi_gen::from_json(j, decoded);
    EXPECT_EQ(decoded, original);
}

// This is the specific case openapi-generator's cpp-pistache-server model
// got wrong for anyOf[enum,string] schemas: it produced an empty class whose
// to_json emitted "{}" and whose operator== did not even compile (see
// ADR-0010). A value outside the known-enum list must still round-trip.
TEST(RoundTrip, NFTypeUnknownValueRoundTrips) {
    sbi_gen::NFType original;
    original.value = "SOME_FUTURE_NF_TYPE_NOT_YET_STANDARDIZED";

    nlohmann::json j;
    sbi_gen::to_json(j, original);
    EXPECT_EQ(j.get<std::string>(), "SOME_FUTURE_NF_TYPE_NOT_YET_STANDARDIZED");

    sbi_gen::NFType decoded;
    sbi_gen::from_json(j, decoded);
    EXPECT_EQ(decoded, original);

    WriteSample("nftype_unknown.json", j);
}

TEST(RoundTrip, GuamiNestedObjectRoundTrips) {
    sbi_gen::PlmnIdNid plmn;
    plmn.mcc = "310";
    plmn.mnc = "410";

    sbi_gen::Guami original;
    original.plmnId = plmn;
    original.amfId = "ABC123";

    nlohmann::json j;
    sbi_gen::to_json(j, original);

    EXPECT_EQ(j.at("plmnId").at("mcc").get<std::string>(), "310");
    EXPECT_EQ(j.at("plmnId").at("mnc").get<std::string>(), "410");
    EXPECT_EQ(j.at("amfId").get<std::string>(), "ABC123");

    sbi_gen::Guami decoded;
    sbi_gen::from_json(j, decoded);
    EXPECT_EQ(decoded.plmnId.mcc, original.plmnId.mcc);
    EXPECT_EQ(decoded.plmnId.mnc, original.plmnId.mnc);
    EXPECT_EQ(decoded.amfId, original.amfId);

    WriteSample("guami.json", j);
}

// AmfId: pattern '^[A-Fa-f0-9]{6}$' (TS29571_CommonData.yaml, line 1155).
// openapi-generator silently drops `pattern` constraints entirely (no
// std::regex usage anywhere in its generated model/ directory for this file
// set -- see ADR-0010); this generator enforces them via a real
// validate_<Type> function, checked here against both a conforming and a
// non-conforming value.
TEST(RoundTrip, AmfIdPatternValidation) {
    EXPECT_TRUE(sbi_gen::validate_AmfId("ABC123"));
    EXPECT_TRUE(sbi_gen::validate_AmfId("abc123"));
    EXPECT_FALSE(sbi_gen::validate_AmfId("ZZZ123"));  // not hex
    EXPECT_FALSE(sbi_gen::validate_AmfId("ABC12"));   // too short (5 not 6 chars)
    EXPECT_FALSE(sbi_gen::validate_AmfId("ABC1234")); // too long
}
