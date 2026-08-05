// Drives nrf and smf as real, separate OS processes: nrf issues real signed OAuth2 tokens, smf
// registers with nrf on its own background thread (see nfs/smf/src/main.cpp's run_nrf_lifecycle,
// docs/DECISIONS.md ADR-0006/ADR-0019/ADR-0020), then this test acts as an SBI client exercising
// smf's /sm-contexts routes over real TLS 1.3 + mTLS HTTP/2, including a real multipart/related
// CreateSMContext request (the actual AMF-triggered PDU Session Establishment trigger,
// TS 23.502 clause 4.3.2.2.1 -- CLAUDE.md's stated Phase 2 end-state goal).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/multipart.hpp"

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
        req.method = "POST";
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
    req.body = "grant_type=client_credentials&nfInstanceId=test-client&scope=nsmf-pdusession&"
               "targetNfType=SMF";
    auto resp = client.send(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

} // namespace

TEST(SmfIntegration, FullSmContextLifecycleOverRealHttp2) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t smf_pid = spawn(SMF_PATH);
    ASSERT_GT(smf_pid, 0) << "failed to fork smf";

    auto client = make_client();
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/nonexistent/retrieve", 50))
        << "smf never became reachable";

    const std::string token = fetch_token(client);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    // CreateSMContext: multipart/related-only per spec (docs/DECISIONS.md ADR-0020). Real
    // mandatory fields per SmContextCreateData: servingNfId, servingNetwork, anType,
    // smContextStatusUri.
    sbi_core::multipart::Part create_json_part;
    create_json_part.content_type = "application/json";
    create_json_part.body =
        json{
            {"servingNfId", "00000000-0000-4000-8000-0000000000aa"},
            {"servingNetwork", json{{"mcc", "999"}, {"mnc", "70"}}},
            {"anType", "3GPP_ACCESS"},
            {"smContextStatusUri", "https://example.com/sm-status"},
        }
            .dump();
    const auto encoded = sbi_core::multipart::encode({create_json_part});

    sbi_core::http2::ClientRequest create_req;
    create_req.method = "POST";
    create_req.url = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts";
    create_req.headers.emplace("content-type", encoded.content_type_header);
    create_req.headers.emplace("authorization", "Bearer " + token);
    create_req.body = encoded.body;

    auto create_resp = client.send(create_req);
    ASSERT_TRUE(create_resp.has_value());
    EXPECT_EQ(create_resp->status, 201);
    const auto created = json::parse(create_resp->body).get<sbi_gen::SmContextCreatedData>();
    (void)created; // proves real deserialization, not just a 2xx status
    const auto location_it = create_resp->headers.find("location");
    ASSERT_NE(location_it, create_resp->headers.end());
    const std::string location = location_it->second;
    const auto ref_pos = location.rfind('/');
    ASSERT_NE(ref_pos, std::string::npos);
    const std::string sm_context_ref = location.substr(ref_pos + 1);
    EXPECT_FALSE(sm_context_ref.empty());

    // RetrieveSMContext: request body is optional per spec -- send none.
    sbi_core::http2::ClientRequest retrieve_req;
    retrieve_req.method = "POST";
    retrieve_req.url =
        "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/" + sm_context_ref + "/retrieve";
    retrieve_req.headers.emplace("authorization", "Bearer " + token);
    auto retrieve_resp = client.send(retrieve_req);
    ASSERT_TRUE(retrieve_resp.has_value());
    EXPECT_EQ(retrieve_resp->status, 200);
    const auto retrieved = json::parse(retrieve_resp->body).get<sbi_gen::SmContextRetrievedData>();
    (void)retrieved;

    // UpdateSMContext.
    sbi_core::http2::ClientRequest update_req;
    update_req.method = "POST";
    update_req.url =
        "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/" + sm_context_ref + "/modify";
    update_req.headers.emplace("content-type", "application/json");
    update_req.headers.emplace("authorization", "Bearer " + token);
    update_req.body = json{{"upCnxState", "ACTIVATED"}}.dump();
    auto update_resp = client.send(update_req);
    ASSERT_TRUE(update_resp.has_value());
    EXPECT_EQ(update_resp->status, 204);

    // ReleaseSMContext, then confirm the context is really gone (second release -> 404).
    sbi_core::http2::ClientRequest release_req;
    release_req.method = "POST";
    release_req.url =
        "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/" + sm_context_ref + "/release";
    release_req.headers.emplace("authorization", "Bearer " + token);
    auto release_resp = client.send(release_req);
    ASSERT_TRUE(release_resp.has_value());
    EXPECT_EQ(release_resp->status, 204);

    auto release_again = client.send(release_req);
    ASSERT_TRUE(release_again.has_value());
    EXPECT_EQ(release_again->status, 404);

    kill(smf_pid, SIGTERM);
    waitpid(smf_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}

TEST(SmfIntegration, MissingSmContextIs404AndTamperedTokenIs401AndNonMultipartCreateIs400) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t smf_pid = spawn(SMF_PATH);
    ASSERT_GT(smf_pid, 0) << "failed to fork smf";

    auto client = make_client();
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/nonexistent/retrieve", 50))
        << "smf never became reachable";

    const std::string token = fetch_token(client);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    sbi_core::http2::ClientRequest retrieve_req;
    retrieve_req.method = "POST";
    retrieve_req.url = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/nonexistent/retrieve";
    retrieve_req.headers.emplace("authorization", "Bearer " + token);
    auto retrieve_resp = client.send(retrieve_req);
    ASSERT_TRUE(retrieve_resp.has_value());
    EXPECT_EQ(retrieve_resp->status, 404);

    sbi_core::http2::ClientRequest tampered_req;
    tampered_req.method = "POST";
    tampered_req.url = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/nonexistent/release";
    tampered_req.headers.emplace("authorization", "Bearer " + token + "tampered");
    auto tampered_resp = client.send(tampered_req);
    ASSERT_TRUE(tampered_resp.has_value());
    EXPECT_EQ(tampered_resp->status, 401);

    // CreateSMContext requires multipart/related -- a plain JSON body must be rejected, not
    // silently accepted.
    sbi_core::http2::ClientRequest wrong_content_type_req;
    wrong_content_type_req.method = "POST";
    wrong_content_type_req.url = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts";
    wrong_content_type_req.headers.emplace("content-type", "application/json");
    wrong_content_type_req.headers.emplace("authorization", "Bearer " + token);
    wrong_content_type_req.body = json{{"anType", "3GPP_ACCESS"}}.dump();
    auto wrong_content_type_resp = client.send(wrong_content_type_req);
    ASSERT_TRUE(wrong_content_type_resp.has_value());
    EXPECT_EQ(wrong_content_type_resp->status, 400);

    kill(smf_pid, SIGTERM);
    waitpid(smf_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}
