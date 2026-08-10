#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "pfcp_core/header.hpp"

// Private to nfs/smf -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule.
//
// A real, persistent, bidirectional PFCP peer -- SMF's single PFCP socket, bound to a stable,
// well-known port (pfcp_core::kSmfCpFunctionPfcpPort) so UPF can reach it for an unsolicited
// message (real TS 29.244 Sx Session Report Request, ADR-0049's quota-consumption-tracking turn),
// not just per-call ephemeral sockets the way this project's earlier PFCP work (ADR-0041/ADR-0042)
// used -- those could only ever receive a *response* to something SMF itself sent, never an
// unsolicited message. Replaces send_pfcp_request_and_await_response's old per-call
// io_context+socket (nfs/smf/src/main.cpp) with one long-lived socket shared by every PFCP call
// site (Association Setup, Session Establishment, and now Session Report handling).
//
// One dedicated receive thread owns the actual blocking socket read, dispatching each datagram by
// message type: a Response matching an outstanding request's sequence number is handed to that
// caller (condition_variable-based handoff, keyed by sequence number -- unique per logical request
// via allocate_sequence_number(), so no type+seqnum collision is possible across different calls);
// an unsolicited Session Report Request is handed to the caller-supplied handler.

namespace smf {

class PfcpPeer {
public:
    using SessionReportHandler = std::function<void(const pfcp_core::Header& header,
                                                    const std::vector<std::uint8_t>& ie_bytes,
                                                    const boost::asio::ip::udp::endpoint& sender)>;

    // Binds the persistent socket to 0.0.0.0:kSmfCpFunctionPfcpPort and starts the receive-
    // dispatch thread immediately -- with no session-report handler set yet (set_session_report_
    // handler below installs one). The handler isn't a constructor parameter: it very often needs
    // to capture this same PfcpPeer by reference (to call send_fire_and_forget for the ack), which
    // would otherwise be a reference to a not-yet-constructed object -- the setter sidesteps that
    // entirely. A Session Report Request arriving before a real handler is installed (possible in
    // principle, harmless in practice given how this project starts up) is simply logged and
    // dropped by the default handler.
    PfcpPeer();
    ~PfcpPeer();

    PfcpPeer(const PfcpPeer&) = delete;
    PfcpPeer& operator=(const PfcpPeer&) = delete;

    // Thread-safe; may be called any time after construction, including from a different thread
    // than the one that constructed this peer.
    void set_session_report_handler(SessionReportHandler handler);

    // Allocates a fresh sequence number for a new logical request. Thread-safe; every call site
    // gets a genuinely unique value now that one shared socket serves every concurrent caller
    // (unlike the old per-call-socket code, which could safely hardcode sequence_number=1
    // everywhere since each call had its own private socket).
    std::uint32_t allocate_sequence_number();

    // Sends request_pdu to target and waits for a response matching expected_response_type +
    // sequence_number, retrying (T1=2s, N1=3 attempts per TS 29.244 §6.4 -- unchanged from the
    // prior per-call-socket implementation's own choices) before giving up. Thread-safe: multiple
    // callers may have outstanding requests concurrently.
    std::optional<std::vector<std::uint8_t>>
    send_request_and_await_response(const boost::asio::ip::udp::endpoint& target,
                                    const std::vector<std::uint8_t>& request_pdu,
                                    pfcp_core::MessageType expected_response_type,
                                    std::uint32_t sequence_number,
                                    const std::string& procedure_name);

    // Sends a PDU without waiting for anything back -- used to acknowledge an unsolicited request
    // (e.g. a Session Report Response) from within the SessionReportHandler.
    void send_fire_and_forget(const boost::asio::ip::udp::endpoint& target,
                              const std::vector<std::uint8_t>& pdu);

private:
    void receive_loop();

    boost::asio::io_context ioc_;
    boost::asio::ip::udp::socket socket_;
    std::mutex handler_mutex_;
    SessionReportHandler on_session_report_request_;
    std::thread receive_thread_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint32_t> next_sequence_number_{1};

    std::mutex send_mutex_; // guards socket_.send_to against concurrent callers
    std::mutex response_mutex_;
    std::condition_variable response_cv_;
    struct PendingResponse {
        pfcp_core::MessageType message_type{};
        std::vector<std::uint8_t> ie_bytes;
    };
    std::unordered_map<std::uint32_t, PendingResponse> pending_responses_;
};

} // namespace smf
