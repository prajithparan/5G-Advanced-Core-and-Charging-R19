#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>

// P15 (CHARGING_PROMPT.md's principle list), delivered as part of P4.12: per-protocol TPS spike
// protection.
//
// A signalling storm -- mass re-registration after a RAN outage, a runaway peer, a retry stampede
// -- is a real carrier failure mode, and the honest position before this was that this project had
// no defence against one at all: every NF accepted whatever arrived and failed on its own terms.
// This is the mechanism that gives a protocol front door a ceiling.
namespace sbi_core {

// Classic token bucket: `sustained_tps` tokens refill per second up to `burst_capacity`, and each
// admitted request takes one. Chosen over a fixed window deliberately -- a window's boundary lets
// through 2x the intended rate across its edge, which is precisely the spike this exists to stop.
//
// Thread-safe: NF servers dispatch handlers across a worker pool, so one bucket is shared.
class TokenBucket {
public:
    // burst_capacity <= 0 is treated as equal to sustained_tps (one second of burst).
    TokenBucket(double sustained_tps, double burst_capacity);

    // Takes one token if available. False means the caller should shed this request.
    bool try_acquire();

    // Requests shed since construction -- what the Prometheus counter and P12's alarming read.
    std::uint64_t shed_count() const;

private:
    mutable std::mutex mutex_;
    double sustained_tps_;
    double capacity_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    std::uint64_t shed_ = 0;
};

// Reads an OPTIONAL `max_tps` (and optional `tps_burst`) from an NF's own config. Absent, null or
// <= 0 means no ceiling -- an NF that has not opted in behaves exactly as it did before, which is
// what makes it safe to wire this into every NF at once.
struct TpsLimitConfig {
    double sustained_tps = 0.0;
    double burst = 0.0;
    bool enabled() const { return sustained_tps > 0.0; }
};
TpsLimitConfig read_tps_limit(const nlohmann::json& config);

} // namespace sbi_core
