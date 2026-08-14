#include "ss7_core/m3ua_protocol_data.hpp"

namespace ss7_core {

namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

constexpr std::size_t kFixedLength = 12; // OPC(4)+DPC(4)+SI(1)+NI(1)+MP(1)+SLS(1)

} // namespace

std::vector<std::uint8_t> encode_m3ua_protocol_data(const M3uaProtocolData& data) {
    std::vector<std::uint8_t> out;
    out.reserve(kFixedLength + data.user_protocol_data.size());

    put_u32(out, data.opc);
    put_u32(out, data.dpc);
    out.push_back(data.si);
    out.push_back(data.ni);
    out.push_back(data.mp);
    out.push_back(data.sls);
    out.insert(out.end(), data.user_protocol_data.begin(), data.user_protocol_data.end());

    return out;
}

std::optional<M3uaProtocolData> decode_m3ua_protocol_data(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kFixedLength) {
        return std::nullopt;
    }

    M3uaProtocolData data;
    data.opc = get_u32(bytes, 0);
    data.dpc = get_u32(bytes, 4);
    data.si = bytes[8];
    data.ni = bytes[9];
    data.mp = bytes[10];
    data.sls = bytes[11];
    data.user_protocol_data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kFixedLength),
                                   bytes.end());
    return data;
}

} // namespace ss7_core
