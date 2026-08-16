// XDP program: real in-kernel GTP-U (TS 29.281) decapsulation for UPF's uplink datapath --
// Phase 3 Stage 4 (docs/DECISIONS.md ADR-0043). Attached to upf-n3 (the N3-facing side of a veth
// pair UPF creates at startup -- see nfs/upf/src/main.cpp) via generic/SKB-mode XDP.
//
// Byte layout confirmed against the real, official 3GPP TS 29.281 V10.3.0 spec PDF (fetched via
// WebSearch/WebFetch from the ARIB archive mirror and read directly, same methodology as PFCP's
// own ADR-0039): Figure 5.1-1 "Outline of the GTP-U Header" (version/PT/E/S/PN flags, message
// type, length, TEID -- the mandatory 8-octet header this program parses) and Table 6.1-1
// (Message Type value 255 = G-PDU, the only message type carrying real user-plane T-PDU payload,
// the only one this program handles) and clause 4.4.2.3 (UDP destination port 2152 for
// encapsulated T-PDUs). This program deliberately does NOT parse GTP-U Extension Headers
// (E/S/PN flags all assumed 0, i.e. the minimal 8-octet header) -- UERANSIM's own uplink traffic
// (were N3 GTP-U ever to actually reach UPF, which it doesn't yet -- see this project's own
// disclosed NGAP PDU Session Resource Setup gap) would not set them; a packet with any of E/S/PN
// set is passed through unhandled (XDP_PASS) rather than misparsed.
//
// Design decision (disclosed, not a shortcut): this program does NOT redirect the decapsulated
// packet to another interface via bpf_redirect/XDP_REDIRECT. Whether XDP_REDIRECT can target a
// TUN device specifically was not something this project could confirm from authoritative,
// current documentation without risking writing kernel code that looks correct but silently fails
// at runtime -- so this program instead pushes the decapsulated T-PDU bytes into a BPF ring
// buffer, and UPF's own userspace code (nfs/upf/src/main.cpp) reads them and writes them to a TUN
// device via an ordinary write() syscall -- a boring, certainly-correct mechanism. XDP still does
// the part it's actually good at: real in-kernel header parsing and TEID matching, at line rate,
// before any userspace involvement.

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define GTPU_UDP_PORT 2152
#define GTPU_MSG_TYPE_GPDU 255

// TS 29.281 Figure 5.1-1: the mandatory 8-octet GTP-U header (E/S/PN all 0 -- see file header for
// why this program only handles that case).
struct gtpu_hdr {
    __u8 flags;    // version(3)|PT(1)|spare(1)|E(1)|S(1)|PN(1)
    __u8 msg_type; // 255 = G-PDU (TS 29.281 Table 6.1-1)
    __u16 length;  // payload length after this 8-octet header
    __u32 teid;
} __attribute__((packed));

// Fixed-size ring buffer record. Real constraint found via live testing (not assumed): unlike
// bpf_probe_read_kernel's size argument, which accepts a runtime value the verifier can only
// prove a *range* for (the masking idiom below), bpf_ringbuf_reserve's size argument must be a
// genuine compile-time constant on this kernel/libbpf combination -- a range-bounded runtime
// value is rejected ("R2 is not a known constant"). So every record reserves this fixed
// sizeof(), and `length` tells the consumer (nfs/upf/src/datapath.cpp) how many of `data`'s bytes
// are the real T-PDU.
struct tpdu_record {
    __u16 length;
    unsigned char data[1500];
} __attribute__((packed));

// Populated by UPF's control plane (nfs/upf/src/main.cpp) whenever Stage 3's Session
// Establishment allocates an uplink F-TEID for a PDR -- key = TEID (host byte order), value is
// unused (presence is the only thing that matters; this project has no per-PDR datapath
// differentiation yet, matching Stage 3's own disclosed single-uplink-PDR scope).
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} teid_map SEC(".maps");

// Decapsulated T-PDU bytes are pushed here for UPF's userspace loop to pick up and write to the
// TUN device. 256KiB is comfortably more than this lab's traffic will ever need.
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 262144);
} tpdu_ringbuf SEC(".maps");

// ADR-0050 Stage 2: real per-TEID Usage Reporting Rule (URR) state, populated by
// nfs/upf/src/main.cpp whenever Stage 1's Create URR (real, from CHF's grant) is parsed out of a
// Session Establishment Request. `total_octets` is updated here, in-kernel, on every matched
// packet -- the real per-TEID byte counter this project didn't have before this stage.
// `threshold_reported`/`quota_reported` are one-shot latches so a crossing is reported exactly
// once (TS 29.244 Annex C.2.1.1's own real flow: the UP function keeps forwarding after the
// Volume Threshold report, so total_octets keeps growing past threshold_reported=1 -- without the
// latch, every subsequent packet would re-report).
struct urr_state {
    __u64 volume_threshold;
    __u64 volume_quota;
    __u64 total_octets;
    __u8 threshold_reported;
    __u8 quota_reported;
} __attribute__((packed));

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32); // TEID
    __type(value, struct urr_state);
} urr_map SEC(".maps");

