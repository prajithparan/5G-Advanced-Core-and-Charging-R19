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

} // namespace aka_crypto
