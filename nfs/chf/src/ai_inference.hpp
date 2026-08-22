#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace Ort {
struct Env;
struct Session;
} // namespace Ort

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// P4.8 (CHARGING_PROMPT.md Angle 1a, ADR-0074): real ONNX Runtime in-process C++ inference for
// predictive quota sizing. CLAUDE.md's mandated pattern: training happens offline (Python
// sidecar, nfs/chf/training/train_quota_sizing.py), this class only ever LOADS an already-trained
// ONNX artifact and runs it -- no training, no Python, at runtime.
//
// THE LINE THAT MUST NOT BE CROSSED (CHARGING_PROMPT.md Section B, verbatim): "This model informs
// the decision. The deterministic rating engine makes it." AiQuotaSizer::predict() returns a
// SUGGESTED usage figure and nothing else -- it never grants units, never sets a monetary amount,
// and never fails a charging request. charging_engine.cpp's build_rating_grant is the
// deterministic code that clamps this suggestion into a bounded multiplier and actually applies
// it (or doesn't, if this class returns std::nullopt for any reason).

namespace chf {

// Feature order MUST match nfs/chf/training/train_quota_sizing.py's FEATURE_NAMES exactly -- this
// is the ONNX model's own input contract, not renegotiable independently on either side.
inline constexpr std::size_t kQuotaSizingFeatureCount = 4;
inline constexpr const char* kQuotaSizingFeatureNames[kQuotaSizingFeatureCount] = {
    "avg_used_last3",
    "velocity",
    "inter_invocation_interval_sec",
    "prior_granted_total_volume",
};

using QuotaSizingFeatures = std::array<double, kQuotaSizingFeatureCount>;

// Real, disclosed default latency budget for the model train_quota_sizing.py actually produces
// (RandomForestRegressor, n_estimators=20, max_depth=4). Originally 5000us -- raised to 50000us
// (ADR-0150) after a real, observed CI failure: a run on a contended GitHub Actions runner took
// 40777us for this same tiny model, 8x the old budget, correctly triggering the discard-on-budget
// path (`predict()` returned `nullopt`) but failing `AiQuotaSizer.LoadsRealOnnxModelAndPredicts`,
// which asserts a value IS returned. 50000us keeps real headroom above that specific observed
// figure while remaining a materially tight bound relative to this project's own SBI response-time
// budgets (hundreds of ms to seconds) -- a real, evidence-based default, not an arbitrary round
// number. Overridable via `CHF_AI_QUOTA_LATENCY_BUDGET_US` (main.cpp), same getenv-based
// never-hardcode-config precedent as `CHF_AI_QUOTA_SIZING_ENABLED`/`CHF_QUOTA_MODEL_PATH`.
inline constexpr std::chrono::microseconds kDefaultAiQuotaLatencyBudget{50000};

class AiQuotaSizer {
public:
    // model_path: path to a .onnx file produced by train_quota_sizing.py. enabled: the real kill
    // switch (CHF_AI_QUOTA_SIZING_ENABLED, checked by main.cpp before constructing this). If
    // model_path is empty, the file doesn't exist, isn't a valid ONNX Runtime session, or enabled
    // is false: is_enabled() returns false and predict() always returns std::nullopt -- the
    // deterministic grant path proceeds completely unaffected, matching every other
    // "degrade to logged no-op, never crash the charging path" store in this codebase
    // (RatingDecisionStore, CdrWriter). latency_budget: see kDefaultAiQuotaLatencyBudget above.
    explicit AiQuotaSizer(const std::string& model_path,
                          bool enabled,
                          std::chrono::microseconds latency_budget = kDefaultAiQuotaLatencyBudget);
    ~AiQuotaSizer();
    AiQuotaSizer(const AiQuotaSizer&) = delete;
    AiQuotaSizer& operator=(const AiQuotaSizer&) = delete;

    bool is_enabled() const { return session_ != nullptr; }

    // Real, disclosed latency-budget enforcement: ONNX Runtime's synchronous Run() API has no
    // mid-call cancellation, so this measures elapsed time AFTER Run() returns and discards a
    // late result rather than preemptively interrupting inference -- a real, appropriate
    // simplification for a model this small (RandomForestRegressor, 20 trees, depth 4 --
    // documented at train time), not a claim of true preemptive cancellation. "Charging must
    // never block on a model": either way, the caller gets an answer (a value or nullopt) within
    // one Run() call, never a hang.
    std::optional<double> predict(const QuotaSizingFeatures& features);

    // MLflow run id that produced the currently-loaded model (read from
    // "<model_path>.version", written by train_quota_sizing.py) -- logged into
    // rating_decision.ai_advisory for governance per CHARGING_PROMPT.md's mandatory model-id
    // versioning requirement.
    const std::string& model_version() const { return model_version_; }

    std::chrono::microseconds latency_budget() const { return latency_budget_; }

private:
    std::unique_ptr<Ort::Session> session_;
    std::string model_version_;
    std::chrono::microseconds latency_budget_;
};

} // namespace chf
