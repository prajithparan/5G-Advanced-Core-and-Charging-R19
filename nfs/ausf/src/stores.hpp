#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "aka_crypto/kdf.hpp"
#include "aka_crypto/milenage.hpp"

// Private to nfs/ausf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts, no expiry/TTL --
// same disclosed simplification as every other NF's store so far (ADR-0015). See
// docs/DECISIONS.md ADR-0027.
//
// Holds exactly what AUSF needs to remember between POST /ue-authentications (which creates the
// context) and the later confirmation/deletion calls: the key material UDM handed back, keyed by
// an AUSF-generated authCtxId. Two disjoint shapes share one struct (rather than a variant/two
// classes) because the confirmation-side code only ever reads the fields for the auth_type it
// already knows it's handling -- see nfs/ausf/src/main.cpp.

namespace ausf {

struct AuthContext {
    std::string supi;
    std::string serving_network_name;
    std::string auth_type; // "5G_AKA" or "EAP_AKA_PRIME" --
                           // sbi_gen::AuthType_Nausf_UEAuthentication's values

    // 5G-AKA path (PUT .../5g-aka-confirmation compares ConfirmationData.resStar against this).
    aka_crypto::ResStar xres_star{};
    aka_crypto::Kausf kausf_5g_aka{};

    // EAP-AKA' path. kausf_eap is derived via the same Annex A.2 KDF as the 5G-AKA path
    // (aka_crypto::derive_kausf, FC=0x6A) but keyed on CK'/IK' instead of CK/IK -- NOT RFC 5448's
    // EMSK, which is the wrong size (64 bytes) for every Kausf field in the YAML (32 bytes). See
    // docs/DECISIONS.md ADR-0027.
    std::array<uint8_t, 32> k_aut{};
    aka_crypto::Res64 xres{};
    aka_crypto::Kausf kausf_eap{};
    std::array<uint8_t, 64> msk{};
};

class AuthContextStore {
public:
    std::string create(AuthContext ctx);
    std::optional<AuthContext> get(const std::string& auth_ctx_id);
    bool remove(const std::string& auth_ctx_id);
    // Nausf_UEAuthentication's deregister operation removes by supi, not authCtxId -- a SEAF/AMF
    // asking to tear down a UE's security context doesn't necessarily still have the authCtxId
    // (e.g. it may have been created by a different AMF instance in the same set). Returns true if
    // at least one context was removed.
    bool remove_by_supi(const std::string& supi);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, AuthContext> contexts_;
    std::uint64_t next_id_ = 1;
};

} // namespace ausf
