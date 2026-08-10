#pragma once

#include <cstdint>
#include <mutex>
#include <ngap_core/sctp_socket.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "aka_crypto/kdf.hpp"
#include "ue_context_store.hpp"

// AMF's NGAP/N2 termination (TS 38.413) + minimal NAS-5GS (TS 24.501), per docs/DECISIONS.md's
// staged NGAP/NAS plan (Stage 1: NG Setup; Stage 2: InitialUEMessage -> RegistrationRequest ->
// real AUSF call -> AuthenticationRequest). Runs on its own dedicated thread doing blocking SCTP
// I/O, never on the HTTP/2 server's io_context -- the same "blocking transport gets its own
// thread" discipline ADR-0006 already established for run_nrf_lifecycle, extended to SCTP by
// ADR-0030's libs/ngap-core.

namespace amf::ngap {

// Thread-safe registry mapping a UE's SUPI to enough live state for the SBI HTTP/2 server thread
// (route handlers, e.g. Namf_Communication N1N2MessageTransfer) to deliver a secured downlink NAS
// message to that UE's live NGAP association -- a genuine cross-thread handoff, since each NGAP
// association runs on its own dedicated thread doing blocking SCTP I/O (ADR-0030) while the SBI
// server runs on Boost.Asio's io_context thread. ADR-0038.
//
// Holds a non-owning pointer to the association's SctpSocket, valid only while that association's
// handle_association() call is still running -- register_ue/unregister_ue bracket that lifetime.
// All access serialized through this class's own mutex; in this build's current
// single-UE-per-association scope (ADR-0031) the owning NGAP thread is always blocked in
// SctpSocket::receive() (not sending) by the time an N1N2MessageTransfer can arrive for that UE,
// so there is no real send/send race today -- the mutex exists for correctness regardless of
// timing, not to paper over an observed race.
class NgapUeRegistry {
public:
    struct Entry {
        ngap_core::SctpSocket* socket = nullptr;
        std::uint32_t amf_ue_id = 0;
        std::uint32_t ran_ue_id = 0;
        aka_crypto::NasIntKey knas_int{};
        aka_crypto::NasEncKey knas_enc{};
        std::uint32_t next_downlink_count = 0;
    };

    void register_ue(const std::string& supi, Entry entry);
    void unregister_ue(const std::string& supi);

    // Encodes (amf::nas::encode_dl_nas_transport) and sends a secured DlNasTransport carrying
    // n1_sm_container over the registered UE's live association, incrementing its downlink_count.
    // Returns false if no live association is registered for this SUPI.
    bool send_dl_nas_transport(const std::string& supi,
                               std::uint8_t pdu_session_id,
                               const std::vector<std::uint8_t>& n1_sm_container);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

// Blocks forever: binds SCTP on address:port, accepts gNB associations, and handles NGAP PDUs for
// each one -- making real SBI calls to AUSF (Stage 2/3) and, once registration completes, to PCF
// (Stage 5, storing the resulting PolicyAssociation in `ue_contexts`). amf_instance_id and
// nrf_base are needed to build those clients' own OAuth2Client. Call from a dedicated std::thread.
// `ue_contexts`/`ue_ngap_registry` must outlive this call (it runs forever) -- same
// shared-by-reference-across-threads convention as main()'s other stores already use with the
// HTTP/2 server's route handlers.
void run_ngap_lifecycle(const std::string& bind_address,
                        unsigned short bind_port,
                        const std::string& amf_instance_id,
                        const std::string& nrf_base,
                        UeContextStore& ue_contexts,
                        NgapUeRegistry& ue_ngap_registry);

} // namespace amf::ngap
