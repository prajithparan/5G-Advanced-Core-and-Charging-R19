// sbi-loadgen -- a real load-generation harness for this project's own SBI network functions.
//
// Why this exists: ADR-0049 made commercialization a mandate ("performance must EXCEED free5GC"),
// and stated honestly that ZERO benchmarking of any kind had ever been performed here. ADR-0238
// then selected TS 28.552 + TS 28.554 as the measurement framework and listed the concrete steps
// in order; this binary is step (3), "build a real load-generation harness (Phase 8's planned
// synthetic traffic generator)". Steps (1) and (4) are NOT done by this binary -- see the honest
// scope statement below.
//
// What this measures, precisely (stated up front because a benchmark that hides its own method is
// worth nothing):
//
//   * CLOSED-LOOP by default. Each worker thread sends one request, waits for the response, then
//     sends the next. Offered load is therefore `concurrency` outstanding requests, NOT a fixed
//     arrival rate. This measures latency-under-a-given-concurrency and the throughput that
//     results, and it is consequently subject to COORDINATED OMISSION: when the server stalls,
//     workers stall with it and simply stop issuing requests, so recorded latencies
//     under-represent what a fixed-rate client would have seen.
//   * OPEN-LOOP with `--rate` (ADR-0246). Request i is due at start + i/rate regardless of
//     whether earlier responses have returned, and latency is measured from that INTENDED due
//     time rather than from the moment a worker actually became free -- which is precisely the
//     coordinated-omission correction: queueing delay caused by a busy pool lands inside the
//     reported latency instead of vanishing because no request was issued. `--concurrency` then
//     sizes the worker pool servicing the schedule, not the offered load, and the count of
//     slots issued late is reported so an unachievable target rate is visible rather than
//     silently reinterpreted as a lower one.
//   * Latency is measured around `Client::send()` -- the exact production client path every NF
//     uses for outbound SBI calls, including ADR-0241's handle pool, real TLS 1.3 + mTLS, and
//     real HTTP/2. It is NOT a synthetic socket benchmark.
//
// What this deliberately does NOT do:
//   * It does not map anything onto TS 28.552 counter families or TS 28.554 KPIs. That is
//     ADR-0238's step (1), and TS 28.552/28.554 are NOT vendored in specs/ -- inventing counter
//     names to fill that gap is exactly the failure mode CLAUDE.md forbids. Blocked on real spec
//     material, flagged, not faked.
//   * It makes no comparison against free5GC or anything else. That is ADR-0238's step (4) and
//     requires the mapping from step (1) first. No performance claim is made by this file.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/tls_config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string url;
    std::string method = "GET";
    std::string body;
    std::string body_file;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
    int concurrency = 1;
    int duration_sec = 10;
    std::int64_t max_requests = 0; // 0 = unbounded, bounded by duration instead
    int warmup_sec = 0;
    double rate_rps = 0.0; // > 0 selects OPEN-LOOP mode; 0 keeps the closed-loop default
    std::string json_out;
};

// Per-worker results, kept thread-local and merged only at the end: a shared latency vector under
// a mutex would itself become a contention point and distort the very number being measured.
struct WorkerResult {
    std::vector<std::uint64_t> latencies_us; // only requests completed after warmup
    std::map<long, std::int64_t> status_counts;
    std::int64_t transport_errors = 0;
    std::string first_error;
};

void print_usage() {
    std::cout << R"(sbi-loadgen -- closed-loop load generator for this project's SBI NFs

Required:
  --url <https://...>        Target SBI endpoint (https only -- the client has no cleartext path)
  --cert <path>              Client certificate (PEM) for mTLS
  --key <path>               Client private key (PEM)
  --ca <path>                CA bundle used to verify the server

Optional:
  --method <VERB>            HTTP method (default GET)
  --body <string>            Request body literal
  --body-file <path>         Request body read from a file (overrides --body)
  --header "K: V"            Extra request header; repeatable
  --concurrency <n>          Worker threads = outstanding requests (default 1)
  --duration <sec>           Measurement window after warmup (default 10)
  --requests <n>             Stop after n completed requests instead of on duration
  --warmup <sec>             Discard results for this long before measuring (default 0)
  --rate <req/s>             OPEN-LOOP mode: issue on a fixed schedule at this arrival rate
  --json <path>              Also write the summary as JSON
  -h, --help                 This text

Load model: without --rate, load is CLOSED-LOOP -- `concurrency` outstanding requests, reporting
latency at that concurrency rather than at a fixed arrival rate, and subject to coordinated
omission. With --rate, load is OPEN-LOOP: request i is due at start + i/rate regardless of whether
earlier responses have returned, and latency is measured from that INTENDED time, which is the
standard coordinated-omission correction. In open-loop mode --concurrency is the size of the
worker pool servicing the schedule, not the offered load.
)";
}

