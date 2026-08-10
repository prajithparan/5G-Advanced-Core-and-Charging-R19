#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// Now backs both Create and Release (ADR-0044/ADR-0046). Still not a full resource store in the
// nfs/pcf/src/stores.hpp sense (AmPolicyStore holds the actual resource JSON for a real GET) --
// Create doesn't read anything back, and Release only needs to know whether a ref is still active
// (to return 404 for an unknown/already-released one) and to stop tracking it, not what its
// content was. A future Update turn (ADR-0044's own deferred scope) would need to start holding
// real resource JSON, same shape as nfs/pcf/src/stores.hpp's precedent.

namespace chf {

class ChargingDataStore {
public:
    // Allocates a new ChargingDataRef and marks it active.
    std::string create();

    // Returns false (and leaves state unchanged) if ref isn't currently active -- an unknown or
    // already-released ChargingDataRef, per TS 32.291's real 404 case for Update/Release.
    bool release(const std::string& ref);

    // ADR-0050 Stage 4: Update's real 404 case (TS 32.291: an unknown/already-released
    // ChargingDataRef) needs a non-destructive check -- unlike release(), Update must NOT remove
    // the ref just for asking whether it's still active.
    bool is_active(const std::string& ref);

private:
    std::mutex mutex_;
    std::unordered_set<std::string> active_refs_;
    std::uint64_t next_id_ = 1;
};

} // namespace chf
