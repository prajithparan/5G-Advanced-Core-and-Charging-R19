#include "pfcp_core/session_ies.hpp"

namespace pfcp_core {

std::vector<std::uint8_t> encode_source_interface(InterfaceValue v) {
    return {static_cast<std::uint8_t>(static_cast<std::uint8_t>(v) & 0x0F)};
}

std::vector<std::uint8_t> encode_destination_interface(InterfaceValue v) {
    return {static_cast<std::uint8_t>(static_cast<std::uint8_t>(v) & 0x0F)};
}

std::optional<InterfaceValue> decode_interface_value(const std::vector<std::uint8_t>& value) {
    if (value.size() != 1) {
        return std::nullopt;
    }
    return static_cast<InterfaceValue>(value[0] & 0x0F);
}

std::vector<std::uint8_t> encode_pdr_id(std::uint16_t id) {
    return {static_cast<std::uint8_t>(id >> 8), static_cast<std::uint8_t>(id & 0xFF)};
}

std::optional<std::uint16_t> decode_pdr_id(const std::vector<std::uint8_t>& value) {
    if (value.size() != 2) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(value[0]) << 8) | value[1]);
}

std::vector<std::uint8_t> encode_precedence(std::uint32_t value) {
    return {
        static_cast<std::uint8_t>((value >> 24) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>(value & 0xFF),
    };
}

std::optional<std::uint32_t> decode_precedence(const std::vector<std::uint8_t>& value) {
    if (value.size() != 4) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(value[0]) << 24) |
           (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) | static_cast<std::uint32_t>(value[3]);
}

std::vector<std::uint8_t> encode_far_id(std::uint32_t id) {
    return {
        static_cast<std::uint8_t>((id >> 24) & 0xFF),
        static_cast<std::uint8_t>((id >> 16) & 0xFF),
        static_cast<std::uint8_t>((id >> 8) & 0xFF),
        static_cast<std::uint8_t>(id & 0xFF),
    };
}

std::optional<std::uint32_t> decode_far_id(const std::vector<std::uint8_t>& value) {
    if (value.size() != 4) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(value[0]) << 24) |
           (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) | static_cast<std::uint32_t>(value[3]);
}

namespace {
constexpr std::uint8_t kApplyActionForwBit = 0x02; // bit 2 - FORW
} // namespace

std::vector<std::uint8_t> encode_apply_action_forward() {
    return {kApplyActionForwBit};
}

bool decode_apply_action_has_forward(const std::vector<std::uint8_t>& value) {
    return value.size() == 1 && (value[0] & kApplyActionForwBit) != 0;
}

namespace {
constexpr std::uint8_t kFTeidV4Bit = 0x01;
constexpr std::uint8_t kFTeidChBit = 0x04;
} // namespace

std::vector<std::uint8_t> encode_f_teid_choose_ipv4() {
    return {kFTeidV4Bit | kFTeidChBit};
}

