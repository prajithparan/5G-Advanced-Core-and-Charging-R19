#include "sbi_core/oauth2_client.hpp"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <sstream>

namespace sbi_core {

namespace {

std::string form_urlencode(const std::string& value) {
    std::ostringstream out;
    for (const char raw_c : value) {
        const auto c = static_cast<unsigned char>(raw_c);
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else if (c == ' ') {
            out << '+';
        } else {
            out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(c);
        }
    }
    return out.str();
}

} // namespace

OAuth2Client::OAuth2Client(http2::Client& http_client,
                           std::string nrf_token_endpoint,
                           std::string nf_instance_id,
                           std::string scope,
                           std::string target_nf_type)
    : http_client_(http_client), nrf_token_endpoint_(std::move(nrf_token_endpoint)),
      nf_instance_id_(std::move(nf_instance_id)), scope_(std::move(scope)),
      target_nf_type_(std::move(target_nf_type)) {}

tl::expected<std::string, std::string> OAuth2Client::get_bearer_token() {
    std::lock_guard<std::mutex> lock(mutex_);

    constexpr auto kSafetyMargin = std::chrono::seconds(10);
    if (cached_token_.has_value() &&
        std::chrono::steady_clock::now() + kSafetyMargin < cached_token_->expires_at) {
        return cached_token_->access_token;
    }

    auto token = fetch_token();
    if (!token.has_value()) {
        return tl::unexpected(token.error());
    }
    cached_token_ = *token;
    return cached_token_->access_token;
}

tl::expected<OAuth2Token, std::string> OAuth2Client::fetch_token() {
    http2::ClientRequest req;
    req.method = "POST";
    req.url = nrf_token_endpoint_;
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");

    std::ostringstream body;
    body << "grant_type=" << form_urlencode("client_credentials");
    body << "&nfInstanceId=" << form_urlencode(nf_instance_id_);
    body << "&scope=" << form_urlencode(scope_);
    body << "&targetNfType=" << form_urlencode(target_nf_type_);
    req.body = body.str();

    auto resp = http_client_.send(req);
    if (!resp.has_value()) {
        return tl::unexpected("oauth2 token request failed: " + resp.error());
    }
    if (resp->status != 200) {
        return tl::unexpected("oauth2 token request returned status " +
                              std::to_string(resp->status) + ": " + resp->body);
    }

    OAuth2Token token;
    try {
        const auto json = nlohmann::json::parse(resp->body);
        token.access_token = json.at("access_token").get<std::string>();
        token.token_type = json.at("token_type").get<std::string>();
        const auto expires_in =
            json.contains("expires_in") ? json.at("expires_in").get<long>() : 3600L;
        token.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(expires_in);
    } catch (const nlohmann::json::exception& e) {
        return tl::unexpected(std::string("failed to parse AccessTokenRsp: ") + e.what());
    }

    return token;
}

} // namespace sbi_core
