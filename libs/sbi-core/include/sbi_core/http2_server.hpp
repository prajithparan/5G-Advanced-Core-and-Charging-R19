#pragma once

#include "sbi_core/tls_config.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Minimal HTTP/2 server built directly on nghttp2's C session API, driven by Boost.Asio for the
// socket I/O loop. This exists because vcpkg does not package upstream nghttp2's own asio_http2
// ("nghttp2-asio") convenience library -- see docs/DECISIONS.md ADR-0004.
//
// TLS 1.3 + mTLS only (see ADR-0011): every connection is wrapped in boost::asio::ssl::stream, the
// server requires and verifies a client certificate against the configured CA, and ALPN must
// negotiate "h2" or the handshake is rejected outright -- there is no cleartext/h2c fallback and no
// TLS version below 1.3. TlsConfig is a required constructor argument specifically so a caller
// cannot accidentally stand up an unauthenticated or unencrypted server by omission.
//
// Routing is a simple exact-segment matcher supporting "{param}" path segments (e.g.
// "/nnrf-nfm/v1/nf-instances/{nfInstanceId}"), which is what Nnrf_NFManagement needs; it is not a
// general-purpose router and isn't meant to become one -- Phase 1's generated server stubs may
// replace this matcher entirely once real per-NF path sets exist.

namespace sbi_core::http2 {

// TlsConfig (cert_path/key_path/ca_path) is defined in tls_config.hpp, shared with
// http2_client.hpp. Server's constructor throws std::runtime_error if any path is
// missing/unreadable or the certificate/key can't be loaded -- fails loudly rather than silently
// falling back to an unauthenticated/unencrypted listener.

struct Request {
    std::string method;
    std::string path;
    std::multimap<std::string, std::string> headers;
    std::map<std::string, std::string> path_params;
    std::string body;
};

struct Response {
    int status = 200;
    std::multimap<std::string, std::string> headers;
    std::string body;

    static Response json(int status, std::string body_json);
};

using Handler = std::function<Response(const Request&)>;

class Server {
public:
    Server(boost::asio::io_context& ioc, std::string address, unsigned short port, TlsConfig tls);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // path_pattern segments wrapped in {} are captured into Request::path_params.
    void add_route(std::string method, std::string path_pattern, Handler handler);

    // Begins accepting connections. Returns immediately; actual I/O happens on the io_context
    // passed to the constructor (caller owns the event loop, e.g. via ioc.run()).
    void start();

    unsigned short local_port() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sbi_core::http2
