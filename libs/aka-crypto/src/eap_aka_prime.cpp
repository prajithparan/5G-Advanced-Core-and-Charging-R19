#include "aka_crypto/eap_aka_prime.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace aka_crypto::eap {

namespace {

std::array<uint8_t, 32> hmac_sha256(const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& data) {
    std::array<uint8_t, 32> out{};
    unsigned int out_len = 0;
    const uint8_t* result = HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                                  data.data(), data.size(), out.data(), &out_len);
    if (result == nullptr || out_len != out.size()) {
        throw std::runtime_error("HMAC-SHA-256 failed");
    }
    return out;
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& in) {
    out.insert(out.end(), in.begin(), in.end());
}

void append_u16be(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(v & 0xff));
}

// Pads `value` so the full attribute (2-byte Type+Length header + value) lands on a 4-octet
// boundary, per RFC 4187 Section 8.1 -- i.e. value.size() must end up congruent to 2 (mod 4), not
// congruent to 0.
void pad_value_to_word_boundary(std::vector<uint8_t>& value) {
    while ((value.size() + 2) % 4 != 0) {
        value.push_back(0);
    }
}

// Wraps `value` (already padded to a multiple of 4) as a full TLV: Type(1) + Length(1, in
// 4-octet words, includes these two bytes) + value.
std::vector<uint8_t> make_attr(uint8_t type, const std::vector<uint8_t>& value) {
    std::vector<uint8_t> attr{type, static_cast<uint8_t>((value.size() + 2) / 4)};
    append(attr, value);
    return attr;
}

std::vector<uint8_t> encode_rand_attr(const Key128& rand) {
    std::vector<uint8_t> value{0, 0};  // reserved
    append(value, std::vector<uint8_t>(rand.begin(), rand.end()));
    return make_attr(1, value);
}

std::vector<uint8_t> encode_autn_attr(const std::array<uint8_t, 16>& autn) {
    std::vector<uint8_t> value{0, 0};  // reserved
    append(value, std::vector<uint8_t>(autn.begin(), autn.end()));
    return make_attr(2, value);
}

std::vector<uint8_t> encode_mac_attr(const std::array<uint8_t, 16>& mac) {
    std::vector<uint8_t> value{0, 0};  // reserved
    append(value, std::vector<uint8_t>(mac.begin(), mac.end()));
    return make_attr(11, value);
}

std::vector<uint8_t> encode_kdf_input_attr(const std::string& network_name) {
    std::vector<uint8_t> value;
    append_u16be(value, static_cast<uint16_t>(network_name.size()));
    append(value, std::vector<uint8_t>(network_name.begin(), network_name.end()));
    pad_value_to_word_boundary(value);
    return make_attr(23, value);
}

std::vector<uint8_t> encode_kdf_attr() {
    std::vector<uint8_t> value;
    append_u16be(value, 1);  // the one standardised EAP-AKA' KDF
    return make_attr(24, value);
}

std::vector<uint8_t> encode_res_attr(const std::vector<uint8_t>& res) {
    std::vector<uint8_t> value;
    append_u16be(value, static_cast<uint16_t>(res.size() * 8));  // Actual RES Length, in bits
    append(value, res);
    pad_value_to_word_boundary(value);
    return make_attr(3, value);
}

std::vector<uint8_t> encode_client_error_attr(uint16_t code) {
    std::vector<uint8_t> value;
    append_u16be(value, code);
    return make_attr(22, value);
}

std::vector<uint8_t> build_header(Code code, uint8_t identifier, size_t total_len,
                                   Subtype subtype) {
    std::vector<uint8_t> header{static_cast<uint8_t>(code), identifier,
                                 static_cast<uint8_t>((total_len >> 8) & 0xff),
                                 static_cast<uint8_t>(total_len & 0xff), kTypeAkaPrime,
                                 static_cast<uint8_t>(subtype), 0, 0};
    return header;
}

