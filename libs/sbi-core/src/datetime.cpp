#include "sbi_core/datetime.hpp"

#include <cstddef>
#include <format>

namespace sbi_core {
namespace {

// Reads exactly `len` ASCII digits starting at `pos`. Rejects anything else (a sign, a space,
// a short field) rather than silently accepting a partial number the way std::stoi would.
bool read_digits(std::string_view s, std::size_t pos, std::size_t len, int& out) {
    if (pos + len > s.size()) {
        return false;
    }
    int value = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = s[pos + i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

} // namespace

std::string format_rfc3339(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;

    const auto ms = floor<milliseconds>(tp);
    const auto days_tp = floor<days>(ms);
    const year_month_day ymd{days_tp};
    const hh_mm_ss<milliseconds> hms{ms - days_tp};

    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
                       static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()),
                       static_cast<unsigned>(ymd.day()),
                       hms.hours().count(),
                       hms.minutes().count(),
                       hms.seconds().count(),
                       hms.subseconds().count());
}

std::optional<std::chrono::system_clock::time_point> parse_rfc3339(std::string_view text) {
    using namespace std::chrono;

    // "YYYY-MM-DDTHH:MM:SS" is the shortest legal form: 19 characters before any fraction or
    // offset, and an offset (or 'Z') is mandatory in RFC 3339 -- so 20 is the true minimum.
    constexpr std::size_t kDateTimeChars = 19;
    if (text.size() < kDateTimeChars + 1) {
        return std::nullopt;
    }
    if (text[4] != '-' || text[7] != '-' || text[13] != ':' || text[16] != ':') {
        return std::nullopt;
    }
    if (text[10] != 'T' && text[10] != 't') {
        return std::nullopt;
    }

    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int sec = 0;
    if (!read_digits(text, 0, 4, y) || !read_digits(text, 5, 2, mo) ||
        !read_digits(text, 8, 2, d) || !read_digits(text, 11, 2, h) ||
        !read_digits(text, 14, 2, mi) || !read_digits(text, 17, 2, sec)) {
        return std::nullopt;
    }

    const year_month_day ymd{
        year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (!ymd.ok()) {
        return std::nullopt; // rejects e.g. 2026-02-31, which read_digits alone would accept
    }
    // Second 60 is a real leap second in the grammar; clamp to 59 rather than reject, since
    // system_clock has no leap-second representation to map it onto anyway.
    if (h > 23 || mi > 59 || sec > 60) {
        return std::nullopt;
    }
    if (sec == 60) {
        sec = 59;
    }

    std::size_t pos = kDateTimeChars;
    milliseconds frac{0};
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        const std::size_t start = pos;
        int scale = 100; // first fractional digit is hundreds of milliseconds
        int ms = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (scale > 0) {
                ms += (text[pos] - '0') * scale;
                scale /= 10;
            } // further digits are real but below milliseconds precision: truncated, not an error
            ++pos;
        }
        if (pos == start) {
            return std::nullopt; // a '.' with no digits after it is malformed
        }
        frac = milliseconds{ms};
    }

    // Offset: 'Z'/'z', or a real numeric offset that must be subtracted to reach UTC.
    minutes offset{0};
    if (pos >= text.size()) {
        return std::nullopt;
    }
    if (text[pos] == 'Z' || text[pos] == 'z') {
        ++pos;
    } else if (text[pos] == '+' || text[pos] == '-') {
        const int sign = text[pos] == '-' ? -1 : 1;
        ++pos;
        int oh = 0;
        int om = 0;
        if (!read_digits(text, pos, 2, oh) || pos + 2 >= text.size() || text[pos + 2] != ':' ||
            !read_digits(text, pos + 3, 2, om)) {
            return std::nullopt;
        }
        if (oh > 23 || om > 59) {
            return std::nullopt;
        }
        offset = sign * (hours{oh} + minutes{om});
        pos += 5;
    } else {
        return std::nullopt;
    }
    if (pos != text.size()) {
        return std::nullopt; // trailing junk after a complete timestamp is malformed
    }

    const sys_days date{ymd};
    return system_clock::time_point{date.time_since_epoch()} + hours{h} + minutes{mi} +
           seconds{sec} + frac - offset;
}

std::optional<std::time_t> parse_rfc3339_to_time_t(std::string_view text) {
    const auto tp = parse_rfc3339(text);
    if (!tp.has_value()) {
        return std::nullopt;
    }
    return std::chrono::system_clock::to_time_t(*tp);
}

} // namespace sbi_core
