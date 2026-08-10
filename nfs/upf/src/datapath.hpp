#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

// Real eBPF/XDP GTP-U decapsulation datapath -- Phase 3 Stage 4 (docs/DECISIONS.md ADR-0043).
// Wraps: a veth pair (upf-n3 / upf-n3-peer, representing UPF's N3-facing interface -- no
// dedicated NIC exists in this lab, matching every other loopback/virtual-interface assumption
// already made throughout this project), a TUN device (upf-tun0, representing the DN/N6-facing
// egress a decapsulated T-PDU is delivered to), the compiled XDP program
// (nfs/upf/bpf/gtpu_decap.bpf.c, attached to upf-n3 in generic/SKB-mode XDP), and a background
// thread polling the program's BPF ring buffer, writing each decapsulated T-PDU to the TUN
// device via an ordinary write() syscall.
//
// Needs CAP_NET_ADMIN (veth/TUN interface creation) and CAP_BPF (loading/attaching the XDP
// program) -- this build does not attempt to gain them itself (no setuid, no capability-dropping
// logic); the operator grants them to the built binary (e.g. via setcap) before running it.
// Datapath::create() returns std::nullopt on any failure, including missing privileges, and the
// caller (main()) treats that as a disclosed, non-fatal degradation: PFCP control-plane
// signalling (Stages 1-3) works identically with or without a datapath, exactly as it did before
// this stage existed.

namespace upf {

class Datapath {
public:
    // ADR-0050 Stage 2: invoked (on the datapath's own ring-buffer-polling thread -- callers
    // needing more than quick, non-blocking work should hand off to their own thread) whenever
    // the real in-kernel per-TEID byte counter crosses a provisioned URR's Volume Threshold or
    // Volume Quota. quota_exhausted distinguishes which.
    using UsageReportHandler =
        std::function<void(std::uint32_t teid, std::uint64_t total_octets, bool quota_exhausted)>;

    static std::optional<Datapath> create(UsageReportHandler on_usage_report);

    Datapath(Datapath&& other) noexcept;
    Datapath& operator=(Datapath&& other) noexcept;
    Datapath(const Datapath&) = delete;
    Datapath& operator=(const Datapath&) = delete;
    ~Datapath();

    // Registers a TEID as belonging to a real uplink PDR -- called from run_pfcp_lifecycle
    // whenever Session Establishment (ADR-0042) allocates one. Returns false if the underlying
    // BPF map update fails.
    bool register_teid(std::uint32_t teid);

    // ADR-0050 Stage 2: provisions the real per-TEID URR state (Volume Threshold/Quota, both in
    // octets) the XDP program counts against -- called from run_pfcp_lifecycle whenever Session
    // Establishment (ADR-0050 Stage 1) parses a real Create URR out of the request. Returns false
    // if the underlying BPF map update fails.
    bool register_urr(std::uint32_t teid,
                      std::uint64_t volume_threshold_octets,
                      std::uint64_t volume_quota_octets);

    // ADR-0050 Stage 5: real Session Modification support -- pushes a re-authorized Volume
    // Threshold/Volume Quota for an ALREADY-registered URR. Unlike register_urr (which always
    // writes a fresh UrrState, zeroing total_octets and both report latches -- correct for a new
    // Create), this does a read-modify-write: total_octets is preserved (it's the real cumulative
    // session usage, TS 29.244's own convention this project already follows -- see
    // nfs/smf/src/main.cpp's own Stage 5 comment on why the new threshold/quota are computed
    // relative to it rather than a fresh baseline) and only the two one-shot report latches are
    // reset to 0, so a future crossing of the newly-provisioned (necessarily higher) values can
    // fire again. Returns false if no URR is currently registered for this TEID, or the underlying
    // BPF map operation fails.
    bool update_urr_thresholds(std::uint32_t teid,
                               std::uint64_t new_volume_threshold_octets,
                               std::uint64_t new_volume_quota_octets);

private:
    Datapath();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace upf
