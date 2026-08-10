#include "datapath.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <fcntl.h>
#include <linux/if.h>       // struct ifreq, IFNAMSIZ
#include <linux/if_link.h>  // XDP_FLAGS_SKB_MODE
#include <linux/if_tun.h>   // IFF_TUN/IFF_NO_PI, TUNSETIFF
#include <sys/capability.h> // libcap -- CAP_NET_ADMIN and cap_set_flag/cap_set_proc
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <unistd.h>

// Deliberately NOT including <net/if.h>: glibc's <net/if.h> and the kernel's <linux/if.h> (which
// <linux/if_tun.h> above already needs for IFF_TUN/struct ifreq) both define struct ifreq/ifmap/
// ifconf and the IFF_* flags -- a well-known, unavoidable conflict when both are included in the
// same translation unit. if_nametoindex() is glibc's own function (from <net/if.h>) but has a
// stable, simple C signature -- declared directly here rather than pulling in the conflicting
// header just for one prototype.
extern "C" unsigned int if_nametoindex(const char* ifname);

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <thread>

#ifndef GTPU_BPF_OBJ_PATH
#error "GTPU_BPF_OBJ_PATH must be defined by CMake (see nfs/upf/CMakeLists.txt)"
#endif

namespace upf {

namespace {
constexpr const char* kN3Iface = "upf-n3";
constexpr const char* kN3PeerIface = "upf-n3-peer";
// kN3PeerIface only exists because no real gNB/N3 traffic reaches this UPF yet (see this
// project's disclosed NGAP PDU Session Resource Setup gap) -- it's a same-host stand-in so
// Stage 4 has *something* to inject real GTP-U test packets from/to. It is moved into its own
// network namespace below rather than left in the same namespace as kN3Iface: with both veth
// ends in one namespace, the kernel ends up with two `proto kernel scope link` routes for the
// exact same /30 (one per interface), which real live testing showed silently breaks ARP replies
// for the address on kN3Iface (request observed arriving on the wire via tcpdump, no reply ever
// generated -- not an XDP-program-logic issue, ruled out by reproducing the same failure with the
// XDP program fully detached). Moving kN3PeerIface into its own namespace is the standard fix for
// exactly this class of problem (the same reason container/veth setups always put at least one
// end in a separate namespace) and structurally removes the ambiguity rather than working around
// a symptom. See docs/DECISIONS.md ADR-0043's live-testing update.
constexpr const char* kN3PeerNetns = "upf-n3-peer-test-ns";
constexpr const char* kTunIface = "upf-tun0";

// Creates a real TUN device via the standard /dev/net/tun + TUNSETIFF ioctl -- not a shell-out,
// since this project needs the resulting fd directly for write() (a shell-out to `ip tuntap add`
// would still require a second, separate open() to get a usable fd). Returns -1 on failure.
int create_tun_device(const char* name) {
    const int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        spdlog::error("upf-datapath: open(/dev/net/tun) failed: {}", std::strerror(errno));
        return -1;
    }
    struct ifreq ifr {};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // no protocol-info prefix -- raw IP packets only
    std::strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        spdlog::error("upf-datapath: TUNSETIFF failed for {}: {}", name, std::strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

// Real interface/address setup delegated to iproute2 (`ip`) via a shell-out, rather than
// hand-written netlink code -- a disclosed, deliberate simplification (netlink socket/message
// construction is a substantial separate scope this stage doesn't need to justify a real
// datapath's correctness). Returns false on any non-zero exit status.
//
// Real bugs found and fixed via live testing (ADR-0043's own disclosed "not yet live-verified"
// gap, closed once privileges were actually granted and live testing became possible):
//
// Bug 1: `setcap ...+eip` on this binary puts CAP_NET_ADMIN in this process's own effective/
// permitted sets after exec, but a child process spawned via popen()/fork()+exec() does NOT
// automatically inherit capabilities beyond what's already on ITS OWN executable file --
// `/bin/ip` has no file capabilities of its own, so a plain popen("ip ...") call runs
// unprivileged. Confirmed for real: running the exact same `ip link add` command manually,
// unprivileged, produced the exact "RTNETLINK answers: Operation not permitted" this code then
// hit. The standard fix is Linux ambient capabilities (`man 7 capabilities`): a capability raised
// into the ambient set is inherited by child processes across exec().
//
// Bug 2 (found immediately after fixing bug 1): raising a capability into the ambient set
// requires it to already be in BOTH the process's permitted AND inheritable sets -- but per
// execve(2)'s real capability-transition rules, a new process's inheritable set is inherited from
// its PARENT's inheritable set, not populated from the file's inheritable bits the way permitted
// is. This process's parent (whatever shell launched it) has an empty inheritable set, so despite
// `getcap` showing `cap_net_admin,...=eip` on the binary FILE, this process's own inheritable set
// starts empty, and the ambient-raise failed with a second real, confirmed EPERM. The fix:
// explicitly move CAP_NET_ADMIN from this process's permitted set into its own inheritable set
// first (a process is always allowed to do this for a capability it already holds), via libcap's
// cap_set_flag/cap_set_proc, before attempting the ambient raise.
// Also raises CAP_SYS_ADMIN and CAP_DAC_OVERRIDE (not just CAP_NET_ADMIN): `ip netns add` --
// needed to isolate kN3PeerIface into its own namespace, see kN3PeerNetns's comment above --
// performs unshare(CLONE_NEWNET) under the hood (needs CAP_SYS_ADMIN, confirmed via a real EPERM
// hit when this only raised CAP_NET_ADMIN) AND creates a bind-mount target file under
// /run/netns/, which real live testing showed is root:root mode 0755 -- a plain DAC check
// unrelated to CAP_SYS_ADMIN, confirmed via a real EACCES ("Permission denied") once CAP_SYS_ADMIN
// alone was already in place.
bool ensure_datapath_caps_ambient() {
    static bool done = false;
    if (done) {
        return true;
    }
    cap_t caps = cap_get_proc();
    if (caps == nullptr) {
        spdlog::error("upf-datapath: cap_get_proc failed: {}", std::strerror(errno));
        return false;
    }
    cap_value_t cap_list[] = {CAP_NET_ADMIN, CAP_SYS_ADMIN, CAP_DAC_OVERRIDE};
    const bool set_ok =
        cap_set_flag(caps, CAP_INHERITABLE, 3, cap_list, CAP_SET) == 0 && cap_set_proc(caps) == 0;
    cap_free(caps);
    if (!set_ok) {
        spdlog::error("upf-datapath: failed to add CAP_NET_ADMIN/CAP_SYS_ADMIN/CAP_DAC_OVERRIDE "
                      "to the inheritable set: {}",
                      std::strerror(errno));
        return false;
    }
    for (const cap_value_t cap : cap_list) {
        if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) != 0) {
            spdlog::error("upf-datapath: prctl(PR_CAP_AMBIENT_RAISE, {}) failed: {}",
                          static_cast<int>(cap),
                          std::strerror(errno));
            return false;
        }
    }
    done = true;
    return true;
}

bool run_ip_command(const std::string& args) {
    if (!ensure_datapath_caps_ambient()) {
        spdlog::warn("upf-datapath: 'ip {}' will likely fail without ambient capabilities", args);
    }
    const std::string cmd = "ip " + args + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        spdlog::error("upf-datapath: popen('ip {}') failed: {}", args, std::strerror(errno));
        return false;
    }
    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        output += buf;
    }
    const int rc = pclose(pipe);
    if (rc != 0) {
        spdlog::error("upf-datapath: 'ip {}' failed (exit {}): {}", args, rc, output);
        return false;
    }
    return true;
}

