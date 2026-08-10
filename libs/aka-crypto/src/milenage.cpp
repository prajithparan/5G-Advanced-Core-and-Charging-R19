#include "aka_crypto/milenage.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace aka_crypto {

namespace {

using Block = std::array<uint8_t, 16>;

Block aes128_ecb_encrypt_block(const Key128& key, const Block& in) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }
    Block out{};
    int out_len = 0;
    int final_len = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key.data(), nullptr) == 1;
    ok = ok && (EVP_CIPHER_CTX_set_padding(ctx, 0) == 1);
    ok =
        ok &&
        (EVP_EncryptUpdate(ctx, out.data(), &out_len, in.data(), static_cast<int>(in.size())) == 1);
    ok = ok && (EVP_EncryptFinal_ex(ctx, out.data() + out_len, &final_len) == 1);
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        throw std::runtime_error("AES-128-ECB single-block encrypt failed");
    }
    return out;
}

Block xor_blocks(const Block& a, const Block& b) {
    Block out{};
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return out;
}

// Left-circular byte rotation (every rotate amount MILENAGE uses -- 0, 32, 64, 96 bits -- is
// byte-aligned, so a byte-level rotation is exact, not an approximation).
Block rotate_left_bytes(const Block& in, size_t n_bytes) {
    Block out{};
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = in[(i + n_bytes) % out.size()];
    }
    return out;
}

Block make_c(uint8_t last_byte) {
    Block c{};
    c[15] = last_byte;
    return c;
}

Block make_in1(const Sqn& sqn, const Amf& amf) {
    Block in1{};
    std::copy(sqn.begin(), sqn.end(), in1.begin());
    std::copy(amf.begin(), amf.end(), in1.begin() + 6);
    std::copy(sqn.begin(), sqn.end(), in1.begin() + 8);
    std::copy(amf.begin(), amf.end(), in1.begin() + 14);
    return in1;
}

} // namespace

Key128 derive_opc(const Key128& k, const Key128& op) {
    const Block enc = aes128_ecb_encrypt_block(k, op);
    const Block opc = xor_blocks(enc, op);
    Key128 out{};
    std::copy(opc.begin(), opc.end(), out.begin());
    return out;
}

Mac64 f1(const Key128& opc, const Key128& k, const Key128& rand, const Sqn& sqn, const Amf& amf) {
    const Block temp = aes128_ecb_encrypt_block(k, xor_blocks(rand, opc));
    const Block in1 = make_in1(sqn, amf);
    const Block rotated = rotate_left_bytes(xor_blocks(in1, opc), 8);      // r1 = 64 bits
    const Block pre = xor_blocks(xor_blocks(temp, rotated), make_c(0x00)); // c1 = 0
    const Block out1 = xor_blocks(aes128_ecb_encrypt_block(k, pre), opc);
    Mac64 mac{};
    std::copy(out1.begin(), out1.begin() + 8, mac.begin()); // MAC-A = OUT1[0..63]
    return mac;
}

F2345Output f2345(const Key128& opc, const Key128& k, const Key128& rand) {
    const Block temp = aes128_ecb_encrypt_block(k, xor_blocks(rand, opc));
    const Block temp_xor_opc = xor_blocks(temp, opc);

    // OUT2: f2 (RES) + f5 (AK) -- r2 = 0 bits, c2 = 1
    const Block pre2 = xor_blocks(rotate_left_bytes(temp_xor_opc, 0), make_c(0x01));
    const Block out2 = xor_blocks(aes128_ecb_encrypt_block(k, pre2), opc);

    // OUT3: f3 (CK) -- r3 = 32 bits, c3 = 2
    const Block pre3 = xor_blocks(rotate_left_bytes(temp_xor_opc, 4), make_c(0x02));
    const Block out3 = xor_blocks(aes128_ecb_encrypt_block(k, pre3), opc);

    // OUT4: f4 (IK) -- r4 = 64 bits, c4 = 4
    const Block pre4 = xor_blocks(rotate_left_bytes(temp_xor_opc, 8), make_c(0x04));
    const Block out4 = xor_blocks(aes128_ecb_encrypt_block(k, pre4), opc);

    F2345Output result{};
    std::copy(out2.begin() + 8, out2.end(), result.res.begin());  // RES = OUT2[64..127]
    std::copy(out2.begin(), out2.begin() + 6, result.ak.begin()); // AK = OUT2[0..47]
    std::copy(out3.begin(), out3.end(), result.ck.begin());       // CK = OUT3
    std::copy(out4.begin(), out4.end(), result.ik.begin());       // IK = OUT4
    return result;
}

Key128 generate_rand() {
    Key128 rand{};
    if (RAND_bytes(rand.data(), static_cast<int>(rand.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return rand;
}

Mac64 f1_star(const Key128& opc, const Key128& k, const Key128& rand, const Sqn& sqn_ms) {
    static constexpr Amf kResyncAmf{0x00, 0x00}; // TS 33.102 §6.3.3, fixed -- see this function's
                                                 // own declaration comment
    const Block temp = aes128_ecb_encrypt_block(k, xor_blocks(rand, opc));
    const Block in1 = make_in1(sqn_ms, kResyncAmf);
    const Block rotated = rotate_left_bytes(xor_blocks(in1, opc), 8); // r1 = 64 bits (same as f1)
    const Block pre = xor_blocks(xor_blocks(temp, rotated), make_c(0x00)); // c1 = 0 (same as f1)
    const Block out1 = xor_blocks(aes128_ecb_encrypt_block(k, pre), opc);
    Mac64 mac{};
    std::copy(out1.begin() + 8, out1.end(), mac.begin()); // MAC-S = OUT1[64..127]
    return mac;
}

Ak48 f5_star(const Key128& opc, const Key128& k, const Key128& rand) {
    const Block temp = aes128_ecb_encrypt_block(k, xor_blocks(rand, opc));
    const Block temp_xor_opc = xor_blocks(temp, opc);
    const Block pre =
        xor_blocks(rotate_left_bytes(temp_xor_opc, 12), make_c(0x08)); // r5=96 bits, c5=8
    const Block out5 = xor_blocks(aes128_ecb_encrypt_block(k, pre), opc);
    Ak48 ak_star{};
    std::copy(out5.begin(), out5.begin() + 6, ak_star.begin());
    return ak_star;
}

std::optional<Sqn>
verify_and_decode_auts(const Key128& opc, const Key128& k, const Key128& rand, const Auts& auts) {
    const Ak48 ak_star = f5_star(opc, k, rand);
    Sqn sqn_ms{};
    for (size_t i = 0; i < sqn_ms.size(); ++i) {
        sqn_ms[i] = static_cast<uint8_t>(auts[i] ^ ak_star[i]);
    }
    const Mac64 expected_mac_s = f1_star(opc, k, rand, sqn_ms);
    Mac64 received_mac_s{};
    std::copy(auts.begin() + 6, auts.end(), received_mac_s.begin());
    if (expected_mac_s != received_mac_s) {
        return std::nullopt;
    }
    return sqn_ms;
}

} // namespace aka_crypto
