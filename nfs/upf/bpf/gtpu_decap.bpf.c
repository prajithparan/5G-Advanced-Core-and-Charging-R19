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
    // The verifier needs a statically-provable upper bound on a dynamic reserve/copy size; masking
    // with a power-of-two-minus-one is the standard idiom for this (numerically a no-op here since
    // tpdu_len is already known <= 1500 < 2048, but it gives the verifier's range tracker a bound
    // it can prove at compile time, which the raw runtime-computed value alone does not).
    tpdu_len &= 2047;

    void *slot = bpf_ringbuf_reserve(&tpdu_ringbuf, tpdu_len, 0);
    if (!slot) {
        return XDP_DROP; // ring buffer full -- drop rather than block, same as a real datapath would
    }
    if (bpf_probe_read_kernel(slot, (__u32)tpdu_len, tpdu_start)) {
        bpf_ringbuf_discard(slot, 0);
        return XDP_DROP;
    }
    bpf_ringbuf_submit(slot, 0);
    return XDP_DROP; // consumed -- do not let the (still-encapsulated) frame reach the normal stack
}

char _license[] SEC("license") = "GPL";