// Mirrors nfs/upf/bpf/gtpu_decap.bpf.c's struct tpdu_record exactly -- the fixed-size ring
// buffer record format the BPF verifier's constant-size requirement on bpf_ringbuf_reserve forced
// (see that struct's own comment). `length` is the real T-PDU size; `data`'s bytes beyond that
// are stale/unused ring buffer memory, never written to the TUN device.
struct TpduRecord {
    std::uint16_t length;
    unsigned char data[1500];
} __attribute__((packed));

// Mirrors nfs/upf/bpf/gtpu_decap.bpf.c's struct urr_state exactly -- same field order/packing
// requirement as TpduRecord above, since this is written directly into the BPF map's memory via
// bpf_map_update_elem.
struct UrrState {
    std::uint64_t volume_threshold;
    std::uint64_t volume_quota;
    std::uint64_t total_octets;
    std::uint8_t threshold_reported;
    std::uint8_t quota_reported;
} __attribute__((packed));

// Mirrors nfs/upf/bpf/gtpu_decap.bpf.c's struct usage_report_event exactly.
struct UsageReportEventRecord {
    std::uint32_t teid;
    std::uint64_t total_octets;
    std::uint8_t quota_exhausted;
} __attribute__((packed));

int handle_tpdu_sample(void* ctx, void* data, std::size_t data_sz) {
    const int tun_fd = *static_cast<int*>(ctx);
    if (data_sz < sizeof(TpduRecord)) {
        spdlog::warn("upf-datapath: ring buffer sample too small ({} bytes), ignoring", data_sz);
        return 0;
    }
    const auto* rec = static_cast<const TpduRecord*>(data);
    const std::size_t tpdu_len = rec->length;
    if (tpdu_len > sizeof(rec->data)) {
        spdlog::warn("upf-datapath: ring buffer record claims implausible length {}, ignoring",
                     tpdu_len);
        return 0;
    }
    const ssize_t written = write(tun_fd, rec->data, tpdu_len);
    if (written < 0 || static_cast<std::size_t>(written) != tpdu_len) {
        spdlog::warn("upf-datapath: short/failed write of decapsulated T-PDU ({} bytes) to {}: {}",
                     tpdu_len,
                     kTunIface,
                     std::strerror(errno));
    } else {
        spdlog::info(
            "upf-datapath: delivered decapsulated T-PDU ({} bytes) to {}", tpdu_len, kTunIface);
    }
    return 0;
}

} // namespace