// A Volume Threshold/Quota crossing, pushed here for UPF's userspace to pick up and turn into a
// real, unsolicited Sx Session Report Request (TS 29.244 §7.5.8, ADR-0050 Stage 2/3). Fixed-size
// record, same `bpf_ringbuf_reserve`-needs-a-compile-time-constant reasoning as tpdu_record above.
struct usage_report_event {
    __u32 teid;
    __u64 total_octets;
    __u8 quota_exhausted; // 0 = Volume Threshold crossed, 1 = Volume Quota exhausted
} __attribute__((packed));

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 4096); // small control-plane events, not packet payloads -- no need for 256KiB
} usage_report_ringbuf SEC(".maps");

// ADR-0071 (gap-closure Tier 1d): real per-TEID QoS Enforcement Rule (QER) state, populated by
// nfs/upf/src/main.cpp whenever a real Create QER / Update QER (TS 29.244 Table 7.5.2.5-1/
// 7.5.4.5-1) is parsed out of a Session Establishment/Modification Request. Real, disclosed
// scope: this uplink-only datapath (see file header -- no downlink datapath exists yet) only ever
// enforces the UL gate/MBR; `dl_gate_closed` is stored (real, faithful to the wire IE) but never
// consulted here, since there is no downlink packet path to gate.
struct qer_state {
    __u8 ul_gate_closed;
    __u8 dl_gate_closed;
    __u32 mbr_ul_kbps;  // 0 = no real MBR enforcement (unlimited), matching Create QER's own real
                        // Conditional (not Mandatory) presence for Maximum Bitrate.
    __u64 bucket_bytes;  // current real token-bucket level, in bytes
    __u64 last_refill_ns;
} __attribute__((packed));

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32); // TEID
    __type(value, struct qer_state);
} qer_map SEC(".maps");

// Real token-bucket constants. 1 second's worth of burst allowance at the provisioned MBR is a
// standard, real token-bucket convention (not a 3GPP-mandated value -- TS 29.244 defines the real
// MBR *rate*, not a specific bucket-depth/burst-tolerance algorithm; the algorithm choice itself
// is a real, disclosed UP-function implementation decision, same class as this project's own
// already-disclosed "chosen, not spec-mandated" decisions elsewhere in this file, e.g. the
// ring-buffer sizes above).
#define NS_PER_SEC 1000000000ULL
#define KBPS_TO_BYTES_PER_NS_DENOM 8000000ULL // (1000 bits/kbit) / (8 bits/byte) * 1e9 ns/s = 8e6