struct AttrSpan {
    size_t value_offset;  // offset of the byte right after Type+Length
    size_t value_length;  // length_words*4 - 2
};

// Scans the TLV attribute list starting at byte 8 (right after the 8-byte EAP+AKA' header).
std::optional<AttrSpan> find_attribute(const std::vector<uint8_t>& packet, uint8_t type) {
    size_t pos = 8;
    while (pos + 2 <= packet.size()) {
        const uint8_t attr_type = packet[pos];
        const uint8_t length_words = packet[pos + 1];
        if (length_words == 0) {
            return std::nullopt;  // malformed: would infinite-loop
        }
        const size_t attr_total = static_cast<size_t>(length_words) * 4;
        if (pos + attr_total > packet.size()) {
            return std::nullopt;  // malformed: truncated attribute
        }
        if (attr_type == type) {
            return AttrSpan{pos + 2, attr_total - 2};
        }
        pos += attr_total;
    }
    return std::nullopt;
}

}  // namespace

std::vector<uint8_t> prf_prime(const std::vector<uint8_t>& key,
                                const std::vector<uint8_t>& seed,
                                size_t output_len) {
    std::vector<uint8_t> out;
    std::vector<uint8_t> t_prev;
    uint8_t counter = 1;
    while (out.size() < output_len) {
        std::vector<uint8_t> input = t_prev;
        append(input, seed);
        input.push_back(counter);
        const auto t = hmac_sha256(key, input);
        t_prev.assign(t.begin(), t.end());
        append(out, t_prev);
        ++counter;
    }
    out.resize(output_len);
    return out;
}

DerivedKeys derive_keys(const std::array<uint8_t, 16>& ck_prime,
                         const std::array<uint8_t, 16>& ik_prime,
                         const std::string& identity) {
    std::vector<uint8_t> key(ik_prime.begin(), ik_prime.end());  // MK = PRF'(IK'||CK', ...)
    append(key, std::vector<uint8_t>(ck_prime.begin(), ck_prime.end()));

    // NOT std::string("EAP-AKA'").begin()/.end() as two separate expressions -- that constructs
    // TWO distinct temporary std::string objects (one per call) and takes begin() from one, end()
    // from the other, an iterator range spanning two unrelated objects. That's undefined behavior,
    // not merely "the same short string twice" -- found via this turn's cross-process integration
    // test (tests/integration/test_ausf_ue_authentication.cpp), which caught it because it compares
    // AUSF's derived key against an independently-computed one in a different process. See
    // docs/DECISIONS.md ADR-0027.
    static const std::string kEapAkaPrimeLabel = "EAP-AKA'";
    std::vector<uint8_t> seed(kEapAkaPrimeLabel.begin(), kEapAkaPrimeLabel.end());
    append(seed, std::vector<uint8_t>(identity.begin(), identity.end()));

    const auto mk = prf_prime(key, seed, 16 + 32 + 32 + 64 + 64);

    DerivedKeys out{};
    size_t off = 0;
    std::copy(mk.begin() + static_cast<long>(off), mk.begin() + static_cast<long>(off + 16),
              out.k_encr.begin());
    off += 16;
    std::copy(mk.begin() + static_cast<long>(off), mk.begin() + static_cast<long>(off + 32),
              out.k_aut.begin());
    off += 32;
    std::copy(mk.begin() + static_cast<long>(off), mk.begin() + static_cast<long>(off + 32),
              out.k_re.begin());
    off += 32;
    std::copy(mk.begin() + static_cast<long>(off), mk.begin() + static_cast<long>(off + 64),
              out.msk.begin());
    off += 64;
    std::copy(mk.begin() + static_cast<long>(off), mk.begin() + static_cast<long>(off + 64),
              out.emsk.begin());
    return out;
}

