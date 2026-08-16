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

    // ADR-0071 (gap-closure Tier 1d): provisions real per-TEID QoS Enforcement Rule state (Gate
    // Status + Maximum Bitrate) the XDP program gates/rate-limits uplink traffic against --
    // called whenever Session Establishment/Modification (TS 29.244 Create/Update QER) parses one
    // out of a real PFCP request. mbr_ul_kbps=0 means no real MBR enforcement (matching Maximum
    // Bitrate's own real Conditional, not Mandatory, presence in Create QER). Always writes a
    // fresh token-bucket state (bucket_bytes=0, refill starts now) -- correct for both a new
    // Create and a real Update (TS 29.244 doesn't define bucket-state carry-over semantics across
    // an MBR change, unlike URR's own real "total_octets persists" convention -- a fresh bucket on
    // update is the conservative, real, disclosed choice, not spec-mandated either way). Returns
    // false if the underlying BPF map update fails.
    bool register_qer(std::uint32_t teid,
                      bool ul_gate_closed,
                      bool dl_gate_closed,
                      std::uint32_t mbr_ul_kbps);

    // ADR-0071: real Update QER (TS 29.244 Table 7.5.4.5-1) -- unlike register_qer (used for a
    // real Create QER, where Gate Status is Mandatory so always fully specified), a real Update
    // QER's Gate Status and Maximum Bitrate are both Conditional: present only if that field is
    // actually changing. Blindly re-running register_qer on Update would silently reopen a closed
    // gate (or drop an MBR cap) whenever an update changed some OTHER QER field but omitted
    // Gate Status/MBR -- a real correctness bug, not a cosmetic one. This does a real
    // read-modify-write instead (same "preserve what wasn't provided" discipline
    // update_urr_thresholds already established for Update URR's own total_octets field):
    // std::nullopt for either optional means "leave this field as it currently is". Also -- unlike
    // register_qer's own disclosed "always fresh bucket" choice -- preserves the running token
    // bucket (bucket_bytes/last_refill_ns) untouched, since an Update is modifying an existing,
    // already-running QER, not creating a new one; there is no real spec text mandating either
    // choice, this is this project's own conservative pick, disclosed here rather than silently
    // matched to register_qer's different (Create-appropriate) behaviour. Returns false if no QER
    // is currently registered for this TEID (a real Update QER always targets an existing one), or
    // the underlying BPF map operation fails.
    bool update_qer(std::uint32_t teid,
                    std::optional<bool> ul_gate_closed,
                    std::optional<bool> dl_gate_closed,
                    std::optional<std::uint32_t> mbr_ul_kbps);

    // ADR-0071: real Remove QER (TS 29.244 Table 7.5.4.9-1) -- removes the QER state entirely, so
    // this TEID's traffic is neither gated nor rate-limited afterward (same "absent provisioning
    // = no enforcement" convention register_qer's own header comment already establishes). Returns
    // false if no QER was registered for this TEID.
    bool remove_qer(std::uint32_t teid);

    // ADR-0071: real Session Deletion (TS 29.244 §7.5.6/§7.5.7) support -- removes ALL per-TEID
    // state (teid_map, urr_map, qer_map) for a real, full session teardown. Returns the real
    // cumulative total_octets the URR (if any) had counted at deletion time, for a real Usage
    // Report in the Session Deletion Response (Table 7.5.7.1-1's own real Conditional field) --
    // std::nullopt if no URR was registered for this TEID (not an error).
    std::optional<std::uint64_t> remove_teid(std::uint32_t teid);

private:
    Datapath();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace upf
