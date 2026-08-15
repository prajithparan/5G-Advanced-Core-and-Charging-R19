#include "tbcd_core/tbcd.hpp"

namespace tbcd_core {

std::vector<std::uint8_t> encode_tbcd(const std::string& digits) {
    std::vector<std::uint8_t> out;
    out.reserve((digits.size() + 1) / 2);
    for (std::size_t i = 0; i < digits.size(); i += 2) {
        const auto low = static_cast<std::uint8_t>(digits[i] - '0');
        std::uint8_t high = 0x0F;
        if (i + 1 < digits.size()) {
            high = static_cast<std::uint8_t>(digits[i + 1] - '0');
        }
        out.push_back(static_cast<std::uint8_t>((high << 4) | (low & 0x0F)));
    }
    return out;
}

std::string decode_tbcd(const std::vector<std::uint8_t>& bytes) {
    std::string digits;
    digits.reserve(bytes.size() * 2);
    for (const auto b : bytes) {
        const auto low = static_cast<std::uint8_t>(b & 0x0F);
        const auto high = static_cast<std::uint8_t>((b >> 4) & 0x0F);
        digits.push_back(static_cast<char>('0' + low));
        if (high == 0x0F) {
            break;
        }
        digits.push_back(static_cast<char>('0' + high));
    }
    return digits;
}

} // namespace tbcd_core
