// Drives nrf and nef as real, separate OS processes to exercise nef's Nnef_EventExposure
// (TS29591) surface (ADR-0209, gap-closure task #164, third NEF slice) over real TLS 1.3 + mTLS
// HTTP/2 with a real signed OAuth2 token.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

pid_t spawn(const char* path) {
    const pid_t pid = fork();
    if (pid == 0) {
        nf_test::arm_parent_death_signal();
        execl(path, path, static_cast<char*>(nullptr));
        _exit(127); // only reached if execl fails
    }
    return pid;
}

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int max_attempts) {
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        if (client.send(req).has_value()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

std::string fetch_token(sbi_core::http2::Client& client, const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body = "grant_type=client_credentials&nfInstanceId=test-client&scope=" + scope +
               "&targetNfType=NEF";
    auto resp = client.send(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

struct Duo {
    pid_t nrf_pid;
    pid_t nef_pid;
};

Duo spawn_all() {
    Duo d;
    d.nrf_pid = spawn(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    d.nef_pid = spawn(NEF_PATH);
    return d;
}

void reap_all(const Duo& d) {
    kill(d.nef_pid, SIGTERM);
    waitpid(d.nef_pid, nullptr, 0);
    kill(d.nrf_pid, SIGTERM);
    waitpid(d.nrf_pid, nullptr, 0);
}

} // namespace

TEST(NefEventExposureIntegration, CreateReadPutDeleteLifecycle) {
    auto d = spawn_all();
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions/nonexistent", 50))
        << "nef never became reachable";

    const std::string token = fetch_token(client, "nnef-eventexposure");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    const json create_body = json{
        {"eventsSubs", json::array({json{{"event", "UE_MOBILITY"}}})},
        {"notifId", "notif-1"},
        {"notifUri", "https://example.com/event-exposure-notify"},
    };
    sbi_core::http2::ClientRequest create_req;
    create_req.method = "POST";
    create_req.url = "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions";
    create_req.headers.emplace("content-type", "application/json");
    create_req.headers.emplace("authorization", "Bearer " + token);
    create_req.body = create_body.dump();
    auto create_resp = client.send(create_req);
    ASSERT_TRUE(create_resp.has_value());
    EXPECT_EQ(create_resp->status, 201) << create_resp->body;

    std::string sub_id;
    if (create_resp->status == 201) {
        auto loc = create_resp->headers.find("location");
        EXPECT_NE(loc, create_resp->headers.end());
        if (loc != create_resp->headers.end()) {
            sub_id = loc->second.substr(loc->second.rfind('/') + 1);
        }
        const auto created = json::parse(create_resp->body);
        EXPECT_EQ(created.at("notifId"), "notif-1");
    }
    ASSERT_FALSE(sub_id.empty());

    sbi_core::http2::ClientRequest get_req;
    get_req.method = "GET";
    get_req.url = "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions/" + sub_id;
    get_req.headers.emplace("authorization", "Bearer " + token);
    auto get_resp = client.send(get_req);
    ASSERT_TRUE(get_resp.has_value());
    EXPECT_EQ(get_resp->status, 200);

    const json put_body = json{
        {"eventsSubs", json::array({json{{"event", "UE_COMM"}}})},
        {"notifId", "notif-2"},
        {"notifUri", "https://example.com/event-exposure-notify"},
    };
    sbi_core::http2::ClientRequest put_req;
    put_req.method = "PUT";
    put_req.url = "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions/" + sub_id;
    put_req.headers.emplace("content-type", "application/json");
    put_req.headers.emplace("authorization", "Bearer " + token);
    put_req.body = put_body.dump();
    auto put_resp = client.send(put_req);
    ASSERT_TRUE(put_resp.has_value());
    EXPECT_EQ(put_resp->status, 200) << put_resp->body;
    if (put_resp->status == 200) {
        EXPECT_EQ(json::parse(put_resp->body).at("notifId"), "notif-2");
    }

    sbi_core::http2::ClientRequest delete_req;
    delete_req.method = "DELETE";
    delete_req.url = "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions/" + sub_id;
    delete_req.headers.emplace("authorization", "Bearer " + token);
    auto delete_resp = client.send(delete_req);
    ASSERT_TRUE(delete_resp.has_value());
    EXPECT_EQ(delete_resp->status, 204);

    auto delete_again_resp = client.send(delete_req);
    ASSERT_TRUE(delete_again_resp.has_value());
    EXPECT_EQ(delete_again_resp->status, 404);

    reap_all(d);
}

TEST(NefEventExposureIntegration, CreateWithMissingRequiredFieldIs400) {
    auto d = spawn_all();
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions/nonexistent", 50))
        << "nef never became reachable";

    const std::string token = fetch_token(client, "nnef-eventexposure");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    // notifUri is required and missing.
    const json bad_body = json{
        {"eventsSubs", json::array({json{{"event", "UE_MOBILITY"}}})},
        {"notifId", "notif-1"},
    };
    sbi_core::http2::ClientRequest bad_req;
    bad_req.method = "POST";
    bad_req.url = "https://127.0.0.1:7790/nnef-eventexposure/v1/subscriptions";
    bad_req.headers.emplace("content-type", "application/json");
    bad_req.headers.emplace("authorization", "Bearer " + token);
    bad_req.body = bad_body.dump();
    auto bad_resp = client.send(bad_req);
    ASSERT_TRUE(bad_resp.has_value());
    EXPECT_EQ(bad_resp->status, 400);

    reap_all(d);
}
