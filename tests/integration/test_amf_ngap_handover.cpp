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
//
// ADR-0265/0266 added that registration, and ADR-0267 carries it through to a real PDU session --
// the last piece of UE state the relay depends on, since AMF asks SMF for each session's real N2
// transfer and skips any session it has no smContextRef for.

#include <cstdint>
#include <string>

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

// This lab's own configured DNN and S-NSSAI (simulators/ransim/config/ue.yaml, and the values
// tests/integration/test_smf_pdu_session.cpp already drives SMF with).
constexpr const char* kDnn = "internet";
constexpr std::uint8_t kSst = 1;
constexpr std::uint32_t kSd = 0x000001;

constexpr const char* kServingNetworkName = "5G:mnc070.mcc999.3gppnetwork.org";

// The whole registration procedure, from RegistrationRequest to the RegistrationAccept AMF sends
// once it has installed a security context -- ADR-0265's authentication plus ADR-0266's security
// mode procedure, factored out because ADR-0267's PDU session test needs exactly the same prelude.
//
// Assertions live here rather than in the caller so a failure points at the step that broke. The
// substance is unchanged: RES* and the NAS keys are computed from the subscriber's real TS 35.207
// credentials and checked by AUSF/AMF against key material they reached independently, so nothing
// here passes on well-formed framing alone.
struct RegisteredUe {
    std::uint64_t amf_ue_id = 0;
    nf_test::NasKeys keys;
};

void register_ue(NgapTestGnb& gnb, std::uint32_t ran_ue_id, RegisteredUe& out) {
    const auto registration = nf_test::build_registration_request(nf_test::kTestSupi);
    ASSERT_FALSE(registration.empty());
    gnb.send_raw(gnb.build_initial_ue_message(ran_ue_id, registration));

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
    const auto res_star = nf_test::compute_res_star(*challenge, kServingNetworkName);
    ASSERT_TRUE(res_star.has_value()) << "AUTN did not authenticate the network";

    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, ran_ue_id, nf_test::build_authentication_response(*res_star)));

    const auto smc_pdu = gnb.receive_raw();
    ASSERT_FALSE(smc_pdu.empty()) << "AMF sent nothing after the AuthenticationResponse -- AUSF "
                                     "rejected RES*, or the exchange stalled";
    NgapTestGnb::DownlinkNas smc_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(smc_pdu, smc_nas));
    // SecurityModeCommand is integrity-protected with a new context: 0x7E, SHT, MAC(4), SEQ(1),
    // then the inner plain message whose type byte is 0x5D (TS 24.501 §8.2.25).
    ASSERT_GE(smc_nas.nas_pdu.size(), 10u);
    ASSERT_EQ(smc_nas.nas_pdu[0], 0x7E);
    ASSERT_NE(smc_nas.nas_pdu[1], 0x00) << "SecurityModeCommand must be security-protected";
    ASSERT_EQ(smc_nas.nas_pdu[9], 0x5D)
        << "expected the inner NAS message to be a SecurityModeCommand";

    // ADR-0266: complete the security mode procedure. AMF verifies this message's MAC with keys it
    // derived independently through AUSF -- so its acceptance is proof both sides reached the same
    // KAMF, not proof this driver can round-trip its own bytes. On a wrong key AMF logs
    // "SecurityModeComplete MAC verification FAILED" and answers nothing.
    out.amf_ue_id = challenge_nas.amf_ue_id;
    out.keys = nf_test::derive_nas_keys(*challenge, nf_test::kTestSupi, kServingNetworkName);
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id,
        ran_ue_id,
        nf_test::build_security_mode_complete(out.keys, /*uplink_count=*/0)));

    const auto accept_pdu = gnb.receive_raw();
    ASSERT_FALSE(accept_pdu.empty())
        << "AMF answered nothing after SecurityModeComplete -- it rejected the MAC, which means "
           "the UE and AMF did not derive the same KAMF";
    NgapTestGnb::DownlinkNas accept_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(accept_pdu, accept_nas));
    ASSERT_GE(accept_nas.nas_pdu.size(), 8u);
    ASSERT_EQ(accept_nas.nas_pdu[0], 0x7E);
    // RegistrationAccept is integrity protected AND ciphered (security header type 0x02), which is
    // only reachable once AMF has installed the security context this whole exchange establishes.
    ASSERT_EQ(accept_nas.nas_pdu[1], 0x02)
        << "expected a ciphered, integrity-protected RegistrationAccept";

    // The UE deciphers and MAC-verifies it for real. This is the FIRST check in either direction
    // that AMF's own downlink protection is correct: every earlier assertion only read the
    // security header AMF wrote, which a wrong key would not have changed. downlink_count=1 --
    // SecurityModeCommand was 0.
    const auto accept_plain =
        nf_test::open_secured_downlink(out.keys, /*downlink_count=*/1, accept_nas.nas_pdu);
    ASSERT_TRUE(accept_plain.has_value())
        << "RegistrationAccept's MAC did not verify against the UE's own KNASint";
    ASSERT_GE(accept_plain->size(), 3u);
    ASSERT_EQ((*accept_plain)[2], 0x42) << "deciphered downlink was not a RegistrationAccept";
}

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

