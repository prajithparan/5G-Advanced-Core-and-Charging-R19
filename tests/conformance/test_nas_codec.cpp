// Unit tests for nfs/amf/src/nas_codec.{hpp,cpp} -- the hand-rolled minimal NAS-5GS (TS 24.501)
// codec (see docs/DECISIONS.md ADR-0032). Byte layouts here mirror what real interop testing
// against UERANSIM's nr-gnb/nr-ue already confirmed correct (Stage 2/3 of the staged NGAP/NAS
// plan) -- these tests exist to catch regressions in that already-verified behavior
// deterministically, not to establish correctness from scratch (that was done empirically, per
// ADR-0031/ADR-0032's own methodology, real bytes/real peer, not assumed).

#include "aka_crypto/nas_security.hpp"
#include "nas_codec.hpp"

#include <gtest/gtest.h>

namespace {
// TS 24.501's NAS MAC construction prepends the 1-octet NAS sequence number (COUNT's low-order
// byte) to the transmitted bytes before computing the MAC -- confirmed against a real nr-ue build
// (UERANSIM's nas_enc::ComputeMac), see nfs/amf/src/nas_codec.cpp's encode_secured_downlink for
// the full citation. Test helper mirrors that exact construction so these hand-built "genuine
// message" vectors match what amf::nas's own encode/decode functions actually produce/expect.
uint32_t nia2_mac_with_seqno_prefix(const aka_crypto::NasIntKey& key,
                                    uint32_t count,
                                    uint8_t bearer,
                                    uint8_t direction,
                                    const std::vector<uint8_t>& wire_bytes) {
    std::vector<uint8_t> mac_input;
    mac_input.reserve(wire_bytes.size() + 1);
    mac_input.push_back(static_cast<uint8_t>(count & 0xff));
    mac_input.insert(mac_input.end(), wire_bytes.begin(), wire_bytes.end());
    return aka_crypto::nia2_mac(key, count, bearer, direction, mac_input);
}
} // namespace

TEST(NasCodec, DecodesRegistrationRequestNullSchemeSuci) {
    // imsi-999700000000001 (mcc=999, mnc=70, msin=0000000001), null protection scheme,
    // routing indicator "0000" -- same subscriber this project's own UDM/AUSF seed data and
    // simulators/ransim/config/ue.yaml use throughout.
    const std::vector<std::uint8_t> nas_pdu = {0x7e,
                                               0x00,
                                               0x41,
                                               0x00,
                                               0x00,
                                               0x0d,
                                               0x01,
                                               0x99,
                                               0xf9,
                                               0x07,
                                               0x00,
                                               0x00,
                                               0x00,
                                               0x00,
                                               0x00,
                                               0x00,
                                               0x00,
                                               0x00,
                                               0x10};

    const auto info = amf::nas::decode_registration_request(nas_pdu);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->supi, "imsi-999700000000001");
}

TEST(NasCodec, RejectsNonRegistrationRequestMessageType) {
    std::vector<std::uint8_t> nas_pdu = {0x7e, 0x00, 0x56, 0x00}; // AuthenticationRequest, not RR
    EXPECT_FALSE(amf::nas::decode_registration_request(nas_pdu).has_value());
}

TEST(NasCodec, RejectsRegistrationRequestWithNonNullProtectionScheme) {
    // Same as the accepted case, but protectionSchemeId=1 instead of 0 -- out of scope, must be
    // rejected (nullopt), not silently misparsed as if it were plaintext.
    std::vector<std::uint8_t> nas_pdu = {0x7e,
                                         0x00,
                                         0x41,
                                         0x00,
                                         0x00,
                                         0x0d,
                                         0x01,
                                         0x99,
                                         0xf9,
                                         0x07,
                                         0x00,
                                         0x00,
                                         0x01,
                                         0x00,
                                         0x00,
                                         0x00,
                                         0x00,
                                         0x00,
                                         0x10};
    EXPECT_FALSE(amf::nas::decode_registration_request(nas_pdu).has_value());
}

