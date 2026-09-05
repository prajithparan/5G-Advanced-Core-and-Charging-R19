#include "sbi_core/rate_limit.hpp"

#include <algorithm>
#include <cstdlib>

namespace sbi_core {

TokenBucket::TokenBucket(double sustained_tps, double burst_capacity)
    : sustained_tps_(sustained_tps),
      capacity_(burst_capacity > 0.0 ? burst_capacity : sustained_tps),
      // Starts FULL, not empty: an NF that receives its configured burst immediately after startup
      // is not in overload, and starting empty would shed a legitimate startup surge -- exactly
      // the mass re-registration this is supposed to survive rather than amplify.
      tokens_(burst_capacity > 0.0 ? burst_capacity : sustained_tps),
      last_refill_(std::chrono::steady_clock::now()) {}

bool TokenBucket::try_acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_refill_).count();
    last_refill_ = now;
    tokens_ = std::min(capacity_, tokens_ + elapsed * sustained_tps_);

    if (tokens_ < 1.0) {
        ++shed_;
        return false;
    }
    tokens_ -= 1.0;
    return true;
}

std::uint64_t TokenBucket::shed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shed_;
}

TpsLimitConfig read_tps_limit(const nlohmann::json& config) {
    TpsLimitConfig out;
    if (config.contains("max_tps") && !config.at("max_tps").is_null()) {
        out.sustained_tps = config.at("max_tps").get<double>();
    }
    if (config.contains("tps_burst") && !config.at("tps_burst").is_null()) {
        out.burst = config.at("tps_burst").get<double>();
    }
    // Env override, same config-file-first-then-env convention nf_config::require uses per key.
    // One shared name rather than per-service, because this function is given the config object
    // and not the service name -- an operator setting a ceiling is doing it per container anyway,
    // and it is what lets a test drive a real NF into shedding without editing checked-in config.
    if (const char* env = std::getenv("SBI_MAX_TPS")) {
        out.sustained_tps = std::strtod(env, nullptr);
    }
    if (const char* env = std::getenv("SBI_TPS_BURST")) {
        out.burst = std::strtod(env, nullptr);
    }
    return out;
}

} // namespace sbi_core
