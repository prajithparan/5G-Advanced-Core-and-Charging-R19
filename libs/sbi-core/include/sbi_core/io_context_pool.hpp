#pragma once

#include <boost/asio/io_context.hpp>

// ADR-0239: real server-side request concurrency. Every NF's main() used to call ioc.run() once,
// single-threaded -- meaning the entire process, however many TCP connections or HTTP/2 streams
// were open, could only ever process one request at a time. Server::Connection is now
// strand-per-connection with off-strand handler dispatch (see http2_server.cpp), which makes it
// SAFE to drive the same io_context from multiple threads; this is the piece that actually does
// it, replacing a bare `ioc.run();` call.

namespace sbi_core {

// Runs `ioc` on a small pool of worker threads (the calling thread plus N-1 additional threads,
// N = std::thread::hardware_concurrency(), floored at 2 so single-core/undetectable-core-count
// environments still get at least one extra worker thread rather than silently degrading back to
// the old single-threaded behavior). Blocks until ioc runs out of work (or is stopped), same as a
// direct ioc.run() call would -- a drop-in replacement, just with real parallelism.
void run_multi_threaded(boost::asio::io_context& ioc);

} // namespace sbi_core
