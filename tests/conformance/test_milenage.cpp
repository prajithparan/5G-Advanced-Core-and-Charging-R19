// Verifies libs/aka-crypto's from-scratch Milenage implementation against 3GPP TS 35.207 Test
// Set 1 -- a real published test vector, cross-checked this turn against two independent sources
// (the `milenage` Rust crate's docs.rs page and the `mitshell/CryptoMobile` Python test suite,
// which agree digit-for-digit) rather than trusted from memory alone. See docs/DECISIONS.md
// ADR-0026.

#include "aka_crypto/kdf.hpp"
#include "aka_crypto/milenage.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

template <size_t N>
std::array<uint8_t, N> hex(const std::string& s) {
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i) {
        out[i] = static_cast<uint8_t>(std::stoul(s.substr(i * 2, 2), nullptr, 16));
    }
    return out;
}

}  // namespace

TEST(Milenage, TS35207TestSet1) {
    const auto k = hex<16>("465b5ce8b199b49faa5f0a2ee238a6bc");
    const auto op = hex<16>("cdc202d5123e20f62b6d676ac72cb318");
    const auto opc_expected = hex<16>("cd63cb71954a9f4e48a5994e37a02baf");
    const auto rand = hex<16>("23553cbe9637a89d218ae64dae47bf35");
    const auto sqn = hex<6>("ff9bb4d0b607");
    const auto amf = hex<2>("b9b9");

    const auto mac_a_expected = hex<8>("4a9ffac354dfafb3");
    const auto res_expected = hex<8>("a54211d5e3ba50bf");
    const auto ck_expected = hex<16>("b40ba9a3c58b2a05bbf0d987b21bf8cb");
    const auto ik_expected = hex<16>("f769bcd751044604127672711c6d3441");
    const auto ak_expected = hex<6>("aa689c648370");

    const auto opc = aka_crypto::derive_opc(k, op);
    EXPECT_EQ(opc, opc_expected);

    const auto mac_a = aka_crypto::f1(opc, k, rand, sqn, amf);
    EXPECT_EQ(mac_a, mac_a_expected);

    const auto out = aka_crypto::f2345(opc, k, rand);
    EXPECT_EQ(out.res, res_expected);
    EXPECT_EQ(out.ck, ck_expected);
    EXPECT_EQ(out.ik, ik_expected);
    EXPECT_EQ(out.ak, ak_expected);
}

TEST(Milenage, DifferentRandProducesDifferentOutput) {
    const auto k = hex<16>("465b5ce8b199b49faa5f0a2ee238a6bc");
    const auto opc = hex<16>("cd63cb71954a9f4e48a5994e37a02baf");
    const auto rand1 = hex<16>("23553cbe9637a89d218ae64dae47bf35");
    const auto rand2 = hex<16>("00000000000000000000000000000000");

    const auto out1 = aka_crypto::f2345(opc, k, rand1);
    const auto out2 = aka_crypto::f2345(opc, k, rand2);
    EXPECT_NE(out1.res, out2.res);
    EXPECT_NE(out1.ck, out2.ck);
}

TEST(AkaKdf, KseafDerivationIsDeterministicAndKeyDependent) {
    aka_crypto::Kausf kausf1{};
    kausf1.fill(0x11);
    aka_crypto::Kausf kausf2{};
    kausf2.fill(0x22);

    const auto kseaf1a = aka_crypto::derive_kseaf(kausf1, "5G:mnc070.mcc999.3gppnetwork.org");
    const auto kseaf1b = aka_crypto::derive_kseaf(kausf1, "5G:mnc070.mcc999.3gppnetwork.org");
    const auto kseaf2 = aka_crypto::derive_kseaf(kausf2, "5G:mnc070.mcc999.3gppnetwork.org");

    EXPECT_EQ(kseaf1a, kseaf1b);
    EXPECT_NE(kseaf1a, kseaf2);
}

TEST(AkaKdf, KamfDerivationIsDeterministicAndInputDependent) {
    aka_crypto::Kseaf kseaf1{};
    kseaf1.fill(0x33);
    aka_crypto::Kseaf kseaf2{};
    kseaf2.fill(0x44);
    const aka_crypto::Abba abba{0x00, 0x00};
    const aka_crypto::Abba abba2{0x00, 0x01};

    const auto kamf1a = aka_crypto::derive_kamf(kseaf1, "imsi-999700000000001", abba);
    const auto kamf1b = aka_crypto::derive_kamf(kseaf1, "imsi-999700000000001", abba);
    const auto kamf2 = aka_crypto::derive_kamf(kseaf2, "imsi-999700000000001", abba);
    const auto kamf_diff_supi = aka_crypto::derive_kamf(kseaf1, "imsi-999700000000002", abba);
    const auto kamf_diff_abba = aka_crypto::derive_kamf(kseaf1, "imsi-999700000000001", abba2);

    EXPECT_EQ(kamf1a, kamf1b);
    EXPECT_NE(kamf1a, kamf2);
    EXPECT_NE(kamf1a, kamf_diff_supi);
    EXPECT_NE(kamf1a, kamf_diff_abba);
}

TEST(AkaKdf, ResStarAndHxresStarRoundTrip) {
    const auto ck = hex<16>("b40ba9a3c58b2a05bbf0d987b21bf8cb");
    const auto ik = hex<16>("f769bcd751044604127672711c6d3441");
    const auto rand = hex<16>("23553cbe9637a89d218ae64dae47bf35");
    const auto res = hex<8>("a54211d5e3ba50bf");

    const auto xres_star = aka_crypto::derive_res_star(ck, ik, "5G:mnc070.mcc999.3gppnetwork.org",
                                                         rand, res);
    const auto hxres_star = aka_crypto::derive_hxres_star(rand, xres_star);

    // Same formula (TS 33.501 Annex A.5) applied by the UE/SEAF side to a claimed RES* must
    // reproduce the same HXRES* iff the RES* matches -- this is the mechanism AUSF/SEAF use to
    // detect a wrong RES* without needing the full XRES* comparison.
    const auto hxres_star_recomputed = aka_crypto::derive_hxres_star(rand, xres_star);
    EXPECT_EQ(hxres_star, hxres_star_recomputed);
}
