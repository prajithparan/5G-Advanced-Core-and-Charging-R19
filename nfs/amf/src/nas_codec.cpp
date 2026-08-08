#include "nas_codec.hpp"

#include "aka_crypto/nas_security.hpp"

namespace amf::nas {

namespace {

// TS 24.007 §11.2.3.1.1 plain NAS message header (mobility management): Extended Protocol
// Discriminator, Security Header Type, Message Type. Values confirmed against
// simulators/ransim/vendor/UERANSIM/src/lib/nas/enums.hpp (EExtendedProtocolDiscriminator::
// MOBILITY_MANAGEMENT_MESSAGES, ESecurityHeaderType::NOT_PROTECTED, EMessageType::
// REGISTRATION_REQUEST/AUTHENTICATION_REQUEST) -- not guessed from memory.
constexpr std::uint8_t kEpdMobilityManagement = 0x7E;
constexpr std::uint8_t kSecurityHeaderNotProtected = 0x00;
constexpr std::uint8_t kMessageTypeRegistrationRequest = 0x41;
constexpr std::uint8_t kMessageTypeAuthenticationRequest = 0x56;
constexpr std::uint8_t kMessageTypeAuthenticationResponse = 0x57;
constexpr std::uint8_t kMessageTypeAuthenticationFailure = 0x59;
constexpr std::uint8_t kMessageTypeSecurityModeCommand = 0x5D;
constexpr std::uint8_t kMessageTypeSecurityModeComplete = 0x5E;
constexpr std::uint8_t kMessageTypeRegistrationAccept = 0x42;
constexpr std::uint8_t kMessageTypeRegistrationComplete = 0x43;
constexpr std::uint8_t kMessageTypeUlNasTransport = 0x67;

// TS 24.501 §9.1.1 security header type values (the byte carried in the outer secured envelope,
// distinct from the inner plaintext message's own header, which always carries
// kSecurityHeaderNotProtected). Confirmed against
// simulators/ransim/vendor/UERANSIM/src/lib/nas/enums.hpp's ESecurityHeaderType.
constexpr std::uint8_t kShtIntegrityProtectedAndCiphered = 0x02;
constexpr std::uint8_t kShtIntegrityProtectedWithNewSecurityContext = 0x03;
constexpr std::uint8_t kShtIntegrityProtectedAndCipheredWithNewSecurityContext = 0x04;

// TS 24.501 IEIs, confirmed against UERANSIM's own onBuild registrations (msg.cpp) for
// AuthenticationResponse/AuthenticationFailure, not guessed.
constexpr std::uint8_t kIeiAuthenticationResponseParameter = 0x2D;
constexpr std::uint8_t kIeiAuthenticationFailureParameter = 0x30;
constexpr std::uint8_t kIeiUeSecurityCapability = 0x2E;

// UlNasTransport's optional IEs (TS 24.501 §8.2.10 / msg.cpp's own onBuild order: pduSessionId,
// oldPduSessionId, requestType, sNssai, dnn, additionalInformation).
constexpr std::uint8_t kIeiUlNasPduSessionId = 0x12;
constexpr std::uint8_t kIeiUlNasOldPduSessionId = 0x59;
constexpr std::uint8_t kIeiUlNasSNssai = 0x22;
constexpr std::uint8_t kIeiUlNasDnn = 0x25;
constexpr std::uint8_t kIeiUlNasAdditionalInformation = 0x24;
// requestType is a Type-1 half-octet IE (IENasKeySetIdentifier-style: no length byte, IEI packed
// into the high nibble alongside the value in the low nibble) -- checked via high-nibble match,
// not full-byte equality, unlike the Type-3/4 IEIs above.
constexpr std::uint8_t kIeiNibbleUlNasRequestType = 0x8;

// TS 24.501 §9.11.3.5 Payload container type -- Type-1 half-octet, low nibble of its own byte
// (no paired field, so the high nibble is spare/0). Confirmed against
// simulators/ransim/vendor/UERANSIM/src/lib/nas/enums.hpp's EPayloadContainerType.
constexpr std::uint8_t kPayloadContainerTypeN1SmInformation = 0x01;

// This project's only NAS access type in scope (single gNB, 3GPP access -- see ADR-0031), used as
// the fixed BEARER input to 128-NEA2/128-NIA2 (aka_crypto::nea2_apply/nia2_mac). Confirmed against
// simulators/ransim/vendor/UERANSIM/src/ue/nas/enc.cpp's own `is3gppAccess ? 1 : 2` convention.
constexpr std::uint8_t kNasBearerId = 1;
constexpr std::uint8_t kDirectionUplink = 0;
constexpr std::uint8_t kDirectionDownlink = 1;

// TS 24.008 §10.5.1.13-style half-octet BCD digit -- '?' marks the 0xF spare/filler nibble
// (odd-length terminator), matching UERANSIM's own DecodeBcdString digit table.
char bcd_digit(std::uint8_t nibble) {
    return (nibble <= 9) ? static_cast<char>('0' + nibble) : '?';
}

// Shared secured-NAS-message envelope logic (TS 24.501 §9.1.1: EPD + SHT + MAC(4) + SeqNo(1) +
// NAS message container), used by every downlink message this AMF sends once a NAS security
// context exists -- SecurityModeCommand (sht=..NewSecurityContext, never ciphered) and
// RegistrationAccept (sht=..AndCiphered, always ciphered) differ only in `sht`/`ciphered`, not in
// how the envelope itself is built.
std::vector<std::uint8_t> encode_secured_downlink(const aka_crypto::NasIntKey& knas_int,
                                                   const aka_crypto::NasEncKey& knas_enc,
                                                   std::uint8_t sht, bool ciphered,
                                                   std::uint32_t downlink_count,
                                                   const std::vector<std::uint8_t>& inner_plain) {
    const std::vector<std::uint8_t> wire_inner =
        ciphered ? aka_crypto::nea2_apply(knas_enc, downlink_count, kNasBearerId, kDirectionDownlink,
                                          inner_plain)
                 : inner_plain;

    // TS 24.501's NAS MAC construction prepends the 1-octet NAS sequence number (COUNT's
    // low-order byte, the same byte transmitted as the envelope's own SeqNo field below) to the
    // transmitted bytes before computing the MAC -- confirmed directly against a real nr-ue build
    // (UERANSIM's nas_enc::ComputeMac: `OctetString::Concat(OctetString::FromOctet(count.sqn),
    // plainMessage)`, applied to the POST-CIPHER bytes). This is NOT something the raw
    // 128-NIA2/EIA2 algorithm itself needs (COUNT is already a separate parameter to EIA2), but a
    // TS 24.501 NAS-security-layer detail -- missed initially (self-consistency tests and a
    // cross-check against the raw EIA2 primitive alone couldn't catch it), found only by
    // instrumenting a real nr-ue build and diffing its internal MAC input against this project's
    // own for the exact same live exchange. See docs/DECISIONS.md ADR-0037.
    std::vector<std::uint8_t> mac_input;
    mac_input.reserve(wire_inner.size() + 1);
    mac_input.push_back(static_cast<std::uint8_t>(downlink_count & 0xff));
    mac_input.insert(mac_input.end(), wire_inner.begin(), wire_inner.end());
    const auto mac =
        aka_crypto::nia2_mac(knas_int, downlink_count, kNasBearerId, kDirectionDownlink, mac_input);

    std::vector<std::uint8_t> out;
    out.push_back(kEpdMobilityManagement);
    out.push_back(sht);
    out.push_back(static_cast<std::uint8_t>((mac >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((mac >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((mac >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(mac & 0xff));
    out.push_back(static_cast<std::uint8_t>(downlink_count & 0xff)); // sequence number
    out.insert(out.end(), wire_inner.begin(), wire_inner.end());
    return out;
}

// The uplink mirror of encode_secured_downlink -- verifies the MAC (always computed over the
// bytes as transmitted, i.e. still-ciphered when `ciphered`, matching UERANSIM's enc.cpp Decrypt()
// convention, see decode_security_mode_complete's original comment) then deciphers if applicable.
// std::nullopt means the envelope itself doesn't match (wrong EPD, wrong SHT, too short) -- not a
// verification failure, which is instead mac_valid=false on the returned struct (see
// SecurityModeCompleteOutcome-style split, kept consistent for RegistrationComplete below).
struct SecuredUplinkResult {
    bool mac_valid = false;
    std::vector<std::uint8_t> plain_inner; // only meaningful if mac_valid
};

std::optional<SecuredUplinkResult> decode_secured_uplink(const aka_crypto::NasIntKey& knas_int,
                                                          const aka_crypto::NasEncKey& knas_enc,
                                                          std::uint8_t expected_sht, bool ciphered,
                                                          std::uint32_t uplink_count,
                                                          const std::vector<std::uint8_t>& p) {
    if (p.size() < 7 || p[0] != kEpdMobilityManagement || p[1] != expected_sht) return std::nullopt;

    const std::vector<std::uint8_t> wire_inner(p.begin() + 7, p.end());
    const std::uint32_t received_mac =
        (static_cast<std::uint32_t>(p[2]) << 24) | (static_cast<std::uint32_t>(p[3]) << 16) |
        (static_cast<std::uint32_t>(p[4]) << 8) | static_cast<std::uint32_t>(p[5]);
    // Same 1-octet NAS sequence number prefix as encode_secured_downlink's own MAC input -- see
    // that function's comment.
    std::vector<std::uint8_t> mac_input;
    mac_input.reserve(wire_inner.size() + 1);
    mac_input.push_back(static_cast<std::uint8_t>(uplink_count & 0xff));
    mac_input.insert(mac_input.end(), wire_inner.begin(), wire_inner.end());
    const auto expected_mac =
        aka_crypto::nia2_mac(knas_int, uplink_count, kNasBearerId, kDirectionUplink, mac_input);

    SecuredUplinkResult result;
    result.mac_valid = (expected_mac == received_mac);
    if (!result.mac_valid) return result;

    result.plain_inner = ciphered ? aka_crypto::nea2_apply(knas_enc, uplink_count, kNasBearerId,
                                                           kDirectionUplink, wire_inner)
                                  : wire_inner;
    return result;
}

} // namespace

std::optional<RegistrationRequestInfo> decode_registration_request(
    const std::vector<std::uint8_t>& p) {
    if (p.size() < 4 || p[0] != kEpdMobilityManagement || p[1] != kSecurityHeaderNotProtected ||
        p[2] != kMessageTypeRegistrationRequest) {
        return std::nullopt;
    }

    // byte3: ngKSI (high nibble) | 5GSRegistrationType (low nibble), TS 24.501 §9.11.3.32/§9.11.3.7
    // packed via the "IE1" half-octet convention -- neither value is needed to extract SUPI, so
    // not decoded further this stage.
    std::size_t off = 4;

    // 5GS Mobile Identity (TS 24.501 §9.11.3.4): Type-6 IE, 2-octet big-endian length, then value.
    if (off + 2 > p.size()) return std::nullopt;
    const std::size_t id_len = (static_cast<std::size_t>(p[off]) << 8) | p[off + 1];
    off += 2;
    if (id_len < 8 || off + id_len > p.size()) return std::nullopt;

    const std::uint8_t* id = &p[off];
    // byte0: bits0-2 identity type (1=SUCI), bit3 odd/even indicator, bits4-6 SUPI format
    // (0=IMSI). Only the common SUCI+IMSI-format case (encoded as the literal byte 0x01) is in
    // scope -- everything else (GUTI-based registration, a non-IMSI SUPI format) is unsupported.
    if ((id[0] & 0b111) != 0b001 || ((id[0] >> 4) & 0b111) != 0b000) {
        return std::nullopt;
    }

    // PLMN, TS 24.008 §10.5.1.13 half-octet BCD (same convention as
    // ngap_task.cpp's encode_plmn_identity, decode direction).
    const int mcc1 = id[1] & 0xF;
    const int mcc2 = (id[1] >> 4) & 0xF;
    const int mcc3 = id[2] & 0xF;
    const int mnc3_nibble = (id[2] >> 4) & 0xF;
    const int mnc1 = id[3] & 0xF;
    const int mnc2 = (id[3] >> 4) & 0xF;
    const bool long_mnc = mnc3_nibble != 0xF;
    const std::string mcc = {bcd_digit(static_cast<std::uint8_t>(mcc1)),
                             bcd_digit(static_cast<std::uint8_t>(mcc2)),
                             bcd_digit(static_cast<std::uint8_t>(mcc3))};
    const std::string mnc =
        long_mnc ? std::string{bcd_digit(static_cast<std::uint8_t>(mnc1)),
                               bcd_digit(static_cast<std::uint8_t>(mnc2)),
                               bcd_digit(static_cast<std::uint8_t>(mnc3_nibble))}
                 : std::string{bcd_digit(static_cast<std::uint8_t>(mnc1)),
                               bcd_digit(static_cast<std::uint8_t>(mnc2))};

    // Routing indicator (TS 24.501 §9.11.3.4, TS 23.003 §2.2A): 1 or 2 BCD-packed octets.
    // Disclosed limitation -- this project's only real UE config
    // (simulators/ransim/config/ue.yaml) uses a 4-digit ("0000") routing indicator, i.e. exactly
    // 2 clean octets with no 0xF padding, the only case decoded here (matching UERANSIM's own
    // DecodeBcdString(riLen=2,...) path for this exact config). A padded 1-octet (2-digit)
    // routing indicator is a real, valid TS 24.501 encoding this project does not yet handle --
    // flagged via the neither-nibble-is-0xF check below, not silently misparsed.
    std::size_t ri_off = 4;
    if (((id[ri_off + 1] >> 4) & 0xF) == 0xF || (id[ri_off + 1] & 0xF) == 0xF) {
        return std::nullopt;
    }
    ri_off += 2;

    const std::uint8_t protection_scheme_id = id[ri_off] & 0xF;
    if (protection_scheme_id != 0) {
        return std::nullopt; // only the null protection scheme is in scope this stage
    }
    ri_off += 2; // protection scheme id octet + home network public key id octet

    // Remaining bytes: BCD-packed MSIN digits, plaintext (protection scheme is null, so the
    // "scheme output" is the MSIN itself, not concealed) -- TS 23.003 §2.2A.2.
    std::string msin;
    for (std::size_t i = ri_off; i < id_len; ++i) {
        const std::uint8_t lo = id[i] & 0xF;
        const std::uint8_t hi = (id[i] >> 4) & 0xF;
        msin += bcd_digit(lo);
        if (hi == 0xF) break; // odd-length terminator
        msin += bcd_digit(hi);
    }

    RegistrationRequestInfo info;
    info.supi = "imsi-" + mcc + mnc + msin;

    // Scan the optional IEs that follow the 5GS Mobile Identity for ueSecurityCapability (IEI
    // 0x2E), a generic Type-4 TLV walk (IEI + 1-octet length + value, same shape
    // decode_authentication_outcome already uses). Disclosed limitation: this assumes every
    // optional IE preceding 0x2E in the wire stream is also Type-4-shaped -- true for every real
    // config this project drives (UERANSIM's RegistrationRequest builder,
    // simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/register.cpp, never sets the three Type-1
    // half-octet optional IEs -- nonCurrentNgKsi 0xC, micoIndication 0xB,
    // networkSlicingIndication 0x9 -- that could otherwise appear before it for a fresh
    // INITIAL_REGISTRATION); a Type-1 IE appearing here would desync this walk, not silently
    // misparse past it, since the length byte would then be read from the wrong offset and either
    // the loop bails out (out-of-range) or finds no match -- ue_security_capability stays empty.
    std::size_t scan = off + id_len;
    while (scan + 2 <= p.size()) {
        const std::uint8_t iei = p[scan];
        const std::uint8_t len = p[scan + 1];
        if (scan + 2 + len > p.size()) break;
        if (iei == kIeiUeSecurityCapability) {
            const auto* value = p.data() + scan + 2;
            info.ue_security_capability.assign(value, value + len);
            break;
        }
        scan += 2 + len;
    }

    return info;
}

std::vector<std::uint8_t> encode_authentication_request(const std::array<std::uint8_t, 16>& rand,
                                                         const std::array<std::uint8_t, 16>& autn,
                                                         int ngksi) {
    std::vector<std::uint8_t> out;
    out.push_back(kEpdMobilityManagement);
    out.push_back(kSecurityHeaderNotProtected);
    out.push_back(kMessageTypeAuthenticationRequest);
    out.push_back(static_cast<std::uint8_t>(ngksi & 0xF)); // high nibble spare(0), low nibble ngKSI

    // ABBA (TS 24.501 §9.11.3.10), mandatory, Type-4 IE (1-octet length + value). Default value
    // 0x0000 per TS 33.501 Annex A.7.1 -- this project doesn't negotiate a non-default ABBA.
    out.push_back(0x02);
    out.push_back(0x00);
    out.push_back(0x00);

    // Authentication parameter RAND (TS 24.501 §9.11.3.16), optional TV, IEI 0x21 -- Type-3 IE:
    // fixed 16-octet value, NO length octet (confirmed against
    // simulators/ransim/vendor/UERANSIM/src/lib/nas/ie3.hpp's IEAuthenticationParameterRand,
    // read-only, after a real nr-ue crashed decoding this project's first attempt, which wrongly
    // gave RAND a length octet like AUTN's genuinely-Type-4 encoding below).
    out.push_back(0x21);
    out.insert(out.end(), rand.begin(), rand.end());

    // Authentication parameter AUTN (TS 24.501 §9.11.3.15), optional TLV, IEI 0x20.
    out.push_back(0x20);
    out.push_back(static_cast<std::uint8_t>(autn.size()));
    out.insert(out.end(), autn.begin(), autn.end());

    return out;
}

std::optional<AuthenticationOutcome> decode_authentication_outcome(
    const std::vector<std::uint8_t>& p) {
    if (p.size() < 3 || p[0] != kEpdMobilityManagement || p[1] != kSecurityHeaderNotProtected) {
        return std::nullopt;
    }

    if (p[2] == kMessageTypeAuthenticationResponse) {
        // AuthenticationResponse (TS 24.501 §8.2.8) has NO mandatory IEs -- just a sequence of
        // optional TLVs (authenticationResponseParameter IEI 0x2D, eapMessage IEI 0x78). Walk
        // them generically (Type-4: IEI + 1-octet length + value), same TLV-skip pattern
        // RegistrationRequest's own optional IEs would need if this project decoded them --
        // stopping as soon as RES* (0x2D) is found.
        std::size_t off = 3;
        while (off < p.size()) {
            if (off + 2 > p.size()) return std::nullopt;
            const std::uint8_t iei = p[off];
            const std::uint8_t len = p[off + 1];
            if (off + 2 + len > p.size()) return std::nullopt;
            if (iei == kIeiAuthenticationResponseParameter) {
                if (len != 16) return std::nullopt; // RES* is always 16 octets for 5G-AKA
                AuthenticationOutcome out;
                out.success = true;
                const auto* value = p.data() + off + 2;
                std::copy(value, value + len, out.res_star.begin());
                return out;
            }
            off += 2 + len; // skip an IE we don't need (e.g. eapMessage)
        }
        return std::nullopt; // no RES* present -- an EAP-only response, out of scope
    }

    if (p[2] == kMessageTypeAuthenticationFailure) {
        // AuthenticationFailure (TS 24.501 §8.2.7): mandatory 5GMM cause (Type-3, 1 octet, no
        // IEI/length), then an optional authenticationFailureParameter (AUTS, IEI 0x30).
        if (p.size() < 4) return std::nullopt;
        AuthenticationOutcome out;
        out.success = false;
        out.mm_cause = p[3];
        std::size_t off = 4;
        if (off < p.size()) {
            if (off + 2 > p.size()) return std::nullopt;
            const std::uint8_t iei = p[off];
            const std::uint8_t len = p[off + 1];
            if (off + 2 + len > p.size()) return std::nullopt;
            if (iei == kIeiAuthenticationFailureParameter) {
                if (len != 14) return std::nullopt; // AUTS is always 14 octets (SQN xor AK || MAC-S)
                std::array<std::uint8_t, 14> auts{};
                const auto* value = p.data() + off + 2;
                std::copy(value, value + len, auts.begin());
                out.auts = auts;
            }
        }
        return out;
    }

    return std::nullopt;
}

std::vector<std::uint8_t> encode_security_mode_command(
    const aka_crypto::NasIntKey& knas_int,
    const std::vector<std::uint8_t>& ue_security_capability,
    std::uint32_t downlink_count) {
    // Inner plaintext message (TS 24.501 §8.2.25): own header (EPD + SHT=NOT_PROTECTED) then
    // mandatory selectedNasSecurityAlgorithms (Type-3, 1 octet: ciphering high nibble | integrity
    // low nibble, confirmed against
    // simulators/ransim/vendor/UERANSIM/src/lib/nas/ie3.cpp's IENasSecurityAlgorithms::Encode),
    // mandatoryIE1 ngKSI (1 octet, spare high nibble | TSC+KSI low nibble -- fixed to
    // native/ksi=0, this project's only security context, see ADR-0031), mandatory Type-4
    // replayedUeSecurityCapabilities (length + the bytes RegistrationRequestInfo captured
    // verbatim, no IEI -- mandatory-position Type-4 IEs never carry one).
    std::vector<std::uint8_t> inner;
    inner.push_back(kEpdMobilityManagement);
    inner.push_back(kSecurityHeaderNotProtected);
    inner.push_back(kMessageTypeSecurityModeCommand);
    inner.push_back(static_cast<std::uint8_t>((aka_crypto::kNea2AlgorithmIdentity << 4) |
                                              aka_crypto::kNia2AlgorithmIdentity));
    inner.push_back(0x00); // ngKSI: TSC=native(0), KSI=0
    inner.push_back(static_cast<std::uint8_t>(ue_security_capability.size()));
    inner.insert(inner.end(), ue_security_capability.begin(), ue_security_capability.end());

    // SecurityModeCommand is integrity-protected only, never ciphered (see this function's own
    // declaration comment).
    return encode_secured_downlink(knas_int, /*knas_enc unused, not ciphered*/ aka_crypto::NasEncKey{},
                                   kShtIntegrityProtectedWithNewSecurityContext, /*ciphered=*/false,
                                   downlink_count, inner);
}

std::optional<SecurityModeCompleteOutcome> decode_security_mode_complete(
    const aka_crypto::NasIntKey& knas_int,
    const aka_crypto::NasEncKey& knas_enc,
    std::uint32_t uplink_count,
    const std::vector<std::uint8_t>& p) {
    const auto result = decode_secured_uplink(
        knas_int, knas_enc, kShtIntegrityProtectedAndCipheredWithNewSecurityContext,
        /*ciphered=*/true, uplink_count, p);
    if (!result.has_value()) return std::nullopt;

    SecurityModeCompleteOutcome out;
    out.mac_valid = result->mac_valid;
    if (!out.mac_valid) return out;

    // A valid MAC on a message that doesn't decipher to a SecurityModeComplete header is not
    // realistically reachable (that would need an AES-128-CMAC collision against a
    // different plaintext), but checked anyway rather than assumed -- costs nothing here.
    const auto& plain_inner = result->plain_inner;
    if (plain_inner.size() < 3 || plain_inner[0] != kEpdMobilityManagement ||
        plain_inner[2] != kMessageTypeSecurityModeComplete) {
        out.mac_valid = false;
    }
    return out;
}

std::vector<std::uint8_t> encode_registration_accept(const aka_crypto::NasIntKey& knas_int,
                                                      const aka_crypto::NasEncKey& knas_enc,
                                                      std::uint32_t downlink_count) {
    // Inner plaintext message (TS 24.501 §8.2.7): own header, then the ONLY mandatory IE,
    // registrationResult (Type-4, 1 octet: smsOverNasAllowed bit3 | registrationResult bits0-2,
    // confirmed against
    // simulators/ransim/vendor/UERANSIM/src/lib/nas/ie4.cpp's IE5gsRegistrationResult::Encode).
    // Fixed to THREEGPP_ACCESS (this project's only access type, ADR-0031) and
    // smsOverNasAllowed=NOT_ALLOWED (no SMSF integration). No optional IEs sent -- disclosed
    // simplification: a real deployment normally also reassigns a 5G-GUTI here, which this
    // project doesn't track/allocate (single-registration-per-association scope makes GUTI
    // reassignment moot for what this build proves).
    constexpr std::uint8_t kRegistrationResultThreeGppAccess = 0b001;
    constexpr std::uint8_t kSmsOverNasNotAllowed = 0;
    std::vector<std::uint8_t> inner;
    inner.push_back(kEpdMobilityManagement);
    inner.push_back(kSecurityHeaderNotProtected);
    inner.push_back(kMessageTypeRegistrationAccept);
    inner.push_back(0x01); // registrationResult IE length
    inner.push_back(static_cast<std::uint8_t>((kSmsOverNasNotAllowed << 3) |
                                              kRegistrationResultThreeGppAccess));

    return encode_secured_downlink(knas_int, knas_enc, kShtIntegrityProtectedAndCiphered,
                                   /*ciphered=*/true, downlink_count, inner);
}

std::optional<RegistrationCompleteOutcome> decode_registration_complete(
    const aka_crypto::NasIntKey& knas_int,
    const aka_crypto::NasEncKey& knas_enc,
    std::uint32_t uplink_count,
    const std::vector<std::uint8_t>& p) {
    const auto result = decode_secured_uplink(knas_int, knas_enc, kShtIntegrityProtectedAndCiphered,
                                              /*ciphered=*/true, uplink_count, p);
    if (!result.has_value()) return std::nullopt;

    RegistrationCompleteOutcome out;
    out.mac_valid = result->mac_valid;
    if (!out.mac_valid) return out;

    // RegistrationComplete (TS 24.501 §8.2.5) has no mandatory IEs -- just header + message type,
    // matching simulators/ransim/vendor/UERANSIM/src/lib/nas/msg.cpp's own onBuild (only one
    // optional IE, sorTransparentContainer, which this project's UE config never triggers).
    const auto& plain_inner = result->plain_inner;
    if (plain_inner.size() < 3 || plain_inner[0] != kEpdMobilityManagement ||
        plain_inner[2] != kMessageTypeRegistrationComplete) {
        out.mac_valid = false;
    }
    return out;
}

std::optional<UlNasTransportInfo> decode_ul_nas_transport(
    const aka_crypto::NasIntKey& knas_int,
    const aka_crypto::NasEncKey& knas_enc,
    std::uint32_t uplink_count,
    const std::vector<std::uint8_t>& p) {
    const auto result = decode_secured_uplink(knas_int, knas_enc, kShtIntegrityProtectedAndCiphered,
                                              /*ciphered=*/true, uplink_count, p);
    if (!result.has_value()) return std::nullopt;

    UlNasTransportInfo out;
    out.mac_valid = result->mac_valid;
    if (!out.mac_valid) return out;

    const auto& inner = result->plain_inner;
    // header(3) + payloadContainerType(1) + payloadContainer length(2) = 6 bytes minimum, even for
    // a zero-length payload container.
    if (inner.size() < 6 || inner[0] != kEpdMobilityManagement ||
        inner[2] != kMessageTypeUlNasTransport) {
        out.mac_valid = false; // not a message this function decodes
        return out;
    }
    if ((inner[3] & 0x0F) != kPayloadContainerTypeN1SmInformation) {
        out.mac_valid = false; // SMS/LPP/UE policy container etc. -- out of scope
        return out;
    }

    std::size_t off = 4;
    const std::size_t container_len = (static_cast<std::size_t>(inner[off]) << 8) | inner[off + 1];
    off += 2;
    if (off + container_len > inner.size()) {
        out.mac_valid = false;
        return out;
    }
    off += container_len; // the payload container itself is opaque -- see this function's own
                          // declaration comment for why it's intentionally not decoded further

    // Walk the optional IEs that follow for pduSessionId/sNssai/dnn, skipping the others (Type-3
    // TV for oldPduSessionId, Type-1 half-octet for requestType, Type-4 TLV for
    // additionalInformation) to stay in sync rather than stopping early -- UERANSIM's real UE
    // sends pduSessionId, requestType, sNssai, and dnn all in one message for PDU Session
    // Establishment (simulators/ransim/vendor/UERANSIM/src/ue/nas/sm/transport.cpp), so requestType
    // sitting between pduSessionId and sNssai/dnn must be skippable, not treated as "unknown, stop."
    while (off < inner.size()) {
        const std::uint8_t tag = inner[off];
        if (tag == kIeiUlNasPduSessionId) {
            if (off + 2 > inner.size()) break;
            out.pdu_session_id = inner[off + 1];
            off += 2;
        } else if (tag == kIeiUlNasOldPduSessionId) {
            if (off + 2 > inner.size()) break;
            off += 2;
        } else if (tag == kIeiUlNasSNssai) {
            if (off + 2 > inner.size()) break;
            const std::uint8_t len = inner[off + 1];
            if (off + 2 + len > inner.size()) break;
            if (len >= 1) out.snssai_sst = inner[off + 2];
            if (len >= 4) {
                std::array<std::uint8_t, 3> sd{};
                sd[0] = inner[off + 3];
                sd[1] = inner[off + 4];
                sd[2] = inner[off + 5];
                out.snssai_sd = sd;
            }
            off += 2 + len;
        } else if (tag == kIeiUlNasDnn) {
            if (off + 2 > inner.size()) break;
            const std::uint8_t len = inner[off + 1];
            if (off + 2 + len > inner.size()) break;
            // TS 23.003 §9.1 label encoding: [1-octet label length][ascii label]..., concatenated
            // with no separators -- reconstruct the dotted string by joining labels with '.'.
            std::string dnn;
            std::size_t label_off = off + 2;
            const std::size_t label_end = off + 2 + len;
            while (label_off < label_end) {
                const std::uint8_t label_len = inner[label_off];
                if (label_off + 1 + label_len > label_end) break;
                if (!dnn.empty()) dnn += '.';
                dnn.append(reinterpret_cast<const char*>(&inner[label_off + 1]), label_len);
                label_off += 1 + label_len;
            }
            out.dnn = dnn;
            off += 2 + len;
        } else if (tag == kIeiUlNasAdditionalInformation) {
            if (off + 2 > inner.size()) break;
            const std::uint8_t len = inner[off + 1];
            if (off + 2 + len > inner.size()) break;
            off += 2 + len;
        } else if ((tag >> 4) == kIeiNibbleUlNasRequestType) {
            off += 1;
        } else {
            break; // unknown optional IE -- stop rather than misparse, disclosed limitation
        }
    }

    return out;
}

} // namespace amf::nas
