#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Lowercase hex encode/decode -- every AKA-related JSON field in TS29503_Nudm_UEAU.yaml and
// TS29509_Nausf_UEAuthentication.yaml (Rand, Autn, Kausf, XresStar, CkPrime, IkPrime, ...) is a
// hex string over the wire (codegen emits these as plain `std::string` aliases; the hex
// convention is enforced by the YAML's `pattern` regex, not by the generated C++ type), so both
// nfs/udm and nfs/ausf need this in both directions.

namespace aka_crypto {

template <size_t N>
std::string to_hex(const std::array<uint8_t, N>& data) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(N * 2, '0');
    for (size_t i = 0; i < N; ++i) {
        out[i * 2] = kDigits[(data[i] >> 4) & 0xf];
        out[i * 2 + 1] = kDigits[data[i] & 0xf];
    }
    return out;
}

std::string to_hex(const std::vector<uint8_t>& data);

template <size_t N>
std::optional<std::array<uint8_t, N>> from_hex(const std::string& text) {
    if (text.size() != N * 2) {
        return std::nullopt;
    }
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i) {
        int hi = -1;
        int lo = -1;
        const char c_hi = text[i * 2];
        const char c_lo = text[i * 2 + 1];
        if (c_hi >= '0' && c_hi <= '9') hi = c_hi - '0';
        else if (c_hi >= 'a' && c_hi <= 'f') hi = c_hi - 'a' + 10;
        else if (c_hi >= 'A' && c_hi <= 'F') hi = c_hi - 'A' + 10;
        if (c_lo >= '0' && c_lo <= '9') lo = c_lo - '0';
        else if (c_lo >= 'a' && c_lo <= 'f') lo = c_lo - 'a' + 10;
        else if (c_lo >= 'A' && c_lo <= 'F') lo = c_lo - 'A' + 10;
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return out;
}

}  // namespace aka_crypto
