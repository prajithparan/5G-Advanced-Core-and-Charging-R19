// Drives nrf/udr/udm as real, separate OS processes to exercise UDM's Nudm_SDM group C2
// gap-closure (ADR-0233, docs/CAPABILITY_GAP_ANALYSIS.md's own UDM audit):
// GetTimeSyncSubscriptionData (GET /{supi}/time-sync-data) and GetRangingSlPosData (GET
// /{supi}/ranging-slpos-data). Real, disclosed correction of ADR-0230's own scoping note: UDR
// already had both resources fully live (ADR-0131/ADR-0136) -- this closes the UDM-side wiring
// only, no new UDR work needed.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "TS26510_CommonData_grp.hpp"
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
               "&targetNfType=UDM";
    auto resp = client.send(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

} // namespace

TEST(UdmSdmGapClosure233Integration, TimeSyncAndRangingSlPosReturnRealUdrSeededData) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t udr_pid = spawn(UDR_PATH);
    ASSERT_GT(udr_pid, 0) << "failed to fork udr";

    auto client = make_client();
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7781/nudr-dr/v2/subscription-data/nonexistent", 50))
        << "udr never became reachable";

    const pid_t udm_pid = spawn(UDM_PATH);
    ASSERT_GT(udm_pid, 0) << "failed to fork udm";
    ASSERT_TRUE(wait_reachable(
        client,
        "https://127.0.0.1:7780/nudm-uecm/v1/nonexistent/registrations/amf-3gpp-access",
        50))
        << "udm never became reachable";

    const std::string token = fetch_token(client, "nudm-sdm");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    // Real seeded test subscriber (nfs/udr/src/main.cpp's own startup seed, ADR-0131/ADR-0136).
    const std::string supi = "imsi-999700000000001";
    const std::string base = "https://127.0.0.1:7780/nudm-sdm/v2/" + supi;

    {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = base + "/time-sync-data";
        req.headers.emplace("authorization", "Bearer " + token);
        auto resp = client.send(req);
        ASSERT_TRUE(resp.has_value());
        ASSERT_EQ(resp->status, 200) << resp->body;
        const auto result = json::parse(resp->body).get<sbi_gen::TimeSyncSubscriptionData>();
        ASSERT_TRUE(result.afReqAuthorizations.gptpAllowedInfoList.has_value());
        ASSERT_EQ(result.afReqAuthorizations.gptpAllowedInfoList->size(), 1u);
        EXPECT_EQ((*result.afReqAuthorizations.gptpAllowedInfoList)[0].gptpAllowed, true);
        ASSERT_EQ(result.serviceIds.size(), 1u);
        EXPECT_EQ(result.serviceIds[0].reference, "ts-service-1");
    }
    {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = base + "/ranging-slpos-data";
        req.headers.emplace("authorization", "Bearer " + token);
        auto resp = client.send(req);
        ASSERT_TRUE(resp.has_value());
        ASSERT_EQ(resp->status, 200) << resp->body;
        const auto result = json::parse(resp->body).get<sbi_gen::RangingSlPosSubscriptionData>();
        ASSERT_TRUE(result.rangingSlPosAuth.has_value());
        ASSERT_TRUE(result.rangingSlPosAuth->rgSlPosPc5Auth.has_value());
        EXPECT_EQ(result.rangingSlPosAuth->rgSlPosPc5Auth->value, "AUTHORIZED");
    }

    // A genuinely unseeded SUPI correctly 404s on both.
    for (const std::string path : {"time-sync-data", "ranging-slpos-data"}) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = "https://127.0.0.1:7780/nudm-sdm/v2/imsi-999999999999999/" + path;
        req.headers.emplace("authorization", "Bearer " + token);
        auto resp = client.send(req);
        ASSERT_TRUE(resp.has_value()) << path;
        EXPECT_EQ(resp->status, 404) << path;
    }

    kill(udm_pid, SIGTERM);
    waitpid(udm_pid, nullptr, 0);
    kill(udr_pid, SIGTERM);
    waitpid(udr_pid, nullptr, 0);
    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}
