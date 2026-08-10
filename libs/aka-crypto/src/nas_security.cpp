#include "aka_crypto/nas_security.hpp"

#include <openssl/evp.h>
#include <openssl/params.h>

#include <array>
#include <stdexcept>

namespace aka_crypto {

namespace {

// TS 33.401 Annex B.1.3 (128-EEA2) IV / Annex B.2.3 (128-EIA2) M-prefix share the same first-5-byte
// packing: COUNT(32 bits, big-endian) then one byte of BEARER(5 bits, high) | DIRECTION(1 bit) |
// 00(2 bits spare). The two formats differ only in total padding length after that shared prefix
// (EEA2's IV pads to a full 16-byte block; EIA2's M-prefix is exactly 8 bytes before the message),
// so this helper produces just the shared 5-byte prefix and callers zero-pad the rest themselves.
std::array<uint8_t, 5>
count_bearer_direction_prefix(uint32_t count, uint8_t bearer, uint8_t direction) {
    std::array<uint8_t, 5> out{};
    out[0] = static_cast<uint8_t>((count >> 24) & 0xff);
    out[1] = static_cast<uint8_t>((count >> 16) & 0xff);
    out[2] = static_cast<uint8_t>((count >> 8) & 0xff);
    out[3] = static_cast<uint8_t>(count & 0xff);
    out[4] = static_cast<uint8_t>(((bearer & 0x1F) << 3) | ((direction & 0x1) << 2));
    return out;
}

} // namespace

std::vector<uint8_t> nea2_apply(const NasEncKey& key,
                                uint32_t count,
                                uint8_t bearer,
                                uint8_t direction,
                                const std::vector<uint8_t>& data) {
    std::array<uint8_t, 16> iv{}; // remaining 11 bytes stay zero, per Annex B.1.3
    const auto prefix = count_bearer_direction_prefix(count, bearer, direction);
    std::copy(prefix.begin(), prefix.end(), iv.begin());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }
    std::vector<uint8_t> out(data.size());
    int out_len = 0;
    int final_len = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data()) == 1;
    ok = ok && (data.empty() ||
                EVP_EncryptUpdate(
                    ctx, out.data(), &out_len, data.data(), static_cast<int>(data.size())) == 1);
    ok = ok && (EVP_EncryptFinal_ex(ctx, out.data() + out_len, &final_len) == 1);
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        throw std::runtime_error("128-NEA2 (AES-128-CTR) apply failed");
    }
    return out;
}

uint32_t nia2_mac(const NasIntKey& key,
                  uint32_t count,
                  uint8_t bearer,
                  uint8_t direction,
                  const std::vector<uint8_t>& message) {
    std::vector<uint8_t> input;
    const auto prefix = count_bearer_direction_prefix(count, bearer, direction);
    input.insert(input.end(), prefix.begin(), prefix.end());
    input.insert(input.end(), {0x00, 0x00, 0x00}); // pad the 5-byte prefix to 8 bytes, Annex B.2.3
    input.insert(input.end(), message.begin(), message.end());

    EVP_MAC* mac_algo = EVP_MAC_fetch(nullptr, "CMAC", nullptr);
    if (mac_algo == nullptr) {
        throw std::runtime_error("EVP_MAC_fetch(CMAC) failed");
    }
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac_algo);
    EVP_MAC_free(mac_algo);
    if (ctx == nullptr) {
        throw std::runtime_error("EVP_MAC_CTX_new failed");
    }

    const OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("cipher", const_cast<char*>("AES-128-CBC"), 0),
        OSSL_PARAM_construct_end(),
    };

    std::array<uint8_t, 16> full_mac{};
    size_t out_len = 0;
    bool ok = EVP_MAC_init(ctx, key.data(), key.size(), params) == 1;
    ok = ok && (EVP_MAC_update(ctx, input.data(), input.size()) == 1);
    ok = ok && (EVP_MAC_final(ctx, full_mac.data(), &out_len, full_mac.size()) == 1);
    EVP_MAC_CTX_free(ctx);
    if (!ok || out_len != full_mac.size()) {
        throw std::runtime_error("128-NIA2 (AES-128-CMAC) compute failed");
    }

    return (static_cast<uint32_t>(full_mac[0]) << 24) | (static_cast<uint32_t>(full_mac[1]) << 16) |
           (static_cast<uint32_t>(full_mac[2]) << 8) | static_cast<uint32_t>(full_mac[3]);
}

} // namespace aka_crypto
