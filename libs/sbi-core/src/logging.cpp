#include "sbi_core/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace sbi_core {

void init_logging(const std::string& nf_name, spdlog::level::level_enum level) {
    auto logger = spdlog::stdout_color_mt(nf_name);
    logger->set_pattern(std::string("[%Y-%m-%d %H:%M:%S.%e] [") + nf_name + "] [%^%l%$] %v");
    logger->set_level(level);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::warn);
}

} // namespace sbi_core
