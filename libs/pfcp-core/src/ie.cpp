#include "pfcp_core/ie.hpp"

namespace pfcp_core {

void encode_ie(std::vector<std::uint8_t>& out, std::uint16_t type,
              const std::vector<std::uint8_t>& value) {
    const auto length = static_cast<std::uint16_t>(value.size());
    out.push_back(static_cast<std::uint8_t>(type >> 8));
    out.push_back(static_cast<std::uint8_t>(type & 0xFF));
    out.push_back(static_cast<std::uint8_t>(length >> 8));
    out.push_back(static_cast<std::uint8_t>(length & 0xFF));
    out.insert(out.end(), value.begin(), value.end());
}

std::optional<std::vector<Ie>> decode_ies(const std::vector<std::uint8_t>& bytes) {
    std::vector<Ie> ies;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (offset + 4 > bytes.size()) {
            return std::nullopt;
        }
        Ie ie;
        ie.type = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) |
                                             bytes[offset + 1]);
        const std::uint16_t length =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset + 2]) << 8) |
                                       bytes[offset + 3]);
        offset += 4;
        if (offset + length > bytes.size()) {
            return std::nullopt;
        }
        ie.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        ies.push_back(std::move(ie));
    }
    return ies;
}

const Ie* find_ie(const std::vector<Ie>& ies, std::uint16_t type) {
    for (const auto& ie : ies) {
        if (ie.type == type) {
            return &ie;
        }
    }
    return nullptr;
}

} // namespace pfcp_core
