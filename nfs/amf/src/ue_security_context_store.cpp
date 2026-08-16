#include "ue_security_context_store.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace amf {

namespace {

std::string context_key(std::uint32_t tmsi) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "amf:uesecctx:%08x", tmsi);
    return buf;
}

std::string to_hex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

std::vector<std::uint8_t> from_hex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

} // namespace

void UeSecurityContextStore::put(std::uint32_t tmsi, const UeSecurityContext& context) {
    const auto key = context_key(tmsi);
    redis_->hset(key, "supi", context.supi);
    redis_->hset(key, "kamf", to_hex({context.kamf.begin(), context.kamf.end()}));
    redis_->hset(key, "ngksi", std::to_string(context.ngksi));
    redis_->hset(key, "uplink_count", std::to_string(context.uplink_count));
    redis_->hset(key, "downlink_count", std::to_string(context.downlink_count));
    redis_->hset(key, "ue_sec_cap", to_hex(context.ue_security_capability));
}

std::optional<UeSecurityContext> UeSecurityContextStore::get(std::uint32_t tmsi) {
    const auto key = context_key(tmsi);
    std::vector<sw::redis::OptionalString> values;
    redis_->hmget(key,
                  {"supi", "kamf", "ngksi", "uplink_count", "downlink_count", "ue_sec_cap"},
                  std::back_inserter(values));
    // sw::redis::Optional<T> is redis-plus-plus's own pre-C++17 Optional (`explicit operator
    // bool()`), not std::optional -- same real API confirmed via compilation this project's own
    // P4.8 pass already found (nfs/chf/src/stores.cpp).
    if (!values[0]) {
        return std::nullopt;
    }
    UeSecurityContext context;
    context.supi = *values[0];
    const auto kamf_bytes = values[1] ? from_hex(*values[1]) : std::vector<std::uint8_t>{};
    if (kamf_bytes.size() != context.kamf.size()) {
        spdlog::warn("amf: UeSecurityContext for tmsi={:08x} has malformed kamf, dropping", tmsi);
        return std::nullopt;
    }
    std::copy(kamf_bytes.begin(), kamf_bytes.end(), context.kamf.begin());
    context.ngksi = values[2] ? static_cast<std::uint8_t>(std::stoul(*values[2])) : 0;
    context.uplink_count = values[3] ? static_cast<std::uint32_t>(std::stoul(*values[3])) : 0;
    context.downlink_count = values[4] ? static_cast<std::uint32_t>(std::stoul(*values[4])) : 0;
    context.ue_security_capability = values[5] ? from_hex(*values[5]) : std::vector<std::uint8_t>{};
    return context;
}

std::uint32_t UeSecurityContextStore::next_uplink_count(std::uint32_t tmsi) {
    const auto new_value = redis_->hincrby(context_key(tmsi), "uplink_count", 1);
    return static_cast<std::uint32_t>(new_value - 1);
}

std::uint32_t UeSecurityContextStore::next_downlink_count(std::uint32_t tmsi) {
    const auto new_value = redis_->hincrby(context_key(tmsi), "downlink_count", 1);
    return static_cast<std::uint32_t>(new_value - 1);
}

void UeSecurityContextStore::remove(std::uint32_t tmsi) {
    redis_->del(context_key(tmsi));
}

std::uint32_t UeSecurityContextStore::allocate_tmsi() {
    return static_cast<std::uint32_t>(redis_->incr("amf:next_tmsi"));
}

} // namespace amf