TEST(NasCodec, EncodesAuthenticationRequestWithCorrectIeLayout) {
    std::array<std::uint8_t, 16> rand{};
    std::array<std::uint8_t, 16> autn{};
    for (std::size_t i = 0; i < 16; ++i) {
        rand[i] = static_cast<std::uint8_t>(i);
        autn[i] = static_cast<std::uint8_t>(0x10 + i);
    }

    const auto nas_pdu = amf::nas::encode_authentication_request(rand, autn, /*ngksi=*/0);

    // header(3) + ngksi(1) + abba(1+2) + rand IEI+16(no length octet -- Type-3, see ADR-0032) +
    // autn IEI+len+16(Type-4) = 3+1+3+17+18 = 42
    ASSERT_EQ(nas_pdu.size(), 42u);
    EXPECT_EQ(nas_pdu[0], 0x7e); // EPD
    EXPECT_EQ(nas_pdu[1], 0x00); // security header type
    EXPECT_EQ(nas_pdu[2], 0x56); // AUTHENTICATION_REQUEST
    EXPECT_EQ(nas_pdu[3], 0x00); // ngKSI (high nibble spare)
    EXPECT_EQ(nas_pdu[4], 0x02); // ABBA length
    EXPECT_EQ(nas_pdu[5], 0x00);
    EXPECT_EQ(nas_pdu[6], 0x00);
    EXPECT_EQ(nas_pdu[7], 0x21); // RAND IEI
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(nas_pdu[8 + i], rand[i]);
    }
    EXPECT_EQ(nas_pdu[24], 0x20); // AUTN IEI
    EXPECT_EQ(nas_pdu[25], 0x10); // AUTN length = 16
    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(nas_pdu[26 + i], autn[i]);
    }
}

TEST(NasCodec, DecodesAuthenticationResponseResStar) {
    std::vector<std::uint8_t> nas_pdu = {0x7e, 0x00, 0x57, 0x2d, 0x10};
    std::array<std::uint8_t, 16> expected_res_star{};
    for (std::size_t i = 0; i < 16; ++i) {
        expected_res_star[i] = static_cast<std::uint8_t>(i);
        nas_pdu.push_back(expected_res_star[i]);
    }

    const auto outcome = amf::nas::decode_authentication_outcome(nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->success);
    EXPECT_EQ(outcome->res_star, expected_res_star);
}

TEST(NasCodec, DecodesAuthenticationFailureWithAutsForSyncFailure) {
    // Same shape (mmCause=0x15 SYNCH_FAILURE, with AUTS) real UERANSIM nr-ue actually sends
    // against this project's currently-seeded test subscriber -- see docs/DECISIONS.md ADR-0032.
    std::vector<std::uint8_t> nas_pdu = {0x7e, 0x00, 0x59, 0x15, 0x30, 0x0e};
    std::array<std::uint8_t, 14> expected_auts{};
    for (std::size_t i = 0; i < 14; ++i) {
        expected_auts[i] = static_cast<std::uint8_t>(0x50 + i);
        nas_pdu.push_back(expected_auts[i]);
    }

    const auto outcome = amf::nas::decode_authentication_outcome(nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->success);
    EXPECT_EQ(outcome->mm_cause, 0x15);
    ASSERT_TRUE(outcome->auts.has_value());
    EXPECT_EQ(*outcome->auts, expected_auts);
}

TEST(NasCodec, DecodesAuthenticationFailureWithoutAuts) {
    // mmCause=0x14 MAC_FAILURE has no AUTS (TS 24.501 -- AUTS only accompanies SYNCH_FAILURE).
    const std::vector<std::uint8_t> nas_pdu = {0x7e, 0x00, 0x59, 0x14};

    const auto outcome = amf::nas::decode_authentication_outcome(nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->success);
    EXPECT_EQ(outcome->mm_cause, 0x14);
    EXPECT_FALSE(outcome->auts.has_value());
}

TEST(NasCodec, RejectsTruncatedAuthenticationResponse) {
    // Claims a 16-octet RES* but only provides 4 -- must be rejected, not read out of bounds.
    const std::vector<std::uint8_t> nas_pdu = {
        0x7e, 0x00, 0x57, 0x2d, 0x10, 0x01, 0x02, 0x03, 0x04};
    EXPECT_FALSE(amf::nas::decode_authentication_outcome(nas_pdu).has_value());
}

