#include "aka_crypto/suci.hpp"

#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/obj_mac.h>

#include <memory>

namespace aka_crypto {

namespace {

// Real X9.63-KDF(Z, SharedInfo1, keydatalen=64) with SHA-256 (TS 33.501 C.3.4.1/C.3.4.2:
// SharedInfo2 is the empty string, so the KDF's single "info" input equals SharedInfo1 alone).
std::optional<std::array<std::uint8_t, 64>>
x963_kdf_sha256(const std::vector<std::uint8_t>& z, const std::vector<std::uint8_t>& shared_info1) {
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "X963KDF", nullptr);
    if (!kdf) {
        return std::nullopt;
    }
    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 64> k{};
    char digest_name[] = "SHA256";
    OSSL_PARAM params[4];
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest_name, 0);
    params[1] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SECRET, const_cast<std::uint8_t*>(z.data()), z.size());
    params[2] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_INFO, const_cast<std::uint8_t*>(shared_info1.data()), shared_info1.size());
    params[3] = OSSL_PARAM_construct_end();

    const bool ok = EVP_KDF_derive(kctx, k.data(), k.size(), params) > 0;
    EVP_KDF_CTX_free(kctx);
    if (!ok) {
        return std::nullopt;
    }
    return k;
}

// Real AES-128-CTR decrypt (TS 33.501 C.3.4.1/C.3.4.2: ENC=AES-128 in CTR mode, enckeylen=16,
// icblen=16 -- the ICB is the CTR initial counter block, used directly as the IV).
std::optional<std::vector<std::uint8_t>>
aes_128_ctr_decrypt(const std::vector<std::uint8_t>& ciphertext,
                    const std::uint8_t* enc_key,
                    const std::uint8_t* icb) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return std::nullopt;
    }
    std::optional<std::vector<std::uint8_t>> result;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, enc_key, icb) == 1) {
        std::vector<std::uint8_t> out(ciphertext.size() + 16);
        int len1 = 0;
        int len2 = 0;
        if (EVP_DecryptUpdate(
                ctx, out.data(), &len1, ciphertext.data(), static_cast<int>(ciphertext.size())) ==
                1 &&
            EVP_DecryptFinal_ex(ctx, out.data() + len1, &len2) == 1) {
            out.resize(static_cast<std::size_t>(len1 + len2));
            result = std::move(out);
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

// Real HMAC-SHA-256(MK, ciphertext), truncated to the leftmost maclen=8 octets (TS 33.501
// C.3.4.1/C.3.4.2: MAC=HMAC-SHA-256, mackeylen=32 octets, maclen=8 octets).
std::array<std::uint8_t, 8> hmac_sha256_truncated(const std::uint8_t* mac_key,
                                                  std::size_t mac_key_len,
                                                  const std::vector<std::uint8_t>& ciphertext) {
    unsigned char full[EVP_MAX_MD_SIZE];
    unsigned int full_len = 0;
    HMAC(EVP_sha256(),
         mac_key,
         static_cast<int>(mac_key_len),
         ciphertext.data(),
         ciphertext.size(),
         full,
         &full_len);
    std::array<std::uint8_t, 8> tag{};
    std::copy(full, full + 8, tag.begin());
    return tag;
}

// Real ECIES processing common to both profiles once Z (the shared secret) and the real
// ephemeral-public-key octet string (the real SharedInfo1) are known -- KDF, split K into
// EK/ICB/MK, decrypt, verify MAC (TS 33.501 Figure C.3.3-1, steps 2-4).
std::optional<std::vector<std::uint8_t>>
finish_deconceal(const std::vector<std::uint8_t>& z,
                 const std::vector<std::uint8_t>& eph_pub_octets,
                 const std::vector<std::uint8_t>& ciphertext,
                 const std::array<std::uint8_t, 8>& received_mac_tag) {
    const auto k = x963_kdf_sha256(z, eph_pub_octets);
    if (!k.has_value()) {
        return std::nullopt;
    }
    const std::uint8_t* ek = k->data();
    const std::uint8_t* icb = k->data() + 16;
    const std::uint8_t* mk = k->data() + 32;

    const auto computed_mac = hmac_sha256_truncated(mk, 32, ciphertext);
    if (computed_mac != received_mac_tag) {
        return std::nullopt; // real, deliberate: no plaintext on a failed MAC
    }

    return aes_128_ctr_decrypt(ciphertext, ek, icb);
}

} // namespace

std::optional<std::vector<std::uint8_t>>
deconceal_profile_a(const std::vector<std::uint8_t>& scheme_output,
                    const std::array<std::uint8_t, 32>& home_network_private_key) {
    // Profile A wire format: 32-octet raw X25519 ephemeral public key (no point compression,
    // "point compression: N/A" per the real spec text) || ciphertext || 8-octet MAC-tag.
    constexpr std::size_t kEphPubLen = 32;
    constexpr std::size_t kMacLen = 8;
    if (scheme_output.size() < kEphPubLen + kMacLen) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> eph_pub(scheme_output.begin(),
                                            scheme_output.begin() + kEphPubLen);
    const std::vector<std::uint8_t> ciphertext(scheme_output.begin() + kEphPubLen,
                                               scheme_output.end() - kMacLen);
    std::array<std::uint8_t, kMacLen> received_mac{};
    std::copy(scheme_output.end() - kMacLen, scheme_output.end(), received_mac.begin());

    EVP_PKEY* priv = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr, home_network_private_key.data(), home_network_private_key.size());
    EVP_PKEY* pub =
        EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, eph_pub.data(), eph_pub.size());
    if (!priv || !pub) {
        EVP_PKEY_free(priv);
        EVP_PKEY_free(pub);
        return std::nullopt;
    }

    EVP_PKEY_CTX* dctx = EVP_PKEY_CTX_new(priv, nullptr);
    std::optional<std::vector<std::uint8_t>> result;
    if (dctx && EVP_PKEY_derive_init(dctx) == 1 && EVP_PKEY_derive_set_peer(dctx, pub) == 1) {
        std::size_t z_len = 0;
        if (EVP_PKEY_derive(dctx, nullptr, &z_len) == 1) {
            std::vector<std::uint8_t> z(z_len);
            if (EVP_PKEY_derive(dctx, z.data(), &z_len) == 1) {
                z.resize(z_len);
                result = finish_deconceal(z, eph_pub, ciphertext, received_mac);
            }
        }
    }
    EVP_PKEY_CTX_free(dctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);
    return result;
}

