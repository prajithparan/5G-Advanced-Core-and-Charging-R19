#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule.
//
// P4.5/ADR-0059 Stage 2: CHF as a real Diameter server, accepting a real TCP connection (Diameter
// runs over TCP or SCTP, RFC 6733 -- this project uses plain TCP, matching pfcp_core's own
// UDP-not-SCTP choice for the same reason: Boost.Asio has no native SCTP support, and TCP is a
// fully spec-conformant transport option, not a simplification). Same "blocking I/O gets its own
// thread" discipline this project already uses for SMF's PfcpPeer and run_nrf_lifecycle
// (docs/DECISIONS.md ADR-0006/ADR-0019/ADR-0039): a dedicated accept thread, and one further
// dedicated thread per accepted connection (real Diameter deployments expect a small, stable
// number of long-lived peer connections, not per-request connections -- thread-per-connection is
// the right shape here, unlike per-request HTTP/2 handling).
//
// This Stage handles ONLY the Capabilities-Exchange (CER/CEA) handshake -- it accepts the
// connection, answers CER with a real CEA, and then closes. Stage 3 (ADR-0059) extends the
// per-connection loop to keep the connection open and dispatch CCR/CCA (the real Gy application
// logic, normalized into the same path Nchf_ConvergedCharging's own handlers already use).

namespace chf {

class DiameterServer {
public:
    // Binds 0.0.0.0:port and starts the accept thread immediately. origin_host/origin_realm are
    // this CHF's own real Diameter identity (echoed in every CEA's Origin-Host/Origin-Realm AVPs).
    DiameterServer(std::uint16_t port, std::string origin_host, std::string origin_realm);
    ~DiameterServer();

    DiameterServer(const DiameterServer&) = delete;
    DiameterServer& operator=(const DiameterServer&) = delete;

private:
    void accept_loop();
    void handle_connection(boost::asio::ip::tcp::socket socket);

    boost::asio::io_context ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::string origin_host_;
    std::string origin_realm_;
    std::thread accept_thread_;
    std::atomic<bool> stop_{false};
};

} // namespace chf
