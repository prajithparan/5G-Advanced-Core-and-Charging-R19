// Proves ADR-0239's real request concurrency: an in-process sbi_core::http2::Server (this is a
// property of the shared library itself, not any one NF -- no NF subprocess needed, same
// in-process-server idiom test_udr_ondatachange_webhook.cpp already established) with a
// deliberately slow handler, driven by sbi_core::run_multi_threaded on its own io_context instead
// of a bare single-threaded ioc.run(). N concurrent client threads, each with its OWN Client
// instance (its own independent TCP/TLS connection -- this project's Client is synchronous and
// one-request-at-a-time per instance, so this test proves the "different connections handled in
// parallel" dimension of Phase 1; it cannot exercise the "off-strand dispatch also helps
// multiplexed streams on ONE shared connection" dimension, since no caller in this codebase can
// yet have two requests in flight on the same Client instance -- a real, disclosed limitation, see
// docs/DECISIONS.md ADR-0239), each hitting the same slow route. If real concurrency is happening,
// N concurrent slow requests complete in roughly one request's worth of wall-clock time, not N
// times that (which is what the old single-threaded server would have produced).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/io_context_pool.hpp"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr auto kHandlerDelay = std::chrono::milliseconds(300);
constexpr int kConcurrentRequests = 6;

// RAII wrapper running ioc on sbi_core::run_multi_threaded's own worker pool -- same shape as
// test_udr_ondatachange_webhook.cpp's own IoContextThread, but multi-threaded instead of a bare
// ioc.run(), since that's specifically what this test needs to prove.
class MultiThreadedIoContextRunner {
public:
    explicit MultiThreadedIoContextRunner(boost::asio::io_context& ioc)
        : ioc_(ioc), thread_([&ioc] { sbi_core::run_multi_threaded(ioc); }) {}

    ~MultiThreadedIoContextRunner() {
        ioc_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    MultiThreadedIoContextRunner(const MultiThreadedIoContextRunner&) = delete;
    MultiThreadedIoContextRunner& operator=(const MultiThreadedIoContextRunner&) = delete;

private:
    boost::asio::io_context& ioc_;
    std::thread thread_;
};

} // namespace

TEST(SbiCoreConcurrencyIntegration, ConcurrentRequestsOnDifferentConnectionsRunInParallel) {
    boost::asio::io_context ioc;
    sbi_core::http2::TlsConfig server_tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Server server(ioc, "127.0.0.1", 0, server_tls);
    server.add_route("GET", "/slow", [](const sbi_core::http2::Request&) {
        std::this_thread::sleep_for(kHandlerDelay);
        return sbi_core::http2::Response::json(200, R"({"ok":true})");
    });
    server.start();
    const auto port = server.local_port();

    MultiThreadedIoContextRunner runner(ioc);

    std::vector<std::thread> clients;
    // std::vector<char>, not std::vector<bool>: the latter's packed-bitset representation means
    // concurrent writes to DIFFERENT elements from different threads are a real data race (they
    // can share the same underlying word) -- a real bug this test itself had, caught by TSan
    // during this ADR's own verification, not a theoretical concern. std::vector<char> has no
    // such trap; each element is its own byte.
    std::vector<char> results(static_cast<std::size_t>(kConcurrentRequests), 0);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kConcurrentRequests; ++i) {
        clients.emplace_back([i, port, &results] {
            sbi_core::http2::TlsConfig client_tls{
                .cert_path = CERTS_DIR "/hello-nf/cert.pem",
                .key_path = CERTS_DIR "/hello-nf/key.pem",
                .ca_path = CERTS_DIR "/ca/ca.crt",
            };
            sbi_core::http2::Client client(std::move(client_tls));
            sbi_core::http2::ClientRequest req;
            req.method = "GET";
            req.url = "https://127.0.0.1:" + std::to_string(port) + "/slow";
            auto resp = client.send(req);
            results[static_cast<std::size_t>(i)] =
                (resp.has_value() && resp->status == 200) ? 1 : 0;
        });
    }
    for (auto& t : clients) {
        t.join();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    for (char ok : results) {
        EXPECT_TRUE(ok != 0);
    }

    // Real proof of concurrency: with the old single-threaded server, kConcurrentRequests
    // sequential kHandlerDelay requests would take at least kConcurrentRequests * kHandlerDelay
    // (fully serialized). Real parallelism completes them in well under that. The bound is
    // deliberately generous (70% of the fully-serial time, not close to 1x) so it tolerates
    // ThreadSanitizer/AddressSanitizer's real per-operation instrumentation overhead (this project
    // runs both in CI) while still cleanly distinguishing genuine concurrency from the
    // fully-serial behavior it replaces -- a build with no real parallelism would land at or above
    // the fully-serial bound regardless of sanitizer overhead, not just under this 70% cutoff.
    const auto fully_serial_bound = kHandlerDelay * kConcurrentRequests;
    EXPECT_LT(elapsed, fully_serial_bound * 7 / 10)
        << "requests did not run concurrently (took "
        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms for "
        << kConcurrentRequests << " x " << kHandlerDelay.count() << "ms requests)";
}
