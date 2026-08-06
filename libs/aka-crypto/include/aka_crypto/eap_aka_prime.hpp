#pragma once

#include "aka_crypto/milenage.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// EAP-AKA' (RFC 5448, IANA EAP method Type 50) packet framing, attribute (RFC 4187 Section 8
// "Table of Attributes", cross-checked against the IANA "EAP-AKA and EAP-SIM Parameters"
// registry rather than trusted from memory) encode/decode, and key derivation (RFC 5448
// Section 3.3's PRF'/MK construction). Used by both nfs/ausf (network/AUSF side: builds
// Request/AKA'-Challenge, verifies Response/AKA'-Challenge) and the integration test's UE-role
// test client (the reverse). Only the single-round-trip Challenge exchange is implemented --
// AKA-Identity, Notification, Reauthentication, and Synchronization-Failure (AUTS resync)
// subtypes are not, matching Milenage's f1*/f5* omission. See docs/DECISIONS.md ADR-0026.

namespace aka_crypto::eap {

constexpr uint8_t kTypeAkaPrime = 50;  // IANA EAP Method Type registry

enum class Code : uint8_t { kRequest = 1, kResponse = 2, kSuccess = 3, kFailure = 4 };
enum class Subtype : uint8_t { kChallenge = 1, kClientError = 14 };

struct DerivedKeys {
    std::array<uint8_t, 16> k_encr;
    std::array<uint8_t, 32> k_aut;
    std::array<uint8_t, 32> k_re;
    std::array<uint8_t, 64> msk;
    std::array<uint8_t, 64> emsk;
};

// PRF'(K, S) per RFC 5448 Section 3.2 (the same construction IKEv2 uses): iterated HMAC-SHA-256,
// producing `output_len` bytes.
std::vector<uint8_t> prf_prime(const std::vector<uint8_t>& key,
                                const std::vector<uint8_t>& seed,
                                size_t output_len);

// MK = PRF'(IK' || CK', "EAP-AKA'" || Identity); split into K_encr/K_aut/K_re/MSK/EMSK per
// RFC 5448 Section 3.3. `identity` is the raw SUPI string -- a disclosed simplification versus
// the full 3GPP NAI/pseudonym-identity format (TS 23.003), which is a stage-3 NAS/USIM concern
// with no SBI YAML backing it; both AUSF and the UE-role test client use this same convention so
// the handshake is internally consistent, but it is not the wire-exact NAI a real UE would send.
DerivedKeys derive_keys(const std::array<uint8_t, 16>& ck_prime,
                         const std::array<uint8_t, 16>& ik_prime,
                         const std::string& identity);

// HMAC-SHA-256-128 (RFC 5448 Section 3.1): HMAC-SHA-256 truncated to the leftmost 16 bytes.
std::array<uint8_t, 16> compute_mac(const std::vector<uint8_t>& packet_with_mac_zeroed,
                                     const std::array<uint8_t, 32>& k_aut);

// Builds a full Request/AKA'-Challenge packet (Code/Id/Length/Type/Subtype header +
// AT_RAND/AT_AUTN/AT_KDF_INPUT/AT_KDF/AT_MAC), with a correctly-computed AT_MAC.
std::vector<uint8_t> build_challenge_request(uint8_t identifier,
                                              const Key128& rand,
                                              const std::array<uint8_t, 16>& autn,
                                              const std::string& network_name,
                                              const std::array<uint8_t, 32>& k_aut);

// Builds a full Response/AKA'-Challenge packet (AT_RES + AT_MAC), with a correctly-computed
// AT_MAC. `res` is the raw RES* (or RES) value bytes; its bit-length is encoded per RFC 4187's
// AT_RES "Actual RES Length" field.
std::vector<uint8_t> build_challenge_response(uint8_t identifier,
                                               const std::vector<uint8_t>& res,
                                               const std::array<uint8_t, 32>& k_aut);

std::vector<uint8_t> build_client_error_response(uint8_t identifier, uint16_t error_code);
std::vector<uint8_t> build_success(uint8_t identifier);
std::vector<uint8_t> build_failure(uint8_t identifier);

struct ChallengeRequestFields {
    Key128 rand;
    std::array<uint8_t, 16> autn;
    std::string kdf_input;
    std::array<uint8_t, 16> mac;
};
std::optional<ChallengeRequestFields> parse_challenge_request(const std::vector<uint8_t>& packet);

struct ChallengeResponseFields {
    std::vector<uint8_t> res;
    std::array<uint8_t, 16> mac;
};
std::optional<ChallengeResponseFields> parse_challenge_response(
    const std::vector<uint8_t>& packet);

// Recomputes AT_MAC over `packet` with its AT_MAC value zeroed and compares against the value
// actually present in `packet`.
bool verify_mac(const std::vector<uint8_t>& packet, const std::array<uint8_t, 32>& k_aut);

// Standard base64 (no line wrapping), used for the `EapPayload` ("format: byte") string fields in
// both TS29509_Nausf_UEAuthentication.yaml's EapSession/UEAuthenticationCtx and this library's own
// EAP packet builders/parsers above.
std::string base64_encode(const std::vector<uint8_t>& data);
std::optional<std::vector<uint8_t>> base64_decode(const std::string& text);

}  // namespace aka_crypto::eap
