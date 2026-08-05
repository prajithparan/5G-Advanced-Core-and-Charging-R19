#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "TS29122_CommonData_grp.hpp"

// Private to nfs/amf. Three id-keyed subscription stores backing:
//   - N1N2MessageSubscribe / N1N2MessageUnSubscribe   (TS29518_Namf_Communication.yaml,
//     UeN1N2InfoSubscriptionCreateData, scoped to a ueContextId)
//   - NonUeN2InfoSubscribe / NonUeN2InfoUnSubscribe    (NonUeN2InfoSubscriptionCreateData)
//   - AMFStatusChangeSubscribe / UnSubscribe / SubscribeModfy (SubscriptionData_Namf_Communication)
//
// All three follow the same trivial assign-id/store/remove shape as
// nfs/nrf/src/registry.hpp's SubscriptionRegistry -- factored into one template here since it's
// genuinely triplicated within this NF (not a speculative abstraction; see CLAUDE.md's "three
// similar lines is better than a premature abstraction" -- this is the fourth). In-memory only, no
// persistence across restarts, same disclosed simplification as NRF's registry (ADR-0015).
//
// Disclosed gap shared with NRF's SubscriptionRegistry: notification DELIVERY is not implemented
// here at all (NRF at least attempts best-effort delivery). AMF has no trigger path that would ever
// fire one yet -- the events these subscriptions describe (N1/N2 message arrival, non-UE N2 info,
// AMF status change) all ultimately originate from real UE/RAN activity or operational state
// changes this lab build cannot produce (no NGAP/N2, no multi-AMF deployment). Subscriptions are
// stored and can be created/removed correctly; nothing notifies them. Not silently omitted -- there
// is currently no honest way to test delivery without fabricating a trigger.

namespace amf {

template <typename T> class IdKeyedStore {
public:
    explicit IdKeyedStore(std::string id_prefix) : id_prefix_(std::move(id_prefix)) {}

    std::string create(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string id = id_prefix_ + std::to_string(next_id_++);
        items_.emplace(id, std::move(value));
        return id;
    }

    std::optional<T> get(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = items_.find(id);
        if (it == items_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Returns false if id doesn't exist (caller's responsibility to turn that into a 404).
    bool update(const std::string& id, T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = items_.find(id);
        if (it == items_.end()) {
            return false;
        }
        it->second = std::move(value);
        return true;
    }

    bool remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.erase(id) > 0;
    }

private:
    std::string id_prefix_;
    std::mutex mutex_;
    std::unordered_map<std::string, T> items_;
    std::uint64_t next_id_ = 1;
};

// N1N2MessageSubscribe's resource is scoped by BOTH ueContextId (path) and subscriptionId
// (assigned) -- N1N2MessageUnSubscribe must 404 if subscriptionId exists but under a different
// ueContextId, not just check subscriptionId alone. IdKeyedStore alone can't express that second
// dimension, so it's paired with the owning ueContextId here.
struct UeN1N2Subscription {
    std::string ue_context_id;
    sbi_gen::UeN1N2InfoSubscriptionCreateData data;
};

using UeN1N2SubscriptionStore = IdKeyedStore<UeN1N2Subscription>;
using NonUeN2SubscriptionStore = IdKeyedStore<sbi_gen::NonUeN2InfoSubscriptionCreateData>;
using AmfStatusSubscriptionStore = IdKeyedStore<sbi_gen::SubscriptionData_Namf_Communication>;

} // namespace amf
