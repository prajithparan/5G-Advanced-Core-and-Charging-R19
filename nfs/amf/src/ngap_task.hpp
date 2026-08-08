#pragma once

#include "ue_context_store.hpp"

#include <string>

// AMF's NGAP/N2 termination (TS 38.413) + minimal NAS-5GS (TS 24.501), per docs/DECISIONS.md's
// staged NGAP/NAS plan (Stage 1: NG Setup; Stage 2: InitialUEMessage -> RegistrationRequest ->
// real AUSF call -> AuthenticationRequest). Runs on its own dedicated thread doing blocking SCTP
// I/O, never on the HTTP/2 server's io_context -- the same "blocking transport gets its own
// thread" discipline ADR-0006 already established for run_nrf_lifecycle, extended to SCTP by
// ADR-0030's libs/ngap-core.

namespace amf::ngap {

// Blocks forever: binds SCTP on address:port, accepts gNB associations, and handles NGAP PDUs for
// each one -- making real SBI calls to AUSF (Stage 2/3) and, once registration completes, to PCF
// (Stage 5, storing the resulting PolicyAssociation in `ue_contexts`). amf_instance_id and
// nrf_base are needed to build those clients' own OAuth2Client. Call from a dedicated std::thread.
// `ue_contexts` must outlive this call (it runs forever) -- same shared-by-reference-across-
// threads convention as main()'s other stores already use with the HTTP/2 server's route handlers.
void run_ngap_lifecycle(const std::string& bind_address, unsigned short bind_port,
                        const std::string& amf_instance_id, const std::string& nrf_base,
                        UeContextStore& ue_contexts);

} // namespace amf::ngap