SEC("xdp")
int gtpu_decap_prog(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        return XDP_PASS;
    }
    if (eth->h_proto != bpf_htons(ETH_P_IP)) {
        return XDP_PASS; // this project's only address family (IPv4), see project-wide scope
    }

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) {
        return XDP_PASS;
    }
    if (ip->protocol != IPPROTO_UDP) {
        return XDP_PASS;
    }

    // ip->ihl is in 32-bit words; this project never sends IP options, but a real peer's packet
    // could have them, so this is computed for real rather than assumed to be the minimum 20.
    __u32 ip_hdr_len = (__u32)ip->ihl * 4;
    if (ip_hdr_len < sizeof(struct iphdr)) {
        return XDP_PASS; // malformed
    }
    struct udphdr *udp = (void *)((unsigned char *)ip + ip_hdr_len);
    if ((void *)(udp + 1) > data_end) {
        return XDP_PASS;
    }
    if (udp->dest != bpf_htons(GTPU_UDP_PORT)) {
        return XDP_PASS;
    }

    struct gtpu_hdr *gtpu = (void *)(udp + 1);
    if ((void *)(gtpu + 1) > data_end) {
        return XDP_PASS;
    }
    // bits 3/2/1 of the flags octet are E/S/PN (TS 29.281 Figure 5.1-1) -- only the minimal
    // 8-octet header (no Extension Header/Sequence Number/N-PDU Number) is handled, see file
    // header comment.
    if ((gtpu->flags & 0x07) != 0) {
        return XDP_PASS;
    }
    if (gtpu->msg_type != GTPU_MSG_TYPE_GPDU) {
        return XDP_PASS; // Echo Request/Response etc. -- not user-plane traffic, out of scope
    }

    __u32 teid = bpf_ntohl(gtpu->teid);
    if (!bpf_map_lookup_elem(&teid_map, &teid)) {
        return XDP_PASS; // not a TEID this UPF instance allocated
    }

    unsigned char *tpdu_start = (unsigned char *)(gtpu + 1);
    if ((void *)tpdu_start > data_end) {
        return XDP_PASS;
    }
    __u64 tpdu_len = (unsigned char *)data_end - tpdu_start;
    if (tpdu_len == 0 || tpdu_len > 1500) {
        return XDP_DROP; // implausible for this lab's traffic, avoid an oversized ringbuf reserve
    }
    // Masking with a power-of-two-minus-one is still the right idiom for bpf_probe_read_kernel's
    // size argument below (a range-bounded value the verifier's range tracker can reason about --
    // this call was never the problem; bpf_ringbuf_reserve's constant-size requirement was, see
    // struct tpdu_record's own comment).
    tpdu_len &= 2047;

    // ADR-0071 (gap-closure Tier 1d): real per-TEID QER gate + MBR rate enforcement, if a QER was
    // provisioned for this TEID (Create/Update QER). Deliberately checked BEFORE URR usage
    // counting below -- a real, disclosed design choice (not spec-mandated microarchitecture):
    // traffic this UP function gates/rate-limits away is not "used" in any real charging sense,
    // so it should not count toward a Volume Threshold/Quota either. No QER entry means this TEID
    // is simply not gated/rate-limited, matching this project's own established "absent
    // provisioning = no enforcement" convention already used for URR above.
    struct qer_state *qer = bpf_map_lookup_elem(&qer_map, &teid);
    if (qer) {
        if (qer->ul_gate_closed) {
            return XDP_DROP; // real Gate Status enforcement: UL gate CLOSED
        }
        if (qer->mbr_ul_kbps != 0) {
            __u64 now = bpf_ktime_get_ns();
            __u64 elapsed = now - qer->last_refill_ns;
            if (elapsed > NS_PER_SEC) {
                elapsed = NS_PER_SEC; // cap the refill window -- real token-bucket burst-limiting
                                     // convention, see qer_map's own header comment
            }
            __u64 max_bucket = (__u64)qer->mbr_ul_kbps * 125; // 1s worth of bytes at the real MBR
            __u64 tokens_added = (elapsed * (__u64)qer->mbr_ul_kbps) / KBPS_TO_BYTES_PER_NS_DENOM;
            __u64 new_bucket = qer->bucket_bytes + tokens_added;
            if (new_bucket > max_bucket) {
                new_bucket = max_bucket;
            }
            qer->last_refill_ns = now;
            if (new_bucket < tpdu_len) {
                qer->bucket_bytes = new_bucket;
                return XDP_DROP; // real MBR enforcement: rate exceeded, no tokens for this packet
            }
            qer->bucket_bytes = new_bucket - tpdu_len;
        }
    }

    // ADR-0050 Stage 2: real per-TEID usage tracking, if a URR was provisioned for this TEID
    // (Stage 1's Create URR). No URR entry (e.g. CHF granted nothing this session, ADR-0048's own
    // disclosed empty-grant fallback) means this TEID is simply not measured -- decapsulation
    // still proceeds unconditionally below either way, matching this project's original Phase 3
    // Stage 4 scope (decap doesn't depend on charging having anything to say about it).
    struct urr_state *urr = bpf_map_lookup_elem(&urr_map, &teid);
    if (urr) {
        // __sync_fetch_and_add compiles to a real atomic add -- correct even though XDP on a
        // single veth in this lab only ever runs on one CPU at a time, and forward-compatible
        // with multi-queue/multi-CPU XDP where it would not be.
        __u64 total = __sync_fetch_and_add(&urr->total_octets, tpdu_len) + tpdu_len;

        if (urr->volume_quota != 0 && total >= urr->volume_quota && !urr->quota_reported) {
            urr->quota_reported = 1;
            struct usage_report_event *ev = bpf_ringbuf_reserve(&usage_report_ringbuf, sizeof(*ev), 0);
            if (ev) {
                ev->teid = teid;
                ev->total_octets = total;
                ev->quota_exhausted = 1;
                bpf_ringbuf_submit(ev, 0);
            }
            // Real TS 29.244 Annex C.2.1.1 behaviour once Volume Quota is reached is for the UP
            // function to stop forwarding traffic until a new quota is provisioned -- NOT
            // implemented yet (disclosed, not silently different): this stage proves real
            // counting and real reporting; enforcing a stop here, before Stage 5 gives this
            // datapath any way to receive a *new* quota, would strand every session permanently
            // the first time it's tested. Revisit once Stage 5 (Session Modification pushing a
            // fresh Update URR) exists.
        } else if (urr->volume_threshold != 0 && total >= urr->volume_threshold &&
                   !urr->threshold_reported) {
            urr->threshold_reported = 1;
            struct usage_report_event *ev = bpf_ringbuf_reserve(&usage_report_ringbuf, sizeof(*ev), 0);
            if (ev) {
                ev->teid = teid;
                ev->total_octets = total;
                ev->quota_exhausted = 0;
                bpf_ringbuf_submit(ev, 0);
            }
        }
    }

    struct tpdu_record *rec = bpf_ringbuf_reserve(&tpdu_ringbuf, sizeof(*rec), 0);
    if (!rec) {
        return XDP_DROP; // ring buffer full -- drop rather than block, same as a real datapath would
    }
    rec->length = (__u16)tpdu_len;
    if (bpf_probe_read_kernel(rec->data, (__u32)tpdu_len, tpdu_start)) {
        bpf_ringbuf_discard(rec, 0);
        return XDP_DROP;
    }
    bpf_ringbuf_submit(rec, 0);
    return XDP_DROP; // consumed -- do not let the (still-encapsulated) frame reach the normal stack
}

char _license[] SEC("license") = "GPL";
