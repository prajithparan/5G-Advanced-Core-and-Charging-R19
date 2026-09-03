#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ADR-0265: the UE side of NAS, for tests -- enough of TS 24.501 to get a real UE through
// authentication against this project's real AMF/AUSF/UDM/UDR chain.
//
// Why this is not testing the code against itself. These encoders produce bytes that AMF's own
// `decode_registration_request`/`decode_authentication_outcome` accept, so the framing is
// mirror-derived. That would be circular if the decoders had never seen an independent
// implementation -- but they have: registration was originally built and verified against
// UERANSIM's own UE (ADR-0031/0032/0037), which is a separate codebase by a different author.
// Mirroring a format an independent implementation already agreed on is re-encoding, not
// self-agreement.
//
// The genuinely independent check is the cryptography, and it is what these tests assert on. RES*
// is computed here from the subscriber's real K/OPc and sent to AUSF, which independently computes
// its own expected value from UDR's stored credentials. A wrong RES* is rejected no matter how
// well-formed the NAS framing is. Assertions are therefore on AMF/AUSF *accepting* the exchange,
// never on this file round-tripping its own output.

namespace nf_test {

// TS 35.207 Test Set 1 K/OP -- the values nfs/udm seeds imsi-999700000000001 with (ADR-0026), and
// the same ones tests/integration/test_ausf_ue_authentication.cpp already uses.
constexpr const char* kTestSupi = "imsi-999700000000001";

struct AuthChallenge {
    std::array<std::uint8_t, 16> rand{};
    std::array<std::uint8_t, 16> autn{};
};

// Plain (unprotected) RegistrationRequest carrying a null-protection-scheme SUCI for `supi`, plus
// a UE Security Capability IE. TS 24.501 §8.2.6.
std::vector<std::uint8_t> build_registration_request(const std::string& supi);

// Pulls RAND and AUTN out of a plain AuthenticationRequest (TS 24.501 §8.2.1). std::nullopt if the
// PDU is not one.
std::optional<AuthChallenge> parse_authentication_request(const std::vector<std::uint8_t>& nas_pdu);

// Verifies AUTN's MAC against the subscriber's real credentials and returns RES* (TS 33.501
// Annex A.4). std::nullopt means the network's AUTN did not authenticate -- a real failure this
// driver refuses to paper over by answering anyway.
//
// Deliberately NOT checked: SQN freshness. A real UE rejects an out-of-range SQN and answers
// AuthenticationFailure(SYNCH_FAILURE); this project's seeded subscriber uses a fixed TS 35.207
// SQN that always trips that on first contact, and AMF implements the resync that follows
// (ADR-0037). That path is worth its own test, but it is not what these tests are about -- so this
// UE accepts any SQN and says so, rather than quietly depending on resync working.
std::optional<std::array<std::uint8_t, 16>>
compute_res_star(const AuthChallenge& challenge, const std::string& serving_network_name);

// Plain AuthenticationResponse carrying RES* as IEI 0x2D. TS 24.501 §8.2.2.
std::vector<std::uint8_t>
build_authentication_response(const std::array<std::uint8_t, 16>& res_star);

} // namespace nf_test