bool parse_args(int argc, char** argv, Options& opts, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                error = std::string("missing value for ") + name;
                return {};
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        } else if (arg == "--url") {
            opts.url = next("--url");
        } else if (arg == "--method") {
            opts.method = next("--method");
        } else if (arg == "--body") {
            opts.body = next("--body");
        } else if (arg == "--body-file") {
            opts.body_file = next("--body-file");
        } else if (arg == "--cert") {
            opts.cert_path = next("--cert");
        } else if (arg == "--key") {
            opts.key_path = next("--key");
        } else if (arg == "--ca") {
            opts.ca_path = next("--ca");
        } else if (arg == "--concurrency") {
            opts.concurrency = std::atoi(next("--concurrency").c_str());
        } else if (arg == "--duration") {
            opts.duration_sec = std::atoi(next("--duration").c_str());
        } else if (arg == "--requests") {
            opts.max_requests = std::atoll(next("--requests").c_str());
        } else if (arg == "--warmup") {
            opts.warmup_sec = std::atoi(next("--warmup").c_str());
        } else if (arg == "--rate") {
            opts.rate_rps = std::atof(next("--rate").c_str());
        } else if (arg == "--json") {
            opts.json_out = next("--json");
        } else if (arg == "--header") {
            const std::string header = next("--header");
            const auto colon = header.find(':');
            if (colon == std::string::npos) {
                error = "header must be \"Key: Value\": " + header;
                return false;
            }
            std::string value = header.substr(colon + 1);
            const auto first = value.find_first_not_of(" \t");
            value = first == std::string::npos ? std::string{} : value.substr(first);
            opts.headers.emplace_back(header.substr(0, colon), value);
        } else {
            error = "unknown argument: " + arg;
            return false;
        }
        if (!error.empty()) {
            return false;
        }
    }

    if (opts.url.empty() || opts.cert_path.empty() || opts.key_path.empty() ||
        opts.ca_path.empty()) {
        error = "--url, --cert, --key and --ca are all required";
        return false;
    }
    if (opts.concurrency < 1) {
        error = "--concurrency must be >= 1";
        return false;
    }
    if (opts.rate_rps < 0.0) {
        error = "--rate must be > 0 (omit it for closed-loop mode)";
        return false;
    }
    if (opts.duration_sec < 1 && opts.max_requests <= 0) {
        error = "--duration must be >= 1 (or use --requests)";
        return false;
    }
    if (!opts.body_file.empty()) {
        std::ifstream in(opts.body_file, std::ios::binary);
        if (!in) {
            error = "cannot read --body-file: " + opts.body_file;
            return false;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        opts.body = buf.str();
    }
    return true;
}

// Nearest-rank percentile on the sorted sample set (the simple, unambiguous definition: the
// smallest value at or below which at least p% of observations fall). No interpolation -- an
// interpolated percentile invents a latency no request actually experienced.
std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) {
        return 0;
    }
    const auto rank =
        static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    const std::size_t index = rank == 0 ? 0 : rank - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

