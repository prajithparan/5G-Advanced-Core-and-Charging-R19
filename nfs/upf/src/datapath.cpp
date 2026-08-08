#include "datapath.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <fcntl.h>
#include <linux/if.h>       // struct ifreq, IFNAMSIZ
#include <linux/if_link.h>  // XDP_FLAGS_SKB_MODE
#include <linux/if_tun.h>   // IFF_TUN/IFF_NO_PI, TUNSETIFF
#include <sys/ioctl.h>
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
bool run_ip_command(const std::string& args) {
    const std::string cmd = "ip " + args + " >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        spdlog::error("upf-datapath: 'ip {}' failed (exit {})", args, rc);
        return false;
    }
    return true;
}

int handle_tpdu_sample(void* ctx, void* data, std::size_t data_sz) {
    const int tun_fd = *static_cast<int*>(ctx);
    const ssize_t written = write(tun_fd, data, data_sz);
    if (written < 0 || static_cast<std::size_t>(written) != data_sz) {
        spdlog::warn("upf-datapath: short/failed write of decapsulated T-PDU ({} bytes) to {}: {}",
                    data_sz, kTunIface, std::strerror(errno));
    } else {
        spdlog::info("upf-datapath: delivered decapsulated T-PDU ({} bytes) to {}", data_sz,
                    kTunIface);
    }
    return 0;
}

} // namespace

struct Datapath::Impl {
    bpf_object* obj = nullptr;
    ring_buffer* rb = nullptr;
    int tun_fd = -1;
    int teid_map_fd = -1;
    int n3_ifindex = 0;
    std::thread poll_thread;
    bool stop_polling = false;

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
    }
};

Datapath::Datapath() : impl_(std::make_unique<Impl>()) {}
Datapath::Datapath(Datapath&&) noexcept = default;
Datapath& Datapath::operator=(Datapath&&) noexcept = default;
Datapath::~Datapath() = default;

std::optional<Datapath> Datapath::create() {
    Datapath dp;

    if (!run_ip_command(std::string("link add ") + kN3Iface + " type veth peer name " +
                        kN3PeerIface)) {
        return std::nullopt;
    }
    if (!run_ip_command(std::string("link set ") + kN3Iface + " up") ||
        !run_ip_command(std::string("link set ") + kN3PeerIface + " up")) {
        return std::nullopt;
    }
    // Addresses only matter for a human/tcpdump inspecting the veth pair directly -- the XDP
    // program itself matches on GTP-U/TEID content, not on these addresses.
    run_ip_command(std::string("addr add 10.99.0.1/30 dev ") + kN3Iface);
    run_ip_command(std::string("addr add 10.99.0.2/30 dev ") + kN3PeerIface);

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
    if (teid_map == nullptr || ringbuf_map == nullptr) {
        spdlog::error("upf-datapath: expected BPF maps not found in {}", GTPU_BPF_OBJ_PATH);
        return std::nullopt;
    }
    dp.impl_->teid_map_fd = bpf_map__fd(teid_map);

    dp.impl_->rb = ring_buffer__new(bpf_map__fd(ringbuf_map), handle_tpdu_sample,
                                    &dp.impl_->tun_fd, nullptr);
    if (dp.impl_->rb == nullptr) {
        spdlog::error("upf-datapath: ring_buffer__new failed");
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
                kN3Iface, kTunIface);
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

} // namespace upf
