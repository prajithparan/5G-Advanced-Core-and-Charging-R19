#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <sw/redis++/redis++.h>

// Private to nfs/amf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #100, ADR-0090): a real, previously-missing
// architectural piece PathSwitchRequest needs. Every NGAP procedure this project has handled
// before this one arrives on the SAME SCTP association a UE's context already lives on
// (ue_context_store.hpp/ue_security_context_store.hpp are both keyed by SUPI/TMSI, values a UE
// itself presents). PathSwitchRequest is different: it arrives on a BRAND NEW association from a
// DIFFERENT (target) gNB, carrying only the UE's SourceAMF-UE-NGAP-ID -- an AMF-local integer the
// UE itself never sees or presents. Without a real amf_ue_ngap_id -> tmsi index, this AMF has no
// way to find the existing UeSecurityContext (UeSecurityContextStore, keyed by tmsi) a
// PathSwitchRequest needs to reuse. Redis-backed, same "hot-path state, not a system-of-record"
// pattern ue_security_context_store.hpp's own header comment already documents -- shares the same
// Redis connection/instance, just a distinct key prefix.

namespace amf {

class AmfUeIdIndexStore {
public:
    explicit AmfUeIdIndexStore(std::shared_ptr<sw::redis::Redis> redis)
        : redis_(std::move(redis)) {}

    // amf_ue_id: this AMF's own AMF-UE-NGAP-ID (TS 38.413 §9.3.3.1, 0..2^40-1, this project's own
    // UeAuthState::amf_ue_id field is `unsigned long`) for a UE whose registration/reconnection
    // just completed -- put alongside UeSecurityContextStore::put's own call, same tmsi.
    void put(unsigned long amf_ue_id, std::uint32_t tmsi);
    std::optional<std::uint32_t> get(unsigned long amf_ue_id);
    void remove(unsigned long amf_ue_id);

private:
    std::shared_ptr<sw::redis::Redis> redis_;
};

} // namespace amf
