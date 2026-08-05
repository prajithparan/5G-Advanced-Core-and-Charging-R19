// Drives nrf and amf as real, separate OS processes: nrf issues real signed OAuth2 tokens, amf
// registers with nrf on its own background thread (see docs/DECISIONS.md ADR-0006's
// dedicated-thread note in nfs/amf/src/main.cpp), then this test acts as an SBI client (reusing
// hello-nf's lab cert purely as a caller identity, same as the manual curl verification recorded
// in docs/TRACEABILITY.md) exercising amf's Namf_Communication routes over real TLS 1.3 + mTLS.
//
// Covers the procedures that don't require a pre-existing UE context (nothing in this build can
// create one yet -- CreateUEContext needs multipart/related support sbi-core doesn't have, see
// ADR-0016): N1N2MessageSubscribe/UnSubscribe, AMFStatusChangeSubscribe, NonUeN2MessageTransfer,
// and the 404/401 error paths for a UE-context-scoped operation and a tampered token respectively.
// ReleaseUEContext/EBIAssignment/UEContextTransfer/RegistrationStatusUpdate's "found" branches
// remain unverified by any automated test -- disclosed in nfs/amf/src/main.cpp's file header, not
// hidden.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "TS29122_CommonData_grp.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

pid_t spawn(const char* path) {
    const pid_t pid = fork();
    if (pid == 0) {
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

std::string fetch_token(sbi_core::http2::Client& client) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body = "grant_type=client_credentials&nfInstanceId=test-client&scope=namf-comm&"
               "targetNfType=AMF";
    auto resp = client.send(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

} // namespace

TEST(AmfIntegration, N1N2SubscribeAndUnsubscribe) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t amf_pid = spawn(AMF_PATH);
    ASSERT_GT(amf_pid, 0) << "failed to fork amf";

    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7778/namf-comm/v1/subscriptions/nonexistent", 50))
        << "amf never became reachable";

    // amf's own NRF-registration background thread races this test; give it a moment. (Every
    // request below authenticates via its own bearer token regardless, so this is only about amf
    // having had time to register, not a correctness dependency of the requests themselves.)
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const std::string token = fetch_token(client);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    sbi_core::http2::ClientRequest sub_req;
    sub_req.method = "POST";
    sub_req.url = "https://127.0.0.1:7778/namf-comm/v1/ue-contexts/imsi-999700000000001/"
                  "n1-n2-messages/subscriptions";
    sub_req.headers.emplace("content-type", "application/json");
    sub_req.headers.emplace("authorization", "Bearer " + token);
    sub_req.body = json{{"n1NotifyCallbackUri", "https://example.com/n1"}}.dump();

    auto sub_resp = client.send(sub_req);
    ASSERT_TRUE(sub_resp.has_value());
    EXPECT_EQ(sub_resp->status, 201);
    const auto created =
        json::parse(sub_resp->body).get<sbi_gen::UeN1N2InfoSubscriptionCreatedData>();
    EXPECT_FALSE(created.n1n2NotifySubscriptionId.empty());

    sbi_core::http2::ClientRequest unsub_req;
    unsub_req.method = "DELETE";
    unsub_req.url = "https://127.0.0.1:7778/namf-comm/v1/ue-contexts/imsi-999700000000001/"
                    "n1-n2-messages/subscriptions/" +
                    created.n1n2NotifySubscriptionId;
    unsub_req.headers.emplace("authorization", "Bearer " + token);
    auto unsub_resp = client.send(unsub_req);
    ASSERT_TRUE(unsub_resp.has_value());
    EXPECT_EQ(unsub_resp->status, 204);

    // Removed already -- unsubscribing again must 404, not silently succeed twice.
    auto unsub_again = client.send(unsub_req);
    ASSERT_TRUE(unsub_again.has_value());
    EXPECT_EQ(unsub_again->status, 404);

    kill(amf_pid, SIGTERM);
    waitpid(amf_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}

TEST(AmfIntegration, AmfStatusChangeSubscribeAndNonUeN2Transfer) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t amf_pid = spawn(AMF_PATH);
    ASSERT_GT(amf_pid, 0) << "failed to fork amf";

    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7778/namf-comm/v1/subscriptions/nonexistent", 50))
        << "amf never became reachable";

    const std::string token = fetch_token(client);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    sbi_core::http2::ClientRequest amf_status_req;
    amf_status_req.method = "POST";
    amf_status_req.url = "https://127.0.0.1:7778/namf-comm/v1/subscriptions";
    amf_status_req.headers.emplace("content-type", "application/json");
    amf_status_req.headers.emplace("authorization", "Bearer " + token);
    amf_status_req.body = json{{"amfStatusUri", "https://example.com/amfstatus"}}.dump();
    auto amf_status_resp = client.send(amf_status_req);
    ASSERT_TRUE(amf_status_resp.has_value());
    EXPECT_EQ(amf_status_resp->status, 201);
    auto loc = amf_status_resp->headers.find("location");
    ASSERT_NE(loc, amf_status_resp->headers.end());
    EXPECT_NE(loc->second.find("/namf-comm/v1/subscriptions/"), std::string::npos);

    sbi_core::http2::ClientRequest n2_req;
    n2_req.method = "POST";
    n2_req.url = "https://127.0.0.1:7778/namf-comm/v1/non-ue-n2-messages/transfer";
    n2_req.headers.emplace("content-type", "application/json");
    n2_req.headers.emplace("authorization", "Bearer " + token);
    n2_req.body = json{{"n2Information", json{{"n2InformationClass", "PWS"}}}}.dump();
    auto n2_resp = client.send(n2_req);
    ASSERT_TRUE(n2_resp.has_value());
    EXPECT_EQ(n2_resp->status, 200);
    const auto result = json::parse(n2_resp->body).get<sbi_gen::N2InformationTransferRspData>();
    EXPECT_EQ(result.result.value,
              sbi_gen::N2InformationTransferResult::N2_INFO_TRANSFER_INITIATED);

    kill(amf_pid, SIGTERM);
    waitpid(amf_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}

TEST(AmfIntegration, MissingUeContextIs404AndTamperedTokenIs401) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t amf_pid = spawn(AMF_PATH);
    ASSERT_GT(amf_pid, 0) << "failed to fork amf";

    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7778/namf-comm/v1/subscriptions/nonexistent", 50))
        << "amf never became reachable";

    const std::string token = fetch_token(client);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    sbi_core::http2::ClientRequest release_req;
    release_req.method = "POST";
    release_req.url =
        "https://127.0.0.1:7778/namf-comm/v1/ue-contexts/imsi-999700000000002/release";
    release_req.headers.emplace("content-type", "application/json");
    release_req.headers.emplace("authorization", "Bearer " + token);
    release_req.body = json{{"ngapCause", json{{"group", 0}, {"value", 0}}}}.dump();
    auto release_resp = client.send(release_req);
    ASSERT_TRUE(release_resp.has_value());
    EXPECT_EQ(release_resp->status, 404);

    sbi_core::http2::ClientRequest tampered_req;
    tampered_req.method = "POST";
    tampered_req.url = "https://127.0.0.1:7778/namf-comm/v1/subscriptions";
    tampered_req.headers.emplace("content-type", "application/json");
    tampered_req.headers.emplace("authorization", "Bearer " + token + "tampered");
    tampered_req.body = json{{"amfStatusUri", "https://example.com/amfstatus"}}.dump();
    auto tampered_resp = client.send(tampered_req);
    ASSERT_TRUE(tampered_resp.has_value());
    EXPECT_EQ(tampered_resp->status, 401);

    kill(amf_pid, SIGTERM);
    waitpid(amf_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}