struct Datapath::Impl {
    bpf_object* obj = nullptr;
    ring_buffer* rb = nullptr;
    int tun_fd = -1;
    int teid_map_fd = -1;
    int urr_map_fd = -1;
    int n3_ifindex = 0;
    std::thread poll_thread;
    bool stop_polling = false;
    UsageReportHandler on_usage_report;

    // A static member function (not a free function like handle_tpdu_sample above) specifically
    // because it needs to name the complete Impl type to reach on_usage_report -- Impl is a
    // private nested type of Datapath, so only Datapath's own members (this one included) can
    // refer to it by name at all.
    static int handle_usage_report_sample(void* ctx, void* data, std::size_t data_sz) {
        auto* impl = static_cast<Impl*>(ctx);
        if (data_sz < sizeof(UsageReportEventRecord)) {
            spdlog::warn("upf-datapath: usage report ring buffer sample too small ({} bytes), "
                         "ignoring",
                         data_sz);
            return 0;
        }
        const auto* rec = static_cast<const UsageReportEventRecord*>(data);
        if (impl->on_usage_report) {
            impl->on_usage_report(rec->teid, rec->total_octets, rec->quota_exhausted != 0);
        }
        return 0;
    }

    ~Impl() {
        stop_polling = true;
        if (poll_thread.joinable()) {
            // ring_buffer__poll below runs with a bounded timeout specifically so this join
            // doesn't block indefinitely on shutdown -- moot today (UPF never terminates, see
            // main.cpp's own disclosed simplification) but correct regardless.
            poll_thread.join();
        }
        if (rb != nullptr) {
            ring_buffer__free(rb);
        }
        if (n3_ifindex != 0) {
            bpf_xdp_attach(n3_ifindex, -1, XDP_FLAGS_SKB_MODE, nullptr); // detach
        }
        if (obj != nullptr) {
            bpf_object__close(obj);
        }
        if (tun_fd >= 0) {
            close(tun_fd);
        }
        run_ip_command(std::string("link del ") + kN3Iface); // removes both veth peers at once
        run_ip_command(std::string("netns del ") + kN3PeerNetns);
    }
};

Datapath::Datapath() : impl_(std::make_unique<Impl>()) {}
Datapath::Datapath(Datapath&&) noexcept = default;
Datapath& Datapath::operator=(Datapath&&) noexcept = default;
Datapath::~Datapath() = default;