TEST(NasCodec, EncodesSecurityModeCommandWithCorrectEnvelopeAndInnerLayout) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x77);
    const std::vector<std::uint8_t> ue_sec_cap = {0xe0, 0x00}; // minimal 2-octet capability value

    const auto nas_pdu =
        amf::nas::encode_security_mode_command(knas_int, ue_sec_cap, /*downlink_count=*/0);

    // outer(7: epd+sht+mac4+seqno) + inner(epd+sht+msgtype+algs+ngksi+caplen+2 = 8) = 15
    ASSERT_EQ(nas_pdu.size(), 15u);
    EXPECT_EQ(nas_pdu[0], 0x7e);
    EXPECT_EQ(nas_pdu[1], 0x03); // INTEGRITY_PROTECTED_WITH_NEW_SECURITY_CONTEXT
    EXPECT_EQ(nas_pdu[6], 0x00); // sequence number == downlink_count
    // Inner plaintext message starts at offset 7.
    EXPECT_EQ(nas_pdu[7], 0x7e);
    EXPECT_EQ(nas_pdu[8], 0x00);  // inner SHT: NOT_PROTECTED
    EXPECT_EQ(nas_pdu[9], 0x5d);  // SECURITY_MODE_COMMAND
    EXPECT_EQ(nas_pdu[10], 0x22); // (NEA2<<4)|NIA2 == 0x22
    EXPECT_EQ(nas_pdu[11], 0x00); // ngKSI: native, ksi=0
    EXPECT_EQ(nas_pdu[12], 0x02); // replayed UE security capability length
    EXPECT_EQ(nas_pdu[13], 0xe0);
    EXPECT_EQ(nas_pdu[14], 0x00);
}

TEST(NasCodec, DecodeSecurityModeCompleteAcceptsGenuineMessage) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x88);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0x99);

    // Build a genuine SecurityModeComplete the way a real UE would: minimal inner plaintext (no
    // optional IEs), ciphered with 128-NEA2, MAC computed over the ciphertext with 128-NIA2 --
    // same construction amf::nas::decode_security_mode_complete is meant to accept.
    const std::vector<std::uint8_t> plain_inner = {0x7e, 0x00, 0x5e};
    const auto ciphered =
        aka_crypto::nea2_apply(knas_enc, /*count=*/0, /*bearer=*/1, /*direction=*/0, plain_inner);
    const auto mac =
        nia2_mac_with_seqno_prefix(knas_int, /*count=*/0, /*bearer=*/1, /*direction=*/0, ciphered);

    std::vector<std::uint8_t> nas_pdu = {0x7e,
                                         0x04,
                                         static_cast<std::uint8_t>((mac >> 24) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 16) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 8) & 0xff),
                                         static_cast<std::uint8_t>(mac & 0xff),
                                         0x00};
    nas_pdu.insert(nas_pdu.end(), ciphered.begin(), ciphered.end());

    const auto outcome =
        amf::nas::decode_security_mode_complete(knas_int, knas_enc, /*uplink_count=*/0, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->mac_valid);
}

TEST(NasCodec, DecodeSecurityModeCompleteRejectsTamperedMac) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0xaa);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0xbb);

    const std::vector<std::uint8_t> plain_inner = {0x7e, 0x00, 0x5e};
    const auto ciphered = aka_crypto::nea2_apply(knas_enc, 0, 1, 0, plain_inner);

    std::vector<std::uint8_t> nas_pdu = {0x7e, 0x04, 0xde, 0xad, 0xbe, 0xef, 0x00};
    nas_pdu.insert(nas_pdu.end(), ciphered.begin(), ciphered.end());

    const auto outcome = amf::nas::decode_security_mode_complete(knas_int, knas_enc, 0, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->mac_valid);
}

TEST(NasCodec, DecodeSecurityModeCompleteRejectsWrongSecurityHeaderType) {
    aka_crypto::NasIntKey knas_int{};
    aka_crypto::NasEncKey knas_enc{};
    const std::vector<std::uint8_t> nas_pdu = {0x7e, 0x02, 0, 0, 0, 0, 0, 0xaa};
    EXPECT_FALSE(
        amf::nas::decode_security_mode_complete(knas_int, knas_enc, 0, nas_pdu).has_value());
}

