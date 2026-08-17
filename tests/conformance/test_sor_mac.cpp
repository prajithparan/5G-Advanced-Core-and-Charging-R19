// Verifies libs/aka-crypto's SoR-MAC-IAUSF / SoR-MAC-IUE derivations (TS 33.501 Annex A.17/A.18)
// -- gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #104, ADR-0081). 3GPP does not publish
// official test vectors for these two derivations (confirmed while researching this gap-closure:
// unlike MILENAGE, which test_milenage.cpp verifies against a real published TS 35.207 vector),
// so verification here is self-consistency (determinism, sensitivity to each real KDF input, and
// the documented 2-param vs 3-param P2-optional behavior) plus a structural cross-check that
// derive_sor_mac_iausf/derive_sor_mac_iue really do route through generic_kdf with the exact real
// FC values and parameter order Annex A.17/A.18 specify, not an independently-reimplemented (and
// possibly silently divergent) construction.

#include <array>
#include <cstdint>
#include <vector>

#include "aka_crypto/kdf.hpp"

#include <gtest/gtest.h>

namespace {

aka_crypto::Kausf make_kausf() {
    aka_crypto::Kausf k{};
    for (size_t i = 0; i < k.size(); ++i) {
        k[i] = static_cast<uint8_t>(i);
    }
    return k;
}

} // namespace

TEST(SorMac, IausfIsDeterministic) {
    const auto kausf = make_kausf();
    const std::vector<uint8_t> header{0x01, 0x02, 0x03};
    const auto mac1 = aka_crypto::derive_sor_mac_iausf(kausf, header, 1, nullptr);
    const auto mac2 = aka_crypto::derive_sor_mac_iausf(kausf, header, 1, nullptr);
    EXPECT_EQ(mac1, mac2);
}

TEST(SorMac, IausfMatchesDirectGenericKdfCall) {
    // Structural cross-check: FC=0x77, params = [sorHeader, counterSoR-as-2-BE-bytes], 128 LSBs
    // of the 32-byte HMAC-SHA-256 output -- built independently here via the same generic_kdf
    // primitive every other Annex A derivation in this codebase already uses, not copied from
    // derive_sor_mac_iausf's own implementation.
    const aka_crypto::Kausf kausf = make_kausf();
    const std::vector<uint8_t> header{0xAA, 0xBB};
    const uint16_t counter = 0x0001;

    const std::vector<uint8_t> kausf_bytes(kausf.begin(), kausf.end());
    const std::vector<uint8_t> counter_be{0x00, 0x01};
    const auto expected_full = aka_crypto::generic_kdf(kausf_bytes, 0x77, {header, counter_be});
    aka_crypto::SorMac expected{};
    std::copy(expected_full.begin() + 16, expected_full.end(), expected.begin());

    const auto actual = aka_crypto::derive_sor_mac_iausf(kausf, header, counter, nullptr);
    EXPECT_EQ(actual, expected);
}

TEST(SorMac, IausfChangesWithSorHeader) {
    const auto kausf = make_kausf();
    const auto mac_a = aka_crypto::derive_sor_mac_iausf(kausf, {0x01, 0x02}, 1, nullptr);
    const auto mac_b = aka_crypto::derive_sor_mac_iausf(kausf, {0x01, 0x03}, 1, nullptr);
    EXPECT_NE(mac_a, mac_b);
}

TEST(SorMac, IausfChangesWithCounterSor) {
    const auto kausf = make_kausf();
    const std::vector<uint8_t> header{0x01, 0x02};
    const auto mac_a = aka_crypto::derive_sor_mac_iausf(kausf, header, 1, nullptr);
    const auto mac_b = aka_crypto::derive_sor_mac_iausf(kausf, header, 2, nullptr);
    EXPECT_NE(mac_a, mac_b);
}

TEST(SorMac, IausfWithAndWithoutSteeringInfoListDiffer) {
    // Real spec behavior (Annex A.17): P2/L2 is included only when the caller's own
    // Nausf_SoRProtection request supplied steering-list content -- confirms the optional
    // 3rd-parameter code path is real and reachable, not dead code.
    const auto kausf = make_kausf();
    const std::vector<uint8_t> header{0x01, 0x02};
    const std::vector<uint8_t> steering_list{0xDE, 0xAD, 0xBE, 0xEF};
    const auto mac_without = aka_crypto::derive_sor_mac_iausf(kausf, header, 1, nullptr);
    const auto mac_with = aka_crypto::derive_sor_mac_iausf(kausf, header, 1, &steering_list);
    EXPECT_NE(mac_without, mac_with);
}

TEST(SorMac, IueMatchesDirectGenericKdfCall) {
    // FC=0x78, params = [0x01 (fixed ack octet), counterSoR-as-2-BE-bytes] per Annex A.18.
    const aka_crypto::Kausf kausf = make_kausf();
    const uint16_t counter = 0x0007;
    const std::vector<uint8_t> kausf_bytes(kausf.begin(), kausf.end());
    const std::vector<uint8_t> ack{0x01};
    const std::vector<uint8_t> counter_be{0x00, 0x07};
    const auto expected_full = aka_crypto::generic_kdf(kausf_bytes, 0x78, {ack, counter_be});
    aka_crypto::SorMac expected{};
    std::copy(expected_full.begin() + 16, expected_full.end(), expected.begin());

    const auto actual = aka_crypto::derive_sor_mac_iue(kausf, counter);
    EXPECT_EQ(actual, expected);
}

TEST(SorMac, IausfAndIueAreIndependentEvenWithSameCounter) {
    // Different FC (0x77 vs 0x78) and different P0 (real header vs fixed 0x01 ack octet) mean
    // these must never collide even when CounterSoR happens to match, which the real protocol
    // relies on (the AUSF computes both under the same counter value for one round, per
    // clause 6.14.2.3's own "same CounterSoR value ... reused" rule).
    const auto kausf = make_kausf();
    const auto mac_iausf = aka_crypto::derive_sor_mac_iausf(kausf, {0x01, 0x02}, 5, nullptr);
    const auto mac_iue = aka_crypto::derive_sor_mac_iue(kausf, 5);
    EXPECT_NE(mac_iausf, mac_iue);
}