std::array<uint8_t, 16> compute_mac(const std::vector<uint8_t>& packet_with_mac_zeroed,
                                     const std::array<uint8_t, 32>& k_aut) {
    const auto full = hmac_sha256(std::vector<uint8_t>(k_aut.begin(), k_aut.end()),
                                   packet_with_mac_zeroed);
    std::array<uint8_t, 16> out{};
    std::copy(full.begin(), full.begin() + 16, out.begin());
    return out;
}

namespace {

std::vector<uint8_t> finalize_with_mac(Code code, uint8_t identifier, Subtype subtype,
                                        std::vector<uint8_t> attrs_with_zero_mac,
                                        size_t mac_value_offset_in_attrs,
                                        const std::array<uint8_t, 32>& k_aut) {
    std::vector<uint8_t> packet =
        build_header(code, identifier, 8 + attrs_with_zero_mac.size(), subtype);
    append(packet, attrs_with_zero_mac);
    const auto mac = compute_mac(packet, k_aut);
    std::copy(mac.begin(), mac.end(), packet.begin() + static_cast<long>(8 + mac_value_offset_in_attrs));
    return packet;
}

}  // namespace

std::vector<uint8_t> build_challenge_request(uint8_t identifier, const Key128& rand,
                                              const std::array<uint8_t, 16>& autn,
                                              const std::string& network_name,
                                              const std::array<uint8_t, 32>& k_aut) {
    std::vector<uint8_t> attrs;
    append(attrs, encode_rand_attr(rand));
    append(attrs, encode_autn_attr(autn));
    append(attrs, encode_kdf_input_attr(network_name));
    append(attrs, encode_kdf_attr());
    const size_t mac_offset = attrs.size() + 4;  // skip this attr's own Type+Length+Reserved(2)
    append(attrs, encode_mac_attr({}));
    return finalize_with_mac(Code::kRequest, identifier, Subtype::kChallenge, std::move(attrs),
                              mac_offset, k_aut);
}

std::vector<uint8_t> build_challenge_response(uint8_t identifier, const std::vector<uint8_t>& res,
                                               const std::array<uint8_t, 32>& k_aut) {
    std::vector<uint8_t> attrs;
    append(attrs, encode_res_attr(res));
    const size_t mac_offset = attrs.size() + 4;
    append(attrs, encode_mac_attr({}));
    return finalize_with_mac(Code::kResponse, identifier, Subtype::kChallenge, std::move(attrs),
                              mac_offset, k_aut);
}

std::vector<uint8_t> build_client_error_response(uint8_t identifier, uint16_t error_code) {
    std::vector<uint8_t> attrs = encode_client_error_attr(error_code);
    std::vector<uint8_t> packet =
        build_header(Code::kResponse, identifier, 8 + attrs.size(), Subtype::kClientError);
    append(packet, attrs);
    return packet;
}

std::vector<uint8_t> build_success(uint8_t identifier) {
    return {static_cast<uint8_t>(Code::kSuccess), identifier, 0, 4};
}

std::vector<uint8_t> build_failure(uint8_t identifier) {
    return {static_cast<uint8_t>(Code::kFailure), identifier, 0, 4};
}