std::vector<std::uint8_t> encode_f_teid_allocated_ipv4(std::uint32_t teid,
                                                       std::array<std::uint8_t, 4> ipv4) {
    std::vector<std::uint8_t> out;
    out.push_back(kFTeidV4Bit);
    out.push_back(static_cast<std::uint8_t>((teid >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((teid >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((teid >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(teid & 0xFF));
    out.insert(out.end(), ipv4.begin(), ipv4.end());
    return out;
}

std::optional<FTeidAllocated> decode_f_teid_allocated_ipv4(const std::vector<std::uint8_t>& value) {
    if (value.size() != 9 || (value[0] & kFTeidChBit) != 0 || (value[0] & kFTeidV4Bit) == 0) {
        return std::nullopt;
    }
    FTeidAllocated out;
    out.teid = (static_cast<std::uint32_t>(value[1]) << 24) |
               (static_cast<std::uint32_t>(value[2]) << 16) |
               (static_cast<std::uint32_t>(value[3]) << 8) | static_cast<std::uint32_t>(value[4]);
    out.ipv4 = {value[5], value[6], value[7], value[8]};
    return out;
}

bool decode_f_teid_is_choose_request(const std::vector<std::uint8_t>& value) {
    return !value.empty() && (value[0] & kFTeidChBit) != 0;
}

namespace {
constexpr std::uint8_t kFSeidV4Bit = 0x02;
} // namespace

std::vector<std::uint8_t> encode_f_seid_ipv4(const FSeid& f_seid) {
    std::vector<std::uint8_t> out;
    out.push_back(kFSeidV4Bit);
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((f_seid.seid >> shift) & 0xFF));
    }
    out.insert(out.end(), f_seid.ipv4.begin(), f_seid.ipv4.end());
    return out;
}

std::optional<FSeid> decode_f_seid_ipv4(const std::vector<std::uint8_t>& value) {
    if (value.size() != 13 || (value[0] & kFSeidV4Bit) == 0) {
        return std::nullopt;
    }
    FSeid out;
    std::uint64_t seid = 0;
    for (int i = 0; i < 8; ++i) {
        seid = (seid << 8) | value[static_cast<std::size_t>(1 + i)];
    }
    out.seid = seid;
    out.ipv4 = {value[9], value[10], value[11], value[12]};
    return out;
}

std::vector<std::uint8_t> encode_urr_id(std::uint32_t id) {
    return {
        static_cast<std::uint8_t>((id >> 24) & 0xFF),
        static_cast<std::uint8_t>((id >> 16) & 0xFF),
        static_cast<std::uint8_t>((id >> 8) & 0xFF),
        static_cast<std::uint8_t>(id & 0xFF),
    };
}

std::optional<std::uint32_t> decode_urr_id(const std::vector<std::uint8_t>& value) {
    if (value.size() != 4) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(value[0]) << 24) |
           (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) | static_cast<std::uint32_t>(value[3]);
}

std::vector<std::uint8_t> encode_ur_seqn(std::uint32_t seqn) {
    return {
        static_cast<std::uint8_t>((seqn >> 24) & 0xFF),
        static_cast<std::uint8_t>((seqn >> 16) & 0xFF),
        static_cast<std::uint8_t>((seqn >> 8) & 0xFF),
        static_cast<std::uint8_t>(seqn & 0xFF),
    };
}

std::optional<std::uint32_t> decode_ur_seqn(const std::vector<std::uint8_t>& value) {
    if (value.size() != 4) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(value[0]) << 24) |
           (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) | static_cast<std::uint32_t>(value[3]);
}

namespace {
constexpr std::uint8_t kMeasurementMethodVolumBit = 0x02; // bit 2 - VOLUM
constexpr std::uint8_t kReportingTriggersVolth = 0x02;    // octet 5, bit 2 - VOLTH
constexpr std::uint8_t kReportingTriggersVolqu = 0x01;    // octet 6, bit 1 - VOLQU
constexpr std::uint8_t kVolumeTovolBit = 0x01;          // octet 5, bit 1 - TOVOL (all 3 volume IEs)
constexpr std::uint8_t kReportTypeUsar = 0x02;          // bit 2 - USAR
constexpr std::uint8_t kUsageReportTriggerVolth = 0x02; // octet 5, bit 2 - VOLTH
constexpr std::uint8_t kUsageReportTriggerVolqu = 0x01; // octet 6, bit 1 - VOLQU
} // namespace

std::vector<std::uint8_t> encode_measurement_method_volume() {
    return {kMeasurementMethodVolumBit};
}

std::vector<std::uint8_t> encode_reporting_triggers_volume() {
    return {kReportingTriggersVolth, kReportingTriggersVolqu};
}

std::vector<std::uint8_t> encode_volume_total(std::uint64_t total_octets) {
    std::vector<std::uint8_t> out;
    out.push_back(kVolumeTovolBit);
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((total_octets >> shift) & 0xFF));
    }
    return out;
}

std::optional<std::uint64_t> decode_volume_total(const std::vector<std::uint8_t>& value) {
    if (value.size() != 9 || (value[0] & kVolumeTovolBit) == 0) {
        return std::nullopt;
    }
    std::uint64_t total = 0;
    for (int i = 0; i < 8; ++i) {
        total = (total << 8) | value[static_cast<std::size_t>(1 + i)];
    }
    return total;
}

bool decode_report_type_has_usage_report(const std::vector<std::uint8_t>& value) {
    return !value.empty() && (value[0] & kReportTypeUsar) != 0;
}

std::vector<std::uint8_t> encode_report_type_usage_report() {
    return {kReportTypeUsar};
}

UsageReportTriggerValue decode_usage_report_trigger(const std::vector<std::uint8_t>& value) {
    if (value.size() >= 1 && (value[0] & kUsageReportTriggerVolth) != 0) {
        return UsageReportTriggerValue::VolumeThreshold;
    }
    if (value.size() >= 2 && (value[1] & kUsageReportTriggerVolqu) != 0) {
        return UsageReportTriggerValue::VolumeQuotaExhausted;
    }
    return UsageReportTriggerValue::Other;
}

std::vector<std::uint8_t> encode_usage_report_trigger_volth() {
    return {kUsageReportTriggerVolth, 0x00};
}

std::vector<std::uint8_t> encode_usage_report_trigger_volqu() {
    return {0x00, kUsageReportTriggerVolqu};
}

} // namespace pfcp_core
