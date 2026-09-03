// ADR-0264: the first tests to drive AMF's NGAP handover handlers over a REAL SCTP association.
//
// Everything in the N2 handover chain (ADR-0090/0095/0096/0248/0249/0258/0261) has until now been
// verified by construction and review only. UERANSIM (ADR-0016) cannot help: its gNB implements no
// handover procedure at all. `nf_test::NgapTestGnb` is the real gNB that closes that, and these
// are the first assertions it carries.
//
// Scope, stated rather than implied. These cover the paths reachable WITHOUT a registered UE:
// NGSetup, HandoverRequired for an unknown UE, and HandoverCancel for an unknown UE. The full
// relay (HandoverRequired -> SMF -> HandoverRequest -> target gNB -> HandoverCommand) needs a real
// UE security context in AMF, which means driving NAS registration from this driver -- the next
// increment, deliberately not faked here.

#include <cstdint>

#include "ngap_test_gnb.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {

using nf_test::NgapTestGnb;

// config/amf.json's own ngap_bind_address/ngap_bind_port.
constexpr const char* kAmfNgapAddress = "127.0.0.5";
constexpr std::uint16_t kAmfNgapPort = 38412;

constexpr std::uint32_t kSourceGnbId = 0x000011;
constexpr std::uint32_t kTargetGnbId = 0x000022;

// An AMF-UE-NGAP-ID no registration ever allocated, so the cold lookup through amf_ue_id_index
// deliberately misses.
constexpr std::uint64_t kUnknownAmfUeId = 987654;
constexpr std::uint32_t kRanUeId = 4242;

struct Lab {
    nf_test::SpawnedProcess nrf{NRF_PATH};
    nf_test::SpawnedProcess amf{AMF_PATH};
};

} // namespace

TEST(AmfNgapTestGnb, NgSetupOverRealSctpSucceeds) {
    Lab lab;
    ASSERT_GT(lab.nrf.pid(), 0) << "failed to fork nrf";
    ASSERT_GT(lab.amf.pid(), 0) << "failed to fork amf";

    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort))
        << "never established an SCTP association with AMF's N2 listener";
    EXPECT_TRUE(gnb.ng_setup(kSourceGnbId))
        << "AMF did not answer a real NGSetupRequest with a real NGSetupResponse";
}

// The first end-to-end exercise of handle_handover_required's real decode path. AMF must recognise
// the message, fail to resolve the UE, and answer the spec-defined rejection -- not drop the
// association, and not stay silent.
TEST(AmfNgapTestGnb, HandoverRequiredForUnknownUeIsRejectedWithPreparationFailure) {
    Lab lab;
    ASSERT_GT(lab.nrf.pid(), 0);
    ASSERT_GT(lab.amf.pid(), 0);

    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kSourceGnbId));

    const auto required =
        gnb.build_handover_required(kUnknownAmfUeId, kRanUeId, kTargetGnbId, /*pdu_session_id=*/5);
    ASSERT_FALSE(required.empty()) << "failed to PER-encode a real HandoverRequired";
    gnb.send_raw(required);

    const auto reply = gnb.receive_raw();
    ASSERT_FALSE(reply.empty()) << "AMF closed the association instead of answering";
    const auto summary = NgapTestGnb::summarize(reply);
    EXPECT_EQ(summary.outcome, NgapTestGnb::Outcome::Unsuccessful);
    EXPECT_EQ(summary.procedure_code, NgapTestGnb::kProcHandoverPreparation)
        << "expected an unsuccessfulOutcome for id-HandoverPreparation "
           "(HandoverPreparationFailure)";
}

// ADR-0261's AMF half, over the wire for the first time. Its handler deliberately still
// acknowledges a HandoverCancel whose AMF-UE-NGAP-ID it cannot resolve -- it has nothing to tell
// SMF or a target gNB about, but the source gNB is still owed its answer. That behaviour was a
// written claim until this test.
TEST(AmfNgapTestGnb, HandoverCancelForUnknownUeIsStillAcknowledged) {
    Lab lab;
    ASSERT_GT(lab.nrf.pid(), 0);
    ASSERT_GT(lab.amf.pid(), 0);

    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kSourceGnbId));

    const auto cancel = gnb.build_handover_cancel(kUnknownAmfUeId, kRanUeId);
    ASSERT_FALSE(cancel.empty()) << "failed to PER-encode a real HandoverCancel";
    gnb.send_raw(cancel);

    const auto reply = gnb.receive_raw();
    ASSERT_FALSE(reply.empty()) << "AMF closed the association instead of acknowledging";
    const auto summary = NgapTestGnb::summarize(reply);
    EXPECT_EQ(summary.outcome, NgapTestGnb::Outcome::Successful);
    EXPECT_EQ(summary.procedure_code, NgapTestGnb::kProcHandoverCancel)
        << "expected a successfulOutcome for id-HandoverCancel (HandoverCancelAcknowledge)";
}
