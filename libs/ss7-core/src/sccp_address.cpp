#include "ss7_core/sccp_address.hpp"

#include "ss7_core/sccp_dictionary.hpp"

namespace ss7_core {

namespace {

// Address indicator octet bit layout -- Figure 3/Q.713, cross-checked against the vendored
// Osmocom `sccp_types.h` struct (little-endian bitfield declaration order: the first-declared
// field occupies the low-order bits): bit0=point_code_indicator, bit1=ssn_indicator,
// bits2-5=global_title_indicator, bit6=routing_indicator, bit7=reserved.
constexpr std::uint8_t kPointCodeIndicatorBit = 0x01;
constexpr std::uint8_t kSsnIndicatorBit = 0x02;
constexpr std::uint8_t kGtiShift = 2;
constexpr std::uint8_t kGtiMask = 0x0F;
constexpr std::uint8_t kRoutingIndicatorBit = 0x40;

} // namespace

std::vector<std::uint8_t> encode_sccp_address(const SccpAddress& addr) {
    std::vector<std::uint8_t> out;

    std::uint8_t indicator = 0;
    if (addr.point_code_present) {
        indicator |= kPointCodeIndicatorBit;
    }
    if (addr.ssn_present) {
        indicator |= kSsnIndicatorBit;
    }
    indicator |= static_cast<std::uint8_t>(dictionary::GlobalTitleIndicator::kNone << kGtiShift) &
                 (kGtiMask << kGtiShift);
    if (addr.routing_indicator == dictionary::RoutingIndicator::kRouteOnSsn) {
        indicator |= kRoutingIndicatorBit;
    }
    out.push_back(indicator);

    if (addr.point_code_present) {
        // Figure 6/Q.713: octet1 = lsb (8 bits), octet2 = msb (low 6 bits) + reserved (top 2
        // bits, always 0 here) -- 14-bit ITU point code.
        out.push_back(static_cast<std::uint8_t>(addr.point_code & 0xFF));
        out.push_back(static_cast<std::uint8_t>((addr.point_code >> 8) & 0x3F));
    }

    if (addr.ssn_present) {
        out.push_back(addr.ssn);
    }

    return out;
}

std::optional<SccpAddress> decode_sccp_address(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }

    const std::uint8_t indicator = bytes[0];
    const std::uint8_t gti = (indicator >> kGtiShift) & kGtiMask;
    if (gti != dictionary::GlobalTitleIndicator::kNone) {
        // Real, disclosed rejection -- see this file's own header for why Global-Title addressing
        // isn't implemented this stage.
        return std::nullopt;
    }

    SccpAddress addr;
    addr.point_code_present = (indicator & kPointCodeIndicatorBit) != 0;
    addr.ssn_present = (indicator & kSsnIndicatorBit) != 0;
    addr.routing_indicator = (indicator & kRoutingIndicatorBit) != 0
                                 ? dictionary::RoutingIndicator::kRouteOnSsn
                                 : dictionary::RoutingIndicator::kRouteOnGt;

    std::size_t pos = 1;
    if (addr.point_code_present) {
        if (bytes.size() < pos + 2) {
            return std::nullopt;
        }
        const std::uint16_t lsb = bytes[pos];
        const std::uint16_t msb = bytes[pos + 1] & 0x3F;
        addr.point_code = static_cast<std::uint16_t>(lsb | (msb << 8));
        pos += 2;
    }

    if (addr.ssn_present) {
        if (bytes.size() < pos + 1) {
            return std::nullopt;
        }
        addr.ssn = bytes[pos];
        ++pos;
    }

    return addr;
}

} // namespace ss7_core
