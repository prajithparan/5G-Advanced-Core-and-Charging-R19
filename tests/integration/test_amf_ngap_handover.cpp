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
#include "ue_nas_driver.hpp"

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

// ADR-0265: a real UE, registering through authentication over real NGAP + NAS.
//
// This is the increment that makes the full handover relay reachable: AMF resolves a handover's UE
// through amf_ue_id_index -> ue_security_contexts, and only a real registration populates those.
//
// What makes this a real test rather than a mirror of AMF's own encoders: RES* is computed here
// from the subscriber's TS 35.207 credentials and checked by AUSF against what UDM/UDR
// independently hold. Well-formed NAS framing does not get you past that -- only correct
// cryptography does. The assertion is on AMF proceeding to SecurityModeCommand, which it does only
// after AUSF confirms.
TEST(AmfNgapTestGnb, RegistrationReachesAuthenticationAndSecurityModeCommand) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess udr(UDR_PATH);
    ASSERT_GT(udr.pid(), 0);
    nf_test::SpawnedProcess udm(UDM_PATH);
    ASSERT_GT(udm.pid(), 0);
    nf_test::SpawnedProcess ausf(AUSF_PATH);
    ASSERT_GT(ausf.pid(), 0);
    nf_test::SpawnedProcess amf(AMF_PATH);
    ASSERT_GT(amf.pid(), 0);

    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kSourceGnbId));

    constexpr std::uint32_t kUeRanId = 1;
    const auto registration = nf_test::build_registration_request(nf_test::kTestSupi);
    ASSERT_FALSE(registration.empty());
    gnb.send_raw(gnb.build_initial_ue_message(kUeRanId, registration));

    // AMF must fetch a real authentication vector from AUSF (which fetches from UDM/UDR) before it
    // can send this, so reaching it at all already exercises the whole SBI chain.
    const auto challenge_pdu = gnb.receive_raw();
    ASSERT_FALSE(challenge_pdu.empty()) << "AMF sent nothing after the RegistrationRequest";
    NgapTestGnb::DownlinkNas challenge_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(challenge_pdu, challenge_nas))
        << "expected a DownlinkNASTransport carrying an AuthenticationRequest";
    const auto challenge = nf_test::parse_authentication_request(challenge_nas.nas_pdu);
    ASSERT_TRUE(challenge.has_value()) << "AMF's NAS PDU was not a decodable AuthenticationRequest";

    // Verifies AUTN's MAC against the real credentials before answering -- a wrong vector fails
    // here rather than being answered anyway.
    const auto res_star = nf_test::compute_res_star(*challenge, "5G:mnc070.mcc999.3gppnetwork.org");
    ASSERT_TRUE(res_star.has_value()) << "AUTN did not authenticate the network";

    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, kUeRanId, nf_test::build_authentication_response(*res_star)));

    const auto smc_pdu = gnb.receive_raw();
    ASSERT_FALSE(smc_pdu.empty()) << "AMF sent nothing after the AuthenticationResponse -- AUSF "
                                     "rejected RES*, or the exchange stalled";
    NgapTestGnb::DownlinkNas smc_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(smc_pdu, smc_nas));
    // SecurityModeCommand is integrity-protected with a new context: 0x7E, SHT, MAC(4), SEQ(1),
    // then the inner plain message whose type byte is 0x5D (TS 24.501 §8.2.25).
    ASSERT_GE(smc_nas.nas_pdu.size(), 10u);
    EXPECT_EQ(smc_nas.nas_pdu[0], 0x7E);
    EXPECT_NE(smc_nas.nas_pdu[1], 0x00) << "SecurityModeCommand must be security-protected";
    EXPECT_EQ(smc_nas.nas_pdu[9], 0x5D)
        << "expected the inner NAS message to be a SecurityModeCommand";
}