std::string format_us(std::uint64_t us) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << static_cast<double>(us) / 1000.0 << " ms";
    return out.str();
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    std::string error;
    if (!parse_args(argc, argv, opts, error)) {
        std::cerr << "sbi-loadgen: " << error << "\n\n";
        print_usage();
        return 2;
    }

    // One shared Client, exactly as a real NF holds one per downstream peer: this exercises
    // ADR-0241's real handle pool under genuine concurrency rather than sidestepping it by giving
    // every thread its own client, which would measure a configuration no NF actually runs.
    sbi_core::http2::Client client(
        sbi_core::http2::TlsConfig{opts.cert_path, opts.key_path, opts.ca_path});

    sbi_core::http2::ClientRequest request;
    request.method = opts.method;
    request.url = opts.url;
    request.body = opts.body;
    for (const auto& [key, value] : opts.headers) {
        request.headers.emplace(key, value);
    }

    const auto start_wall = std::chrono::steady_clock::now();
    const auto warmup_end = start_wall + std::chrono::seconds(opts.warmup_sec);
    const auto measure_end = warmup_end + std::chrono::seconds(opts.duration_sec);

    std::atomic<std::int64_t> completed{0}; // measured requests only, for the --requests bound
    // Open-loop schedule cursor: every worker draws the next slot, so request i is due at
    // measure-start + i/rate no matter how slow earlier responses were.
    std::atomic<std::int64_t> next_slot{0};
    std::atomic<std::int64_t> slots_late{0}; // slots already overdue when a worker picked them up
    std::vector<WorkerResult> results(static_cast<std::size_t>(opts.concurrency));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(opts.concurrency));

    std::cerr << "sbi-loadgen: " << opts.method << " " << opts.url
              << " concurrency=" << opts.concurrency;
    if (opts.warmup_sec > 0) {
        std::cerr << " warmup=" << opts.warmup_sec << "s";
    }
    if (opts.max_requests > 0) {
        std::cerr << " requests=" << opts.max_requests;
    } else {
        std::cerr << " duration=" << opts.duration_sec << "s";
    }
    std::cerr << "\n";

    for (int w = 0; w < opts.concurrency; ++w) {
        workers.emplace_back([&, w]() {
            WorkerResult& out = results[static_cast<std::size_t>(w)];
            const bool open_loop = opts.rate_rps > 0.0;
            for (;;) {
                // In OPEN-LOOP mode the reference instant is the slot's scheduled due time, not
                // the moment a worker happened to become free. Measuring from `due` is exactly
                // the coordinated-omission correction: if every worker is busy when a slot comes
                // due, that queueing delay lands in the recorded latency instead of vanishing
                // because no request was issued.
                std::chrono::steady_clock::time_point due{};
                if (open_loop) {
                    const std::int64_t slot = next_slot.fetch_add(1, std::memory_order_relaxed);
                    const double offset_sec = static_cast<double>(slot) / opts.rate_rps;
                    due = start_wall +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(offset_sec));
                    if (due >= measure_end) {
                        return;
                    }
                    const auto now_before = std::chrono::steady_clock::now();
                    if (now_before < due) {
                        std::this_thread::sleep_until(due);
                    } else if (now_before > due) {
                        slots_late.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                const auto now = std::chrono::steady_clock::now();
                const bool measuring = open_loop ? due >= warmup_end : now >= warmup_end;
                if (opts.max_requests > 0) {
                    if (completed.load(std::memory_order_relaxed) >= opts.max_requests) {
                        return;
                    }
                } else if (!open_loop && now >= measure_end) {
                    return;
                }

                const auto sent_at = std::chrono::steady_clock::now();
                const auto reference = open_loop ? due : sent_at;
                const auto response = client.send(request);
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - reference)
                                         .count();

                if (!measuring) {
                    continue; // warmup traffic is real traffic, just not recorded
                }
                if (response.has_value()) {
                    out.latencies_us.push_back(static_cast<std::uint64_t>(elapsed));
                    ++out.status_counts[response->status];
                } else {
                    ++out.transport_errors;
                    if (out.first_error.empty()) {
                        out.first_error = response.error();
                    }
                }
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    const auto measured_wall = std::chrono::steady_clock::now() - warmup_end;

    std::vector<std::uint64_t> all;
    std::map<long, std::int64_t> statuses;
    std::int64_t transport_errors = 0;
    std::string first_error;
    for (const auto& r : results) {
        all.insert(all.end(), r.latencies_us.begin(), r.latencies_us.end());
        for (const auto& [code, count] : r.status_counts) {
            statuses[code] += count;
        }
        transport_errors += r.transport_errors;
        if (first_error.empty()) {
            first_error = r.first_error;
        }
    }
    std::sort(all.begin(), all.end());

    const double seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(measured_wall).count();
    const std::int64_t responses = static_cast<std::int64_t>(all.size());
    const double throughput = seconds > 0.0 ? static_cast<double>(responses) / seconds : 0.0;
    const std::uint64_t mean_us =
        all.empty() ? 0
                    : static_cast<std::uint64_t>(std::accumulate(all.begin(), all.end(), 0.0) /
                                                 static_cast<double>(all.size()));

    std::cout << "\n=== sbi-loadgen results ===\n";
    std::cout << "target            : " << opts.method << " " << opts.url << "\n";
    if (opts.rate_rps > 0.0) {
        std::cout << "load model        : open-loop, target " << opts.rate_rps
                  << " req/s (latency measured from each slot's scheduled time)\n";
        std::cout << "worker pool       : " << opts.concurrency << "\n";
    } else {
        std::cout << "load model        : closed-loop (subject to coordinated omission)\n";
        std::cout << "concurrency       : " << opts.concurrency << "\n";
    }
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    std::cout << "measured window   : " << seconds << " s\n";
    std::cout << "responses         : " << responses << "\n";
    std::cout << "transport errors  : " << transport_errors;
    if (!first_error.empty()) {
        std::cout << " (first: " << first_error << ")";
    }
    std::cout << "\n";
    std::cout << "throughput        : " << throughput << " req/s\n";
    if (opts.rate_rps > 0.0) {
        const std::int64_t late = slots_late.load();
        std::cout << "slots issued late : " << late;
        if (late > 0) {
            // Not a tool bug: it means the target rate exceeded what the pool could service, and
            // the resulting queueing delay is already inside the latencies above.
            std::cout << " (pool could not keep up with the target rate -- the delay is included"
                         " in the latencies above, not hidden)";
        }
        std::cout << "\n";
    }
    if (!all.empty()) {
        std::cout << "latency min       : " << format_us(all.front()) << "\n";
        std::cout << "latency mean      : " << format_us(mean_us) << "\n";
        std::cout << "latency p50       : " << format_us(percentile(all, 50)) << "\n";
        std::cout << "latency p90       : " << format_us(percentile(all, 90)) << "\n";
        std::cout << "latency p95       : " << format_us(percentile(all, 95)) << "\n";
        std::cout << "latency p99       : " << format_us(percentile(all, 99)) << "\n";
        std::cout << "latency p99.9     : " << format_us(percentile(all, 99.9)) << "\n";
        std::cout << "latency max       : " << format_us(all.back()) << "\n";
    }
    std::cout << "status codes      :";
    for (const auto& [code, count] : statuses) {
        std::cout << " " << code << "=" << count;
    }
    std::cout << "\n";

    if (!opts.json_out.empty()) {
        std::ofstream json(opts.json_out);
        if (!json) {
            std::cerr << "sbi-loadgen: cannot write --json " << opts.json_out << "\n";
            return 1;
        }
        json << "{\n";
        json << "  \"target\": \"" << opts.method << " " << opts.url << "\",\n";
        json << "  \"load_model\": \"" << (opts.rate_rps > 0.0 ? "open-loop" : "closed-loop")
             << "\",\n";
        if (opts.rate_rps > 0.0) {
            json << "  \"target_rate_rps\": " << opts.rate_rps << ",\n";
            json << "  \"slots_issued_late\": " << slots_late.load() << ",\n";
        }
        json << "  \"concurrency\": " << opts.concurrency << ",\n";
        json << "  \"measured_seconds\": " << seconds << ",\n";
        json << "  \"responses\": " << responses << ",\n";
        json << "  \"transport_errors\": " << transport_errors << ",\n";
        json << "  \"throughput_rps\": " << throughput << ",\n";
        if (!all.empty()) {
            json << "  \"latency_us\": {\n";
            json << "    \"min\": " << all.front() << ",\n";
            json << "    \"mean\": " << mean_us << ",\n";
            json << "    \"p50\": " << percentile(all, 50) << ",\n";
            json << "    \"p90\": " << percentile(all, 90) << ",\n";
            json << "    \"p95\": " << percentile(all, 95) << ",\n";
            json << "    \"p99\": " << percentile(all, 99) << ",\n";
            json << "    \"p99_9\": " << percentile(all, 99.9) << ",\n";
            json << "    \"max\": " << all.back() << "\n";
            json << "  },\n";
        }
        json << "  \"status_codes\": {";
        bool first = true;
        for (const auto& [code, count] : statuses) {
            json << (first ? "" : ", ") << "\"" << code << "\": " << count;
            first = false;
        }
        json << "}\n";
        json << "}\n";
    }

    // A run that produced no successful response at all is a failed run, not a 0 req/s result.
    return responses > 0 ? 0 : 1;
}
