#include "sbi_core/http2_client.hpp"

#include <curl/curl.h>

#include <mutex>

namespace sbi_core::http2 {

namespace {

void ensure_curl_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t write_body_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

std::size_t write_header_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::multimap<std::string, std::string>*>(userdata);
    const std::string line(ptr, size * nmemb);
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        const auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
                s.pop_back();
            }
            std::size_t start = s.find_first_not_of(' ');
            s = (start == std::string::npos) ? "" : s.substr(start);
        };
        trim(value);
        headers->emplace(std::move(name), std::move(value));
    }
    return size * nmemb;
}

} // namespace

Client::Client() {
    ensure_curl_global_init();
    curl_ = curl_easy_init();
}

Client::~Client() {
    if (curl_ != nullptr) {
        curl_easy_cleanup(static_cast<CURL*>(curl_));
    }
}

tl::expected<ClientResponse, std::string> Client::send(const ClientRequest& request) {
    if (curl_ == nullptr) {
        return tl::unexpected("curl handle not initialized");
    }
    auto* curl = static_cast<CURL*>(curl_);
    curl_easy_reset(curl);

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());

    if (!request.body.empty() || request.method == "PUT" || request.method == "POST" ||
        request.method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }

    curl_slist* header_list = nullptr;
    for (const auto& [name, value] : request.headers) {
        const std::string line = name + ": " + value;
        header_list = curl_slist_append(header_list, line.c_str());
    }
    if (header_list != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    ClientResponse response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

    const CURLcode res = curl_easy_perform(curl);

    if (header_list != nullptr) {
        curl_slist_free_all(header_list);
    }

    if (res != CURLE_OK) {
        return tl::unexpected(std::string(curl_easy_strerror(res)));
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = status;

    return response;
}

} // namespace sbi_core::http2
