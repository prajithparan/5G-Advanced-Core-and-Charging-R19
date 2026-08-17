#include "kausf_store.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace ausf {

namespace {

std::string context_key(const std::string& supi) {
    return "ausf:sorctx:" + supi;
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

void KausfStore::store_fresh_kausf(const std::string& supi, const aka_crypto::Kausf& kausf) {
    const auto key = context_key(supi);
    redis_->hset(key, "kausf", to_hex({kausf.begin(), kausf.end()}));
    redis_->hset(key, "counter_sor", "1");
    redis_->hset(key, "suspended", "0");
}

std::optional<SorContext> KausfStore::get(const std::string& supi) {
    const auto key = context_key(supi);
    std::vector<sw::redis::OptionalString> values;
    redis_->hmget(key, {"kausf", "counter_sor", "suspended"}, std::back_inserter(values));
    // sw::redis::Optional<T> is redis-plus-plus's own pre-C++17 Optional (`explicit operator
    // bool()`), not std::optional -- real API this project already found the hard way once
    // (nfs/chf/src/stores.cpp, P4.8) and confirmed again for AMF's own UeSecurityContextStore.
    if (!values[0]) {
        return std::nullopt;
    }
    SorContext ctx;
    const auto kausf_bytes = from_hex(*values[0]);
    if (kausf_bytes.size() != ctx.kausf.size()) {
        spdlog::warn("ausf: SorContext for SUPI {} has malformed kausf, dropping", supi);
        return std::nullopt;
    }
    std::copy(kausf_bytes.begin(), kausf_bytes.end(), ctx.kausf.begin());
    ctx.counter_sor = values[1] ? static_cast<std::uint16_t>(std::stoul(*values[1])) : 1;
    ctx.suspended = values[2] && *values[2] == "1";
    return ctx;
}

std::optional<std::uint16_t> KausfStore::use_counter(const std::string& supi) {
    auto ctx = get(supi);
    if (!ctx.has_value() || ctx->suspended) {
        return std::nullopt;
    }
    const auto key = context_key(supi);
    const auto new_value = redis_->hincrby(key, "counter_sor", 1);
    const auto used = static_cast<std::uint16_t>(new_value - 1);
    if (new_value > 0xFFFF) {
        // The value just handed out (0xFFFF) was the real, spec-defined last usable one --
        // incrementing further would have to represent 0x10000, which doesn't fit the real
        // 16-bit CounterSoR field at all, let alone the forbidden 0x0000. Suspend now, for every
        // call after this one (this one still returns its own valid `used` value below).
        redis_->hset(key, "suspended", "1");
        spdlog::warn("ausf: CounterSoR for SUPI {} reached its real 16-bit maximum (0xFFFF) -- "
                     "suspending SoR protection service per TS 33.501 §6.14.2.3 until a fresh "
                     "KAUSF is established",
                     supi);
    }
    return used;
}

} // namespace ausf
