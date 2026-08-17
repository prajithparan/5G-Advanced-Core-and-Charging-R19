#include "pfcp_core/common_ies.hpp"

namespace pfcp_core {

namespace {
// Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01) -- standard RFC 5905
// knowledge, not 3GPP-specific.
constexpr std::int64_t kNtpUnixEpochOffsetSeconds = 2208988800LL;
} // namespace

std::vector<std::uint8_t> encode_cause(Cause cause) {
    return {static_cast<std::uint8_t>(cause)};
}

std::optional<Cause> decode_cause(const std::vector<std::uint8_t>& value) {
    if (value.size() != 1) {
        return std::nullopt;
    }
    return static_cast<Cause>(value[0]);
}

std::vector<std::uint8_t> encode_recovery_time_stamp(std::time_t unix_time) {
    const auto ntp_time = static_cast<std::uint32_t>(static_cast<std::int64_t>(unix_time) +
                                                     kNtpUnixEpochOffsetSeconds);
    return {
        static_cast<std::uint8_t>((ntp_time >> 24) & 0xFF),
        static_cast<std::uint8_t>((ntp_time >> 16) & 0xFF),
        static_cast<std::uint8_t>((ntp_time >> 8) & 0xFF),
        static_cast<std::uint8_t>(ntp_time & 0xFF),
    };
}

std::optional<std::time_t> decode_recovery_time_stamp(const std::vector<std::uint8_t>& value) {
    if (value.size() != 4) {
        return std::nullopt;
    }
    const std::uint32_t ntp_time = (static_cast<std::uint32_t>(value[0]) << 24) |
                                   (static_cast<std::uint32_t>(value[1]) << 16) |
                                   (static_cast<std::uint32_t>(value[2]) << 8) |
                                   static_cast<std::uint32_t>(value[3]);
    return static_cast<std::time_t>(static_cast<std::int64_t>(ntp_time) -
                                    kNtpUnixEpochOffsetSeconds);
}

std::vector<std::uint8_t> encode_node_id_ipv4(std::array<std::uint8_t, 4> ipv4) {
    constexpr std::uint8_t kNodeIdTypeIpv4 = 0;
    std::vector<std::uint8_t> out;
    out.push_back(kNodeIdTypeIpv4); // spare(high nibble)=0 | Node ID Type(low nibble)=IPv4
    out.insert(out.end(), ipv4.begin(), ipv4.end());
    return out;
}

std::optional<std::array<std::uint8_t, 4>>
decode_node_id_ipv4(const std::vector<std::uint8_t>& value) {
    constexpr std::uint8_t kNodeIdTypeIpv4 = 0;
    if (value.size() != 5 || (value[0] & 0x0F) != kNodeIdTypeIpv4) {
        return std::nullopt;
    }
    return std::array<std::uint8_t, 4>{value[1], value[2], value[3], value[4]};
}

std::vector<std::uint8_t> encode_up_function_features_none() {
    return {0x00, 0x00};
}

std::vector<std::uint8_t> encode_cp_function_features_none() {
    return {0x00};
}

std::vector<std::uint8_t> encode_node_report_type(const NodeReportType& type) {
    constexpr std::uint8_t kUpfrBit = 0x01;
    std::uint8_t octet = 0;
    if (type.user_plane_path_failure_report) {
        octet |= kUpfrBit;
    }
    return {octet};
}

std::optional<NodeReportType> decode_node_report_type(const std::vector<std::uint8_t>& value) {
    constexpr std::uint8_t kUpfrBit = 0x01;
    if (value.size() != 1) {
        return std::nullopt;
    }
    NodeReportType type;
    type.user_plane_path_failure_report = (value[0] & kUpfrBit) != 0;
    return type;
}

std::vector<std::uint8_t> encode_remote_gtpu_peer_ipv4(std::array<std::uint8_t, 4> ipv4) {
    constexpr std::uint8_t kV4Bit = 0x02;
    std::vector<std::uint8_t> out;
    out.push_back(kV4Bit);
    out.insert(out.end(), ipv4.begin(), ipv4.end());
    return out;
}

std::optional<std::array<std::uint8_t, 4>>
decode_remote_gtpu_peer_ipv4(const std::vector<std::uint8_t>& value) {
    constexpr std::uint8_t kV4Bit = 0x02;
    if (value.size() != 5 || (value[0] & kV4Bit) == 0) {
        return std::nullopt;
    }
    return std::array<std::uint8_t, 4>{value[1], value[2], value[3], value[4]};
}

} // namespace pfcp_core
