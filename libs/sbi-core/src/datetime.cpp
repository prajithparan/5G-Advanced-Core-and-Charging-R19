#include "sbi_core/datetime.hpp"

#include <format>

namespace sbi_core {

std::string format_rfc3339(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;

    const auto ms = floor<milliseconds>(tp);
    const auto days_tp = floor<days>(ms);
    const year_month_day ymd{days_tp};
    const hh_mm_ss<milliseconds> hms{ms - days_tp};

    return std::format(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
        static_cast<int>(ymd.year()),
        static_cast<unsigned>(ymd.month()),
        static_cast<unsigned>(ymd.day()),
        hms.hours().count(),
        hms.minutes().count(),
        hms.seconds().count(),
        hms.subseconds().count());
}

} // namespace sbi_core
