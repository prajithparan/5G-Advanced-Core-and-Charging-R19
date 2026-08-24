#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "TS29122_CommonData_grp.hpp"
#include "TS29518_Namf_MBSBroadcast.hpp"

// Private to nfs/amf. Five id-keyed subscription stores, plus one id-keyed non-subscription
// resource store reusing the same IdKeyedStore template, backing:
//   - N1N2MessageSubscribe / N1N2MessageUnSubscribe   (TS29518_Namf_Communication.yaml,
//     UeN1N2InfoSubscriptionCreateData, scoped to a ueContextId)
//   - NonUeN2InfoSubscribe / NonUeN2InfoUnSubscribe    (NonUeN2InfoSubscriptionCreateData)
//   - AMFStatusChangeSubscribe / UnSubscribe / SubscribeModfy (SubscriptionData_Namf_Communication)
//   - CreateSubscription / ModifySubscription / DeleteSubscription
//   (TS29518_Namf_EventExposure.yaml,
//     AmfEventSubscription, individual-subscription family at /subscriptions)
//   - CreateAMFSetLevelBulkSubscription / Modify.../Delete... (same AmfEventSubscription schema,
//     genuinely separate resource family/id space at /set-subscriptions per the YAML -- kept as a
//     second store instance, not merged with the one above, same "distinct resource, not a rename"
//     precedent as nfs/udr's OperatorSpecificDataStore vs PolicyOperatorSpecificDataStore)
//
// All five follow the same trivial assign-id/store/remove shape as
// nfs/nrf/src/registry.hpp's SubscriptionRegistry -- factored into one template here since it's
// genuinely repeated within this NF (not a speculative abstraction; see CLAUDE.md's "three
// similar lines is better than a premature abstraction" -- this is well past that bar). In-memory
// only, no persistence across restarts, same disclosed simplification as NRF's registry (ADR-0015).
//
// Disclosed gap shared with NRF's SubscriptionRegistry: notification DELIVERY is not implemented
// here at all (NRF at least attempts best-effort delivery). AMF has no trigger path that would ever
// fire one yet -- the events these subscriptions describe (N1/N2 message arrival, non-UE N2 info,
// AMF status change, and now AmfEventSubscription's own eventList -- registration/connectivity/
// reachability/etc.) all ultimately originate from real UE/RAN activity or operational state
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
// Backs both /subscriptions and /set-subscriptions (Namf_EventExposure) -- instantiated twice in
// main.cpp with distinct id prefixes, since the two are genuinely separate resource collections per
// TS29518_Namf_EventExposure.yaml even though they share the same AmfEventSubscription schema.
using AmfEventSubscriptionStore = IdKeyedStore<sbi_gen::AmfEventSubscription>;
// Backs Namf_MBSBroadcast's ContextCreate/ContextDelete/ContextUpdate (ADR-0200) -- not a
// subscription, just the same real assign-id/store/remove shape reused for a different resource
// type (a broadcast MBS session context, keyed by server-assigned mbsContextRef).
using MbsBroadcastContextStore = IdKeyedStore<sbi_gen::ContextCreateReqData>;

} // namespace amf
