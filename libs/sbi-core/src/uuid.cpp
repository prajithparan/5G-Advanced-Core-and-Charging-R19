#include "sbi_core/uuid.hpp"

#include <array>
#include <format>
#include <random>

namespace sbi_core {

std::string generate_uuid_v4() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;

    std::uint64_t hi = dist(rng);
    std::uint64_t lo = dist(rng);

    // Set version (4) and variant (RFC 4122) bits.
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
                       (hi >> 32) & 0xFFFFFFFF,
                       (hi >> 16) & 0xFFFF,
                       hi & 0xFFFF,
                       (lo >> 48) & 0xFFFF,
                       lo & 0xFFFFFFFFFFFFULL);
}

} // namespace sbi_core