TEST(NasCodec, DecodeSecurityModeCompleteRejectsTooShortMessage) {
    aka_crypto::NasIntKey knas_int{};
    aka_crypto::NasEncKey knas_enc{};
    const std::vector<std::uint8_t> nas_pdu = {0x7e, 0x04, 0, 0, 0};
    EXPECT_FALSE(
        amf::nas::decode_security_mode_complete(knas_int, knas_enc, 0, nas_pdu).has_value());
}

TEST(NasCodec, EncodesRegistrationAcceptWithCorrectEnvelopeAndDecryptsToExpectedInner) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0xcc);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0xdd);

    const auto nas_pdu =
        amf::nas::encode_registration_accept(knas_int, knas_enc, /*downlink_count=*/1);

    // outer(7) + inner(epd+sht+msgtype+regResult-len+regResult-value = 5) = 12
    ASSERT_EQ(nas_pdu.size(), 12u);
    EXPECT_EQ(nas_pdu[0], 0x7e);
    EXPECT_EQ(nas_pdu[1], 0x02); // INTEGRITY_PROTECTED_AND_CIPHERED
    EXPECT_EQ(nas_pdu[6], 0x01); // sequence number == downlink_count

    const std::vector<std::uint8_t> ciphered(nas_pdu.begin() + 7, nas_pdu.end());
    const auto plain =
        aka_crypto::nea2_apply(knas_enc, /*count=*/1, /*bearer=*/1, /*direction=*/1, ciphered);
    ASSERT_EQ(plain.size(), 5u);
    EXPECT_EQ(plain[0], 0x7e);
    EXPECT_EQ(plain[1], 0x00);
    EXPECT_EQ(plain[2], 0x42); // REGISTRATION_ACCEPT
    EXPECT_EQ(plain[3], 0x01); // registrationResult IE length
    EXPECT_EQ(plain[4], 0x01); // smsOverNasAllowed=0 | registrationResult=THREEGPP_ACCESS(1)
}

TEST(NasCodec, DecodeRegistrationCompleteAcceptsGenuineMessage) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0xee);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0xff);

    const std::vector<std::uint8_t> plain_inner = {
        0x7e, 0x00, 0x43}; // REGISTRATION_COMPLETE, no IEs
    const auto ciphered = aka_crypto::nea2_apply(knas_enc, /*count=*/1, 1, 0, plain_inner);
    const auto mac = nia2_mac_with_seqno_prefix(knas_int, /*count=*/1, 1, 0, ciphered);

    std::vector<std::uint8_t> nas_pdu = {0x7e,
                                         0x02,
                                         static_cast<std::uint8_t>((mac >> 24) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 16) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 8) & 0xff),
                                         static_cast<std::uint8_t>(mac & 0xff),
                                         0x01};
    nas_pdu.insert(nas_pdu.end(), ciphered.begin(), ciphered.end());

    const auto outcome =
        amf::nas::decode_registration_complete(knas_int, knas_enc, /*uplink_count=*/1, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->mac_valid);
}

TEST(NasCodec, DecodeRegistrationCompleteRejectsTamperedMac) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x12);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0x34);

    const std::vector<std::uint8_t> plain_inner = {0x7e, 0x00, 0x43};
    const auto ciphered = aka_crypto::nea2_apply(knas_enc, 1, 1, 0, plain_inner);

    std::vector<std::uint8_t> nas_pdu = {0x7e, 0x02, 0xba, 0xad, 0xf0, 0x0d, 0x01};
    nas_pdu.insert(nas_pdu.end(), ciphered.begin(), ciphered.end());

    const auto outcome = amf::nas::decode_registration_complete(knas_int, knas_enc, 1, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->mac_valid);
}

TEST(NasCodec, DecodeRegistrationCompleteRejectsWrongSecurityHeaderType) {
    aka_crypto::NasIntKey knas_int{};
    aka_crypto::NasEncKey knas_enc{};
    const std::vector<std::uint8_t> nas_pdu = {0x7e, 0x03, 0, 0, 0, 0, 0, 0xaa};
    EXPECT_FALSE(
        amf::nas::decode_registration_complete(knas_int, knas_enc, 1, nas_pdu).has_value());
}

