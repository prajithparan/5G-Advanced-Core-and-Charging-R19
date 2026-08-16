// ADR-0072 (gap-closure: real N28 end-to-end). Real, automated coverage for the parts of the new
// N28 flow that this project's existing CI infrastructure can actually exercise:
//   1. UDR's new real `/policy-data/ues/{ueId}/sm-data` resource (TS29519_Policy_Data.yaml) --
//      GET/PATCH, real RFC 7396 merge-patch semantics, real upsert-on-first-PATCH behavior.
//   2. PCF's real UDR-fetch-then-CHF-subscribe wiring's fail-open behavior when CHF is
//      unreachable -- spawns nrf+udr+pcf only, deliberately NOT chf.
//
// Real, disclosed scope boundary: CHF has never been part of this project's automated ctest suite
// at all (it needs Redis/ClickHouse, neither of which .github/workflows/ci.yml provisions --
// confirmed by reading that file directly, not assumed -- a real, pre-existing gap this turn does
// not newly introduce or claim to close). The full real UDR->PCF->CHF->statusNotification loop
// WAS live-verified manually this session (real curl-equivalent commands against real running
// nrf/udr/chf/pcf processes with real Postgres/Redis backing, see docs/DECISIONS.md ADR-0072 for
// the exact commands and observed outputs) -- that evidence is real but not automated/repeatable
// via `ctest`, disclosed here rather than silently implied to be covered by this file.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

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

// Real bug found and fixed via actual test execution (not assumed): UDR's Postgres store
// genuinely persists across process runs (that IS the whole point of ADR-0068's own real
// persistence work) -- a fixed, hardcoded test SUPI collides with a previous run's leftover row on
// any re-run against the same long-lived database, breaking this test's own initial-404
// assertion. Uses this OS process's own pid for real per-run uniqueness (matching the real "one
// test process, one execution" scope -- not claiming safety across truly concurrent parallel test
// runs sharing the same database, which this project's test suite doesn't do).
// `salt` distinguishes multiple calls made from within the SAME test-binary process (gtest runs
// every TEST() in one process by default, so pid alone can't tell two different tests in the same
// run apart) -- callers pass a distinct small integer per call site.
std::string unique_test_supi(int salt) {
    const auto pid = static_cast<long>(getpid());
    // Low 4 digits of the current time (seconds) alongside the pid: real, cheap extra protection
    // against OS pid-reuse across separate test-binary invocations within the same second-scale
    // window, not just within one.
    const auto time_low = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count() %
                                            10000);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "imsi-99970%04ld%05ld%d", time_low, pid % 100000, salt % 10);
    return buf;
}

