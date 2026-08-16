// Verifies libs/aka-crypto's SUCI de-concealment (ECIES Profile A/B, TS 33.501 Annex C) against
// the spec's own real, officially-published implementers' test data (Annex C.4.3/C.4.4,
// specs/TS_33_501.pdf V19.6.0, user-supplied). Same "cross-process independent re-derivation"
// rigor as this project's own Milenage verification (ADR-0026): every value below was
// independently checked to decrypt to the spec's own published Plaintext block / MAC-tag value
// via a standalone throwaway program before this file was written, matching this project's
// standing crypto-verification discipline for anything this security-sensitive.
//
// Real, disclosed: the Home Network Private Key values below are the spec's own real test-only
// values ("Not available in the UE, provided here only for test purposes" -- C.4.4.1's own text),
// never a real operational key, safe to commit.

#include <algorithm>

#include "aka_crypto/hex.hpp"
#include "aka_crypto/suci.hpp"

#include <gtest/gtest.h>

namespace {

std::array<std::uint8_t, 32> hex32(const std::string& s) {
    auto v = aka_crypto::from_hex(s);
    std::array<std::uint8_t, 32> out{};
    if (v.has_value() && v->size() == 32) {
        std::copy(v->begin(), v->end(), out.begin());
    }
    return out;
}

std::vector<std::uint8_t> hexv(const std::string& s) {
    return *aka_crypto::from_hex(s);
}

} // namespace

// TS 33.501 Annex C.4.3.1: ECIES Profile A, IMSI-based SUPI (MCC|MNC=274012, MSIN=001002086).
TEST(Suci, ProfileADeconcealsRealImsiTestVector) {
    const auto hn_priv = hex32("c53c22208b61860b06c62e5406a7b330c2b577aa5558981510d128247d38bd1d");
    const auto eph_pub = hexv("b2e92f836055a255837debf850b528997ce0201cb82adfe4be1f587d07d8457d");
    const auto ciphertext = hexv("cb02352410");
    const auto mac_tag = hexv("cddd9e730ef3fa87");
    const auto expected_plaintext = hexv("00012080f6"); // real packed-BCD MSIN "001002086"

    std::vector<std::uint8_t> scheme_output = eph_pub;
    scheme_output.insert(scheme_output.end(), ciphertext.begin(), ciphertext.end());
    scheme_output.insert(scheme_output.end(), mac_tag.begin(), mac_tag.end());

    const auto plaintext = aka_crypto::deconceal_profile_a(scheme_output, hn_priv);
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(*plaintext, expected_plaintext);
}

// TS 33.501 Annex C.4.3.2: ECIES Profile A, NAI-based SUPI ("verylongusername1@3gpp.com").
TEST(Suci, ProfileADeconcealsRealNaiTestVector) {
    const auto hn_priv = hex32("C53C22208B61860B06C62E5406A7B330C2B577AA5558981510D128247D38BD1D");
    const auto eph_pub = hexv("977D8B2FDAA7B64AA700D04227D5B440630EA4EC50F9082273A26BB678C92222");
    const auto ciphertext = hexv("8E358A1582ADB15322C10E515141D2039A");
    const auto mac_tag = hexv("12E1D7783A97F1AC");
    const auto expected_plaintext =
        hexv("766572796C6F6E67757365726E616D6531"); // "verylongusername1"

    std::vector<std::uint8_t> scheme_output = eph_pub;
    scheme_output.insert(scheme_output.end(), ciphertext.begin(), ciphertext.end());
    scheme_output.insert(scheme_output.end(), mac_tag.begin(), mac_tag.end());

    const auto plaintext = aka_crypto::deconceal_profile_a(scheme_output, hn_priv);
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(*plaintext, expected_plaintext);
}

// TS 33.501 Annex C.4.4.1: ECIES Profile B, IMSI-based SUPI (same real IMSI as C.4.3.1).
TEST(Suci, ProfileBDeconcealsRealImsiTestVector) {
    const auto hn_priv = hex32("F1AB1074477EBCC7F554EA1C5FC368B1616730155E0041AC447D6301975FECDA");
    const auto eph_pub =
        hexv("039AAB8376597021E855679A9778EA0B67396E68C66DF32C0F41E9ACCA2DA9B9D1"); // compressed
    const auto ciphertext = hexv("46A33FC271");
    const auto mac_tag = hexv("6AC7DAE96AA30A4D");
    const auto expected_plaintext = hexv("00012080F6");

    std::vector<std::uint8_t> scheme_output = eph_pub;
    scheme_output.insert(scheme_output.end(), ciphertext.begin(), ciphertext.end());
    scheme_output.insert(scheme_output.end(), mac_tag.begin(), mac_tag.end());

    const auto plaintext = aka_crypto::deconceal_profile_b(scheme_output, hn_priv);
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(*plaintext, expected_plaintext);
}

// TS 33.501 Annex C.4.4.2: ECIES Profile B, NAI-based SUPI (same real NAI as C.4.3.2).
TEST(Suci, ProfileBDeconcealsRealNaiTestVector) {
    const auto hn_priv = hex32("F1AB1074477EBCC7F554EA1C5FC368B1616730155E0041AC447D6301975FECDA");
    const auto eph_pub =
        hexv("03759BB22C563D9F4A6B3C1419E543FC2F39D6823F02A9D71162B39399218B244B"); // compressed
    const auto ciphertext = hexv("BE22D8B9F856A52ED381CD7EAF4CF2D525");
    const auto mac_tag = hexv("3CDDC61A0A7882EB");
    const auto expected_plaintext = hexv("766572796C6F6E67757365726E616D6531");

    std::vector<std::uint8_t> scheme_output = eph_pub;
    scheme_output.insert(scheme_output.end(), ciphertext.begin(), ciphertext.end());
    scheme_output.insert(scheme_output.end(), mac_tag.begin(), mac_tag.end());

    const auto plaintext = aka_crypto::deconceal_profile_b(scheme_output, hn_priv);
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(*plaintext, expected_plaintext);
}

// Real, deliberate negative tests: a tampered MAC-tag (real ECIES authenticated-encryption
// guarantee) and a too-short scheme-output must both fail closed (nullopt), never return a
// partial/best-effort plaintext.
TEST(Suci, ProfileARejectsTamperedMac) {
    const auto hn_priv = hex32("c53c22208b61860b06c62e5406a7b330c2b577aa5558981510d128247d38bd1d");
    const auto eph_pub = hexv("b2e92f836055a255837debf850b528997ce0201cb82adfe4be1f587d07d8457d");
    const auto ciphertext = hexv("cb02352410");
    auto mac_tag = hexv("cddd9e730ef3fa87");
    mac_tag[0] ^= 0xFF; // tamper

    std::vector<std::uint8_t> scheme_output = eph_pub;
    scheme_output.insert(scheme_output.end(), ciphertext.begin(), ciphertext.end());
    scheme_output.insert(scheme_output.end(), mac_tag.begin(), mac_tag.end());

    EXPECT_FALSE(aka_crypto::deconceal_profile_a(scheme_output, hn_priv).has_value());
}

TEST(Suci, ProfileARejectsTooShortSchemeOutput) {
    const auto hn_priv = hex32("c53c22208b61860b06c62e5406a7b330c2b577aa5558981510d128247d38bd1d");
    std::vector<std::uint8_t> too_short(10, 0);
    EXPECT_FALSE(aka_crypto::deconceal_profile_a(too_short, hn_priv).has_value());
}