namespace {

// Builds a genuine secured UlNasTransport the way a real UE establishing a PDU session would --
// same construction pattern as DecodeSecurityModeCompleteAcceptsGenuineMessage/
// DecodeRegistrationCompleteAcceptsGenuineMessage, but with UlNasTransport's own inner layout
// (payloadContainerType, an opaque payloadContainer standing in for the real 5GSM message this
// project deliberately never decodes, then pduSessionId/requestType/sNssai/dnn -- the exact IE
// order simulators/ransim/vendor/UERANSIM/src/ue/nas/sm/transport.cpp sends).
std::vector<std::uint8_t> build_ul_nas_transport(const aka_crypto::NasIntKey& knas_int,
                                                 const aka_crypto::NasEncKey& knas_enc,
                                                 std::uint32_t uplink_count) {
    std::vector<std::uint8_t> plain_inner = {
        0x7e, 0x00, 0x67,                   // EPD, inner SHT=NOT_PROTECTED, UL_NAS_TRANSPORT
        0x01,                               // payloadContainerType: N1_SM_INFORMATION
        0x00, 0x03, 0xaa, 0xbb, 0xcc,       // payloadContainer: opaque 3-byte stand-in 5GSM message
        0x12, 0x01,                         // pduSessionId IEI + value=1
        0x81,                               // requestType: IEI nibble 0x8 | INITIAL_REQUEST(1)
        0x22, 0x04, 0x01, 0x00, 0x00, 0x01, // sNssai IEI + len=4 + sst=1 + sd=000001
        0x25, 0x09, 0x08, 'i',  'n',  't',
        'e',  'r',  'n',  'e',  't', // dnn IEI + len + label("internet")
    };

    const auto ciphered = aka_crypto::nea2_apply(knas_enc, uplink_count, 1, 0, plain_inner);
    const auto mac = nia2_mac_with_seqno_prefix(knas_int, uplink_count, 1, 0, ciphered);

    std::vector<std::uint8_t> nas_pdu = {0x7e,
                                         0x02,
                                         static_cast<std::uint8_t>((mac >> 24) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 16) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 8) & 0xff),
                                         static_cast<std::uint8_t>(mac & 0xff),
                                         static_cast<std::uint8_t>(uplink_count & 0xff)};
    nas_pdu.insert(nas_pdu.end(), ciphered.begin(), ciphered.end());
    return nas_pdu;
}

} // namespace

TEST(NasCodec, DecodeUlNasTransportExtractsPduSessionIdSnssaiAndDnn) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x21);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0x43);

    const auto nas_pdu = build_ul_nas_transport(knas_int, knas_enc, /*uplink_count=*/2);

    const auto outcome = amf::nas::decode_ul_nas_transport(knas_int, knas_enc, 2, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->mac_valid);
    EXPECT_EQ(outcome->pdu_session_id, 1);
    ASSERT_TRUE(outcome->dnn.has_value());
    EXPECT_EQ(*outcome->dnn, "internet");
    ASSERT_TRUE(outcome->snssai_sst.has_value());
    EXPECT_EQ(*outcome->snssai_sst, 1);
    ASSERT_TRUE(outcome->snssai_sd.has_value());
    const std::array<std::uint8_t, 3> expected_sd{0x00, 0x00, 0x01};
    EXPECT_EQ(*outcome->snssai_sd, expected_sd);
    // ADR-0038: the opaque payload container bytes are now captured verbatim, not just skipped.
    const std::vector<std::uint8_t> expected_container = {0xaa, 0xbb, 0xcc};
    EXPECT_EQ(outcome->payload_container, expected_container);
}

TEST(NasCodec, DecodeUlNasTransportRejectsTamperedMac) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x55);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0x66);

    auto nas_pdu = build_ul_nas_transport(knas_int, knas_enc, 2);
    nas_pdu[2] ^= 0xff; // flip a MAC byte

    const auto outcome = amf::nas::decode_ul_nas_transport(knas_int, knas_enc, 2, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->mac_valid);
}

