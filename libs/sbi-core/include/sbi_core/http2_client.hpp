#pragma once

#include "sbi_core/tls_config.hpp"

#include <map>
#include <mutex>
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
//
// Thread-safety: `send()` serializes concurrent callers via an internal mutex -- a single libcurl
// easy handle is reused across calls (real, deliberate reuse for connection-keepalive reasons),
// and libcurl's own contract is explicit that one easy handle must never be driven by two threads
// at once. Every NF's own convention has been "one Client instance per thread" so this was never
// exercised until ADR-0050 Stage 5's detached per-report thread design gave `nfs/smf`'s
// `chf_report_client` a second, genuinely concurrent caller -- caught via a real, live, malformed-
// response failure (`curl_easy_perform` returning `CURLE_FAILED_INIT`/empty responses under real
// concurrent access), not by inspection. Serializing here fixes it foundationally for every current
// and future caller, not just the one that surfaced it.

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
    std::mutex mutex_; // see class comment -- guards curl_ against concurrent callers
};

} // namespace sbi_core::http2