// ADR-0265/0266: a real UE, registering through authentication and the security mode procedure
// over real NGAP + NAS.
//
// This is the increment that makes the full handover relay reachable: AMF resolves a handover's UE
// through amf_ue_id_index -> ue_security_contexts, and only a real registration populates those.
//
// What makes this a real test rather than a mirror of AMF's own encoders: RES* is computed here
// from the subscriber's TS 35.207 credentials and checked by AUSF against what UDM/UDR
// independently hold, and SecurityModeComplete's MAC is checked by AMF against a KAMF it reached
// through AUSF without ever seeing the UE's CK/IK. Well-formed NAS framing does not get you past
// either -- only correct cryptography does.
TEST(AmfNgapTestGnb, RegistrationCompletesAndAmfInstallsASecurityContext) {
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

    RegisteredUe ue;
    ASSERT_NO_FATAL_FAILURE(register_ue(gnb, /*ran_ue_id=*/1, ue));
    EXPECT_GT(ue.amf_ue_id, 0u) << "AMF never allocated an AMF-UE-NGAP-ID for this UE";
}

// ADR-0267: the same UE carried on to a real PDU session -- RegistrationComplete, then a PDU
// Session Establishment Request, then the Accept SMF builds and delivers back through AMF.
//
// Why this is the piece the handover relay was waiting on. AMF prepares a handover per PDU session
// by asking SMF for that session's real N2 transfer, keyed on an smContextRef it only holds if it
// really created an SM context for the UE (ADR-0249/0258). A session it has no ref for is SKIPPED,
// not filled with a fabricated tunnel -- so a UE with no PDU session can never get past
// HandoverPreparationFailure, no matter how correct the rest of the chain is.
//
// What is genuinely being checked, beyond framing. The uplink messages are MAC'd with the UE's own
// KNASint at the exact NAS COUNTs AMF's phase machine expects (RegistrationComplete=1,
// UlNasTransport=2), so a count or key that disagrees is rejected in silence rather than answered.
// The downlink Accept is deciphered and MAC-verified here against those same keys, and its content
// is a 5GSM message SMF -- a separate process, which never saw this driver's key material --
// encoded from PCF's real policy decision. AMF is opaque to that content by design; it only
// carries the bytes.
//
// This test spans the widest set of real processes in the suite: NRF, UDR, UDM, AUSF, AMF, PCF and
// SMF, over real NGAP/SCTP for N1/N2 and real TLS 1.3 + mTLS HTTP/2 for every SBI hop between
// them.
TEST(AmfNgapTestGnb, RegisteredUeEstablishesARealPduSession) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess udr(UDR_PATH);
    ASSERT_GT(udr.pid(), 0);
    nf_test::SpawnedProcess udm(UDM_PATH);
    ASSERT_GT(udm.pid(), 0);
    nf_test::SpawnedProcess ausf(AUSF_PATH);
    ASSERT_GT(ausf.pid(), 0);
    // SMF reaches PCF for the SM policy decision that fixes the session AMBR and 5QI the Accept
    // carries, and calls back into AMF's Namf_Communication N1N2MessageTransfer to deliver it.
    nf_test::SpawnedProcess pcf(PCF_PATH);
    ASSERT_GT(pcf.pid(), 0);
    nf_test::SpawnedProcess smf(SMF_PATH);
    ASSERT_GT(smf.pid(), 0);
    nf_test::SpawnedProcess amf(AMF_PATH);
    ASSERT_GT(amf.pid(), 0);

    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kSourceGnbId));

    constexpr std::uint32_t kUeRanId = 1;
    RegisteredUe ue;
    ASSERT_NO_FATAL_FAILURE(register_ue(gnb, kUeRanId, ue));

    // RegistrationComplete. AMF answers nothing to this one -- it moves its own phase machine on
    // and registers the association so SMF's later N1N2MessageTransfer can reach this UE -- so
    // there is no reply to read here, and the assertion is that the NEXT exchange works at all.
    gnb.send_raw(gnb.build_uplink_nas_transport(
        ue.amf_ue_id, kUeRanId, nf_test::build_registration_complete(ue.keys, /*uplink_count=*/1)));

    constexpr std::uint8_t kPduSessionId = 5;
    constexpr std::uint8_t kPti = 1;
    gnb.send_raw(gnb.build_uplink_nas_transport(
        ue.amf_ue_id,
        kUeRanId,
        nf_test::build_pdu_session_establishment_request(ue.keys,
                                                         /*uplink_count=*/2,
                                                         kPduSessionId,
                                                         kPti,
                                                         kDnn,
                                                         kSst,
                                                         kSd)));

    // The Accept comes back on this same association, but written from AMF's SBI server thread
    // (SMF calls N1N2MessageTransfer, ADR-0038) rather than as a reply on the NGAP thread.
    const auto accept_pdu = gnb.receive_raw();
    ASSERT_FALSE(accept_pdu.empty())
        << "nothing came back after the PDU Session Establishment Request -- AMF rejected the MAC "
           "or NAS COUNT, SMF never created the SM context, or the N1N2MessageTransfer failed";
    NgapTestGnb::DownlinkNas dl;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(accept_pdu, dl))
        << "expected a DownlinkNASTransport carrying the DlNasTransport with the Accept";

    // downlink_count=2: SecurityModeCommand was 0 and RegistrationAccept 1, which is exactly what
    // AMF registers for this UE (next_downlink_count=2). A desync here fails the MAC check.
    const auto plain = nf_test::open_secured_downlink(ue.keys, /*downlink_count=*/2, dl.nas_pdu);
    ASSERT_TRUE(plain.has_value())
        << "the DlNasTransport's MAC did not verify against the UE's own KNASint";

    const auto n1_sm = nf_test::extract_dl_nas_payload_container(*plain);
    ASSERT_TRUE(n1_sm.has_value())
        << "deciphered downlink was not a DlNasTransport carrying an N1 SM container";

    // The payload is SMF's own 5GSM message (nfs/smf/src/nas_5gsm_codec.cpp): EPD 0x2E, the PDU
    // session ID and PTI echoed back from the request, then message type 0xC2 -- PDU Session
    // Establishment Accept (TS 24.501 §8.3.5). AMF never decoded any of this; it relayed the
    // bytes.
    ASSERT_GE(n1_sm->size(), 4u);
    EXPECT_EQ((*n1_sm)[0], 0x2E) << "not a 5GSM message";
    EXPECT_EQ((*n1_sm)[1], kPduSessionId) << "SMF answered about a different PDU session";
    EXPECT_EQ((*n1_sm)[2], kPti) << "TS 24.501 requires the PTI to be echoed back";
    EXPECT_EQ((*n1_sm)[3], 0xC2)
        << "expected a PDU Session Establishment Accept; 0xC3 would be a Reject";
}
