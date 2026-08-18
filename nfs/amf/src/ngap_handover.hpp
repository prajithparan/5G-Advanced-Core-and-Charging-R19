#pragma once

#include <cstdint>
#include <ngap_core/sctp_socket.hpp>

#include "amf_ue_id_index_store.hpp"
#include "gnb_association_registry.hpp"
#include "ngap_task.hpp"
#include "ue_security_context_store.hpp"

extern "C" {
#include <InitiatingMessage.h>
}

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #100, ADR-0095/ADR-0096): real N2-based
// handover (HandoverRequired/.../HandoverNotify) -- split into its own translation unit from
// ngap_task.cpp for a real, non-cosmetic reason: this repo's CI (`-DCMAKE_BUILD_TYPE=Debug
// -D5GC_ENABLE_ASAN=ON`) was consistently OOM-killed by the GitHub-hosted free runner exactly
// while compiling ngap_task.cpp (confirmed via the runner log: the build was interrupted ~60s
// after ngap_task.cpp.o started, reproducibly, across multiple reruns) -- that single translation
// unit had grown very large (2600+ lines) and, under ASan instrumentation + full debug info, its
// peak compiler memory use is a real, disclosed CI resource constraint, not a code-correctness
// bug. Splitting the handover-specific code (genuinely the largest single addition this session)
// into its own TU reduces peak-per-file compiler memory without changing any behavior.

namespace amf::ngap {

// Real Handover Preparation (TS 38.413 §8.4.2) -- decodes HandoverRequired (real cold lookup via
// amf_ue_id_index/ue_security_contexts, not association-local state, see ngap_handover.cpp's own
// header comment), relays a real HandoverRequest to the target gNB via gnb_associations, and
// replies to source_assoc with HandoverCommand/HandoverPreparationFailure.
void handle_handover_required(ngap_core::SctpSocket& source_assoc,
                              UeSecurityContextStore& ue_security_contexts,
                              amf::AmfUeIdIndexStore& amf_ue_id_index,
                              amf::ngap::GnbAssociationRegistry& gnb_associations,
                              std::uint8_t amf_region_id,
                              std::uint16_t amf_set_id,
                              std::uint8_t amf_pointer,
                              const InitiatingMessage_t& msg);

// Real Handover Notification (TS 38.413 §8.4.4) -- decodes HandoverNotify (arrives on the
// TARGET association's own thread), sends a real AMF-initiated UEContextReleaseCommand to the
// source, then re-points ue_ngap_registry's entry for this UE to target_assoc.
void handle_handover_notify(ngap_core::SctpSocket& target_assoc,
                            NgapUeRegistry& ue_ngap_registry,
                            UeSecurityContextStore& ue_security_contexts,
                            AmfUeIdIndexStore& amf_ue_id_index,
                            const InitiatingMessage_t& msg);

} // namespace amf::ngap
