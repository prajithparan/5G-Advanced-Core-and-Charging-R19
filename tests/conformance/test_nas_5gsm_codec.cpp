// Unit tests for nfs/smf/src/nas_5gsm_codec.{hpp,cpp} -- see that header's own comment for the
// byte-layout citations. Verified end-to-end against a real nr-ue (docs/DECISIONS.md ADR-0038:
// the real UE decoded and accepted an Accept built by this exact codec, "PDU Session
// establishment is successful"); these tests lock that already-verified behavior down
// deterministically.

#include "nas_5gsm_codec.hpp"

#include <gtest/gtest.h>

TEST(NasFiveGsmCodec, DecodeEstablishmentRequestExtractsPduSessionIdAndPti) {
    const std::vector<std::uint8_t> p = {
        0x2e, // EPD: SESSION_MANAGEMENT_MESSAGES
        0x01, // pduSessionId
        0x07, // pti
        0xc1, // PDU_SESSION_ESTABLISHMENT_REQUEST
        0x00, 0x00, // integrityProtectionMaximumDataRate (ignored by this decoder)
    };
    const auto info = smf::nas5gsm::decode_establishment_request(p);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->pdu_session_id, 1);
    EXPECT_EQ(info->pti, 7);
}

TEST(NasFiveGsmCodec, DecodeEstablishmentRequestRejectsWrongMessageType) {
    const std::vector<std::uint8_t> p = {0x2e, 0x01, 0x07, 0xc2}; // C2 = Accept, not Request
    EXPECT_FALSE(smf::nas5gsm::decode_establishment_request(p).has_value());
}

TEST(NasFiveGsmCodec, DecodeEstablishmentRequestRejectsWrongEpd) {
    const std::vector<std::uint8_t> p = {0x7e, 0x01, 0x07, 0xc1}; // 0x7e = 5GMM, not 5GSM
    EXPECT_FALSE(smf::nas5gsm::decode_establishment_request(p).has_value());
}

TEST(NasFiveGsmCodec, DecodeEstablishmentRequestRejectsTooShort) {
    const std::vector<std::uint8_t> p = {0x2e, 0x01, 0x07};
    EXPECT_FALSE(smf::nas5gsm::decode_establishment_request(p).has_value());
}

TEST(NasFiveGsmCodec, EncodeEstablishmentAcceptHasCorrectHeaderAndMandatoryIes) {
    const auto out = smf::nas5gsm::encode_establishment_accept(
        /*pdu_session_id=*/1, /*pti=*/7, /*session_ambr_uplink=*/"1 Gbps",
        /*session_ambr_downlink=*/"1 Gbps", /*qfi=*/9);

    // header(4) + sscMode|pduSessionType(1) + qosRules(2-byte len + 6-byte rule = 8) +
    // sessionAmbr(1-byte len + 6-byte value = 7) = 4+1+8+7 = 20
    ASSERT_EQ(out.size(), 20u);
    EXPECT_EQ(out[0], 0x2e); // EPD: SESSION_MANAGEMENT_MESSAGES
    EXPECT_EQ(out[1], 0x01); // pduSessionId
    EXPECT_EQ(out[2], 0x07); // pti
    EXPECT_EQ(out[3], 0xc2); // PDU_SESSION_ESTABLISHMENT_ACCEPT
    EXPECT_EQ(out[4], 0x11); // sscMode=1 (high nibble) | pduSessionType=IPV4=1 (low nibble)

    // authorizedQoSRules: 2-byte length = 6, then ruleId(1)+ruleLen(2)=3+content(3).
    EXPECT_EQ(out[5], 0x00);
    EXPECT_EQ(out[6], 0x06);
    EXPECT_EQ(out[7], 0x01); // QoS rule identifier
    EXPECT_EQ(out[8], 0x00);
    EXPECT_EQ(out[9], 0x03); // QoS rule length = 3
    EXPECT_EQ(out[10], 0x30); // opcode=001(create) | DQR=1 | numPacketFilters=0000
    EXPECT_EQ(out[11], 0x01); // precedence
    EXPECT_EQ(out[12], 0x09); // QFI = 9

    // sessionAmbr: 1-byte length = 6, then unit/value for downlink then uplink.
    EXPECT_EQ(out[13], 0x06);
    EXPECT_EQ(out[14], 0x0B); // MULT_1Gbps (downlink unit)
    EXPECT_EQ(out[15], 0x00);
    EXPECT_EQ(out[16], 0x01); // downlink value = 1
    EXPECT_EQ(out[17], 0x0B); // MULT_1Gbps (uplink unit)
    EXPECT_EQ(out[18], 0x00);
    EXPECT_EQ(out[19], 0x01); // uplink value = 1
}

TEST(NasFiveGsmCodec, EncodeEstablishmentAcceptQfiIsMaskedTo6Bits) {
    const auto out = smf::nas5gsm::encode_establishment_accept(1, 1, "1 Mbps", "1 Mbps",
                                                                /*qfi=*/0xFF);
    EXPECT_EQ(out[12] & 0xC0, 0); // top 2 bits (spare + segregation) must be zero
    EXPECT_EQ(out[12] & 0x3F, 0x3F); // QFI truncated to its 6 bits
}

TEST(NasFiveGsmCodec, EncodeEstablishmentAcceptFallsBackOnUnparseableBitRate) {
    const auto out = smf::nas5gsm::encode_establishment_accept(1, 1, "not-a-bitrate",
                                                                "not-a-bitrate", 1);
    // Falls back to the disclosed default (1 Mbps) rather than encoding garbage.
    EXPECT_EQ(out[14], 0x06); // MULT_1Mbps
    EXPECT_EQ(out[15], 0x00);
    EXPECT_EQ(out[16], 0x01);
}
