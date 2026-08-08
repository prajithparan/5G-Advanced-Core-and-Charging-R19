#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Thin RAII wrapper around a kernel one-to-one style SCTP socket (AF_INET, IPPROTO_SCTP, via the
// system libsctp-dev headers/library), used for AMF's N2/NGAP transport (TS 38.412 specifies port
// 38412; TS 38.413 is the NGAP protocol carried over it). Boost.Asio, already this project's
// event-loop library for SBI/HTTP2, has no native SCTP support -- this class is meant to be used
// from a dedicated thread doing blocking I/O instead, the same "blocking transport gets its own
// thread" discipline docs/DECISIONS.md ADR-0006 already established for run_nrf_lifecycle. See
// ADR-0030.
//
// One-to-one style (SOCK_STREAM, not SOCK_SEQPACKET/one-to-many): accept() returns a fully
// connected per-association socket directly, the same accept() model as a TCP listening socket --
// no SCTP_ASSOC_CHANGE notification handling needed, unlike a one-to-many socket multiplexing
// several associations over one fd.
//
// PPID (Payload Protocol Identifier) for NGAP is 60 -- confirmed against UERANSIM's own real,
// already-building implementation (simulators/ransim/vendor/UERANSIM/src/lib/sctp/types.hpp),
// not from memory or general knowledge.
//
// Disclosed simplification: every message in this build is sent/received on SCTP stream 0. Real
// NGAP deployments reserve stream 0 for non-UE-associated signaling (e.g. NG Setup) and assign
// dynamic per-UE streams for UE-associated signaling; this build's scope (sequential procedures
// for one UE at a time) doesn't need that yet. `send`'s `stream` parameter exists so a future turn
// can add real per-association stream assignment without changing this class's shape.

namespace ngap_core {

constexpr std::uint32_t kNgapPpid = 60;

class SctpSocket {
public:
    // Throws std::runtime_error on any socket()/bind()/listen() failure.
    SctpSocket();
    ~SctpSocket();

    SctpSocket(const SctpSocket&) = delete;
    SctpSocket& operator=(const SctpSocket&) = delete;
    SctpSocket(SctpSocket&& other) noexcept;
    SctpSocket& operator=(SctpSocket&& other) noexcept;

    void bind_and_listen(const std::string& address, std::uint16_t port, int backlog = 8);

    // Blocks until a peer (e.g. a real gNB) establishes an association; returns a new connected
    // socket for it. Throws std::runtime_error on failure.
    SctpSocket accept();

    // One send() call = one SCTP message = one NGAP PDU (SCTP preserves message boundaries,
    // unlike a TCP stream -- no length-prefixing needed). Throws std::runtime_error on failure.
    void send(const std::vector<std::uint8_t>& data, std::uint16_t stream = 0);

    // Blocking receive of one SCTP message. Returns an empty vector on graceful peer shutdown
    // (ECONNRESET) rather than throwing, since that's an expected, routine event (a gNB
    // disconnecting), not an error condition. Throws std::runtime_error on any other failure.
    std::vector<std::uint8_t> receive();

    bool valid() const { return fd_ >= 0; }

private:
    explicit SctpSocket(int fd);
    void close_if_open();

    int fd_ = -1;
};

} // namespace ngap_core
