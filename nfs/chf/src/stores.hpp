#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "TS29594_Nchf_SpendingLimitControl.hpp"

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

// P4.2 (ADR-0055): Nchf_OfflineOnlyCharging's own OfflineChargingDataRef resource collection --
// a genuinely separate 3GPP resource from ChargingDataRef above (different service,
// /offlinechargingdata not /chargingdata), not reused, even though the tracking shape is
// identical (active-ref-set, same as ChargingDataStore -- no rating engine involved here, per
// TS32291_Nchf_OfflineOnlyCharging.yaml's own ChargingDataResponse schema carrying no
// multipleUnitInformation/grantedUnit field at all).
class OfflineChargingDataStore {
public:
    std::string create();
    bool release(const std::string& ref);
    bool is_active(const std::string& ref);

private:
    std::mutex mutex_;
    std::unordered_set<std::string> active_refs_;
    std::uint64_t next_id_ = 1;
};

// P4.2 (ADR-0055): Nchf_SpendingLimitControl's subscriptionId resource collection (TS 29.594).
// Unlike ChargingDataStore/OfflineChargingDataStore above, this is a real resource store (holds
// the actual SpendingLimitContext, not just an active-ref marker) -- PUT /subscriptions/{id} is a
// real update-in-place that needs the previous context, and building each SpendingLimitStatus
// response (Subscribe/Update) needs the subscription's own policyCounterIds to enumerate.
class SpendingLimitSubscriptionStore {
public:
    // Server-assigned subscriptionId, matching every other resource-creation convention in this
    // codebase (see e.g. bss/product-catalog/src/store.hpp).
    std::string create(sbi_gen::SpendingLimitContext context);

    // Real update-in-place. Returns false (leaves state unchanged) if id isn't a currently active
    // subscription -- TS 29.594's real 404 case.
    bool update(const std::string& id, sbi_gen::SpendingLimitContext context);

    bool remove(const std::string& id);

    std::optional<sbi_gen::SpendingLimitContext> get(const std::string& id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, sbi_gen::SpendingLimitContext> subscriptions_;
    std::uint64_t next_id_ = 1;
};

} // namespace chf
