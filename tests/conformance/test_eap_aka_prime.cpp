// Structural/self-consistency tests for libs/aka-crypto's EAP-AKA' packet framing and key
// derivation (RFC 5448, IANA "EAP-AKA and EAP-SIM Parameters" registry). Unlike Milenage
// (test_milenage.cpp), no official published numeric EAP-AKA' test vector was found and verified
// this turn -- these tests check packet round-tripping, determinism, and tamper-detection against
// this implementation's own output, not against an external known-correct value. The attribute
// type codes (AT_RAND=1, AT_AUTN=2, AT_RES=3, AT_MAC=11, AT_KDF_INPUT=23, AT_KDF=24) and EAP
// method Type=50 were cross-checked against IANA's registry (see docs/DECISIONS.md ADR-0026),
// which is the part most at risk of silent fabrication; the PRF'/MK bit-layout follows the
// RFC 5448 Section 3.3 text quoted in ADR-0026 directly.

#include "aka_crypto/eap_aka_prime.hpp"

#include <gtest/gtest.h>

namespace {

aka_crypto::Key128 make_rand() {
    aka_crypto::Key128 rand{};
    for (size_t i = 0; i < rand.size(); ++i) {
        rand[i] = static_cast<uint8_t>(i);
    }
    return rand;
}

std::array<uint8_t, 32> make_k_aut(uint8_t seed) {
    std::array<uint8_t, 32> k{};
    k.fill(seed);
    return k;
}

} // namespace

TEST(EapAkaPrime, PrfPrimeIsDeterministicAndKeySensitive) {
    const std::vector<uint8_t> key1(32, 0x01);
    const std::vector<uint8_t> key2(32, 0x02);
    const std::vector<uint8_t> seed{'s', 'e', 'e', 'd'};

    const auto out1a = aka_crypto::eap::prf_prime(key1, seed, 64);
    const auto out1b = aka_crypto::eap::prf_prime(key1, seed, 64);
    const auto out2 = aka_crypto::eap::prf_prime(key2, seed, 64);

    EXPECT_EQ(out1a, out1b);
    EXPECT_NE(out1a, out2);
    EXPECT_EQ(out1a.size(), 64u);
}

TEST(EapAkaPrime, DeriveKeysProducesDistinctNonZeroKeys) {
    std::array<uint8_t, 16> ck_prime{};
    ck_prime.fill(0xaa);
    std::array<uint8_t, 16> ik_prime{};
    ik_prime.fill(0xbb);

    const auto keys = aka_crypto::eap::derive_keys(ck_prime, ik_prime, "imsi-999700000000001");

    EXPECT_NE(keys.k_encr, (std::array<uint8_t, 16>{}));
    EXPECT_NE(keys.k_aut, (std::array<uint8_t, 32>{}));
    EXPECT_NE(std::vector<uint8_t>(keys.k_aut.begin(), keys.k_aut.end()),
              std::vector<uint8_t>(keys.k_re.begin(), keys.k_re.end()));
    EXPECT_NE(std::vector<uint8_t>(keys.msk.begin(), keys.msk.end()),
              std::vector<uint8_t>(keys.emsk.begin(), keys.emsk.end()));
}

// Regression test for a real bug (see docs/DECISIONS.md ADR-0027): derive_keys used to build its
// internal seed via std::string("EAP-AKA'").begin(), std::string("EAP-AKA'").end() -- two SEPARATE
// temporary std::string objects, so the iterator range spanned two unrelated objects (undefined
// behavior, not "the same short string twice"). That bug produced output that varied by stack
// layout, so it was silent under every previous test in this file (each only ever called
// derive_keys once, self-consistently) -- only caught by a cross-process integration test
// comparing independently-computed keys. This test calls derive_keys twice with byte-identical
// inputs and asserts byte-identical output, which the buggy version would have failed.
TEST(EapAkaPrime, DeriveKeysIsDeterministicForIdenticalInputs) {
    std::array<uint8_t, 16> ck_prime{};
    ck_prime.fill(0xcc);
    std::array<uint8_t, 16> ik_prime{};
    ik_prime.fill(0xdd);
    const std::string identity = "imsi-999700000000002";

    const auto keys_a = aka_crypto::eap::derive_keys(ck_prime, ik_prime, identity);
    const auto keys_b = aka_crypto::eap::derive_keys(ck_prime, ik_prime, identity);

    EXPECT_EQ(keys_a.k_encr, keys_b.k_encr);
    EXPECT_EQ(keys_a.k_aut, keys_b.k_aut);
    EXPECT_EQ(keys_a.k_re, keys_b.k_re);
    EXPECT_EQ(keys_a.msk, keys_b.msk);
    EXPECT_EQ(keys_a.emsk, keys_b.emsk);
}

