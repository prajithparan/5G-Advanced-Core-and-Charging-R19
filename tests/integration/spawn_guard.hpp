#pragma once

// Task #170: stop timed-out tests from leaving orphaned NF processes squatting their ports.
//
// Every integration test here spawns real NF binaries (nrf, udr, udm, ...) with fork()+execl() and
// tears them down with SIGTERM+waitpid at the end of the test. That teardown is correct for every
// path the test binary itself controls -- but it cannot run when ctest kills the test binary with
// SIGKILL on --timeout. SIGKILL is not catchable, so no atexit handler, no RAII destructor, and no
// signal handler in this process gets a chance to reap the children. The result, hit repeatedly in
// practice: a timed-out run leaves nrf/udr alive holding their listen ports, and every subsequent
// run of any test fails with "Address already in use" until someone kills them by hand.
//
// The fix has to come from the kernel, because only the kernel observes the parent's death on the
// SIGKILL path. PR_SET_PDEATHSIG asks the kernel to deliver a signal to THIS process when its
// parent dies, for any reason including SIGKILL.
//
// Two portability facts this depends on, both real and both load-bearing:
//   - PR_SET_PDEATHSIG is cleared across fork(), which is why it must be set in the child rather
//     than inherited from the test binary.
//   - It SURVIVES execve() for ordinary binaries (it is reset only when exec'ing a setuid/setgid
//     or file-capability binary). The NF binaries are ordinary, so the disposition set here is
//     still in force once execl() has replaced this image with the NF.
//
// Known residual race, disclosed rather than engineered around: if the parent dies in the window
// between fork() returning and the first getppid() below, the re-check cannot detect it and the
// child survives. That window is microseconds wide, and its failure mode is exactly today's
// behaviour -- so this is a strict improvement, not a complete guarantee.
//
// Known scope limit, also disclosed: this ties an NF's lifetime to the TEST BINARY, not to the
// individual test case. A GoogleTest ASSERT_* that returns early from the middle of a test body
// still skips that test's own SIGTERM/waitpid teardown and leaks the NF until the binary exits;
// that is task #166's RAII-cleanup territory and is deliberately left open here. NFs started
// manually outside ctest are likewise unaffected.

#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nf_test {

// Call in the CHILD, after fork() and before exec. Only async-signal-safe calls are used
// (getppid/prctl/_exit), so this is legal in the child of a multithreaded parent.
inline void arm_parent_death_signal() {
    const pid_t parent_before = getppid();
    if (parent_before <= 1) {
        _exit(127); // already reparented -- the test binary is gone, so there is nothing to serve
    }
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent_before) {
        _exit(127); // parent died inside the fork/prctl window; the signal will never arrive
    }
}

// RAII wrapper for a spawned NF: SIGTERM + waitpid in the destructor, so teardown runs on EVERY
// exit path out of a test body.
//
// This closes the other half of the orphan problem (task #166). arm_parent_death_signal() above
// covers the test BINARY dying; it does nothing for a test that returns early while the binary
// keeps running -- and a GoogleTest ASSERT_* is exactly that: a `return` from the middle of the
// test body. A test that spawns NFs and tears them down with a kill/waitpid at the END of the
// body therefore skips that teardown entirely the moment any ASSERT_* fires, leaking every NF it
// started and leaving them holding their listen ports for the rest of the run. Declaring these as
// locals fixes it structurally: destruction is guaranteed, and happens in reverse declaration
// order, which matches the usual spawn order (nrf, udr, udm -> torn down udm, udr, nrf).
//
// Same real precedent this generalises: test_udr_ondatachange_webhook.cpp introduced this exact
// pattern after a real early-returning ASSERT there orphaned nrf/udr during development.
class SpawnedProcess {
public:
    explicit SpawnedProcess(const char* path) {
        pid_ = fork();
        if (pid_ == 0) {
            arm_parent_death_signal();
            execl(path, path, static_cast<char*>(nullptr));
            _exit(127); // only reached if execl fails
        }
    }

    ~SpawnedProcess() {
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
        }
    }

    SpawnedProcess(const SpawnedProcess&) = delete;
    SpawnedProcess& operator=(const SpawnedProcess&) = delete;

    pid_t pid() const { return pid_; }

private:
    pid_t pid_ = -1;
};

} // namespace nf_test