TEST(NasCodec, DecodeUlNasTransportRejectsNonN1SmPayloadContainerType) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x77);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0x88);

    std::vector<std::uint8_t> plain_inner = {
        0x7e, 0x00, 0x67, 0x02 /* SMS, not N1_SM_INFORMATION */, 0x00, 0x00};
    const auto ciphered = aka_crypto::nea2_apply(knas_enc, 2, 1, 0, plain_inner);
    const auto mac = nia2_mac_with_seqno_prefix(knas_int, 2, 1, 0, ciphered);
    std::vector<std::uint8_t> nas_pdu = {0x7e,
                                         0x02,
                                         static_cast<std::uint8_t>((mac >> 24) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 16) & 0xff),
                                         static_cast<std::uint8_t>((mac >> 8) & 0xff),
                                         static_cast<std::uint8_t>(mac & 0xff),
                                         0x02};
    nas_pdu.insert(nas_pdu.end(), ciphered.begin(), ciphered.end());

    const auto outcome = amf::nas::decode_ul_nas_transport(knas_int, knas_enc, 2, nas_pdu);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->mac_valid);
}

TEST(NasCodec, DecodeUlNasTransportRejectsWrongSecurityHeaderType) {
    aka_crypto::NasIntKey knas_int{};
    aka_crypto::NasEncKey knas_enc{};
    const std::vector<std::uint8_t> nas_pdu = {0x7e, 0x03, 0, 0, 0, 0, 0x02, 0xaa};
    EXPECT_FALSE(amf::nas::decode_ul_nas_transport(knas_int, knas_enc, 2, nas_pdu).has_value());
}

// ADR-0038: encode_dl_nas_transport is AMF's delivery vehicle for SMF's real PDU Session
// Establishment Accept (Namf_Communication N1N2MessageTransfer). Verified end-to-end against a
// real nr-ue (docs/DECISIONS.md ADR-0038); this test locks the byte layout down deterministically.
TEST(NasCodec, EncodesDlNasTransportWithCorrectEnvelopeAndDecryptsToExpectedInner) {
    aka_crypto::NasIntKey knas_int{};
    knas_int.fill(0x55);
    aka_crypto::NasEncKey knas_enc{};
    knas_enc.fill(0x66);

    const std::vector<std::uint8_t> n1_sm_container = {0xde, 0xad, 0xbe, 0xef};
    const auto nas_pdu = amf::nas::encode_dl_nas_transport(knas_int,
                                                           knas_enc,
                                                           /*downlink_count=*/2,
                                                           /*pdu_session_id=*/1,
                                                           n1_sm_container);

    // outer(7) + inner(epd+sht+msgtype+payloadContainerType+containerLen(2)+container(4)+
    // pduSessionIdIEI+pduSessionId = 3+1+2+4+2 = 12) = 19
    ASSERT_EQ(nas_pdu.size(), 19u);
    EXPECT_EQ(nas_pdu[0], 0x7e);
    EXPECT_EQ(nas_pdu[1], 0x02); // INTEGRITY_PROTECTED_AND_CIPHERED
    EXPECT_EQ(nas_pdu[6], 0x02); // sequence number == downlink_count

    const std::vector<std::uint8_t> ciphered(nas_pdu.begin() + 7, nas_pdu.end());
    const auto plain =
        aka_crypto::nea2_apply(knas_enc, /*count=*/2, /*bearer=*/1, /*direction=*/1, ciphered);
    ASSERT_EQ(plain.size(), 12u);
    EXPECT_EQ(plain[0], 0x7e);
    EXPECT_EQ(plain[1], 0x00);
    EXPECT_EQ(plain[2], 0x68); // DL_NAS_TRANSPORT
    EXPECT_EQ(plain[3], 0x01); // payloadContainerType: N1_SM_INFORMATION
    EXPECT_EQ(plain[4], 0x00);
    EXPECT_EQ(plain[5], 0x04); // payloadContainer length = 4
    const std::vector<std::uint8_t> container(plain.begin() + 6, plain.begin() + 10);
    EXPECT_EQ(container, n1_sm_container);
    EXPECT_EQ(plain[10], 0x12); // pduSessionId IEI
    EXPECT_EQ(plain[11], 0x01); // pduSessionId value
}
