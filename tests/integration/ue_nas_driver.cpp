#include "ue_nas_driver.hpp"

#include <cstring>

#include "aka_crypto/hex.hpp"
#include "aka_crypto/kdf.hpp"
#include "aka_crypto/milenage.hpp"

namespace nf_test {

namespace {

// TS 24.501 constants, mirroring nfs/amf/src/nas_codec.cpp's own named values rather than
// re-deriving them.
constexpr std::uint8_t kEpdMobilityManagement = 0x7E;
constexpr std::uint8_t kSecurityHeaderNotProtected = 0x00;
constexpr std::uint8_t kMessageTypeRegistrationRequest = 0x41;
constexpr std::uint8_t kMessageTypeAuthenticationRequest = 0x56;
constexpr std::uint8_t kMessageTypeAuthenticationResponse = 0x57;
constexpr std::uint8_t kIeiUeSecurityCapability = 0x2E;
constexpr std::uint8_t kIeiAuthResponseParameter = 0x2D;
constexpr std::uint8_t kIeiRand = 0x21;
constexpr std::uint8_t kIeiAutn = 0x20;

// Same TS 35.207 Test Set 1 credentials nfs/udm seeds imsi-999700000000001 with (ADR-0026), and
// that tests/integration/test_ausf_ue_authentication.cpp already proves work against this stack.
aka_crypto::Key128 test_k() {
    return *aka_crypto::from_hex<16>("465b5ce8b199b49faa5f0a2ee238a6bc");
}

aka_crypto::Key128 test_opc() {
    const auto op = *aka_crypto::from_hex<16>("cdc202d5123e20f62b6d676ac72cb318");
    return aka_crypto::derive_opc(test_k(), op);
}

// Half-octet BCD, filler 0xF for a 2-digit MNC -- the decode direction of this is
// nfs/amf/src/nas_codec.cpp's own SUCI PLMN parsing.
void append_plmn_bcd(std::vector<std::uint8_t>& out,
                     const std::string& mcc,
                     const std::string& mnc) {
    const auto d = [](char c) { return static_cast<std::uint8_t>(c - '0'); };
    const bool long_mnc = mnc.size() == 3;
    out.push_back(static_cast<std::uint8_t>((d(mcc[1]) << 4) | d(mcc[0])));
    out.push_back(static_cast<std::uint8_t>(((long_mnc ? d(mnc[2]) : 0xF) << 4) | d(mcc[2])));
    out.push_back(static_cast<std::uint8_t>((d(mnc[1]) << 4) | d(mnc[0])));
}

} // namespace

std::vector<std::uint8_t> build_registration_request(const std::string& supi) {
    // "imsi-<mcc(3)><mnc(2)><msin>" -- the only shape this lab seeds.
    const std::string digits = supi.substr(std::strlen("imsi-"));
    const std::string mcc = digits.substr(0, 3);
    const std::string mnc = digits.substr(3, 2);
    const std::string msin = digits.substr(5);

    // 5GS Mobile Identity value: SUCI, IMSI format, null protection scheme.
    std::vector<std::uint8_t> id;
    id.push_back(0x01); // identity type SUCI (bits0-2 = 001), SUPI format IMSI (bits4-6 = 000)
    append_plmn_bcd(id, mcc, mnc);
    // Routing indicator "0000" -- 2 clean BCD octets, the only form AMF's decoder accepts and the
    // one simulators/ransim/config/ue.yaml uses.
    id.push_back(0x00);
    id.push_back(0x00);
    id.push_back(0x00); // protection scheme id: null
    id.push_back(0x00); // home network public key id
    for (std::size_t i = 0; i < msin.size(); i += 2) {
        const std::uint8_t lo = static_cast<std::uint8_t>(msin[i] - '0');
        const std::uint8_t hi = (i + 1 < msin.size()) ? static_cast<std::uint8_t>(msin[i + 1] - '0')
                                                      : 0x0F; // odd-length terminator
        id.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    std::vector<std::uint8_t> out;
    out.push_back(kEpdMobilityManagement);
    out.push_back(kSecurityHeaderNotProtected);
    out.push_back(kMessageTypeRegistrationRequest);
    out.push_back(0x71); // ngKSI 7 (no key available) | 5GS registration type 1 (initial)
    out.push_back(static_cast<std::uint8_t>((id.size() >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(id.size() & 0xFF));
    out.insert(out.end(), id.begin(), id.end());

    // UE Security Capability (TS 24.501 §9.11.3.54). AMF replays these bytes verbatim in
    // SecurityModeCommand and a real UE compares them byte-for-byte (the anti-bidding-down check),
    // so the value matters even though AMF never interprets it. 0x80/0x80 advertises
    // 128-NEA1..NEA0/128-NIA1 style bitmaps with the top bit set; this lab always selects
    // 128-NEA2/128-NIA2, so what matters here is only that the IE is present and stable.
    out.push_back(kIeiUeSecurityCapability);
    out.push_back(0x02);
    out.push_back(0xE0); // 5G-EA0, 128-5G-EA1, 128-5G-EA2 supported
    out.push_back(0xE0); // 5G-IA0, 128-5G-IA1, 128-5G-IA2 supported
    return out;
}

std::optional<AuthChallenge>
parse_authentication_request(const std::vector<std::uint8_t>& nas_pdu) {
    // 0x7E 0x00 0x56 | ngKSI | ABBA(len,val...) | 0x21 + RAND(16) | 0x20 + len + AUTN(16)
    if (nas_pdu.size() < 4 || nas_pdu[0] != kEpdMobilityManagement ||
        nas_pdu[1] != kSecurityHeaderNotProtected ||
        nas_pdu[2] != kMessageTypeAuthenticationRequest) {
        return std::nullopt;
    }
    std::size_t off = 4;
    if (off >= nas_pdu.size()) {
        return std::nullopt;
    }
    off += 1 + nas_pdu[off]; // ABBA: 1-octet length + value

    AuthChallenge challenge;
    bool have_rand = false;
    bool have_autn = false;
    while (off < nas_pdu.size()) {
        const std::uint8_t iei = nas_pdu[off];
        if (iei == kIeiRand) {
            if (off + 1 + 16 > nas_pdu.size()) {
                return std::nullopt;
            }
            std::memcpy(challenge.rand.data(), &nas_pdu[off + 1], 16); // Type-3: no length octet
            off += 1 + 16;
            have_rand = true;
        } else if (iei == kIeiAutn) {
            if (off + 2 > nas_pdu.size()) {
                return std::nullopt;
            }
            const std::uint8_t len = nas_pdu[off + 1];
            if (len != 16 || off + 2 + len > nas_pdu.size()) {
                return std::nullopt;
            }
            std::memcpy(challenge.autn.data(), &nas_pdu[off + 2], 16);
            off += 2 + len;
            have_autn = true;
        } else {
            break;
        }
    }
    if (!have_rand || !have_autn) {
        return std::nullopt;
    }
    return challenge;
}

std::optional<std::array<std::uint8_t, 16>>
compute_res_star(const AuthChallenge& challenge, const std::string& serving_network_name) {
    const auto k = test_k();
    const auto opc = test_opc();
    const auto out = aka_crypto::f2345(opc, k, challenge.rand);

    // AUTN = (SQN xor AK) || AMF || MAC-A. Recover SQN with AK, then recompute MAC-A and compare:
    // this is the real network-authentication check, and it fails loudly if the credentials or the
    // vector are wrong rather than answering anyway.
    aka_crypto::Sqn sqn{};
    for (std::size_t i = 0; i < sqn.size(); ++i) {
        sqn[i] = static_cast<std::uint8_t>(challenge.autn[i] ^ out.ak[i]);
    }
    aka_crypto::Amf amf{};
    std::memcpy(amf.data(), &challenge.autn[6], amf.size());
    const auto expected_mac = aka_crypto::f1(opc, k, challenge.rand, sqn, amf);
    if (std::memcmp(expected_mac.data(), &challenge.autn[8], expected_mac.size()) != 0) {
        return std::nullopt;
    }

    return aka_crypto::derive_res_star(
        out.ck, out.ik, serving_network_name, challenge.rand, out.res);
}

std::vector<std::uint8_t>
build_authentication_response(const std::array<std::uint8_t, 16>& res_star) {
    std::vector<std::uint8_t> out;
    out.push_back(kEpdMobilityManagement);
    out.push_back(kSecurityHeaderNotProtected);
    out.push_back(kMessageTypeAuthenticationResponse);
    out.push_back(kIeiAuthResponseParameter);
    out.push_back(static_cast<std::uint8_t>(res_star.size()));
    out.insert(out.end(), res_star.begin(), res_star.end());
    return out;
}

} // namespace nf_test