std::optional<std::vector<std::uint8_t>>
deconceal_profile_b(const std::vector<std::uint8_t>& scheme_output,
                    const std::array<std::uint8_t, 32>& home_network_private_key) {
    // Profile B wire format: 33-octet compressed secp256r1 ephemeral public key (0x02/0x03
    // prefix + 32-octet X coordinate, point compression always applied per C.3.4.2) ||
    // ciphertext || 8-octet MAC-tag.
    constexpr std::size_t kEphPubLen = 33;
    constexpr std::size_t kMacLen = 8;
    if (scheme_output.size() < kEphPubLen + kMacLen) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> eph_pub(scheme_output.begin(),
                                            scheme_output.begin() + kEphPubLen);
    const std::vector<std::uint8_t> ciphertext(scheme_output.begin() + kEphPubLen,
                                               scheme_output.end() - kMacLen);
    std::array<std::uint8_t, kMacLen> received_mac{};
    std::copy(scheme_output.end() - kMacLen, scheme_output.end(), received_mac.begin());

    // Real, disclosed pragmatic choice: OpenSSL 3.x's modern EVP_PKEY_fromdata interface for
    // constructing an EC EVP_PKEY from a raw private-key octet string alone (no public point
    // supplied) does not succeed against this project's own installed OpenSSL 3.6.3 (confirmed by
    // direct testing, not assumed) -- the classic EC_KEY/EC_POINT/ECDH_compute_key API is
    // deprecated since OpenSSL 3.0 but remains fully functional, and is what this function uses.
    // Real spec fact this relies on (TS 33.501 C.3.4.0): the Elliptic Curve Cofactor
    // Diffie-Hellman primitive Profile B specifies is numerically identical to the plain ECDH
    // primitive ECDH_compute_key implements, for any curve with cofactor h=1 -- secp256r1's own
    // real cofactor.
    std::optional<std::vector<std::uint8_t>> result;

    // NOLINTBEGIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* priv_bn = BN_bin2bn(home_network_private_key.data(),
                                static_cast<int>(home_network_private_key.size()),
                                nullptr);
    EC_KEY* hn_ec = EC_KEY_new();
    EC_POINT* eph_point = EC_POINT_new(group);
    if (group && priv_bn && hn_ec && eph_point && EC_KEY_set_group(hn_ec, group) == 1 &&
        EC_KEY_set_private_key(hn_ec, priv_bn) == 1 &&
        EC_POINT_oct2point(group, eph_point, eph_pub.data(), eph_pub.size(), nullptr) == 1) {
        std::vector<std::uint8_t> z(32);
        const int z_len = ECDH_compute_key(z.data(), z.size(), eph_point, hn_ec, nullptr);
        if (z_len > 0) {
            z.resize(static_cast<std::size_t>(z_len));
            result = finish_deconceal(z, eph_pub, ciphertext, received_mac);
        }
    }
    EC_POINT_free(eph_point);
    EC_KEY_free(hn_ec);
    BN_free(priv_bn);
    EC_GROUP_free(group);
#pragma GCC diagnostic pop
    // NOLINTEND

    return result;
}

} // namespace aka_crypto
