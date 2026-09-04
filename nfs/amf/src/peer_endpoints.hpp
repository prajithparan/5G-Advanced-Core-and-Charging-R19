#pragma once

#include <string>

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #109, ADR-0077): AMF's peer NF base URLs,
// loaded from config/amf.json instead of being compile-time literals in ngap_task.cpp and
// ngap_handover.cpp.
//
// Why one struct rather than four more parameters: run_ngap_lifecycle already takes 12, and the
// handlers it dispatches to take 8 or more each. Four separate strings would be threaded through
// every one of them, at every call site. One const& carries the same information and matches the
// "shared by reference across threads" convention run_ngap_lifecycle's own header comment states
// for the stores. It is deliberately a plain aggregate: read-only after construction in main(),
// so it needs no synchronisation of its own.
//
// Deliberately NOT in scope for task #109, so the next reader knows it was decided rather than
// missed: kMcc/kMnc/kSst/kSd stay duplicated between the two translation units. Those are this
// lab's PLMN/slice identity, not a deployment endpoint, and the TU split that forces the
// duplication exists for a real CI compiler-memory reason (see ngap_handover.hpp's own header).
namespace amf::ngap {

struct PeerEndpoints {
    std::string ausf_base;
    std::string pcf_base;
    std::string smf_base;

    // ADR-0277: NSACF, for Network Slice Admission Control during Registration
    // (TS 23.501 §5.15.11, TS 23.502 §4.2.2.2.2).
    std::string nsacf_base;

    // AMF's OWN advertised base, not a peer. It is derived in main() from the same address and
    // port AMF registers with NRF rather than given its own config key -- a fifth key would have
    // to be kept in sync with `port` by hand, which is the exact drift this task removes.
    std::string self_base;
};

} // namespace amf::ngap
