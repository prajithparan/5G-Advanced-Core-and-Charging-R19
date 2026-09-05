// ADR-0280 (P15, delivered under P4.12): per-protocol TPS spike protection.
//
// Two levels, because they prove different things:
//
//  1. TokenBucket semantics, deterministically -- a burst is admitted, the ceiling is enforced,
//     and capacity comes back over time. No processes, no timing luck.
//  2. A REAL NF driven past its ceiling over real HTTP/2 + mTLS, asserting that it sheds with the
//     status the spec defines (503 + ProblemDetails, TS29571_CommonData.yaml), that the shed
//     carries a Retry-After, and -- the part that actually matters -- that the NF is still
//     serving afterwards. A limiter that protected nothing, or that wedged the NF, would pass a
//     "did we get a 503" check and fail this one.
//
// Honest scope, stated here rather than implied: this validates the mechanism under a small,
// deliberate overload from one client. It is NOT a load campaign, and P4.12's "validated under
// load" is only partly discharged by it -- see ADR-0280.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/rate_limit.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

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

} // namespace

TEST(TpsSpikeProtection, TokenBucketAdmitsABurstThenEnforcesTheCeiling) {
    // 10/s sustained, 5 burst. The bucket starts full by design (see rate_limit.cpp).
    sbi_core::TokenBucket bucket(10.0, 5.0);

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(bucket.try_acquire()) << "the configured burst must be admitted, request " << i;
    }
    // Sixth in the same instant: the burst is spent and refill at 10/s has had no time to happen.
    EXPECT_FALSE(bucket.try_acquire()) << "beyond the burst, with no time elapsed, must shed";
    EXPECT_EQ(bucket.shed_count(), 1u);

    // Refill: 10 tokens/s means ~250 ms buys ~2.5 tokens, so at least two more get through. Not
    // asserting an exact count -- that would be a timing assertion, and this is a rate assertion.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_TRUE(bucket.try_acquire()) << "capacity must come back as time passes";
    EXPECT_TRUE(bucket.try_acquire());
}

TEST(TpsSpikeProtection, ZeroMeansUnlimitedSoAnNfThatHasNotOptedInIsUnchanged) {
    // The property that makes it safe to wire this into all 22 NFs at once.
    const auto limit = sbi_core::read_tps_limit(json::object());
    EXPECT_FALSE(limit.enabled());
    EXPECT_DOUBLE_EQ(limit.sustained_tps, 0.0);
}

TEST(TpsSpikeProtection, RealNfShedsWithA503AndKeepsServing) {
    // A deliberately tiny ceiling, set through the documented env override so no checked-in config
    // has to carry a test value. SpawnedProcess inherits this environment.
    ::setenv("SBI_MAX_TPS", "5", 1);
    ::setenv("SBI_TPS_BURST", "5", 1);
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    ::unsetenv("SBI_MAX_TPS");
    ::unsetenv("SBI_TPS_BURST");

    auto client = make_client();
    const std::string url =
        "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/00000000-0000-4000-8000-000000000000";
    ASSERT_TRUE(wait_reachable(client, url, 200)) << "nrf never became reachable";

    // Fire well past the ceiling as fast as one client can. Some of these are the reachability
    // probe's own leftovers being refilled, which is why the assertion is "some were shed", not
    // an exact count -- an exact count would be asserting on the scheduler.
    int shed = 0;
    int served = 0;
    bool saw_retry_after = false;
    for (int i = 0; i < 60; ++i) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        auto resp = client.send(req);
        ASSERT_TRUE(resp.has_value()) << "the NF stopped answering entirely at request " << i
                                      << " -- shedding must not wedge the server";
        if (resp->status == 503) {
            ++shed;
            if (resp->headers.find("retry-after") != resp->headers.end()) {
                saw_retry_after = true;
            }
            // The shed body is the spec's own shape, not a bare status.
            const auto problem = json::parse(resp->body);
            EXPECT_EQ(problem.at("status").get<int>(), 503);
            EXPECT_EQ(problem.at("title").get<std::string>(), "Service Unavailable");
        } else {
            ++served;
        }
    }

    EXPECT_GT(shed, 0) << "60 back-to-back requests against a 5 req/s ceiling shed nothing -- the "
                          "limiter is not in the dispatch path";
    EXPECT_TRUE(saw_retry_after) << "a shed response must tell the caller when to come back";

    // The point of shedding: the NF survives it. After the storm passes, it serves again.
    std::this_thread::sleep_for(std::chrono::seconds(2));
    sbi_core::http2::ClientRequest after;
    after.method = "GET";
    after.url = url;
    auto after_resp = client.send(after);
    ASSERT_TRUE(after_resp.has_value())
        << "the NF did not recover after the overload -- shedding that kills the NF is worse than "
           "no shedding at all";
    EXPECT_NE(after_resp->status, 503)
        << "capacity must refill once the spike is over; a permanently-shedding NF is an outage";
}
