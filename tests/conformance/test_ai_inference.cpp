// P4.8 (CHARGING_PROMPT.md Angle 1a, ADR-0074): real ONNX Runtime in-process inference wrapper
// (nfs/chf/src/ai_inference.hpp/.cpp) -- unit tests exercising the kill-switch, cold-start/
// missing-model, and real-inference paths directly, which is hard to trigger deterministically
// through this project's own integration-test-against-a-real-running-NF discipline (the latency
// budget path in particular). Compiled directly against the same nfs/chf/src/ai_inference.cpp
// translation unit CHF itself builds -- ai_inference.hpp/.cpp has no dependency on any other
// nfs/chf-private header, so this is a clean, standalone compilation unit, not a violation of
// CLAUDE.md's "no NF includes another NF's private headers" rule (that rule is about cross-NF
// coupling; this is the SAME NF's own code, exercised by its own test).
//
// The fixture (tests/conformance/fixtures/test_quota_model.onnx) is a small, deterministic,
// hand-built ONNX graph (output = sum(input), via onnx.helper's ReduceSum -- see the Python
// snippet that generated it, kept alongside this file's own history) with the SAME real I/O
// contract shape (input "X" [batch,4], output "variable" [batch,1]) nfs/chf/training/
// train_quota_sizing.py's real skl2onnx export produces -- exercising the exact same AiQuotaSizer
// code path a real trained model would, with an exactly-known expected output so assertions don't
// need floating-point-fuzzy trained-model tolerances.

#include <fstream>

#include "ai_inference.hpp"

#include <gtest/gtest.h>

namespace {

TEST(AiQuotaSizer, DisabledReturnsNullopt) {
    chf::AiQuotaSizer sizer("", false);
    EXPECT_FALSE(sizer.is_enabled());
    EXPECT_FALSE(sizer.predict({1.0, 2.0, 3.0, 4.0}).has_value());
}

TEST(AiQuotaSizer, EnabledWithNoModelPathStaysDisabled) {
    // Real kill-switch-off-equivalent state: enabled=true but no model configured at all --
    // matches main.cpp's own real CHF_QUOTA_MODEL_PATH-unset default.
    chf::AiQuotaSizer sizer("", true);
    EXPECT_FALSE(sizer.is_enabled());
}

TEST(AiQuotaSizer, MissingModelFileStaysDisabled) {
    chf::AiQuotaSizer sizer("/nonexistent/path/does-not-exist.onnx", true);
    EXPECT_FALSE(sizer.is_enabled());
    EXPECT_FALSE(sizer.predict({1.0, 2.0, 3.0, 4.0}).has_value());
}

TEST(AiQuotaSizer, LoadsRealOnnxModelAndPredicts) {
    chf::AiQuotaSizer sizer(QUOTA_MODEL_FIXTURE_PATH, true);
    ASSERT_TRUE(sizer.is_enabled());

    const auto result = sizer.predict({1.0, 2.0, 3.0, 4.0});
    ASSERT_TRUE(result.has_value());
    // Fixture computes sum(input) exactly -- 1+2+3+4=10, real, not a trained approximation.
    EXPECT_NEAR(*result, 10.0, 1e-3);
}

TEST(AiQuotaSizer, ModelVersionEmptyWithoutSidecarVersionFile) {
    // The fixture has no "<path>.version" file (that sidecar is written by
    // train_quota_sizing.py's own MLflow-run-id logging) -- a real, valid "unknown version" state.
    chf::AiQuotaSizer sizer(QUOTA_MODEL_FIXTURE_PATH, true);
    ASSERT_TRUE(sizer.is_enabled());
    EXPECT_TRUE(sizer.model_version().empty());
}

TEST(AiQuotaSizer, ReadsModelVersionFromSidecarFile) {
    // Real end-to-end check of the model-version sidecar convention train_quota_sizing.py writes
    // and AiQuotaSizer reads -- copies the fixture to a temp path with a real ".version" file next
    // to it, rather than mutating the checked-in fixture.
    const std::string temp_model = "/tmp/test_quota_model_with_version.onnx";
    {
        std::ifstream src(QUOTA_MODEL_FIXTURE_PATH, std::ios::binary);
        std::ofstream dst(temp_model, std::ios::binary);
        dst << src.rdbuf();
    }
    {
        std::ofstream version_file(temp_model + ".version");
        version_file << "test-run-id-12345";
    }

    chf::AiQuotaSizer sizer(temp_model, true);
    ASSERT_TRUE(sizer.is_enabled());
    EXPECT_EQ(sizer.model_version(), "test-run-id-12345");

    std::remove(temp_model.c_str());
    std::remove((temp_model + ".version").c_str());
}

} // namespace
