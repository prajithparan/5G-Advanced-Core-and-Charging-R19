#include "aka_crypto/kdf.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace aka_crypto {

namespace {

std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

template <size_t N> std::vector<uint8_t> to_bytes(const std::array<uint8_t, N>& a) {
    return std::vector<uint8_t>(a.begin(), a.end());
}

std::vector<uint8_t> len_prefix(const std::vector<uint8_t>& p) {
    const auto n = p.size();
    return {static_cast<uint8_t>((n >> 8) & 0xff), static_cast<uint8_t>(n & 0xff)};
}

// The counter's own 2-byte big-endian VALUE (a KDF parameter, Annex A.17/A.18's own P1) --
// distinct from len_prefix above (a KDF-framing length field) even though both happen to produce
// 2 big-endian bytes here, since CounterSoR is itself exactly 2 octets wide.
std::vector<uint8_t> be16(uint16_t v) {
    return {static_cast<uint8_t>((v >> 8) & 0xff), static_cast<uint8_t>(v & 0xff)};
}

// Uplink NAS COUNT's own 4-byte big-endian VALUE (TS 33.501 Annex A.9's own P0) -- same real
// "value, not framing length" distinction be16 above already carries for CounterSoR.
std::vector<uint8_t> be32(uint32_t v) {
    return {static_cast<uint8_t>((v >> 24) & 0xff),
            static_cast<uint8_t>((v >> 16) & 0xff),
            static_cast<uint8_t>((v >> 8) & 0xff),
            static_cast<uint8_t>(v & 0xff)};
}

} // namespace

std::array<uint8_t, 32> generic_kdf(const std::vector<uint8_t>& key,
                                    uint8_t fc,
                                    const std::vector<std::vector<uint8_t>>& params) {
    std::vector<uint8_t> s{fc};
    for (const auto& p : params) {
        s.insert(s.end(), p.begin(), p.end());
        const auto lp = len_prefix(p);
        s.insert(s.end(), lp.begin(), lp.end());
    }

    std::array<uint8_t, 32> out{};
    unsigned int out_len = 0;
    const uint8_t* result = HMAC(EVP_sha256(),
                                 key.data(),
                                 static_cast<int>(key.size()),
                                 s.data(),
                                 s.size(),
                                 out.data(),
                                 &out_len);
    if (result == nullptr || out_len != out.size()) {
        throw std::runtime_error("HMAC-SHA-256 failed in generic_kdf");
    }
    return out;
}

Ak48 sqn_xor_ak(const Sqn& sqn, const Ak48& ak) {
    Ak48 out{};
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<uint8_t>(sqn[i] ^ ak[i]);
    }
    return out;
}

Kausf derive_kausf(const Key128& ck,
                   const Key128& ik,
                   const std::string& serving_network_name,
                   const Ak48& sqn_xor_ak_value) {
    const auto key = concat({to_bytes(ck), to_bytes(ik)});
    return generic_kdf(key, 0x6A, {to_bytes(serving_network_name), to_bytes(sqn_xor_ak_value)});
}

std::pair<CkPrime, IkPrime> derive_ck_ik_prime(const Key128& ck,
                                               const Key128& ik,
                                               const std::string& serving_network_name,
                                               const Ak48& sqn_xor_ak_value) {
    const auto key = concat({to_bytes(ck), to_bytes(ik)});
    const auto out =
        generic_kdf(key, 0x20, {to_bytes(serving_network_name), to_bytes(sqn_xor_ak_value)});
    CkPrime ck_prime{};
    IkPrime ik_prime{};
    std::copy(out.begin(), out.begin() + 16, ck_prime.begin());
    std::copy(out.begin() + 16, out.end(), ik_prime.begin());
    return {ck_prime, ik_prime};
}

ResStar derive_res_star(const Key128& ck,
                        const Key128& ik,
                        const std::string& serving_network_name,
                        const Key128& rand,
                        const Res64& res) {
    const auto key = concat({to_bytes(ck), to_bytes(ik)});
    const auto out =
        generic_kdf(key, 0x6B, {to_bytes(serving_network_name), to_bytes(rand), to_bytes(res)});
    ResStar res_star{};
    std::copy(out.begin() + 16, out.end(), res_star.begin()); // right half of the 256-bit output
    return res_star;
}

HxresStar derive_hxres_star(const Key128& rand, const ResStar& res_star) {
    const auto input = concat({to_bytes(rand), to_bytes(res_star)});
    std::array<uint8_t, 32> digest{};
    unsigned int digest_len = 0;
    if (EVP_Digest(input.data(), input.size(), digest.data(), &digest_len, EVP_sha256(), nullptr) !=
            1 ||
        digest_len != digest.size()) {
        throw std::runtime_error("SHA-256 failed in derive_hxres_star");
    }
    HxresStar out{};
    std::copy(digest.begin(), digest.begin() + 16, out.begin()); // leftmost 128 bits
    return out;
}

Kseaf derive_kseaf(const Kausf& kausf, const std::string& serving_network_name) {
    return generic_kdf(to_bytes(kausf), 0x6C, {to_bytes(serving_network_name)});
}

Kamf derive_kamf(const Kseaf& kseaf, const std::string& supi, const Abba& abba) {
    return generic_kdf(to_bytes(kseaf), 0x6D, {to_bytes(supi), to_bytes(abba)});
}

NasEncKey derive_knas_enc(const Kamf& kamf, uint8_t algorithm_identity) {
    const auto out =
        generic_kdf(to_bytes(kamf), 0x69, {{kNasEncAlgorithmDistinguisher}, {algorithm_identity}});
    NasEncKey key{};
    std::copy(out.begin() + 16, out.end(), key.begin()); // rightmost 128 bits
    return key;
}

NasIntKey derive_knas_int(const Kamf& kamf, uint8_t algorithm_identity) {
    const auto out =
        generic_kdf(to_bytes(kamf), 0x69, {{kNasIntAlgorithmDistinguisher}, {algorithm_identity}});
    NasIntKey key{};
    std::copy(out.begin() + 16, out.end(), key.begin()); // rightmost 128 bits
    return key;
}

SorMac derive_sor_mac_iausf(const Kausf& kausf,
                            const std::vector<uint8_t>& sor_header,
                            uint16_t counter_sor,
                            const std::vector<uint8_t>* steering_info_list) {
    std::vector<std::vector<uint8_t>> params{sor_header, be16(counter_sor)};
    if (steering_info_list != nullptr) {
        params.push_back(*steering_info_list);
    }
    const auto out = generic_kdf(to_bytes(kausf), 0x77, params);
    SorMac mac{};
    std::copy(out.begin() + 16, out.end(), mac.begin()); // 128 LSBs
    return mac;
}

SorMac derive_sor_mac_iue(const Kausf& kausf, uint16_t counter_sor) {
    const auto out = generic_kdf(to_bytes(kausf), 0x78, {{0x01}, be16(counter_sor)});
    SorMac mac{};
    std::copy(out.begin() + 16, out.end(), mac.begin()); // 128 LSBs
    return mac;
}

Kgnb derive_kgnb(const Kamf& kamf, uint32_t uplink_nas_count, uint8_t access_type_distinguisher) {
    return generic_kdf(to_bytes(kamf), 0x6E, {be32(uplink_nas_count), {access_type_distinguisher}});
}

NextHopKey derive_nh(const Kamf& kamf, const std::array<uint8_t, 32>& sync_input) {
    return generic_kdf(to_bytes(kamf), 0x6F, {to_bytes(sync_input)});
}

} // namespace aka_crypto