TEST(EapAkaPrime, ChallengeRequestRoundTripsAndMacVerifies) {
    const auto rand = make_rand();
    std::array<uint8_t, 16> autn{};
    autn.fill(0x42);
    const auto k_aut = make_k_aut(0x11);
    const std::string network_name = "5G:mnc070.mcc999.3gppnetwork.org";

    const auto packet =
        aka_crypto::eap::build_challenge_request(7, rand, autn, network_name, k_aut);

    EXPECT_TRUE(aka_crypto::eap::verify_mac(packet, k_aut));

    const auto parsed = aka_crypto::eap::parse_challenge_request(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->rand, rand);
    EXPECT_EQ(parsed->autn, autn);
    EXPECT_EQ(parsed->kdf_input, network_name);
}

TEST(EapAkaPrime, ChallengeResponseRoundTripsAndMacVerifies) {
    const std::vector<uint8_t> res{0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04};
    const auto k_aut = make_k_aut(0x22);

    const auto packet = aka_crypto::eap::build_challenge_response(9, res, k_aut);

    EXPECT_TRUE(aka_crypto::eap::verify_mac(packet, k_aut));

    const auto parsed = aka_crypto::eap::parse_challenge_response(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->res, res);
}

TEST(EapAkaPrime, TamperedMacFailsVerification) {
    const std::vector<uint8_t> res{0x01, 0x02, 0x03, 0x04};
    const auto k_aut = make_k_aut(0x33);

    auto packet = aka_crypto::eap::build_challenge_response(1, res, k_aut);
    packet.back() ^= 0xff; // corrupt the last byte of AT_MAC's value

    EXPECT_FALSE(aka_crypto::eap::verify_mac(packet, k_aut));
}

TEST(EapAkaPrime, WrongKeyFailsVerification) {
    const std::vector<uint8_t> res{0x01, 0x02, 0x03, 0x04};
    const auto packet = aka_crypto::eap::build_challenge_response(1, res, make_k_aut(0x44));

    EXPECT_FALSE(aka_crypto::eap::verify_mac(packet, make_k_aut(0x55)));
}

TEST(EapAkaPrime, Base64RoundTrips) {
    const std::vector<uint8_t> data{0x00, 0xff, 0x10, 0x7f, 0x80, 0xaa, 0xbb, 0xcc, 0xdd};
    const auto encoded = aka_crypto::eap::base64_encode(data);
    const auto decoded = aka_crypto::eap::base64_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, data);
}

TEST(EapAkaPrime, Base64RoundTripsFullChallengePacket) {
    const auto rand = make_rand();
    std::array<uint8_t, 16> autn{};
    autn.fill(0x77);
    const auto packet = aka_crypto::eap::build_challenge_request(
        1, rand, autn, "5G:mnc070.mcc999.3gppnetwork.org", make_k_aut(0x66));
    const auto encoded = aka_crypto::eap::base64_encode(packet);
    const auto decoded = aka_crypto::eap::base64_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, packet);
}

TEST(EapAkaPrime, SuccessAndFailurePacketsAreFourBytes) {
    const auto success = aka_crypto::eap::build_success(3);
    const auto failure = aka_crypto::eap::build_failure(3);
    EXPECT_EQ(success.size(), 4u);
    EXPECT_EQ(failure.size(), 4u);
    EXPECT_EQ(success[0], static_cast<uint8_t>(aka_crypto::eap::Code::kSuccess));
    EXPECT_EQ(failure[0], static_cast<uint8_t>(aka_crypto::eap::Code::kFailure));
}