std::optional<Datapath> Datapath::create(UsageReportHandler on_usage_report) {
    Datapath dp;
    dp.impl_->on_usage_report = std::move(on_usage_report);

    if (!run_ip_command(std::string("link add ") + kN3Iface + " type veth peer name " +
                        kN3PeerIface)) {
        return std::nullopt;
    }
    // kN3PeerIface moves into its own namespace before anything else touches it -- see
    // kN3PeerNetns's comment for why (avoids two same-namespace /30 routes breaking ARP).
    // `ip netns add` is idempotent-adjacent here in that a leftover namespace from an
    // ungracefully-killed prior run (this NF has no signal handling, see main.cpp's own disclosed
    // simplification) fails with "File exists" -- non-fatal, the subsequent `link set netns`
    // still works against the pre-existing namespace either way.
    run_ip_command(std::string("netns add ") + kN3PeerNetns);
    if (!run_ip_command(std::string("link set ") + kN3PeerIface + " netns " + kN3PeerNetns)) {
        return std::nullopt;
    }
    if (!run_ip_command(std::string("link set ") + kN3Iface + " up") ||
        !run_ip_command(std::string("-n ") + kN3PeerNetns + " link set " + kN3PeerIface + " up")) {
        return std::nullopt;
    }
    // Addresses only matter for a human/tcpdump inspecting the veth pair directly -- the XDP
    // program itself matches on GTP-U/TEID content, not on these addresses.
    run_ip_command(std::string("addr add 10.99.0.1/30 dev ") + kN3Iface);
    run_ip_command(std::string("-n ") + kN3PeerNetns + " addr add 10.99.0.2/30 dev " +
                   kN3PeerIface);

    dp.impl_->tun_fd = create_tun_device(kTunIface);
    if (dp.impl_->tun_fd < 0) {
        return std::nullopt;
    }
    if (!run_ip_command(std::string("link set ") + kTunIface + " up")) {
        return std::nullopt;
    }

    dp.impl_->obj = bpf_object__open_file(GTPU_BPF_OBJ_PATH, nullptr);
    if (dp.impl_->obj == nullptr) {
        spdlog::error("upf-datapath: bpf_object__open_file({}) failed", GTPU_BPF_OBJ_PATH);
        return std::nullopt;
    }
    if (bpf_object__load(dp.impl_->obj) != 0) {
        spdlog::error("upf-datapath: bpf_object__load failed -- likely missing CAP_BPF, see "
                      "nfs/upf/src/datapath.hpp's own comment");
        return std::nullopt;
    }

    bpf_program* prog = bpf_object__find_program_by_name(dp.impl_->obj, "gtpu_decap_prog");
    if (prog == nullptr) {
        spdlog::error("upf-datapath: gtpu_decap_prog not found in {}", GTPU_BPF_OBJ_PATH);
        return std::nullopt;
    }
    dp.impl_->n3_ifindex = static_cast<int>(if_nametoindex(kN3Iface));
    if (dp.impl_->n3_ifindex == 0) {
        spdlog::error("upf-datapath: if_nametoindex({}) failed", kN3Iface);
        return std::nullopt;
    }
    if (bpf_xdp_attach(dp.impl_->n3_ifindex, bpf_program__fd(prog), XDP_FLAGS_SKB_MODE, nullptr) !=
        0) {
        spdlog::error("upf-datapath: bpf_xdp_attach to {} failed -- likely missing CAP_NET_ADMIN",
                      kN3Iface);
        dp.impl_->n3_ifindex = 0; // nothing to detach in the destructor
        return std::nullopt;
    }

    bpf_map* teid_map = bpf_object__find_map_by_name(dp.impl_->obj, "teid_map");
    bpf_map* ringbuf_map = bpf_object__find_map_by_name(dp.impl_->obj, "tpdu_ringbuf");
    bpf_map* urr_map = bpf_object__find_map_by_name(dp.impl_->obj, "urr_map");
    bpf_map* usage_ringbuf_map =
        bpf_object__find_map_by_name(dp.impl_->obj, "usage_report_ringbuf");
    if (teid_map == nullptr || ringbuf_map == nullptr || urr_map == nullptr ||
        usage_ringbuf_map == nullptr) {
        spdlog::error("upf-datapath: expected BPF maps not found in {}", GTPU_BPF_OBJ_PATH);
        return std::nullopt;
    }
    dp.impl_->teid_map_fd = bpf_map__fd(teid_map);
    dp.impl_->urr_map_fd = bpf_map__fd(urr_map);

    dp.impl_->rb =
        ring_buffer__new(bpf_map__fd(ringbuf_map), handle_tpdu_sample, &dp.impl_->tun_fd, nullptr);
    if (dp.impl_->rb == nullptr) {
        spdlog::error("upf-datapath: ring_buffer__new failed");
        return std::nullopt;
    }
    // Second BPF map added to the *same* ring_buffer manager/poll loop (not a second thread) --
    // libbpf's ring_buffer__poll() services every map added via ring_buffer__add() together.
    if (ring_buffer__add(dp.impl_->rb,
                         bpf_map__fd(usage_ringbuf_map),
                         Impl::handle_usage_report_sample,
                         dp.impl_.get()) != 0) {
        spdlog::error("upf-datapath: ring_buffer__add(usage_report_ringbuf) failed");
        return std::nullopt;
    }

    Impl* impl_ptr = dp.impl_.get();
    dp.impl_->poll_thread = std::thread([impl_ptr]() {
        while (!impl_ptr->stop_polling) {
            // 1s timeout: bounds how long a future shutdown would need to wait for this thread
            // to notice stop_polling -- moot today (UPF never terminates) but a correct choice
            // regardless of that.
            const int n = ring_buffer__poll(impl_ptr->rb, 1000);
            if (n < 0) {
                spdlog::warn("upf-datapath: ring_buffer__poll error {}", n);
            }
        }
    });

    spdlog::info("upf-datapath: real eBPF/XDP GTP-U decapsulation active on {} (generic/SKB "
                 "mode), delivering decapsulated T-PDUs to {}",
                 kN3Iface,
                 kTunIface);
    return dp;
}

