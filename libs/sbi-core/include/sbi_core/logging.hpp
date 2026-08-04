#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

// Thin wrapper so every NF binary sets up the same log pattern (including its own NF name) with
// one call, instead of each main() hand-rolling spdlog setup.

namespace sbi_core {

// Call once at NF startup. Sets the default logger's pattern to include `nf_name` and installs it
// as spdlog's global default, so plain spdlog::info(...) etc. work everywhere after this call.
void init_logging(const std::string& nf_name,
                  spdlog::level::level_enum level = spdlog::level::info);

} // namespace sbi_core
