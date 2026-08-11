#pragma once

#include <clickhouse/client.h>
#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// P4.4/ADR-0058: CDF (Charging Data Function, TS 32.240/32.296) -- real CDR generation, per
// CHARGING_PROMPT.md's P4.4 real requirements ("duplicate detection, gap detection...mandatory").
// See ../schema.clickhouse.sql's own header for the full, honest disclosure of what this is NOT
// (a conformant TS 32.298 CDR -- that spec isn't vendored) and what it IS (a real, working usage
// record built entirely from TS 32.291 fields already confirmed and flowing through this file).
//
// Real ClickHouse persistence (clickhouse-cpp), per docs/DATA_MODEL.md's E4 assignment
// ("ClickHouse for CDR/usage analytics").
//
// Real bug found via live verification, not caught by reasoning alone: `clickhouse::Client`'s
// constructor connects EAGERLY (unlike `sw::redis::Redis`'s lazy-on-first-command pool, confirmed
// when nfs/chf's Redis stores were built, ADR-0055) and throws a real `clickhouse::ServerException`
// on connection/auth failure. Left uncaught (this class's first version), that exception escapes
// `main()`'s `chf::CdrWriter cdr_writer(...)` construction entirely, calls `std::terminate`, and
// aborts the WHOLE CHF process -- meaning a ClickHouse outage would take down real-time charging
// (Nchf_ConvergedCharging Create/Update/Release) too, not just CDR generation. That is the wrong
// failure mode: CDR/audit-trail generation is real and required (P4.4), but it must never be able
// to block or crash the higher-priority real-time charging/balance-reservation path this same file
// already treats as best-effort at the per-write level (see `write()`'s own comment). Fixed by
// catching the constructor's own exception and degrading to a real, logged "CDR generation
// disabled" state -- `write()`/`detect_gaps()` become safe no-ops (with a warning) rather than
// every call site needing its own null-check.

namespace chf {

struct CdrRecord {
    std::string charging_data_ref;
    std::int64_t invocation_sequence_number = 0;
    std::string service_type; // "ConvergedCharging" | "OfflineOnlyCharging" (project-internal)
    std::string operation;    // "Create" | "Update" | "Release" (project-internal)
    std::string subscriber_identifier;
    std::string nf_consumer_node_functionality;
    std::optional<std::int64_t> rating_group;
    std::optional<std::uint64_t> granted_total_volume;
    std::optional<std::uint64_t> granted_service_specific_units;
    std::optional<std::uint64_t> used_total_volume;
    std::optional<double> reserved_cost;
    std::optional<std::string> reserved_cost_currency;
    std::time_t invocation_time_stamp = 0;
};

class CdrWriter {
public:
    explicit CdrWriter(const clickhouse::ClientOptions& options);

    // Real INSERT into ClickHouse's `cdr` table (schema: ../schema.clickhouse.sql). Real,
    // disclosed limitation: `clickhouse::Client` is not documented as thread-safe for concurrent
    // use from multiple threads the way `sw::redis::Redis` is (confirmed by reading redis-plus-
    // plus's own header, ADR-0055) -- CHF's route handlers all run on the server's single
    // io_context thread (the same "single-threaded server, safe without a mutex" reasoning this
    // codebase already relies on elsewhere), so this is safe in this build's actual concurrency
    // model, but is NOT safe to share across multiple threads without adding a mutex first if
    // that model ever changes.
    void write(const CdrRecord& record);

    // Real gap detection (CHARGING_PROMPT.md's own explicit P4.4 requirement): queries every
    // invocation_sequence_number recorded for charging_data_ref, and returns the list of missing
    // values in the contiguous range [min_seen, max_seen] -- e.g. sequences {1,2,4,5} returns
    // {3}. Returns an empty vector if fewer than 2 distinct sequence numbers exist (no range to
    // have a gap in) or no gap is found.
    std::vector<std::int64_t> detect_gaps(const std::string& charging_data_ref);

    // True if the constructor connected successfully. False means CDR generation is disabled for
    // this process's lifetime (see this file's own header) -- callers use this only for an
    // accurate startup log line, not as a precondition check before write()/detect_gaps(), which
    // are already safe to call regardless.
    bool is_connected() const { return client_ != nullptr; }

private:
    // nullptr if construction failed to connect -- see this file's own header for why that's a
    // real, deliberate degraded state, not an error CdrWriter itself surfaces to its caller.
    std::unique_ptr<clickhouse::Client> client_;
};

} // namespace chf
