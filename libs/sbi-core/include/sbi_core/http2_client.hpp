#pragma once

#include "sbi_core/tls_config.hpp"

#include <map>
#include <string>
#include <tl/expected.hpp>

// HTTP/2 client built on libcurl (which already has mature native HTTP/2 support), used for all
// outbound SBI calls (NF -> NRF, NF -> NF). Synchronous/blocking per call -- see docs/DECISIONS.md
// ADR-0006 for why that's an acceptable Phase 0 simplification and what integrating it with the
// Boost.Asio event loop (curl_multi + asio) would take later.
//
// TLS 1.3 + mTLS only (see ADR-0011): every request presents the client's own certificate and
// verifies the server's against the configured CA. There is no cleartext fallback -- pass a `url`
// with anything other than an https:// scheme and the request fails.

namespace sbi_core::http2 {

struct ClientRequest {
    std::string method;
    std::string url;
    std::multimap<std::string, std::string> headers;
    std::string body;
};

struct ClientResponse {
    long status = 0;
    std::multimap<std::string, std::string> headers;
    std::string body;
};

class Client {
public:
    explicit Client(TlsConfig tls);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    tl::expected<ClientResponse, std::string> send(const ClientRequest& request);

private:
    void* curl_ = nullptr; // CURL*, opaque here to avoid leaking <curl/curl.h> into every includer
    TlsConfig tls_;
};

} // namespace sbi_core::http2
