#pragma once

#include <string>

// Shared by http2_server.hpp and http2_client.hpp: every NF is both an SBI server and an SBI
// client (see CLAUDE.md's non-negotiable rules), and in both roles it needs the same three things
// -- its own cert+key to authenticate itself, and a CA bundle to verify the peer -- so this is one
// type, not two near-identical ones.

namespace sbi_core::http2 {

// Paths to this NF's own certificate + private key (PEM), and the CA bundle used to verify the
// peer's certificate during mTLS. All three are required; see scripts/gen-lab-pki.sh for how to
// generate a lab set. Required (not defaulted/optional) specifically so a caller cannot
// accidentally end up with an unauthenticated or unencrypted connection by omission.
struct TlsConfig {
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
};

} // namespace sbi_core::http2
