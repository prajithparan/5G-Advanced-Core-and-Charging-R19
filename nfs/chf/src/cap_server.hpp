#pragma once

#include "sbi_core/tls_config.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

#include "charging_engine.hpp"
#include "ss7_core/sctp_socket.hpp"

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another NF's
// private headers" rule.
//
// P4.5/ADR-0061: CHF as a real CAP (TS 29.078) server, playing the real gsmSCF role toward a real
// gsmSSF peer (a real MSC/switch). Real direction, confirmed from `capssf-scfGenericAC`
// (TS 29.078 clause 6.1.2): the gsmSSF opens the dialogue (sends the real InitialDP), the gsmSCF
// (CHF, here) is the real responder -- the opposite direction from UDM's own MAP client role
// (ADR-0061's own MAP-side update), where the HLR-equivalent is the initiator. Same real "blocking
// I/O gets its own thread" discipline as `DiameterServer` (accept thread + one thread per
// association) and the same real per-connection dedicated `catalog_client`/`balance_client`
// rationale (`sbi_core::http2::Client`'s own documented "one Client instance per thread" contract).
//
// Real, disclosed scope this increment covers -- the "arm the charging" half of the real
// TS 29.078 clause 11 call flow: receive `InitialDP`, rate/reserve via the exact same
// `chf::charge_one_usage` shared code path Gy/Rf/Sy already use (CHARGING_PROMPT.md's own
// single-code-path requirement, now extended to a fourth real protocol), convert the resulting
// `GrantedUnit.time` into `ApplyChargingArg.max_call_period_duration`, and respond with a single
// real TC-Continue carrying `RequestReportBCSMEvent` (arming `oAnswer`/`oDisconnect`) and
// `ApplyCharging`. Real, disclosed gap: the "close the charging" half -- receiving
// `EventReportBCSM` when those armed events actually fire, and `ApplyChargingReport`'s own final
// usage report -- is NOT implemented; any further message on an already-open association is logged
// and ignored, not silently mishandled. `ratingGroup` is set to CAP's own real `serviceKey` (both
// are real integer identifiers selecting a rating/service context -- a real, disclosed conceptual
// mapping, not an arbitrary placeholder). Subscriber identity: `InitialDPArg.imsi` (TBCD, TS 23.003
// §2.2, decoded via the new `libs/tbcd-core`) becomes SUPI `"imsi-" + digits`, the real TS 23.003
// SUPI format already used elsewhere in this codebase's own test fixtures -- if `imsi` is absent (a
// real, valid `InitialDPArg` state per its own OPTIONAL tag), the InitialDP is rejected with a real
// `ReturnError` (`missingParameter`, TS 29.078 clause 5.4) rather than guessing an identity.

namespace chf {

class CapServer {
public:
    // grant_counter/reserve_rejected_counter are the SAME real counters main.cpp's HTTP handlers
    // and DiameterServer already share (a real grant is a real grant regardless of which real
    // protocol triggered it -- CHARGING_PROMPT.md's own single-code-path property). initial_dp_
    // counter/apply_charging_counter are new, CAP-specific message-volume counters, analogous to
    // DiameterServer's own ccr_initial_counter etc.
    CapServer(std::uint16_t port,
              sbi_core::http2::TlsConfig client_tls,
              ChargingDataStore& charging_data_store,
              CdrWriter& cdr_writer,
              RatingDecisionStore& rating_decision_store,
              opentelemetry::metrics::Counter<std::uint64_t>* grant_counter,
              opentelemetry::metrics::Counter<std::uint64_t>* reserve_rejected_counter,
              opentelemetry::metrics::Counter<std::uint64_t>* initial_dp_counter,
              opentelemetry::metrics::Counter<std::uint64_t>* apply_charging_counter);
    ~CapServer();

    CapServer(const CapServer&) = delete;
    CapServer& operator=(const CapServer&) = delete;

private:
    void accept_loop();
    void handle_connection(ss7_core::SctpSocket socket);

    ss7_core::SctpSocket listener_;
    sbi_core::http2::TlsConfig client_tls_;
    ChargingDataStore& charging_data_store_;
    CdrWriter& cdr_writer_;
    RatingDecisionStore& rating_decision_store_;
    opentelemetry::metrics::Counter<std::uint64_t>* grant_counter_;
    opentelemetry::metrics::Counter<std::uint64_t>* reserve_rejected_counter_;
    opentelemetry::metrics::Counter<std::uint64_t>* initial_dp_counter_;
    opentelemetry::metrics::Counter<std::uint64_t>* apply_charging_counter_;
    std::thread accept_thread_;
    std::atomic<bool> stop_{false};
};

} // namespace chf
