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

namespace {

pid_t spawn(const char* path) {
    const pid_t pid = fork();
    if (pid == 0) {
        nf_test::arm_parent_death_signal();
        execl(path, path, static_cast<char*>(nullptr));
        _exit(127); // only reached if execl fails
    }
    return pid;
}

} // namespace

TEST(HelloNfIntegration, RegistersHeartbeatsAndDeregistersAgainstRealNrf) {
    const pid_t nrf_pid = spawn(NRF_PATH);
    ASSERT_GT(nrf_pid, 0) << "failed to fork nrf";

    // Belt-and-suspenders: hello-nf itself retries on connection failure, but give nrf a moment to
    // finish process startup before we even try.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const pid_t hello_pid = spawn(HELLO_NF_PATH);
    ASSERT_GT(hello_pid, 0) << "failed to fork hello-nf";

    int status = 0;
    const pid_t waited = waitpid(hello_pid, &status, 0);
    ASSERT_EQ(waited, hello_pid);
    ASSERT_TRUE(WIFEXITED(status)) << "hello-nf did not exit normally";
    EXPECT_EQ(WEXITSTATUS(status), 0) << "hello-nf lifecycle failed; see its stderr output above";

    kill(nrf_pid, SIGTERM);
    waitpid(nrf_pid, nullptr, 0);
}