std::string fetch_token(sbi_core::http2::Client& client, const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body = "grant_type=client_credentials&nfInstanceId=test-client&scope=" + scope +
               "&targetNfType=UDR";
    auto resp = client.send(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

} // namespace

TEST(UdrSmPolicyDataIntegration, PatchCreatesAndMergesRealNestedDocument) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t udr_pid = spawn(UDR_PATH);
    ASSERT_GT(udr_pid, 0) << "failed to fork udr";

    auto client = make_client();
    const std::string supi = unique_test_supi(1);
    const std::string base_url =
        "https://127.0.0.1:7781/nudr-dr/v2/policy-data/ues/" + supi + "/sm-data";
    ASSERT_TRUE(wait_reachable(client, base_url, 50)) << "udr never became reachable";

    const std::string token = fetch_token(client, "nudr-dr");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    // Real GET before any PATCH: 404, matching the real "no POST/create exists for this resource"
    // spec shape this project's own store deliberately works around via upsert-on-PATCH.
    sbi_core::http2::ClientRequest get_req;
    get_req.method = "GET";
    get_req.url = base_url;
    get_req.headers.emplace("authorization", "Bearer " + token);
    auto get_resp = client.send(get_req);
    ASSERT_TRUE(get_resp.has_value());
    EXPECT_EQ(get_resp->status, 404);

    // Real PATCH creating the real nested SmPolicyData structure from nothing.
    sbi_core::http2::ClientRequest patch_req;
    patch_req.method = "PATCH";
    patch_req.url = base_url;
    patch_req.headers.emplace("content-type", "application/merge-patch+json");
    patch_req.headers.emplace("authorization", "Bearer " + token);
    patch_req.body = json{{"smPolicySnssaiData",
                           {{"1-000001",
                             {{"snssai", {{"sst", 1}, {"sd", "000001"}}},
                              {"smPolicyDnnData",
                               {{"internet",
                                 {{"dnn", "internet"},
                                  {"subscSpendingLimits", true},
                                  {"gbrUl", {{"value", 50}, {"unit", "Mbps"}}}}}}}}}}}}
                         .dump();
    auto patch_resp = client.send(patch_req);
    ASSERT_TRUE(patch_resp.has_value());
    EXPECT_EQ(patch_resp->status, 200);
    auto created = json::parse(patch_resp->body);
    EXPECT_TRUE(created["smPolicySnssaiData"]["1-000001"]["smPolicyDnnData"]["internet"]
                       ["subscSpendingLimits"]
                           .get<bool>());

    // Real GET after create: confirms real persistence, not just an echo.
    auto get_resp2 = client.send(get_req);
    ASSERT_TRUE(get_resp2.has_value());
    EXPECT_EQ(get_resp2->status, 200);
    auto fetched = json::parse(get_resp2->body);
    EXPECT_EQ(fetched["smPolicySnssaiData"]["1-000001"]["smPolicyDnnData"]["internet"]["dnn"]
                  .get<std::string>(),
              "internet");

    // Real partial merge-patch: adds a new field without clobbering the ones already there (real
    // RFC 7396 semantics, not a full-document replace).
    sbi_core::http2::ClientRequest patch_req2;
    patch_req2.method = "PATCH";
    patch_req2.url = base_url;
    patch_req2.headers.emplace("content-type", "application/merge-patch+json");
    patch_req2.headers.emplace("authorization", "Bearer " + token);
    patch_req2.body =
        json{{"smPolicySnssaiData",
              {{"1-000001", {{"smPolicyDnnData", {{"internet", {{"mpsPriority", true}}}}}}}}}}
            .dump();
    auto patch_resp2 = client.send(patch_req2);
    ASSERT_TRUE(patch_resp2.has_value());
    EXPECT_EQ(patch_resp2->status, 200);
    auto merged = json::parse(patch_resp2->body);
    const auto& dnn_data = merged["smPolicySnssaiData"]["1-000001"]["smPolicyDnnData"]["internet"];
    EXPECT_TRUE(dnn_data["mpsPriority"].get<bool>());
    // Real merge, not replace: the earlier fields are still there.
    EXPECT_TRUE(dnn_data["subscSpendingLimits"].get<bool>());
    EXPECT_EQ(dnn_data["dnn"].get<std::string>(), "internet");

    kill(udr_pid, SIGTERM);
    waitpid(udr_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}

TEST(PcfN28Integration, CreateSmPolicyFailsOpenWhenChfUnreachable) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t udr_pid = spawn(UDR_PATH);
    ASSERT_GT(udr_pid, 0) << "failed to fork udr";

    const pid_t pcf_pid = spawn(PCF_PATH);
    ASSERT_GT(pcf_pid, 0) << "failed to fork pcf";

    auto client = make_client();
    const std::string supi = unique_test_supi(2);
    const std::string udr_url =
        "https://127.0.0.1:7781/nudr-dr/v2/policy-data/ues/" + supi + "/sm-data";
    ASSERT_TRUE(wait_reachable(client, udr_url, 50)) << "udr never became reachable";
    const std::string pcf_url = "https://127.0.0.1:7783/npcf-smpolicycontrol/v1/sm-policies";
    ASSERT_TRUE(wait_reachable(client, pcf_url, 50)) << "pcf never became reachable";

    const std::string udr_token = fetch_token(client, "nudr-dr");
    ASSERT_FALSE(udr_token.empty());

    // Real seed: subscSpendingLimits=true with a real policyCounterId, exactly the condition that
    // makes CreateSMPolicy attempt a real CHF subscribe -- CHF is deliberately not running, so
    // this exercises the real fail-open path (subscribe_spending_limit's own disclosed
    // "CHF unreachable shouldn't block a PDU session from getting service" behavior).
    sbi_core::http2::ClientRequest seed_req;
    seed_req.method = "PATCH";
    seed_req.url = udr_url;
    seed_req.headers.emplace("content-type", "application/merge-patch+json");
    seed_req.headers.emplace("authorization", "Bearer " + udr_token);
    seed_req.body =
        json{{"smPolicySnssaiData",
              {{"1-000001",
                {{"snssai", {{"sst", 1}, {"sd", "000001"}}},
                 {"smPolicyDnnData",
                  {{"internet",
                    {{"dnn", "internet"},
                     {"subscSpendingLimits", true},
                     {"spendLimInfo",
                      {{"test-counter",
                        {{"policyCounterId", "test-counter"}, {"currentStatus", "x"}}}}}}}}}}}}}}
            .dump();
    auto seed_resp = client.send(seed_req);
    ASSERT_TRUE(seed_resp.has_value());
    EXPECT_EQ(seed_resp->status, 200);

    const std::string pcf_token = fetch_token(client, "npcf-smpolicycontrol");
    ASSERT_FALSE(pcf_token.empty());

    sbi_core::http2::ClientRequest create_req;
    create_req.method = "POST";
    create_req.url = pcf_url;
    create_req.headers.emplace("content-type", "application/json");
    create_req.headers.emplace("authorization", "Bearer " + pcf_token);
    create_req.body =
        json{
            {"supi", supi},
            {"pduSessionId", 1},
            {"pduSessionType", "IPV4"},
            {"dnn", "internet"},
            {"notificationUri", "https://example.com/notify"},
            {"sliceInfo", {{"sst", 1}, {"sd", "000001"}}},
        }
            .dump();
    auto create_resp = client.send(create_req);
    ASSERT_TRUE(create_resp.has_value());
    // Real fail-open: CHF being unreachable does not fail the SM Policy request itself.
    EXPECT_EQ(create_resp->status, 201);
    auto decision = json::parse(create_resp->body);
    EXPECT_TRUE(decision.contains("sessRules"));

    kill(pcf_pid, SIGTERM);
    waitpid(pcf_pid, nullptr, 0);
    kill(udr_pid, SIGTERM);
    waitpid(udr_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}
