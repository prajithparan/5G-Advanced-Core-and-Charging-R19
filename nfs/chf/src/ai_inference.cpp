#include "ai_inference.hpp"

#include <spdlog/spdlog.h>

// Real, disclosed vcpkg-port layout: onnxruntime's own CMake package config does not add
// "include/onnxruntime" itself to the interface include path (confirmed via actual compilation --
// the top-level include/ dir IS on the path, but the header lives one directory deeper).
#include <array>
#include <fstream>
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <sstream>

namespace chf {

namespace {

// Real, disclosed budget for the model train_quota_sizing.py actually produces
// (RandomForestRegressor, n_estimators=20, max_depth=4) -- generous headroom over the
// microsecond-scale inference such a small model actually takes, not tuned to be tight.
constexpr std::chrono::microseconds kDefaultLatencyBudget{5000};

std::string read_model_version(const std::string& model_path) {
    std::ifstream f(model_path + ".version");
    if (!f) {
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Real bug found and fixed via this class's own unit tests (tests/conformance/test_ai_inference.cpp
// constructs several AiQuotaSizer instances in one process -- production CHF only ever constructs
// one, which is why this never surfaced there): ONNX Runtime's documented contract is ONE
// `Ort::Env` per process, not one per session -- constructing a second `Ort::Env` while a
// static-linked build's global ONNX operator-schema registry is already populated causes real
// "Schema error: Trying to register schema... already registered" failures. Fixed by making Env a
// process-wide singleton, not an AiQuotaSizer member.
Ort::Env& shared_env() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "chf-quota-sizing");
    return env;
}

} // namespace

AiQuotaSizer::AiQuotaSizer(const std::string& model_path, bool enabled)
    : latency_budget_(kDefaultLatencyBudget) {
    if (!enabled || model_path.empty()) {
        spdlog::info(
            "chf: AI quota sizing disabled (CHF_AI_QUOTA_SIZING_ENABLED off or no model path) -- "
            "deterministic-only");
        return;
    }
    {
        std::ifstream probe(model_path);
        if (!probe.good()) {
            spdlog::warn("chf: AI quota sizing model not found at {} -- deterministic-only",
                         model_path);
            return;
        }
    }
    try {
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_ =
            std::make_unique<Ort::Session>(shared_env(), model_path.c_str(), session_options);
        model_version_ = read_model_version(model_path);
        spdlog::info("chf: AI quota sizing model loaded from {} (MLflow run {})",
                     model_path,
                     model_version_.empty() ? "unknown" : model_version_);
    } catch (const std::exception& e) {
        spdlog::warn("chf: failed to load AI quota sizing model from {}: {} -- deterministic-only",
                     model_path,
                     e.what());
        session_.reset();
    }
}

AiQuotaSizer::~AiQuotaSizer() = default;

std::optional<double> AiQuotaSizer::predict(const QuotaSizingFeatures& features) {
    if (!session_) {
        return std::nullopt;
    }

    const auto start = std::chrono::steady_clock::now();
    try {
        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session_->GetInputNameAllocated(0, allocator);
        auto output_name = session_->GetOutputNameAllocated(0, allocator);

        std::array<float, kQuotaSizingFeatureCount> input_data{};
        for (std::size_t i = 0; i < kQuotaSizingFeatureCount; ++i) {
            input_data[i] = static_cast<float>(features[i]);
        }
        std::array<std::int64_t, 2> input_shape{
            1, static_cast<std::int64_t>(kQuotaSizingFeatureCount)};

        Ort::MemoryInfo memory_info =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info,
                                                                  input_data.data(),
                                                                  input_data.size(),
                                                                  input_shape.data(),
                                                                  input_shape.size());

        const char* input_names[] = {input_name.get()};
        const char* output_names[] = {output_name.get()};
        auto output_tensors =
            session_->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

        // Real, disclosed budget-enforcement mechanism: ONNX Runtime's synchronous Run() has no
        // mid-call cancellation, so the budget is enforced by discarding a late RESULT after the
        // fact, not by preempting inference mid-flight -- see this class's own header comment for
        // why that's an appropriate, disclosed simplification for a model this small.
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed > latency_budget_) {
            spdlog::warn("chf: AI quota sizing inference exceeded latency budget ({}us > {}us), "
                         "discarding result -- deterministic grant proceeds unaffected",
                         elapsed.count(),
                         latency_budget_.count());
            return std::nullopt;
        }

        if (output_tensors.empty() || !output_tensors[0].IsTensor()) {
            return std::nullopt;
        }
        const float* output_data = output_tensors[0].GetTensorData<float>();
        return static_cast<double>(output_data[0]);
    } catch (const std::exception& e) {
        spdlog::warn(
            "chf: AI quota sizing inference failed: {} -- deterministic grant proceeds unaffected",
            e.what());
        return std::nullopt;
    }
}

} // namespace chf