bool Datapath::register_teid(std::uint32_t teid) {
    constexpr std::uint32_t kDummyValue = 1;
    if (bpf_map_update_elem(impl_->teid_map_fd, &teid, &kDummyValue, BPF_ANY) != 0) {
        spdlog::warn("upf-datapath: failed to register TEID {:#x} with the XDP program", teid);
        return false;
    }
    return true;
}

bool Datapath::register_urr(std::uint32_t teid,
                            std::uint64_t volume_threshold_octets,
                            std::uint64_t volume_quota_octets) {
    UrrState state{};
    state.volume_threshold = volume_threshold_octets;
    state.volume_quota = volume_quota_octets;
    state.total_octets = 0;
    state.threshold_reported = 0;
    state.quota_reported = 0;
    if (bpf_map_update_elem(impl_->urr_map_fd, &teid, &state, BPF_ANY) != 0) {
        spdlog::warn("upf-datapath: failed to register URR for TEID {:#x} with the XDP program",
                     teid);
        return false;
    }
    return true;
}

bool Datapath::update_urr_thresholds(std::uint32_t teid,
                                     std::uint64_t new_volume_threshold_octets,
                                     std::uint64_t new_volume_quota_octets) {
    UrrState state{};
    if (bpf_map_lookup_elem(impl_->urr_map_fd, &teid, &state) != 0) {
        spdlog::warn("upf-datapath: no URR registered for TEID {:#x}, cannot update thresholds",
                     teid);
        return false;
    }
    state.volume_threshold = new_volume_threshold_octets;
    state.volume_quota = new_volume_quota_octets;
    // total_octets deliberately left as read -- see this method's own header comment. Both latches
    // reset so the newly-provisioned (necessarily higher) values can be crossed and reported again.
    state.threshold_reported = 0;
    state.quota_reported = 0;
    if (bpf_map_update_elem(impl_->urr_map_fd, &teid, &state, BPF_EXIST) != 0) {
        spdlog::warn("upf-datapath: failed to update URR thresholds for TEID {:#x}", teid);
        return false;
    }
    return true;
}

} // namespace upf