std::optional<ChallengeRequestFields> parse_challenge_request(
    const std::vector<uint8_t>& packet) {
    if (packet.size() < 8 || packet[0] != static_cast<uint8_t>(Code::kRequest) ||
        packet[4] != kTypeAkaPrime || packet[5] != static_cast<uint8_t>(Subtype::kChallenge)) {
        return std::nullopt;
    }
    const auto rand_span = find_attribute(packet, 1);
    const auto autn_span = find_attribute(packet, 2);
    const auto kdf_input_span = find_attribute(packet, 23);
    const auto mac_span = find_attribute(packet, 11);
    if (!rand_span || !autn_span || !kdf_input_span || !mac_span || rand_span->value_length < 18 ||
        autn_span->value_length < 18 || mac_span->value_length < 18 ||
        kdf_input_span->value_length < 2) {
        return std::nullopt;
    }

    ChallengeRequestFields out{};
    std::copy(packet.begin() + static_cast<long>(rand_span->value_offset + 2),
              packet.begin() + static_cast<long>(rand_span->value_offset + 18), out.rand.begin());
    std::copy(packet.begin() + static_cast<long>(autn_span->value_offset + 2),
              packet.begin() + static_cast<long>(autn_span->value_offset + 18), out.autn.begin());
    std::copy(packet.begin() + static_cast<long>(mac_span->value_offset + 2),
              packet.begin() + static_cast<long>(mac_span->value_offset + 18), out.mac.begin());

    const size_t name_len =
        (static_cast<size_t>(packet[kdf_input_span->value_offset]) << 8) |
        packet[kdf_input_span->value_offset + 1];
    if (2 + name_len > kdf_input_span->value_length) {
        return std::nullopt;
    }
    out.kdf_input = std::string(
        packet.begin() + static_cast<long>(kdf_input_span->value_offset + 2),
        packet.begin() + static_cast<long>(kdf_input_span->value_offset + 2 + name_len));
    return out;
}

std::optional<ChallengeResponseFields> parse_challenge_response(
    const std::vector<uint8_t>& packet) {
    if (packet.size() < 8 || packet[0] != static_cast<uint8_t>(Code::kResponse) ||
        packet[4] != kTypeAkaPrime || packet[5] != static_cast<uint8_t>(Subtype::kChallenge)) {
        return std::nullopt;
    }
    const auto res_span = find_attribute(packet, 3);
    const auto mac_span = find_attribute(packet, 11);
    if (!res_span || !mac_span || res_span->value_length < 2 || mac_span->value_length < 18) {
        return std::nullopt;
    }

    const size_t res_bits =
        (static_cast<size_t>(packet[res_span->value_offset]) << 8) | packet[res_span->value_offset + 1];
    const size_t res_bytes = (res_bits + 7) / 8;
    if (2 + res_bytes > res_span->value_length) {
        return std::nullopt;
    }

    ChallengeResponseFields out{};
    out.res.assign(packet.begin() + static_cast<long>(res_span->value_offset + 2),
                    packet.begin() + static_cast<long>(res_span->value_offset + 2 + res_bytes));
    std::copy(packet.begin() + static_cast<long>(mac_span->value_offset + 2),
              packet.begin() + static_cast<long>(mac_span->value_offset + 18), out.mac.begin());
    return out;
}

bool verify_mac(const std::vector<uint8_t>& packet, const std::array<uint8_t, 32>& k_aut) {
    const auto mac_span = find_attribute(packet, 11);
    if (!mac_span || mac_span->value_length < 18) {
        return false;
    }
    std::vector<uint8_t> zeroed = packet;
    std::fill(zeroed.begin() + static_cast<long>(mac_span->value_offset + 2),
              zeroed.begin() + static_cast<long>(mac_span->value_offset + 18), 0);
    const auto expected = compute_mac(zeroed, k_aut);
    return std::equal(expected.begin(), expected.end(),
                       packet.begin() + static_cast<long>(mac_span->value_offset + 2));
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return "";
    }
    std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
    const int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data.data(),
                                     static_cast<int>(data.size()));
    out.resize(static_cast<size_t>(len));
    return out;
}

std::optional<std::vector<uint8_t>> base64_decode(const std::string& text) {
    if (text.empty()) {
        return std::vector<uint8_t>{};
    }
    std::vector<uint8_t> out(3 * ((text.size() + 3) / 4) + 1, 0);
    const int len = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(text.data()),
                                     static_cast<int>(text.size()));
    if (len < 0) {
        return std::nullopt;
    }
    size_t actual_len = static_cast<size_t>(len);
    size_t pad = 0;
    if (!text.empty() && text[text.size() - 1] == '=') {
        ++pad;
    }
    if (text.size() > 1 && text[text.size() - 2] == '=') {
        ++pad;
    }
    actual_len -= pad;
    out.resize(actual_len);
    return out;
}

}  // namespace aka_crypto::eap
