#include "aka_crypto/hex.hpp"

namespace aka_crypto {

std::string to_hex(const std::vector<uint8_t>& data) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(data.size() * 2, '0');
    for (size_t i = 0; i < data.size(); ++i) {
        out[i * 2] = kDigits[(data[i] >> 4) & 0xf];
        out[i * 2 + 1] = kDigits[data[i] & 0xf];
    }
    return out;
}

std::optional<std::vector<uint8_t>> from_hex(const std::string& text) {
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> out(text.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        int hi = -1;
        int lo = -1;
        const char c_hi = text[i * 2];
        const char c_lo = text[i * 2 + 1];
        if (c_hi >= '0' && c_hi <= '9')
            hi = c_hi - '0';
        else if (c_hi >= 'a' && c_hi <= 'f')
            hi = c_hi - 'a' + 10;
        else if (c_hi >= 'A' && c_hi <= 'F')
            hi = c_hi - 'A' + 10;
        if (c_lo >= '0' && c_lo <= '9')
            lo = c_lo - '0';
        else if (c_lo >= 'a' && c_lo <= 'f')
            lo = c_lo - 'a' + 10;
        else if (c_lo >= 'A' && c_lo <= 'F')
            lo = c_lo - 'A' + 10;
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return out;
}

} // namespace aka_crypto
