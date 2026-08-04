#pragma once

#include "sbi_core/http2_client.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <tl/expected.hpp>

// OAuth2 client-credentials flow against NRF's Nnrf_AccessToken service
// (specs/5G_APIs-REL-19/TS29510_Nnrf_AccessToken.yaml, POST /oauth2/token). Request/response field
// names below are transcribed from that file's AccessTokenReq/AccessTokenRsp schemas, not invented.
//
// Only the fields Phase 0 needs are modeled (grant_type, nfInstanceId, scope on the request;
// access_token, token_type, expires_in on the response) -- AccessTokenReq has many more optional
// fields (targetNfType, requesterPlmn, requesterSnssaiList, ...) that real NF-to-NF token requests
// will need; those are added when the NF procedures that populate them are implemented, per
// CLAUDE.md's "no speculative abstraction" rule.

namespace sbi_core {

struct OAuth2Token {
    std::string access_token;
    std::string token_type;
    std::chrono::steady_clock::time_point expires_at;
};

class OAuth2Client {
public:
    // nrf_token_endpoint e.g. "http://127.0.0.1:7777/oauth2/token"
    OAuth2Client(http2::Client& http_client,
                 std::string nrf_token_endpoint,
                 std::string nf_instance_id,
                 std::string scope);

    // Returns a cached token if still valid (with a small safety margin), otherwise fetches a new
    // one synchronously.
    tl::expected<std::string, std::string> get_bearer_token();

private:
    tl::expected<OAuth2Token, std::string> fetch_token();

    http2::Client& http_client_;
    std::string nrf_token_endpoint_;
    std::string nf_instance_id_;
    std::string scope_;

    std::mutex mutex_;
    std::optional<OAuth2Token> cached_token_;
};

} // namespace sbi_core
