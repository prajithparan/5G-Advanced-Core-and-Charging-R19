#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis++.h>

#include "aka_crypto/kdf.hpp"

// Private to nfs/ausf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #104, ADR-0081): real, persistent per-SUPI
// KAUSF + CounterSoR state -- the real, load-bearing prerequisite Nausf_SoRProtection needs and
// this project never had. Every KAUSF this project has computed so far (nfs/ausf/src/main.cpp's
// own AuthContext) lives only in the short-lived, per-in-flight-authentication AuthContextStore,
// removed once the 5G-AKA-confirmation/eap-session exchange completes -- correct for that
// exchange's own real scope, but Nausf_SoRProtection is invoked LATER, by UDM, well after
// authentication has finished (TS 33.501 clause 6.14.2, step 8-9: "The UDM shall select the AUSF
// that holds the latest KAUSF of the UE"). Real, disclosed architectural gap found via this
// project's own free5GC/open5GS capability sweep, not assumed -- same real prerequisite shape as
// AMF's own UeSecurityContextStore (ADR-0076), a different NF, same underlying "a root key must
// outlive the request that produced it" lesson.
//
// Redis-backed, same "hot-path state, not a system-of-record" pattern every other NF's own
// Redis-backed store already uses (CHF's ChargingDataStore, PCF's SpendingLimitTrackingStore,
// AMF's UeSecurityContextStore).

namespace ausf {

struct SorContext {
    aka_crypto::Kausf kausf{};
    // TS 33.501 clause 6.14.2.3's own real freshness counter -- see KausfStore::use_counter's
    // own comment for the exact state-machine rules this value's mutation follows.
    std::uint16_t counter_sor = 1;
    // Real, spec-mandated ("the AUSF shall suspend the SoR protection service for the UE, if the
    // CounterSoR ... is about to wrap around") -- set once counter_sor has been used at its own
    // real maximum (0xFFFF) and cleared only by a fresh KAUSF (store_fresh_kausf).
    bool suspended = false;
    // ADR-0195 (gap-closure, Nausf_UPUProtection): CounterUPU, TS 33.501 clause 6.15.2.2 -- a
    // real, SEPARATE 16-bit counter from CounterSoR above, "associated" with the same KAUSF but
    // maintained independently ("The AUSF and the UE shall associate a 16-bit counter,
    // CounterUPU, with the key KAUSF" -- clause 6.15.2.2, distinct from CounterSoR's own
    // identical-shaped clause 6.14.2.3). Same real state-machine rules (init 0x0001, increment
    // per computation, suspend on wraparound, reset on fresh KAUSF) -- see
    // KausfStore::use_upu_counter's own comment.
    std::uint16_t counter_upu = 1;
    bool suspended_upu = false;
};

class KausfStore {
public:
    explicit KausfStore(std::shared_ptr<sw::redis::Redis> redis) : redis_(std::move(redis)) {}

    // Real, disclosed trigger: called whenever a fresh KAUSF is established (5G-AKA confirmation
    // or EAP-AKA' success in nfs/ausf/src/main.cpp) -- TS 33.501 §6.14.2.3's own real rule ("the
    // AUSF ... shall initialize the CounterSoR to 0x00 01 when the newly derived KAUSF is
    // stored", and "When a fresh KAUSF is generated for the UE, the CounterSoR at the AUSF is
    // reset to 0x00 01"). Overwrites any prior context for this SUPI, clears suspended.
    void store_fresh_kausf(const std::string& supi, const aka_crypto::Kausf& kausf);

    std::optional<SorContext> get(const std::string& supi);

    // Real CounterSoR state machine (§6.14.2.3): returns the counter value to use for THIS
    // SoR-MAC-IAUSF computation (atomic Redis HINCRBY, no read-modify-write race against a
    // concurrent call for the same SUPI), then advances stored state for the next one --
    // 0x0002 after the first use, monotonically incrementing after that; 0x0000 is never
    // returned or stored (real, spec-forbidden value). Returns nullopt if no KAUSF is on record
    // for this SUPI, or if the counter is already suspended (real wrap-around protection --
    // 0xFFFF is the last value ever handed out; the call that hands it out also marks the
    // context suspended for every call after it, until a fresh KAUSF resets it).
    std::optional<std::uint16_t> use_counter(const std::string& supi);

    // ADR-0195: real CounterUPU state machine (clause 6.15.2.2), same shape as use_counter above
    // but tracking the separate counter_upu/suspended_upu fields -- CounterSoR and CounterUPU are
    // real, independent counters per the spec text cited on SorContext's own fields, not aliases
    // of each other.
    std::optional<std::uint16_t> use_upu_counter(const std::string& supi);

private:
    std::shared_ptr<sw::redis::Redis> redis_;
};

} // namespace ausf
