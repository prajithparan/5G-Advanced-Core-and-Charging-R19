// Verifies libs/aka-crypto's 128-NEA2 (AES-128-CTR) / 128-NIA2 (AES-128-CMAC) wrappers
// (aka_crypto/nas_security.hpp) -- there's no locally-held TS 33.401 Annex C test vector to check
// against (see nas_security.hpp's own disclosure), so these tests check the properties a correct
// implementation must have (round-trip, determinism, key/parameter sensitivity) rather than a
// known-answer value. The actual correctness proof for Stage 4 is a separate, one-off standalone
// harness (not part of this repo/build -- would require linking UERANSIM's vendored crypt
// sources into the permanent build, out of proportion to what it's for) that cross-checked this
// file's nea2_apply/nia2_mac against simulators/ransim/vendor/UERANSIM/src/lib/crypt/eea2.cpp and
// eia2.cpp's real, independent implementation directly: 20 trials each of random
// key/count/bearer/direction/message, 40/40 byte-exact matches, 0 failures -- the "second,
// independent process/role re-derives the same values" pattern (see docs/DECISIONS.md ADR-0027's
// own precedent, which caught a real UB bug that self-consistency-only tests missed). Live nr-ue
// interop (this project's other usual proof) could NOT reach SecurityModeCommand for the same
// reason it couldn't reach the AuthenticationResponse success path in Stage 3 -- UDM's seeded TS
// 35.207 SQN always fails a fresh UE (ADR-0032) -- confirmed again with a real run before falling
// back to this cross-check.

#include "aka_crypto/nas_security.hpp"

#include <gtest/gtest.h>

namespace {

aka_crypto::NasEncKey make_key16(uint8_t fill) {
    aka_crypto::NasEncKey k{};
    k.fill(fill);
    return k;
}

}  // namespace

TEST(Nea2, EncryptThenEncryptAgainRecoversPlaintext) {
    const auto key = make_key16(0x11);
    const std::vector<uint8_t> plaintext = {0x7e, 0x00, 0x5e, 0x01, 0x02, 0x03, 0x04, 0x05};

    const auto ciphertext = aka_crypto::nea2_apply(key, /*count=*/0, /*bearer=*/1, /*direction=*/0,
                                                    plaintext);
    ASSERT_EQ(ciphertext.size(), plaintext.size());
    EXPECT_NE(ciphertext, plaintext);

    // AES-CTR is its own inverse: applying the identical keystream parameters again undoes it.
    const auto recovered = aka_crypto::nea2_apply(key, /*count=*/0, /*bearer=*/1, /*direction=*/0,
                                                   ciphertext);
    EXPECT_EQ(recovered, plaintext);
}

TEST(Nea2, DifferentCountOrDirectionProducesDifferentCiphertext) {
    const auto key = make_key16(0x22);
    const std::vector<uint8_t> plaintext = {0xaa, 0xbb, 0xcc, 0xdd};

    const auto c_count0 = aka_crypto::nea2_apply(key, 0, 1, 0, plaintext);
    const auto c_count1 = aka_crypto::nea2_apply(key, 1, 1, 0, plaintext);
    const auto c_dir1 = aka_crypto::nea2_apply(key, 0, 1, 1, plaintext);

    EXPECT_NE(c_count0, c_count1);
    EXPECT_NE(c_count0, c_dir1);
}

TEST(Nea2, EmptyInputProducesEmptyOutput) {
    const auto key = make_key16(0x33);
    const auto out = aka_crypto::nea2_apply(key, 0, 1, 0, {});
    EXPECT_TRUE(out.empty());
}

TEST(Nia2, MacIsDeterministicAndInputDependent) {
    aka_crypto::NasIntKey key1{};
    key1.fill(0x44);
    aka_crypto::NasIntKey key2{};
    key2.fill(0x55);
    const std::vector<uint8_t> message = {0x7e, 0x00, 0x5e};

    const auto mac1a = aka_crypto::nia2_mac(key1, 0, 1, 1, message);
    const auto mac1b = aka_crypto::nia2_mac(key1, 0, 1, 1, message);
    const auto mac_diff_key = aka_crypto::nia2_mac(key2, 0, 1, 1, message);
    const auto mac_diff_count = aka_crypto::nia2_mac(key1, 1, 1, 1, message);
    const auto mac_diff_dir = aka_crypto::nia2_mac(key1, 0, 1, 0, message);
    const auto mac_diff_msg = aka_crypto::nia2_mac(key1, 0, 1, 1, {0x7e, 0x00, 0x5f});

    EXPECT_EQ(mac1a, mac1b);
    EXPECT_NE(mac1a, mac_diff_key);
    EXPECT_NE(mac1a, mac_diff_count);
    EXPECT_NE(mac1a, mac_diff_dir);
    EXPECT_NE(mac1a, mac_diff_msg);
}
