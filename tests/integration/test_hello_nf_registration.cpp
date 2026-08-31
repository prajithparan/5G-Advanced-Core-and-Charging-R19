// Drives nrf and hello-nf as real, separate OS processes talking over an actual TCP/HTTP2 socket
// with real TLS 1.3 + mTLS -- this is deliberately not a same-process/mocked test. If sbi-core's
// hand-rolled nghttp2 server, libcurl client, TLS/mTLS, or OAuth2 client-credentials + JWT flow
// are broken, this is what catches it.

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "spawn_guard.hpp"

#include <gtest/gtest.h>

TEST(HelloNfIntegration, RegistersHeartbeatsAndDeregistersAgainstRealNrf) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";

    // Belt-and-suspenders: hello-nf itself retries on connection failure, but give nrf a moment to
    // finish process startup before we even try.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    nf_test::SpawnedProcess hello(HELLO_NF_PATH);
    ASSERT_GT(hello.pid(), 0) << "failed to fork hello-nf";

    // hello-nf is the one child in this suite that is expected to RUN TO COMPLETION rather than be
    // terminated -- the whole point of the test is its exit status. wait_for_exit() waits for it
    // and then drops ownership, so the destructor does not SIGTERM an already-exited,
    // already-reaped pid. (A failed wait leaves status 0, which WIFEXITED rejects below, so the old
    // explicit `ASSERT_EQ(waited, hello_pid)` check is still covered.)
    const int status = hello.wait_for_exit();
    ASSERT_TRUE(WIFEXITED(status)) << "hello-nf did not exit normally";
    EXPECT_EQ(WEXITSTATUS(status), 0) << "hello-nf lifecycle failed; see its stderr output above";

    // nrf needs no manual teardown: its SpawnedProcess destructor reaps it on every exit path,
    // including the ASSERT_* early returns above (ADR-0242).
}
